#pragma once

#include "CrossoverFilter.h"
#include "BandCompressor.h"
#include <cmath>

/**
 * Complete single-channel signal path of the AUMultibandCompressor clone:
 *
 *   input -> pregain -> 4 parallel 3rd-order Butterworth bands
 *         -> per-band look-ahead compressor -> sum -> postgain
 *
 * One instance handles a mono or stereo stream. Stereo channels are linked
 * (measured on the real AU): each band's detector listens to (L+R)/2 and both
 * channels get the same gain.
 *
 * Pregain / Postgain placement (before the split / after the sum) and the
 * per-band EQ gains (not implemented, 0 dB in Peep's preset) are unverified.
 */
struct MultibandSettings {
    double attackTime  = 0.020;   // seconds
    double releaseTime = 0.040;   // seconds
    double pregainDb   = 0.0;
    double postgainDb  = 0.0;
    double crossover[3] = {200.0, 900.0, 4000.0};        // Hz
    double threshold[4] = {-69.0, -54.0, -51.0, -52.0};  // dBFS
    double headroom[4]  = {30.0, 30.0, 30.0, 30.0};      // dB
};

class MultibandCore {
public:
    static constexpr int kNumBands = 4;

    void prepare(double sampleRate, const MultibandSettings& s) {
        sampleRate_ = sampleRate;
        for (auto& x : crossover_) x.setSampleRate(sampleRate);
        for (auto& b : bands_) b.setSampleRate(sampleRate);
        setSettings(s);
        reset();
    }

    void setSettings(const MultibandSettings& s) {
        settings_ = s;
        for (auto& x : crossover_) x.setCrossovers(s.crossover[0], s.crossover[1], s.crossover[2]);
        for (int i = 0; i < kNumBands; ++i) {
            bands_[i].setAttackTime(s.attackTime);
            bands_[i].setReleaseTime(s.releaseTime);
            bands_[i].setThreshold(s.threshold[i]);
            bands_[i].setHeadroom(s.headroom[i]);
        }
        pregain_  = std::pow(10.0, s.pregainDb / 20.0);
        postgain_ = std::pow(10.0, s.postgainDb / 20.0);
    }

    void setBlockPhase(int samples) { for (auto& b : bands_) b.setBlockPhase(samples); }

    void reset() {
        for (auto& x : crossover_) x.reset();
        for (auto& b : bands_) b.reset();
    }

    /** Delay of the whole path in samples (the detector look-ahead). */
    int getLatency() const { return bands_[0].getLatency(); }

    /** Gain reduction of one band in dB (positive), for metering. */
    double getGainReduction(int band) const { return bands_[band].getGainReduction(); }

    /** Mono. */
    double processSample(double input) { double f[1] = {input}; processFrame(f, 1); return f[0]; }

    /** One frame of 1 or 2 channels in place; channels share one gain per band (linked, as in the AU). */
    void processFrame(double* io, int channels) {
        double b[kNumBands][2];
        for (int c = 0; c < channels; ++c)
            crossover_[c].processSample(io[c] * pregain_, b[0][c], b[1][c], b[2][c], b[3][c]);
        for (int c = 0; c < channels; ++c) io[c] = 0.0;
        for (int i = 0; i < kNumBands; ++i) {
            bands_[i].processFrame(b[i], channels);
            for (int c = 0; c < channels; ++c) io[c] += b[i][c];
        }
        for (int c = 0; c < channels; ++c) io[c] *= postgain_;
    }

private:
    double sampleRate_ = 44100.0;
    MultibandSettings settings_;
    CrossoverFilter crossover_[2];
    BandCompressor bands_[kNumBands];
    double pregain_ = 1.0, postgain_ = 1.0;
};
