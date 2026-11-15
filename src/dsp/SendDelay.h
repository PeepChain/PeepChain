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
    void prepare(double sampleRate, const Settings& s) {
        s_ = s;
        len_ = std::max(1, (int)std::lround(s.delaySeconds * sampleRate));
        buf_.assign((size_t)len_, 0.0);
        const double pi = 3.141592653589793;
        const double kl = std::tan(pi * std::min(s.lowpassHz, 0.49 * sampleRate) / sampleRate);
        lpB_ = kl / (1.0 + kl); lpP_ = (1.0 - kl) / (1.0 + kl);
        const double kh = std::tan(pi * s.highpassHz / sampleRate);
        hpB_ = 1.0 / (1.0 + kh); hpP_ = (1.0 - kh) / (1.0 + kh);
        reset();
    }
    void reset() { std::fill(buf_.begin(), buf_.end(), 0.0); pos_ = 0; lpX_ = lpY_ = hpX_ = hpY_ = 0.0; }
    int getDelaySamples() const { return len_; }

    /** One sample of the (mono) send signal in, one sample of echo out. */
    double process(double in) {
        const double d = buf_[(size_t)pos_];                              // what went round the loop D samples ago
        const double lp = lpB_ * (d + lpX_) + lpP_ * lpY_; lpX_ = d; lpY_ = lp;
        const double v = hpB_ * (lp - hpX_) + hpP_ * hpY_; hpX_ = lp; hpY_ = v;
        buf_[(size_t)pos_] = in + s_.feedback * v;
        if (++pos_ == len_) pos_ = 0;
        return v * s_.level;
    }
private:
    Settings s_;
    double lpB_ = 0, lpP_ = 0, hpB_ = 1, hpP_ = 0, lpX_ = 0, lpY_ = 0, hpX_ = 0, hpY_ = 0;
    int len_ = 1, pos_ = 0;
    std::vector<double> buf_;
};
