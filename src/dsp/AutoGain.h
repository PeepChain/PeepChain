#pragma once
#include "DspMath.h"
#include <algorithm>
#include <cmath>

/**
 * Loudness match for the end of the plugin: turns the output up or down so it is as loud as what came in,
 * so switching the plugin on and off compares the sound, not the level (louder always sounds "better").
 *
 * Loudness is measured as in ITU-R BS.1770 (K-weighting: a high-shelf of +4 dB above ~1.7 kHz and a
 * high-pass at ~38 Hz, then mean square), because plain RMS undercounts the top end the chain adds.
 *  - Input and output each get a running loudness meter: the 400 ms ("momentary") level is averaged over
 *    averageMs, but only while it is above gateDb (-60 LUFS) and no more than 10 LU below that meter's own
 *    running loudness (BS.1770's absolute and relative gates). Until a meter has seen averageMs of signal it
 *    is a plain running mean, so the match is right within the first phrase (hosts reset plugins on every play).
 *  - The correction is 10*log10(input / output loudness), clamped to +-maxDb.
 *  - The applied gain follows that correction (or 0 dB while switched off) in dB, smoothed over smoothMs.
 * Stereo is measured as the mean of the two channels' power. No latency.
 */
class AutoGain {
public:
    struct Settings { double averageMs = 3000.0, momentaryMs = 400.0, gateDb = -60.0, smoothMs = 300.0, maxDb = 30.0; };

    void prepare(double sampleRate) { prepare(sampleRate, Settings{}); }
    void prepare(double sampleRate, const Settings& s) {
        s_ = s; sr_ = sampleRate;
        for (auto* k : {&inK_[0], &inK_[1], &outK_[0], &outK_[1]}) k->design(sampleRate);
        aLong_ = coef(s.averageMs); aShort_ = coef(s.momentaryMs); aSmooth_ = coef(s.smoothMs);
        gate_ = std::pow(10.0, (s.gateDb + 0.691) / 10.0);           // LUFS = -0.691 + 10*log10(mean square)
        reset();
    }
    void reset() {
        for (auto* k : {&inK_[0], &inK_[1], &outK_[0], &outK_[1]}) k->reset();
        in_.reset(); out_.reset(); targetDb_ = 0.0; appliedDb_ = 0.0;
    }

    /** One input frame (before the plugin touches it): returns its K-weighted power, to hand to process(). */
    double inputPower(double l, double r) {
        const double a = inK_[0].process(l), b = inK_[1].process(r);
        return 0.5 * (a * a + b * b);
    }

    /** One output frame in place. inPower is inputPower() of the matching input frame. */
    void process(double* io, double inPower, bool on) {
        const double a = outK_[0].process(io[0]), b = outK_[1].process(io[1]);
        const double outPower = 0.5 * (a * a + b * b);
        // Each side is measured as a loudness meter would (BS.1770 "integrated"), gated on its own level: an
        // absolute floor, and nothing more than 10 LU below its own running loudness. So pauses, breaths and room
        // noise count on neither side (the chain's noise gate silences them on the output only).
        in_.push(inPower, aShort_, aLong_, gate_); out_.push(outPower, aShort_, aLong_, gate_);
        if (in_.count > 0 && out_.count > 0 && out_.loud > 1e-12)
            targetDb_ = std::clamp(10.0 * std::log10((in_.loud + 1e-30) / out_.loud), -s_.maxDb, s_.maxDb);
        appliedDb_ += ((on ? targetDb_ : 0.0) - appliedDb_) * aSmooth_;
        if (!on && std::abs(appliedDb_) < 1e-4) appliedDb_ = 0.0;     // switched off and settled: exactly untouched
        if (appliedDb_ != 0.0) {
            const double g = std::pow(10.0, appliedDb_ / 20.0);
            io[0] *= g; io[1] *= g;
        }
    }
    /** The gain being applied now, in dB (0 while off and settled). */
    double getAppliedDb() const { return appliedDb_; }
    /** The correction it would apply if switched on, in dB. */
    double getMatchDb() const { return targetDb_; }

private:
    /** BS.1770 K-weighting: pre-filter (high shelf) then RLB high-pass, designed for any sample rate. */
    struct KWeight {
        double b[2][3] = {}, a[2][3] = {}, z[2][2] = {};
        void design(double fs) {
            const double pi = dspmath::kPi;
            {   // high shelf
                const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
                const double K = std::tan(pi * f0 / fs), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
                const double a0 = 1.0 + K / Q + K * K;
                b[0][0] = (Vh + Vb * K / Q + K * K) / a0; b[0][1] = 2.0 * (K * K - Vh) / a0; b[0][2] = (Vh - Vb * K / Q + K * K) / a0;
                a[0][1] = 2.0 * (K * K - 1.0) / a0; a[0][2] = (1.0 - K / Q + K * K) / a0;
            }
            {   // high-pass
                const double f0 = 38.13547087602444, Q = 0.5003270373238773;
                const double K = std::tan(pi * f0 / fs), a0 = 1.0 + K / Q + K * K;
                b[1][0] = 1.0; b[1][1] = -2.0; b[1][2] = 1.0;
                a[1][1] = 2.0 * (K * K - 1.0) / a0; a[1][2] = (1.0 - K / Q + K * K) / a0;
            }
        }
        void reset() { for (auto& s : z) s[0] = s[1] = 0.0; }
        double process(double x) {
            for (int i = 0; i < 2; ++i) {                              // transposed direct form II
                const double y = b[i][0] * x + z[i][0];
                z[i][0] = b[i][1] * x - a[i][1] * y + z[i][1];
                z[i][1] = b[i][2] * x - a[i][2] * y;
                x = y;
            }
            return x;
        }
    };
    double coef(double ms) const { return ms <= 0 ? 1.0 : 1.0 - std::exp(-1.0 / (ms * 1e-3 * sr_)); }

    Settings s_;
    double sr_ = 44100, aLong_ = 1, aShort_ = 1, aSmooth_ = 1, gate_ = 0;
    /** Gated running loudness of one signal (mean square of the K-weighted signal). */
    struct Meter {
        double momentary = 0, loud = 0, count = 0;
        void reset() { momentary = loud = count = 0; }
        void push(double p, double aShort, double aLong, double floor) {
            momentary += (p - momentary) * aShort;
            if (momentary > floor && (count == 0 || momentary > loud * 0.1))      // absolute and relative (-10 LU) gates
                loud += (momentary - loud) * std::max(aLong, 1.0 / ++count);      // running mean until averageMs of signal
        }
    };
    Meter in_, out_;
    double targetDb_ = 0, appliedDb_ = 0;
    KWeight inK_[2], outK_[2];
};
