#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

/**
 * Level detector matching GarageBand's AUMultibandCompressor.
 *
 * The reference does not use a classic attack/release envelope follower.
 * The level that drives the gain computer at time t is the mean square of
 * the band signal over the NEXT ~5.8 ms (256 samples at 44.1 kHz), i.e. a
 * rectangular look-ahead RMS window.
 *
 * In a real-time implementation this means the audio path is delayed by
 * (window - 1) samples while the detector sees the undelayed signal; the
 * host compensates the reported latency, which is why Apple's renders are
 * sample-aligned with the dry signal.
 *
 * Usage: push every input sample; the returned value is the mean square of
 * the last `window` samples, which is the level to apply to the sample that
 * was pushed (window - 1) samples ago.
 */
class EnvelopeFollower {
public:
    EnvelopeFollower() { setWindow(256); }

    /** Window length in samples (>= 1). */
    void setWindow(int samples) {
        window_ = std::max(samples, 1);
        buffer_.assign(static_cast<size_t>(window_), 0.0);
        reset();
    }

    int getWindow() const { return window_; }
    int getLatency() const { return window_ - 1; }

    void reset() {
        std::fill(buffer_.begin(), buffer_.end(), 0.0);
        pos_ = 0;
        sum_ = 0.0;
        sinceRefresh_ = 0;
    }

    /** Push one sample, get the mean square of the last `window` samples. */
    double processSample(double sample) {
        const double sq = sample * sample;
        sum_ += sq - buffer_[static_cast<size_t>(pos_)];
        buffer_[static_cast<size_t>(pos_)] = sq;
        if (++pos_ == window_) pos_ = 0;

        // The running sum accumulates rounding error; rebuild it now and then.
        if (++sinceRefresh_ >= 16 * window_) {
            sinceRefresh_ = 0;
            double s = 0.0;
            for (double v : buffer_) s += v;
            sum_ = s;
        }
        if (sum_ < 0.0) sum_ = 0.0;
        return sum_ / window_;
    }

    /** Level of the current window in dBFS (RMS). */
    static double toDb(double meanSquare) {
        return 10.0 * std::log10(meanSquare + 1e-30);
    }

private:
    int window_ = 256;
    std::vector<double> buffer_;
    int pos_ = 0;
    double sum_ = 0.0;
    int sinceRefresh_ = 0;
};
