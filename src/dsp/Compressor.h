#pragma once
#include <algorithm>
#include <cmath>

/**
 * Clone of the GarageBand / Logic "Compressor" at Lil Peep's settings (Platinum Digital, RMS,
 * threshold -26 dB, ratio 8:1, attack 2 ms, release 48 ms, make-up 11 dB, knee 0.7, side chain "Max",
 * auto gain / limiter / distortion off). It cannot be hosted outside GarageBand, so it was measured
 * from a GarageBand bounce of a synthetic test signal and then checked on three vocal bounces that were
 * not used for the fit.
 *
 *  - Detector: one-pole average of x^2 with tau = 160 ms / ln 10 (= 69.5 ms). No latency.
 *  - Static curve on that RMS level: nothing below (Threshold - 6.15 dB); above it, with o = dB over
 *    that point,  gain = -0.8955 * o^2 / (o + 7.56) dB.  It starts with zero slope and bends slowly
 *    toward ~9.5:1; there is no straight 8:1 segment.
 *  - The linear gain is then smoothed by a one-pole: tau = Attack / ln 10 when falling,
 *    Release / ln 10 when recovering (the knob values are times to reach 10 %).
 *  - Make-up gain is a plain gain.
 *
 * Gain match (plugin only, off unless Settings::matchThresholdDb is set): moving the Threshold away from
 * Peep's changes how much level reaches the plugins after this one (the multiband and limiter thresholds are
 * fixed levels), by an amount that depends on how loud the take is. With matching on, a slow level
 * follower tracks the loud passages of the detector level (L) and the output gets
 *     curve(L at matchThresholdDb) - curve(L at Threshold)
 * so the loud passages leave at the level they would with Peep's threshold, while the compression
 * itself still follows the Threshold. At Threshold == matchThresholdDb the correction is exactly 0.
 *
 * Accuracy: gain within 0.006 dB rms of GarageBand on the test signal, < 0.01 dB rms on the vocals.
 * The curve constants were measured at ratio 8:1 / knee 0.7 only; other ratio or knee settings are
 * not modelled.
 */
class Compressor {
public:
    struct Settings {
        double thresholdDb = -26.0, ratio = 8.0, attackMs = 2.0, releaseMs = 48.0, makeupDb = 11.0, knee = 0.7;
        double matchThresholdDb = std::nan("");   // gain match reference (the plugin uses Peep's -26); NaN = off
    };
    struct Fit {
        int    mode = 1;                 // 0: smooth the gain in dB, 1: smooth the linear gain (matches GarageBand)
        // Static curve, measured with a level staircase:
        //   o = rmsLevel - (Threshold + curveStartDb);  gain = -curveSlope * o^2 / (o + curveKneeDb)  for o > 0
        double curveStartDb = -6.149;    // the curve starts this far from the Threshold setting (RMS terms)
        double curveSlope   = 0.8955;    // asymptotic slope (~9.5:1); the window says 8:1
        double curveKneeDb  = 7.558;
        double rmsMs = 160.0 * 0.4342944819;   // RMS averaging time constant (one-pole on x^2): 160 ms / ln 10
        double attackScale = 0.4342944819;     // gain smoothing when falling:    tau = attackMs  / ln 10
        double releaseScale = 0.4342944819;    // gain smoothing when recovering: tau = releaseMs / ln 10
        // gain match: loud-passage level follower and smoothing of the correction
        double matchLevelMs = 2000.0;          // follower time constant (power domain)
        double matchWindowDb = 6.0;            // only levels within this of the follower move it (pauses don't)
        double matchStartDb = -60.0;           // the follower starts at the first level above this
        double matchSmoothMs = 100.0;          // smoothing of the correction itself
    };
    void prepare(double sampleRate, const Settings& s, const Fit& f, int channels = 2) {
        sr_ = sampleRate; s_ = s; f_ = f; ch_ = channels;
        aRms_ = coef(f.rmsMs); aAtt_ = coef(s.attackMs * f.attackScale); aRel_ = coef(s.releaseMs * f.releaseScale);
        aLoud_ = coef(f.matchLevelMs); aMatch_ = coef(f.matchSmoothMs);
        matchStartPow_ = std::pow(10.0, f.matchStartDb / 10.0); matchWindowPow_ = std::pow(10.0, -f.matchWindowDb / 10.0);
        curvePow_ = 0.0; setThresholdDb(s.thresholdDb); setMakeupDb(s.makeupDb);
        reset();
    }
    void reset() { ms_ = 0; smooth_ = (f_.mode == 1) ? 1.0 : 0.0; loud_ = 0; loudInit_ = false; matchDb_ = 0; loudCount_ = 0; }
    /** Gain reduction now (dB, positive). Computed when asked: the audio path itself only needs the linear gain. */
    double getGainReductionDb() const { return f_.mode == 0 ? -smooth_ : -20.0 * std::log10(smooth_); }
    /** The compression gain now, linear (<= 1; without make-up). Lower means more reduction, so the most reduction
     *  over a stretch is getGainReductionDb() at the lowest of these. */
    double getGainLinear() const { return f_.mode == 0 ? std::pow(10.0, smooth_ / 20.0) : smooth_; }
    /** The detector's level now (RMS, dBFS): what the curve is read at. */
    double getLevelDb() const { return 10.0 * std::log10(ms_ + 1e-30); }
    /** Move the threshold while running; the whole curve is defined relative to it. */
    void setThresholdDb(double db) {
        if (db == s_.thresholdDb && curvePow_ > 0.0) return;
        s_.thresholdDb = db;
        curvePow_ = std::pow(10.0, (db + f_.curveStartDb) / 10.0);   // the mean square where the curve starts
    }
    /** Move the attack / release while running. */
    void setTimes(double attackMs, double releaseMs) {
        s_.attackMs = attackMs; s_.releaseMs = releaseMs;
        aAtt_ = coef(attackMs * f_.attackScale); aRel_ = coef(releaseMs * f_.releaseScale);
    }
    /** Move the make-up gain while running (a plain gain after the compression). */
    void setMakeupDb(double db) { s_.makeupDb = db; makeupLin_ = std::pow(10.0, db / 20.0); }

    /** Current gain-match correction in dB (0 when matching is off or at the reference threshold). */
    double getMatchDb() const { return matchDb_; }

    double curve(double levelDb) const { return curveAt(levelDb, s_.thresholdDb); }
    double curveAt(double levelDb, double thresholdDb) const {   // gain in dB (<= 0) for an RMS level in dBFS
        const double o = levelDb - (thresholdDb + f_.curveStartDb);
        return o <= 0.0 ? 0.0 : -f_.curveSlope * o * o / (o + f_.curveKneeDb);
    }
    void processFrame(double* io) {
        double p = 0.0;
        for (int c = 0; c < ch_; ++c) p = std::max(p, io[c] * io[c]);  // side chain detection: Max
        ms_ += (p - ms_) * aRms_;
        if (f_.mode == 0) { processFrameDb(io); return; }
        // Linear gain smoothing (what GarageBand does). The same sums as written in the class comment, arranged
        // so that the logs and powers are only taken when they can change the result: below the curve's start
        // (and while the gain match has nothing to correct) a frame costs no transcendental function at all.
        const double m = ms_ + 1e-30;
        const double t = m > curvePow_ ? std::pow(10.0, curve(10.0 * std::log10(m)) / 20.0) : 1.0;
        smooth_ += (t - smooth_) * (t < smooth_ ? aAtt_ : aRel_);
        if (!std::isnan(s_.matchThresholdDb)) updateMatch(m);
        const double g = smooth_ * makeupLin_ * (matchDb_ == 0.0 ? 1.0 : std::pow(10.0, matchDb_ / 20.0));
        for (int c = 0; c < ch_; ++c) io[c] *= g;
    }
private:
    /** The gain match's follower (see the class comment). Comparisons are made on the mean square rather than
     *  in dB, which is the same test without the logs. */
    void updateMatch(double m) {
        // follower: one-pole average of the detector's mean square, fed only while within the window of it
        // Until it has seen matchLevelMs worth of signal it is a plain running mean (weight 1/count),
        // so it settles on the take's level within the first phrase instead of creeping up from the
        // first sound over several seconds (hosts reset plugins every time playback starts).
        if (!loudInit_) { if (m > matchStartPow_) { loud_ = ms_; loudInit_ = true; loudCount_ = 1; } }
        else if (m > loud_ * matchWindowPow_) {
            loud_ += (ms_ - loud_) * std::max(aLoud_, 1.0 / ++loudCount_);
        }
        double want = 0.0;
        if (loudInit_ && s_.thresholdDb != s_.matchThresholdDb) {          // at the reference it is exactly 0
            const double L = 10.0 * std::log10(loud_ + 1e-30);
            want = curveAt(L, s_.matchThresholdDb) - curveAt(L, s_.thresholdDb);
        }
        matchDb_ += (want - matchDb_) * aMatch_;
    }
    /** Smoothing in dB (Fit::mode 0, not what GarageBand does; kept for the fitting tools). */
    void processFrameDb(double* io) {
        const double levelDb = 10.0 * std::log10(ms_ + 1e-30);
        const double target = curve(levelDb);
        smooth_ += (target - smooth_) * (target < smooth_ ? aAtt_ : aRel_);
        if (!std::isnan(s_.matchThresholdDb)) updateMatch(ms_ + 1e-30);
        const double g = std::pow(10.0, (smooth_ + s_.makeupDb + matchDb_) / 20.0);
        for (int c = 0; c < ch_; ++c) io[c] *= g;
    }
    double coef(double ms) const { return ms <= 0 ? 1.0 : 1.0 - std::exp(-1.0 / (ms * 1e-3 * sr_)); }
    Settings s_; Fit f_;
    double sr_ = 44100, aRms_ = 1, aAtt_ = 1, aRel_ = 1, ms_ = 0, smooth_ = 0;
    double curvePow_ = 0, makeupLin_ = 1, matchStartPow_ = 0, matchWindowPow_ = 1;
    double aLoud_ = 1, aMatch_ = 1, loud_ = 0, matchDb_ = 0, loudCount_ = 0; bool loudInit_ = false;
    int ch_ = 2;
};
