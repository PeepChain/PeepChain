#pragma once
#include "NoiseGate.h"
#include "Compressor.h"
#include "Equalizers.h"
#include "MultibandCore.h"
#include "PeakLimiter.h"
#include <algorithm>
#include <vector>

/**
 * Lil Peep's GarageBand vocal chain. His order:
 *   Noise Gate -> Compressor -> AUFilter -> AUGraphicEQ -> AUMultibandCompressor -> AUPeakLimiter
 * Stereo in/out (a mono vocal is fed to both channels). Total latency = getLatency().
 * Defaults are Lil Peep's settings.
 *
 * Two ways to run it:
 *  - Offline tools (liveSwitching = false): a disabled stage is simply skipped, and the latency is the
 *    sum of the enabled stages. This is what the verification against GarageBand uses.
 *  - The plugin (liveSwitching = true): stages can be switched and reordered while audio runs. Every
 *    stage always runs; a disabled one is replaced by the dry signal delayed by that stage's latency,
 *    with a short crossfade, so the total latency never changes and nothing clicks. A new order is
 *    applied behind a short dip to silence. With everything on in Peep's order the output is identical
 *    to the offline mode, sample for sample.
 */
class PeepChain {
public:
    enum Stage { kGate = 0, kComp, kFilter, kGeq, kMulti, kLimit, kStages };

    struct Settings {
        NoiseGate::Settings gate;
        Compressor::Settings comp;
        Compressor::Fit compFit;
        AUFilterEQ::Settings filter;
        double geqGains[GraphicEQ31::kBands];
        MultibandSettings multiband;
        double limiterAttack = 0.002, limiterRelease = 0.005, limiterPregainDb = 18.0;
        bool enable[kStages] = {true, true, true, true, true, true};
        int  order[kStages] = {kGate, kComp, kFilter, kGeq, kMulti, kLimit};   // order[slot] = stage
        bool liveSwitching = false;        // see the class comment
        int  multibandBlockPhase = 55;     // 0..63; 55 = where GarageBand's grid falls with all six plugins on (see BandCompressor)
        bool filterLeftBypassed = false;   // testing only: bypasses the left channel's filter; never set in the plugin
        Settings() {
            for (int i = 0; i < GraphicEQ31::kBands; ++i) geqGains[i] = GraphicEQ31::peepGains()[i];
            // The plugin window shows -69 / -54 / -51 / -52 (rounded to whole dB); these are the unrounded
            // values that make the real AU reproduce GarageBand's bounces.
            multiband.threshold[0] = -68.92; multiband.threshold[1] = -53.98;
            multiband.threshold[2] = -51.33; multiband.threshold[3] = -51.84;
        }
    };

    static bool isValidOrder(const int* order) {
        int seen = 0;
        for (int i = 0; i < kStages; ++i) { if (order[i] < 0 || order[i] >= kStages) return false; seen |= 1 << order[i]; }
        return seen == (1 << kStages) - 1;
    }

    void prepare(double sampleRate, const Settings& s) {
        s_ = s; sr_ = sampleRate;
        if (!isValidOrder(s_.order)) for (int i = 0; i < kStages; ++i) s_.order[i] = i;
        gate_.prepare(sampleRate, s.gate, 2);
        comp_.prepare(sampleRate, s.comp, s.compFit, 2);
        for (int c = 0; c < 2; ++c) { filter_[c].prepare(sampleRate, s.filter); geq_[c].prepare(sampleRate, s.geqGains); }
        mb_.prepare(sampleRate, s.multiband);
        mb_.setBlockPhase(s.multibandBlockPhase);
        limiter_.prepare(sampleRate, s.limiterAttack, s.limiterRelease, s.limiterPregainDb, 2);

        stageLatency_[kGate] = gate_.getLatency(); stageLatency_[kMulti] = mb_.getLatency(); stageLatency_[kLimit] = limiter_.getLatency();
        stageLatency_[kComp] = stageLatency_[kFilter] = stageLatency_[kGeq] = 0;
        for (int i = 0; i < kStages; ++i) {
            dry_[i].assign((size_t)stageLatency_[i] * 2, 0.0); dryPos_[i] = 0;
            mix_[i] = s_.enable[i] ? 1.0 : 0.0;
            pendingOrder_[i] = s_.order[i];
        }
        mixStep_ = 1.0 / (0.010 * sampleRate);          // 10 ms stage crossfade
        fadeStep_ = 1.0 / (0.005 * sampleRate);         // 5 ms dip either side of a reorder
        fade_ = 1.0; fadeState_ = kSteady; muteLeft_ = 0;
    }

    /** Clear every stage's state (audio thread safe: no allocation). A reorder still waiting behind its
     *  fade is applied at once, and each stage is fully on or off as switched. */
    void reset() {
        gate_.reset(); comp_.reset(); limiter_.reset(); mb_.reset();
        for (int c = 0; c < 2; ++c) { filter_[c].reset(); geq_[c].reset(); }
        for (int i = 0; i < kStages; ++i) {
            std::fill(dry_[i].begin(), dry_[i].end(), 0.0); dryPos_[i] = 0;
            mix_[i] = s_.enable[i] ? 1.0 : 0.0;
            s_.order[i] = pendingOrder_[i];
        }
        fade_ = 1.0; fadeState_ = kSteady; muteLeft_ = 0;
    }

    int getLatency() const {
        int n = 0;
        for (int i = 0; i < kStages; ++i) if (s_.liveSwitching || s_.enable[i]) n += stageLatency_[i];
        return n;
    }

    // ---- live control (audio thread) ----
    void setEnabled(int stage, bool on) { s_.enable[stage] = on; }
    bool isEnabled(int stage) const { return s_.enable[stage]; }
    void setGateThresholdDb(double db) { s_.gate.thresholdDb = db; gate_.setThresholdDb(db); }
    void setCompressorThresholdDb(double db) { s_.comp.thresholdDb = db; comp_.setThresholdDb(db); }
    /** The plugin's editable settings, while running: the Noise Gate's levels and times (not its threshold or
     *  look-ahead), the Compressor's times and make-up, all of AUFilter and AUGraphicEQ,
     *  AUMultibandCompressor's crossovers, thresholds, headrooms and times, AUPeakLimiter's pre-gain and release.
     *  Only what differs from the current settings is touched (no allocation). Everything else in s is ignored. */
    void setEffectSettings(const Settings& s) {
        if (!sameGate(s.gate, s_.gate)) {
            const double thr = s_.gate.thresholdDb;                 // the threshold has its own setter
            s_.gate = s.gate; s_.gate.thresholdDb = thr;
            gate_.setLevelsAndTimes(s_.gate);
        }
        if (s.comp.attackMs != s_.comp.attackMs || s.comp.releaseMs != s_.comp.releaseMs) {
            s_.comp.attackMs = s.comp.attackMs; s_.comp.releaseMs = s.comp.releaseMs; comp_.setTimes(s.comp.attackMs, s.comp.releaseMs);
        }
        if (s.comp.makeupDb != s_.comp.makeupDb) { s_.comp.makeupDb = s.comp.makeupDb; comp_.setMakeupDb(s.comp.makeupDb); }
        if (!sameFilter(s.filter, s_.filter)) { s_.filter = s.filter; for (auto& f : filter_) f.set(sr_, s.filter); }
        if (!std::equal(s.geqGains, s.geqGains + GraphicEQ31::kBands, s_.geqGains)) {
            std::copy(s.geqGains, s.geqGains + GraphicEQ31::kBands, s_.geqGains);
            for (auto& q : geq_) q.set(sr_, s.geqGains);
        }
        if (!sameMultiband(s.multiband, s_.multiband)) { s_.multiband = s.multiband; mb_.setSettings(s.multiband); }
        if (s.limiterRelease != s_.limiterRelease) { s_.limiterRelease = s.limiterRelease; limiter_.setRelease(s.limiterRelease); }
        if (s.limiterPregainDb != s_.limiterPregainDb) { s_.limiterPregainDb = s.limiterPregainDb; limiter_.setPregainDb(s.limiterPregainDb); }
    }
    /** Ask for a new order (order[slot] = stage). In live mode it is applied behind a short dip to silence. */
    void setOrder(const int* order) {
        if (!isValidOrder(order)) return;
        bool same = true;
        for (int i = 0; i < kStages; ++i) { same = same && order[i] == pendingOrder_[i]; pendingOrder_[i] = order[i]; }
        if (same) return;
        if (!s_.liveSwitching) { for (int i = 0; i < kStages; ++i) s_.order[i] = order[i]; return; }
        fadeState_ = kFadeOut;
    }
    const int* getOrder() const { return s_.order; }

    // Metering (read on the audio thread, published by the plugin)
    double gateGain() const            { return gate_.getGain(); }
    double compressorReductionDb() const { return comp_.getGainReductionDb(); }
    double bandReductionDb(int band) const { return mb_.getGainReduction(band); }
    double limiterReductionDb() const  { return limiter_.getGainReductionDb(); }
    /** The same three as linear gains (<= 1), which cost nothing to read every sample: the lowest over a block,
     *  converted to dB once, is the block's most reduction. */
    double compressorGain() const      { return comp_.getGainLinear(); }
    double bandGain(int band) const    { return mb_.getGainLinear(band); }
    double limiterGain() const         { return limiter_.getGain(); }
    double compressorLevelDb() const   { return comp_.getLevelDb(); }
    /** Peaks going into the limiter (after its pre-gain) and coming out of it since the last resetLimiterPeaks()
     *  (linear; for the editor's picture of the limiter). */
    double limiterInPeak() const  { return limIn_; }
    double limiterOutPeak() const { return limOut_; }
    void resetLimiterPeaks() { limIn_ = limOut_ = 0.0; }
    /** Peak going into the gate since the last resetGatePeak() (linear; for the editor's picture of the gate). */
    double gateInPeak() const { return gateIn_; }
    void resetGatePeak() { gateIn_ = 0.0; }
    /** A multiband band's detector level now (mean square, the band's own threshold scale is 10 log10(2 ms)). */
    double bandMeanSquare(int band) const { return mb_.getMeanSquare(band); }

    void processFrame(double* io) {
        if (!s_.liveSwitching) {
            for (int slot = 0; slot < kStages; ++slot) if (s_.enable[s_.order[slot]]) runStage(s_.order[slot], io);
            return;
        }
        for (int slot = 0; slot < kStages; ++slot) {
            const int st = s_.order[slot];
            double dry[2] = {io[0], io[1]};
            if (const int n = stageLatency_[st]) {                    // dry path, delayed like the stage delays
                double* d = &dry_[st][(size_t)dryPos_[st] * 2];
                for (int c = 0; c < 2; ++c) { const double out = d[c]; d[c] = dry[c]; dry[c] = out; }
                if (++dryPos_[st] == n) dryPos_[st] = 0;
            }
            runStage(st, io);
            double& m = mix_[st];
            const double target = s_.enable[st] ? 1.0 : 0.0;
            if (m != target) m = target > m ? std::min(1.0, m + mixStep_) : std::max(0.0, m - mixStep_);
            if (m >= 1.0) continue;                                     // fully on: the stage's output, untouched
            if (m <= 0.0) { io[0] = dry[0]; io[1] = dry[1]; continue; }
            for (int c = 0; c < 2; ++c) io[c] = dry[c] + (io[c] - dry[c]) * m;
        }
        if (fadeState_ == kSteady) return;
        if (fadeState_ == kFadeOut) {
            fade_ -= fadeStep_;
            if (fade_ <= 0.0) {
                fade_ = 0.0;
                for (int i = 0; i < kStages; ++i) s_.order[i] = pendingOrder_[i];
                muteLeft_ = getLatency() + 64;                          // let the old order's audio drain out of the delays
                fadeState_ = kMuted;
            }
        } else if (fadeState_ == kMuted) {
            if (--muteLeft_ <= 0) fadeState_ = kFadeIn;
        } else {
            fade_ += fadeStep_;
            if (fade_ >= 1.0) { fade_ = 1.0; fadeState_ = kSteady; }
        }
        io[0] *= fade_; io[1] *= fade_;
    }

private:
    static bool sameGate(const NoiseGate::Settings& a, const NoiseGate::Settings& b) {
        return a.hysteresisDb == b.hysteresisDb && a.reductionDb == b.reductionDb && a.attackMs == b.attackMs
            && a.holdMs == b.holdMs && a.releaseMs == b.releaseMs;
    }
    static bool sameFilter(const AUFilterEQ::Settings& a, const AUFilterEQ::Settings& b) {
        if (a.lowIsCut != b.lowIsCut || a.lowFreq != b.lowFreq || a.lowGain != b.lowGain) return false;
        if (a.highIsCut != b.highIsCut || a.highFreq != b.highFreq || a.highGain != b.highGain) return false;
        for (int i = 0; i < 3; ++i) if (a.freq[i] != b.freq[i] || a.gain[i] != b.gain[i] || a.bw[i] != b.bw[i]) return false;
        return true;
    }
    static bool sameMultiband(const MultibandSettings& a, const MultibandSettings& b) {
        if (a.attackTime != b.attackTime || a.releaseTime != b.releaseTime || a.pregainDb != b.pregainDb || a.postgainDb != b.postgainDb) return false;
        for (int i = 0; i < 3; ++i) if (a.crossover[i] != b.crossover[i]) return false;
        for (int i = 0; i < 4; ++i) if (a.threshold[i] != b.threshold[i] || a.headroom[i] != b.headroom[i]) return false;
        return true;
    }
    void runStage(int st, double* io) {
        switch (st) {
            case kGate:   gateIn_ = std::max(gateIn_, std::max(std::fabs(io[0]), std::fabs(io[1]))); gate_.processFrame(io); break;
            case kComp:   comp_.processFrame(io); break;
            case kFilter: for (int c = 0; c < 2; ++c) if (!(c == 0 && s_.filterLeftBypassed)) io[c] = filter_[c].process(io[c]); break;
            case kGeq:    for (int c = 0; c < 2; ++c) io[c] = geq_[c].process(io[c]); break;
            case kMulti:  mb_.processFrame(io, 2); break;
            case kLimit:
                limIn_ = std::max(limIn_, std::max(std::fabs(io[0]), std::fabs(io[1])) * limiter_.getPregain());
                limiter_.processFrame(io);
                limOut_ = std::max(limOut_, std::max(std::fabs(io[0]), std::fabs(io[1])));
                break;
            default: break;
        }
    }
    enum FadeState { kSteady, kFadeOut, kMuted, kFadeIn };

    Settings s_;
    double sr_ = 44100.0;
    double limIn_ = 0.0, limOut_ = 0.0, gateIn_ = 0.0;
    NoiseGate gate_; Compressor comp_; AUFilterEQ filter_[2]; GraphicEQ31 geq_[2]; MultibandCore mb_; PeakLimiter limiter_;
    int stageLatency_[kStages] = {0, 0, 0, 0, 0, 0};
    std::vector<double> dry_[kStages]; int dryPos_[kStages] = {0, 0, 0, 0, 0, 0};
    double mix_[kStages] = {1, 1, 1, 1, 1, 1}, mixStep_ = 1.0, fadeStep_ = 1.0, fade_ = 1.0;
    int pendingOrder_[kStages] = {0, 1, 2, 3, 4, 5}, muteLeft_ = 0;
    FadeState fadeState_ = kSteady;
};
