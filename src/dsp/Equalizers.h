#pragma once
#include "DspMath.h"
#include <cmath>
#include <vector>

/** Transposed direct-form II biquad, double precision. */
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    void set(double B0, double B1, double B2, double A0, double A1, double A2) { b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0; }
    void reset() { z1 = z2 = 0; }
    double process(double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

/**
 * Apple AUFilter: low shelf (or low cut) + 3 parametric bands + high shelf (or
 * high cut). Every section is the plain RBJ cookbook biquad; verified against
 * the real AU's impulse response to better than -130 dB on random settings.
 *   shelves: slope S = 1;  peaks: bandwidth in octaves (RBJ sinh form);
 *   cut types: 2nd-order with Q = 10^(gain/20) (the gain knob becomes resonance).
 */
class AUFilterEQ {
public:
    struct Settings {
        bool lowIsCut = false; double lowFreq = 134.0, lowGain = 12.3;   // values as shown in the plugin window
        double freq[3] = {602.0, 2801.0, 5000.0};
        double gain[3] = {3.1, 1.6, 0.0};
        double bw[3]   = {2.0, 2.0, 2.0};
        bool highIsCut = false; double highFreq = 10000.0, highGain = 0.0;
    };
    void prepare(double sr, const Settings& s) { set(sr, s); reset(); }
    /** New settings while running: the coefficients change, the filter state is kept (as the AU does). */
    void set(double sr, const Settings& s) {
        const double top = 0.495 * sr;                               // the AU's own top (21829.5 Hz at 44.1 kHz)
        shelfOrCut(sec_[0], sr, std::min(s.lowFreq, top), s.lowGain, true, s.lowIsCut);
        for (int i = 0; i < 3; ++i) peak(sec_[1 + i], sr, std::min(s.freq[i], top), s.gain[i], s.bw[i]);
        shelfOrCut(sec_[4], sr, std::min(s.highFreq, top), s.highGain, false, s.highIsCut);
    }
    void reset() { for (auto& b : sec_) b.reset(); }
    double process(double x) { for (auto& b : sec_) x = b.process(x); return x; }
    /** The response at f Hz, in dB (for drawing it). */
    double magnitudeDb(double f, double sr) const {
        const double w = 2 * dspmath::kPi * f / sr, c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
        double db = 0.0;
        for (const auto& q : sec_) {
            const double nr = q.b0 + q.b1 * c1 + q.b2 * c2, ni = -(q.b1 * s1 + q.b2 * s2);
            const double dr = 1.0 + q.a1 * c1 + q.a2 * c2, di = -(q.a1 * s1 + q.a2 * s2);
            db += 10.0 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di) + 1e-30);
        }
        return db;
    }

    static void peak(Biquad& q, double sr, double f, double gDb, double bwOct) {
        const double A = std::pow(10.0, gDb / 40.0), w = 2 * dspmath::kPi * f / sr, s = std::sin(w), c = std::cos(w);
        const double al = s * std::sinh(std::log(2.0) / 2 * bwOct * w / s);
        q.set(1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
    static void shelfOrCut(Biquad& q, double sr, double f, double gDb, bool low, bool cut) {
        const double w = 2 * dspmath::kPi * f / sr, s = std::sin(w), c = std::cos(w);
        if (cut) {
            const double al = s / (2 * std::pow(10.0, gDb / 20.0));
            if (low) q.set((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);   // low cut = high-pass
            else     q.set((1 - c) / 2, (1 - c), (1 - c) / 2, 1 + al, -2 * c, 1 - al);    // high cut = low-pass
            return;
        }
        const double A = std::pow(10.0, gDb / 40.0), k = 2 * std::sqrt(A) * (s / 2 * std::sqrt(2.0));
        if (low) q.set(A * ((A + 1) - (A - 1) * c + k), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - k),
                       (A + 1) + (A - 1) * c + k, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - k);
        else     q.set(A * ((A + 1) + (A - 1) * c + k), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - k),
                       (A + 1) - (A - 1) * c + k, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - k);
    }
private:
    Biquad sec_[5];
};

/**
 * Apple AUGraphicEQ, 31-band mode: 31 peaking biquads in series. Measured from
 * the real AU: with G = 10^(gain/20) and K = 4*tan(sqrt(2)/12 * w0),
 *   H(z) = [(1+n) - 2cos(w0) z^-1 + (1-n) z^-2] / [(1+d) - 2cos(w0) z^-1 + (1-d) z^-2],
 *   d = K/(1+G), n = K*G/(1+G).
 * Matches the AU's response to ~0.1 dB on random settings (10-band mode not modelled).
 */
class GraphicEQ31 {
public:
    static constexpr int kBands = 31;
    static const double* frequencies() {
        static const double f[kBands] = {20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12000, 16000, 20000};
        return f;
    }
    /** Lil Peep's curve, as shown in the plugin window. */
    static const double* peepGains() {
        static const double g[kBands] = {5.5, 0, 2.4, 4.6, 0, 3.2, 0, 3.0, 0, 7.2, 0, 0, 0, 0, 0, 0, 0,
                                         6.5, 0, 2.8, 0, 7.8, 0, 0, 0, 0, 0, 12.6, 0, 0, 6.5};
        return g;
    }
    void prepare(double sr, const double* gainsDb) { set(sr, gainsDb); reset(); }
    /** New gains while running (no allocation). A band at 0 dB is skipped (it would pass the signal unchanged);
     *  one that comes back starts from silence, the others keep their state. */
    void set(double sr, const double* gainsDb) {
        n_ = 0;
        for (int i = 0; i < kBands; ++i) {
            const bool on = gainsDb[i] != 0.0 && frequencies()[i] < 0.5 * sr;
            if (!on) { active_[i] = false; continue; }
            if (!active_[i]) sec_[i].reset();
            active_[i] = true; idx_[n_++] = i;
            const double G = std::pow(10.0, gainsDb[i] / 20.0), w = 2 * dspmath::kPi * frequencies()[i] / sr, K = 4 * std::tan(std::sqrt(2.0) / 12 * w);
            const double d = K / (1 + G), n = K * G / (1 + G), c = std::cos(w);
            sec_[i].set(1 + n, -2 * c, 1 - n, 1 + d, -2 * c, 1 - d);
        }
    }
    void reset() { for (auto& b : sec_) b.reset(); }
    double process(double x) { for (int k = 0; k < n_; ++k) x = sec_[idx_[k]].process(x); return x; }
private:
    Biquad sec_[kBands];
    bool active_[kBands] = {};
    int idx_[kBands] = {}, n_ = 0;
};
