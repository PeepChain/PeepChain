#pragma once
#include "DspMath.h"

#include "EnvelopeFollower.h"
#include <cmath>
#include <algorithm>
#include <vector>

/**
 * One band of Apple's AUMultibandCompressor, measured by driving the real
 * Audio Unit with DC steps and steady tones.
 *
 * The AU works in blocks of 64 samples and delays the audio by 256 samples.
 * For the output block starting at sample b:
 *
 * Detector
 *   level = mean square of the band input over [b, b + 256), i.e. the block
 *   itself plus 192 samples of look-ahead. Updated once per block.
 *
 * Static curve (threshold T, headroom H in dB; exact to < 0.01 dB for H = 0.1...40)
 *   d   = 10*log10(2 * meanSquare) - T        (threshold is in sine-peak terms)
 *   out = H * (2/pi) * atan(pi/2 * d / H)     (slope 1 at T, saturates at T + H)
 *   target gain = out - d   for d > 0, else 0
 *
 * Ballistics (the branch is chosen once per block and kept for the whole block)
 *   Attack  (target below current gain): one-pole per sample on the linear
 *           gain, tau = AttackTime / ln 10 (AttackTime = the time to get 90 % of the way).
 *           (Fitted to the real AU at 1-200 ms with steps from silence into heavy reduction: exact
 *           to 0.002 dB. An earlier fit at light reduction had ln 9, which made onsets up to 0.6 dB
 *           too loud: a low-end "punch" in null tests.)
 *   Release (target above current gain): straight line in dB, 5 dB per
 *           ReleaseTime, NOT clamped to the target inside the block (so it
 *           can overshoot by up to one block; the next block's attack
 *           corrects it).
 *           The release rate is chosen per block from a second, slow detector:
 *           S = one-pole average of the 256-sample mean square, updated once
 *           per block, time constant ~0.157 s, starting at 0. While the current
 *           level is ABOVE that average (a sound that has not lasted long) the
 *           gain recovers at a fixed 250 dB/s, whatever the Release setting;
 *           once the level has fallen below its own average, at 5 dB per
 *           ReleaseTime.
 */
class BandCompressor {
public:
    static constexpr int    kBlock            = 64;
    static constexpr int    kWindow           = 256;
    static constexpr int    kMaxChannels      = 2;
    static constexpr double kTimingRate       = 44100.0;   // see updateCoefficients()
    static constexpr double kReleaseDbPerTime = 5.0;
    static constexpr double kFastReleaseDbPerSec = 250.0;
    static constexpr double kSlowAverageSec = 108.0 * 64.0 / 44100.0;  // ~0.157 s, fitted (104-108 blocks fit the probes)

    BandCompressor() { setSampleRate(44100.0); }

    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate;
        detector_.setWindow(kWindow);
        delay_.assign(static_cast<size_t>(kWindow * kMaxChannels), 0.0);
        updateCoefficients();
        reset();
    }

    void setAttackTime(double seconds)  { attackTime_  = std::max(seconds, 1e-5); updateCoefficients(); }
    void setReleaseTime(double seconds) { releaseTime_ = std::max(seconds, 1e-5); updateCoefficients(); }
    void setThreshold(double dB)        { threshold_   = std::clamp(dB, -100.0, 0.0); }
    void setHeadroom(double dB)         { headroom_    = std::clamp(dB, 0.1, 40.0); }

    /** Where the 64-sample block grid starts (0..63). The real AU's grid depends on how many samples the
     *  host has already pushed through it, so it is arbitrary; exposed only to compare against renders. */
    void setBlockPhase(int samples) { initialPhase_ = ((samples % kBlock) + kBlock) % kBlock; phase_ = initialPhase_; }

    /** Samples of delay between input and output. */
    int getLatency() const { return kWindow; }

    /** Current gain reduction in dB (positive number). */
    double getGainReduction() const { return mode_ == Attack ? -20.0 * std::log10(gainLin_) : -gainDb_; }
    /** The gain now, linear (<= 1): the lowest over a stretch is the most reduction. */
    double getGainLinear() const { return gainLin_; }

    /** The detector's mean square now (the level the curve reads, for metering). */
    double getMeanSquare() const { return lastMeanSquare_; }

    void reset() {
        detector_.reset();
        std::fill(delay_.begin(), delay_.end(), 0.0);
        delayPos_ = 0; phase_ = initialPhase_; slowMeanSquare_ = 0.0; gainDb_ = 0.0; gainLin_ = 1.0; targetLin_ = 1.0; mode_ = Hold; lastMeanSquare_ = 0.0;
    }

    /** Static curve: gain in dB (<= 0) for a mean-square level. */
    double targetGainDb(double meanSquare) const {
        const double d = 10.0 * std::log10(2.0 * meanSquare + 1e-30) - threshold_;
        if (d <= 0.0) return 0.0;
        const double out = headroom_ * (2.0 / dspmath::kPi) * std::atan(0.5 * dspmath::kPi * d / headroom_);
        return out - d;
    }

    /** Mono convenience wrapper. */
    double processSample(double input) { processFrame(&input, 1); return input; }

    /**
     * Process one frame of `channels` samples in place (channels <= kMaxChannels).
     * The AU links its channels: the detector listens to the mono sum (L+R)/2 of the
     * band and the same gain is applied to every channel. Output is delayed by getLatency().
     */
    void processFrame(double* io, int channels) {
        if (phase_ == 0) {
            // lastMeanSquare_ covers the 256 samples before this one = [b, b+256) for the output block b.
            // While attacking only the linear gain is kept up to date; its dB value is needed here, once a block.
            if (mode_ == Attack) gainDb_ = 20.0 * std::log10(gainLin_);
            const double target = targetGainDb(lastMeanSquare_);
            if (target < gainDb_ - 1e-9)      { mode_ = Attack; targetLin_ = std::pow(10.0, target / 20.0); }
            else if (target > gainDb_ + 1e-9) {
                mode_ = Release; releaseStep_ = (lastMeanSquare_ > slowMeanSquare_) ? fastReleaseStepDb_ : releaseStepDb_;
                // a straight line in dB is a constant factor per sample: two pows a block instead of one a sample
                releaseMul_ = std::pow(10.0, releaseStep_ / 20.0); gainLin_ = std::pow(10.0, gainDb_ / 20.0);
            }
            else                              mode_ = Hold;
            slowMeanSquare_ += slowCoeff_ * (lastMeanSquare_ - slowMeanSquare_);
        }
        if (++phase_ == kBlock) phase_ = 0;

        double mid = 0.0;
        for (int c = 0; c < channels; ++c) mid += io[c];
        lastMeanSquare_ = detector_.processSample(mid / channels);

        if (mode_ == Attack) {
            gainLin_ = attackCoeff_ * gainLin_ + (1.0 - attackCoeff_) * targetLin_;
        } else if (mode_ == Release) {
            gainDb_ += releaseStep_;
            if (gainDb_ >= 0.0) { gainDb_ = 0.0; gainLin_ = 1.0; } else gainLin_ *= releaseMul_;
        }
        for (int c = 0; c < channels; ++c) {
            double& d = delay_[static_cast<size_t>(delayPos_ * kMaxChannels + c)];
            const double out = d * gainLin_; d = io[c]; io[c] = out;
        }
        if (++delayPos_ == kWindow) delayPos_ = 0;
    }

private:
    enum Mode { Hold, Attack, Release };
    void updateCoefficients() {
        // Measured quirk of the real AU: every time constant is computed as if the sample rate were
        // 44.1 kHz, whatever the host rate is (and the 64 / 256-sample block and window never change).
        // At 48 kHz it therefore runs ~9 % faster in real time, at 96 kHz about twice as fast.
        // Only the crossover filters follow the real sample rate. Reproduced here on purpose.
        attackCoeff_   = std::exp(-std::log(10.0) / (attackTime_ * kTimingRate));
        releaseStepDb_ = kReleaseDbPerTime / (releaseTime_ * kTimingRate);
        fastReleaseStepDb_ = kFastReleaseDbPerSec / kTimingRate;
        slowCoeff_ = 1.0 - std::exp(-kBlock / (kSlowAverageSec * kTimingRate));
    }

    double sampleRate_  = 44100.0;
    double threshold_   = -50.0;
    double headroom_    = 6.0;
    double attackTime_  = 0.02;
    double releaseTime_ = 0.04;

    double attackCoeff_ = 0.0, releaseStepDb_ = 0.0, fastReleaseStepDb_ = 0.0, releaseStep_ = 0.0, releaseMul_ = 1.0, slowCoeff_ = 0.0, slowMeanSquare_ = 0.0;
    double gainDb_ = 0.0, gainLin_ = 1.0, targetLin_ = 1.0, lastMeanSquare_ = 0.0;
    Mode mode_ = Hold;
    int phase_ = 0, initialPhase_ = 0;

    EnvelopeFollower detector_;
    std::vector<double> delay_;
    int delayPos_ = 0;
};
