#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

/**
 * Clone of the GarageBand / Logic "Noise Gate" (gate mode, side-chain filter off),
 * fitted to GarageBand renders (raw vocal vs. gate-only bounce).
 *
 *  - Detector: peak envelope, instant attack, short exponential fall (kDetectorFallMs).
 *  - Opens when the envelope exceeds Threshold; the gain then rises LINEARLY to 1
 *    in Attack (1 ms -> 45 samples at 44.1 kHz). The audio is delayed by Lookahead
 *    so the ramp starts before the transient (the host compensates the delay).
 *  - Closes when the envelope has stayed below Threshold + Hysteresis for Hold;
 *    the gain then falls exponentially, 20 dB per Release time, down to Reduction.
 */
class NoiseGate {
public:
    static constexpr double kDetectorFallMs = 14.0; // fitted to GarageBand's gate

    struct Settings {
        double thresholdDb = -46.0, hysteresisDb = -6.0, reductionDb = -100.0;
        double attackMs = 1.0, holdMs = 160.0, releaseMs = 27.1, lookaheadMs = 0.6;
        double detectorFallMs = kDetectorFallMs;
    };
    void prepare(double sampleRate, const Settings& s, int channels = 2) {
        sr_ = sampleRate; ch_ = channels; s_ = s;
        setLevelsAndTimes(s);
        look_ = std::max(0, (int)std::floor(s.lookaheadMs * 1e-3 * sr_));
        envMul_ = s.detectorFallMs > 0 ? std::exp(-1.0 / (s.detectorFallMs * 1e-3 * sr_)) : 0.0;
        delay_.assign((size_t)std::max(look_, 1) * ch_, 0.0);
        reset();
    }
    void reset() {
        std::fill(delay_.begin(), delay_.end(), 0.0);
        pos_ = 0; env_ = 0; gain_ = floor_; isOpen_ = false; below_ = 0;
    }
    int getLatency() const { return look_; }
    double getGain() const { return gain_; }
    /** Everything but the look-ahead and the detector, while running (real-time safe). */
    void setLevelsAndTimes(const Settings& s) {
        s_.thresholdDb = s.thresholdDb; s_.hysteresisDb = s.hysteresisDb; s_.reductionDb = s.reductionDb;
        s_.attackMs = s.attackMs; s_.holdMs = s.holdMs; s_.releaseMs = s.releaseMs;
        open_ = std::pow(10.0, s.thresholdDb / 20.0);
        close_ = std::pow(10.0, (s.thresholdDb + s.hysteresisDb) / 20.0);
        floor_ = std::pow(10.0, s.reductionDb / 20.0);
        attackStep_ = 1.0 / (std::floor(s.attackMs * 1e-3 * sr_) + 1.0);
        releaseMul_ = std::pow(10.0, -1.0 / (std::max(s.releaseMs, 1e-3) * 1e-3 * sr_));
        holdSamples_ = (long)std::lround(s.holdMs * 1e-3 * sr_);
    }
    /** Move the threshold while running (the hysteresis point moves with it). */
    void setThresholdDb(double db) {
        if (db == s_.thresholdDb) return;
        s_.thresholdDb = db;
        open_ = std::pow(10.0, db / 20.0);
        close_ = std::pow(10.0, (db + s_.hysteresisDb) / 20.0);
    }

    void processFrame(double* io) {
        double a = 0.0;
        for (int c = 0; c < ch_; ++c) a = std::max(a, std::fabs(io[c]));
        env_ = std::max(a, env_ * envMul_);
        if (!isOpen_) {
            if (env_ > open_) { isOpen_ = true; below_ = 0; }
        } else {
            if (env_ > close_) below_ = 0;
            else if (++below_ > holdSamples_) isOpen_ = false;
        }
        if (isOpen_) gain_ = std::min(1.0, gain_ + attackStep_);
        else gain_ = std::max(floor_, gain_ * releaseMul_);
        if (look_ > 0) {
            for (int c = 0; c < ch_; ++c) {
                double& d = delay_[(size_t)pos_ * ch_ + c];
                const double out = d * gain_; d = io[c]; io[c] = out;
            }
            if (++pos_ == look_) pos_ = 0;
        } else {
            for (int c = 0; c < ch_; ++c) io[c] *= gain_;
        }
    }
private:
    Settings s_;
    double sr_ = 44100, open_ = 0, close_ = 0, floor_ = 0, attackStep_ = 0, releaseMul_ = 0, envMul_ = 0, env_ = 0, gain_ = 0;
    long holdSamples_ = 0, below_ = 0;
    int ch_ = 2, look_ = 0, pos_ = 0;
    bool isOpen_ = false;
    std::vector<double> delay_;
};
