#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

/**
 * A plain delay line (mono) whose time can change while it runs: the reverb's pre-delay, so it can follow
 * the song's tempo. prepare() with room for the longest time, then setDelaySamples(). Like SendDelay, the
 * read point glides over to a new time in 20 ms so it doesn't click. A delay of 0 passes the input straight on.
 */
class PreDelay {
public:
    void prepare(double sampleRate, double maxDelaySeconds) {
        cap_ = std::max(1, (int)std::ceil(maxDelaySeconds * sampleRate)) + 1;
        buf_.assign((size_t)cap_, 0.0f);
        fadeLen_ = std::max(1, (int)std::lround(0.02 * sampleRate));
        len_ = std::min(len_, cap_ - 1);
        reset();
    }
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.0f); pos_ = 0; fadeLeft_ = 0; oldLen_ = len_; }
    int getDelaySamples() const { return len_; }

    /** A new delay time (clamped to what prepare() made room for). Real-time safe. */
    void setDelaySamples(int samples) {
        samples = std::clamp(samples, 0, cap_ - 1);
        if (samples == len_) return;
        oldLen_ = len_; len_ = samples; fadeLeft_ = fadeLen_;
    }

    double process(double in) {
        buf_[(size_t)pos_] = (float)in;
        double d = tap(len_);
        if (fadeLeft_ > 0) { const double a = (double)fadeLeft_-- / (double)fadeLen_; d = a * tap(oldLen_) + (1.0 - a) * d; }
        if (++pos_ == cap_) pos_ = 0;
        return d;
    }
private:
    double tap(int delay) const { const int i = pos_ - delay; return buf_[(size_t)(i < 0 ? i + cap_ : i)]; }
    int len_ = 0, cap_ = 1, pos_ = 0, oldLen_ = 0, fadeLeft_ = 0, fadeLen_ = 1;
    std::vector<float> buf_ = std::vector<float>(1, 0.0f);   // float: what it feeds (the convolution) is float anyway
};
