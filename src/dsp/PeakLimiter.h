#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

/**
 * Clone of Apple's AUPeakLimiter, measured by driving the real Audio Unit with
 * step, spike and sine signals.
 *
 *  - Pre-gain is applied first; the ceiling is fixed at 0 dBFS.
 *  - Look-ahead = AttackTime (N = round(attack * sr) samples of latency).
 *  - When a sample entering the look-ahead would exceed the ceiling at the
 *    current gain (|x| * g > 1), the gain starts falling at a constant rate in
 *    dB: ln(|x| * g) / (attack * sr - 10) per sample, for N samples. Because
 *    the ramp is computed over 10 samples fewer than it runs, the limiter
 *    always over-shoots (a peak of P ends up at about P^-0.125 for 2 ms).
 *    A request only replaces a running ramp if it is steeper.
 *  - While any over-ceiling sample is still inside the look-ahead window the
 *    gain is held.
 *  - Otherwise (1 - g) halves every ReleaseTime.
 *  - Channels are linked (max of |L|, |R|).
 */
class PeakLimiter {
public:
    void prepare(double sampleRate, double attackSec, double releaseSec, double pregainDb, int channels = 2) {
        sr_ = sampleRate; ch_ = channels;
        n_ = std::max(1, (int)std::lround(attackSec * sr_));
        rampLen_ = std::max(1.0, attackSec * sr_ - 10.0);
        relK_ = std::pow(0.5, 1.0 / (releaseSec * sr_));
        pre_ = std::pow(10.0, pregainDb / 20.0);
        delay_.assign((size_t)n_ * ch_, 0.0);
        reset();
    }
    void reset() {
        std::fill(delay_.begin(), delay_.end(), 0.0);
        pos_ = 0; g_ = 1.0; mul_ = 1.0; slope_ = 0.0; count_ = 0; overAge_ = n_ + 1;
    }
    int getLatency() const { return n_; }
    /** Release and pre-gain can change while running (the attack can't: it is the look-ahead, i.e. the latency). */
    void setRelease(double releaseSec) { relK_ = std::pow(0.5, 1.0 / (releaseSec * sr_)); }
    void setPregainDb(double pregainDb) { pre_ = std::pow(10.0, pregainDb / 20.0); }
    double getPregain() const { return pre_; }
    double getGainReductionDb() const { return -20.0 * std::log10(g_); }
    /** The gain now, linear (<= 1): the lowest over a stretch is the most reduction. */
    double getGain() const { return g_; }

    /** io: one frame (ch_ samples), processed in place; output is delayed by getLatency(). */
    void processFrame(double* io) {
        double a = 0.0;
        for (int c = 0; c < ch_; ++c) { io[c] *= pre_; a = std::max(a, std::fabs(io[c])); }
        // newest sample enters the look-ahead
        if (a > 1.0) overAge_ = 0; else if (overAge_ <= n_) ++overAge_;
        // The gain is kept linear; a ramp's constant dB step is a constant factor. Only starting a ramp takes a
        // log and an exp, so a frame that changes nothing (or releases) costs none.
        const double ag = a * g_;
        if (ag > 1.0) {                                                // ln(|x| * g) > 0
            const double cand = std::log(ag) / rampLen_;
            if (cand > slope_ || count_ == 0) { slope_ = cand; count_ = n_; mul_ = std::exp(-cand); }
        }
        if (count_ > 0) {
            g_ *= mul_;
            if (--count_ == 0) slope_ = 0.0;
        } else if (overAge_ >= n_) {                                   // no over-ceiling sample left in the window
            g_ = 1.0 - (1.0 - g_) * relK_;
        }
        for (int c = 0; c < ch_; ++c) {
            double& d = delay_[(size_t)pos_ * ch_ + c];
            const double out = d * g_; d = io[c]; io[c] = out;
        }
        if (++pos_ == n_) pos_ = 0;
    }
private:
    double sr_ = 44100, pre_ = 1, relK_ = 0, rampLen_ = 78, g_ = 1, mul_ = 1, slope_ = 0;
    int ch_ = 2, n_ = 88, pos_ = 0, count_ = 0, overAge_ = 0;
    std::vector<double> delay_;
};
