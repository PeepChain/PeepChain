#pragma once
#include "DspMath.h"

#include <cmath>
#include <algorithm>

/**
 * 4-band splitter matching GarageBand's AUMultibandCompressor.
 *
 * Measured from renders of the real AU:
 *   - every edge is a 3rd-order Butterworth filter (18 dB/oct), NOT Linkwitz-Riley
 *   - the bands are four independent parallel filters fed from the input:
 *         sub  = LP3(f1)
 *         low  = LP3(f2) . HP3(f1)
 *         mid  = LP3(f3) . HP3(f2)
 *         high = HP3(f3)
 *   - there is no phase compensation, so the sum of the bands is NOT flat
 *     (about -6 dB at 900 Hz, -3 dB at 200 Hz with 200/900/4000 Hz). That
 *     colouration is part of Apple's sound and is reproduced here.
 *
 * In passages where nothing is compressed, the reference output equals the
 * sum of these four bands to within 0.01 dB / 0.3 degrees from 500 Hz up.
 */
class CrossoverFilter {
public:
    CrossoverFilter()
        : sampleRate_(44100.0),
          crossover1_(200.0), crossover2_(900.0), crossover3_(4000.0) {
        updateCoefficients();
    }

    void setSampleRate(double sampleRate) {
        sampleRate_ = sampleRate;
        updateCoefficients();
        reset();
    }

    void setCrossover1(double freq) { crossover1_ = clampFreq(freq); updateCoefficients(); }
    void setCrossover2(double freq) { crossover2_ = clampFreq(freq); updateCoefficients(); }
    void setCrossover3(double freq) { crossover3_ = clampFreq(freq); updateCoefficients(); }

    void setCrossovers(double f1, double f2, double f3) {
        crossover1_ = clampFreq(f1);
        crossover2_ = clampFreq(f2);
        crossover3_ = clampFreq(f3);
        updateCoefficients();
    }

    void processSample(double input, double& sub, double& low, double& mid, double& high) {
        sub  = lp1_.process(input);
        low  = lp2_.process(hp1_.process(input));
        mid  = lp3_.process(hp2_.process(input));
        high = hp3_.process(input);
    }

    void processBlock(const double* input,
                      double* outputSub, double* outputLow,
                      double* outputMid, double* outputHigh,
                      int numSamples) {
        for (int i = 0; i < numSamples; ++i) {
            processSample(input[i], outputSub[i], outputLow[i], outputMid[i], outputHigh[i]);
        }
    }

    void reset() {
        lp1_.reset(); lp2_.reset(); lp3_.reset();
        hp1_.reset(); hp2_.reset(); hp3_.reset();
    }

private:
    /** 3rd-order Butterworth = one 1st-order section + one 2nd-order section with Q = 1. */
    struct Butter3 {
        // 1st-order section
        double fb0 = 1.0, fb1 = 0.0, fa1 = 0.0;
        double fx1 = 0.0, fy1 = 0.0;
        // 2nd-order section
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

        void reset() { fx1 = fy1 = x1 = x2 = y1 = y2 = 0.0; }

        void design(double sampleRate, double cutoff, bool highpass) {
            // Bilinear transform with frequency pre-warping.
            const double K = std::tan(dspmath::kPi * cutoff / sampleRate);
            const double K2 = K * K;
            const double Q = 1.0;
            const double norm = 1.0 / (1.0 + K / Q + K2);
            fa1 = (K - 1.0) / (K + 1.0);
            a1 = 2.0 * (K2 - 1.0) * norm;
            a2 = (1.0 - K / Q + K2) * norm;
            if (highpass) {
                fb0 = 1.0 / (1.0 + K);  fb1 = -fb0;
                b0 = norm;              b1 = -2.0 * norm;  b2 = norm;
            } else {
                fb0 = K / (1.0 + K);    fb1 = fb0;
                b0 = K2 * norm;         b1 = 2.0 * b0;     b2 = b0;
            }
        }

        double process(double in) {
            const double f = fb0 * in + fb1 * fx1 - fa1 * fy1;
            fx1 = in; fy1 = f;
            const double out = b0 * f + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = f;
            y2 = y1; y1 = out;
            return out;
        }
    };

    double clampFreq(double f) const {
        return std::clamp(f, 20.0, 0.49 * sampleRate_);
    }

    void updateCoefficients() {
        lp1_.design(sampleRate_, crossover1_, false);
        hp1_.design(sampleRate_, crossover1_, true);
        lp2_.design(sampleRate_, crossover2_, false);
        hp2_.design(sampleRate_, crossover2_, true);
        lp3_.design(sampleRate_, crossover3_, false);
        hp3_.design(sampleRate_, crossover3_, true);
    }

    double sampleRate_;
    double crossover1_, crossover2_, crossover3_;
    Butter3 lp1_, lp2_, lp3_, hp1_, hp2_, hp3_;
};
