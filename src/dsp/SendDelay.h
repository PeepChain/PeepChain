#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

/**
 * The "Echo" on GarageBand's master track, used as a send (100 % wet). Peep's settings: Time 1/4 note,
 * Feedback 38 %, Color -11. His projects stay at GarageBand's default 120 bpm whatever the song's real
 * tempo, so the delay is always 500 ms and does not follow the host.
 *
 * Measured from GarageBand bounces of a click / sweep test signal. The Echo is linear and does not change over time, and every repeat is the
 * previous one passed through the same small filter and turned down:
 *
 *     v[n] = F( in[n - D] + feedback * v[n - D] )        out = level * v
 *
 *  - D is exactly 22050 samples (500 ms).
 *  - F is a one-pole low-pass at 9.85 kHz in series with a one-pole high-pass at 17 Hz (both bilinear),
 *    so each repeat is a little duller than the last. That is what Color -11 does.
 *  - The gain round the loop is 0.234 (-12.6 dB), not the 0.38 the "38 %" suggests.
 *  - `level` is the first repeat's level relative to the dry signal at Peep's send amount (-16.5 on the
 *    track's Echo send): -13.4 dB, i.e. about 3 dB more than the send figure.
 * Mono in, the same signal on both sides out. No latency.
 *
 * The delay time can also be changed while it runs (the plugin's tempo sync and TIME): prepare() with room for
 * the longest time, then setDelaySamples(). The read point glides over to the new time in 20 ms so it doesn't
 * click. Feedback and the two filters can change too (the plugin's Delay settings).
 */
class SendDelay {
public:
    struct Settings {
        double delaySeconds = 0.5;       // 1/4 note at 120 bpm
        double feedback = 0.23417;       // fitted
        double lowpassHz = 9851.8;       // fitted
        double highpassHz = 16.863;      // fitted
        double level = 0.21408;          // first repeat vs dry, at Peep's send amount
    };
    /** maxDelaySeconds: the longest time setDelaySamples() may ask for later (0: just s.delaySeconds). */
    void prepare(double sampleRate, const Settings& s, double maxDelaySeconds = 0.0) {
        s_ = s;
        len_ = std::max(1, (int)std::lround(s.delaySeconds * sampleRate));
        cap_ = std::max(len_, (int)std::ceil(maxDelaySeconds * sampleRate));
        buf_.assign((size_t)cap_, 0.0f);
        fadeLen_ = std::max(1, (int)std::lround(0.02 * sampleRate));
        sr_ = sampleRate;
        setFilters(s.lowpassHz, s.highpassHz);
        reset();
    }
    /** The loop's two filters, while running (real-time safe). */
    void setFilters(double lowpassHz, double highpassHz) {
        s_.lowpassHz = lowpassHz; s_.highpassHz = highpassHz;
        const double pi = 3.141592653589793;
        const double kl = std::tan(pi * std::min(lowpassHz, 0.49 * sr_) / sr_);
        lpB_ = kl / (1.0 + kl); lpP_ = (1.0 - kl) / (1.0 + kl);
        const double kh = std::tan(pi * std::min(highpassHz, 0.49 * sr_) / sr_);
        hpB_ = 1.0 / (1.0 + kh); hpP_ = (1.0 - kh) / (1.0 + kh);
    }
    /** The gain round the loop, while running. */
    void setFeedback(double loopGain) { s_.feedback = loopGain; }
    const Settings& getSettings() const { return s_; }
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.0f); pos_ = 0; fadeLeft_ = 0; oldLen_ = len_; lpX_ = lpY_ = hpX_ = hpY_ = 0.0; }
    int getDelaySamples() const { return len_; }
    int getMaxDelaySamples() const { return cap_; }

    /** A new delay time (clamped to what prepare() made room for). Real-time safe. */
    void setDelaySamples(int samples) {
        samples = std::clamp(samples, 1, cap_);
        if (samples == len_) return;
        oldLen_ = len_; len_ = samples; fadeLeft_ = fadeLen_;
    }

    /** One sample of the (mono) send signal in, one sample of echo out. */
    double process(double in) {
        double d = tap(len_);                                             // what went round the loop D samples ago
        if (fadeLeft_ > 0) { const double a = (double)fadeLeft_-- / (double)fadeLen_; d = a * tap(oldLen_) + (1.0 - a) * d; }
        const double lp = lpB_ * (d + lpX_) + lpP_ * lpY_; lpX_ = d; lpY_ = lp;
        const double v = hpB_ * (lp - hpX_) + hpP_ * hpY_; hpX_ = lp; hpY_ = v;
        buf_[(size_t)pos_] = (float)(in + s_.feedback * v);
        if (++pos_ == cap_) pos_ = 0;
        return v * s_.level;
    }
private:
    Settings s_;
    double sr_ = 44100.0, lpB_ = 0, lpP_ = 0, hpB_ = 1, hpP_ = 0, lpX_ = 0, lpY_ = 0, hpX_ = 0, hpY_ = 0;
    double tap(int delay) const { const int i = pos_ - delay; return buf_[(size_t)(i < 0 ? i + cap_ : i)]; }
    int len_ = 1, cap_ = 1, pos_ = 0, oldLen_ = 1, fadeLeft_ = 0, fadeLen_ = 1;
    std::vector<float> buf_;                               // float: a long line (4 s) in half the memory
};
