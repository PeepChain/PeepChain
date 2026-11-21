#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <complex>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

/** How the measured response is reshaped before use (the plugin's Reverb settings). The default leaves it
 *  untouched, bit for bit. */
struct ReverbShape {
    double decayScale = 1.0;      // new decay time / the response's own (rt60): < 1 shorter, > 1 longer
    double rt60 = 1.65;           // the response's own decay time, s
    double lowCutHz = 0.0;        // 0: off
    double highCutHz = 0.0;       // 0: off
    bool operator==(const ReverbShape& o) const { return decayScale == o.decayScale && rt60 == o.rt60 && lowCutHz == o.lowCutHz && highCutHz == o.highCutHz; }
    bool operator!=(const ReverbShape& o) const { return !(*this == o); }
};

/**
 * The Reverb send's convolution: a mono input through a stereo impulse response. It replaces
 * juce::dsp::Convolution so that ten vocal layers can each have the plugin without the reverb eating the
 * machine (JUCE's took about 1 % CPU and 14 MB per instance):
 *  - The response's spectra are computed once per sample rate and shared by every instance in the process.
 *  - The input is transformed once for both output channels (JUCE kept and transformed a copy per channel).
 *  - Two partition sizes: B for the first 16 B samples of the response, 8 B for the long tail, so the tail
 *    costs far fewer multiplications per sample than one small partition size throughout.
 *  - It runs exactly B samples late. The plugin takes those B samples out of the reverb's pre-delay, which
 *    is always longer, so the reverb lands exactly where it did.
 *  - The tail's work for one 8 B block is spread over the next 8 small blocks (it isn't needed until 16 B
 *    later), so no audio callback has to do it all at once: with many instances running, those would line up.
 * Plain overlap-save partitioned convolution: the same linear convolution JUCE computed, to float precision.
 * For other sample rates the response is resampled exactly as JUCE did it (ResamplingAudioSource, then
 * scaled by the ratio of the rates).
 */
class ReverbConvolver {
public:
    using Shape = ReverbShape;

    /** ir: the response (1 or 2 channels) at irRate. blockSize: the latency, a power of two (64..4096).
     *  Allocates: not on the audio thread. */
    void prepare(const juce::AudioBuffer<float>& ir, double irRate, double sampleRate, int blockSize, const Shape& shape = {}) {
        {
            const juce::SpinLock::ScopedLockType l(offerLock);
            offered.reset(); retired.reset(); offerReady = false;
        }
        source = &ir; sourceRate = irRate; rate = sampleRate; shapeNow = shape;
        spec = Spectra::get(ir, irRate, sampleRate, blockSize, shape);
        for (int t = 0; t < 2; ++t) {
            auto& tier = tiers[t]; const auto& st = spec->tiers[t];
            tier.B = st.B; tier.segs = st.segs; tier.offset = st.offset;
            tier.fft = std::make_unique<juce::dsp::FFT>(st.order);
            tier.in.assign((size_t)(2 * st.B), 0.0f);
            tier.work.assign((size_t)(4 * st.B), 0.0f);
            tier.fdl.assign((size_t)std::max(1, st.segs) * (size_t)(st.B + 1), {});
            for (auto& a : tier.acc) a.assign((size_t)(st.B + 1), {});
        }
        latency = blockSize;
        const int need = spec->tiers[1].offset + spec->tiers[1].B + latency + 1;
        ringSize = 1; while (ringSize < need) ringSize <<= 1;
        for (auto& r : ring) r.assign((size_t)ringSize, 0.0f);
        reset();
    }
    int getLatency() const { return latency; }

    /** Message thread: reshape the response (its spectra are rebuilt here, or shared with another instance that
     *  has the same shape). The audio thread takes the new one at its next process() call; nothing is freed
     *  there. Call it again, or with the same shape, to let go of the one it replaced. */
    void setShape(const Shape& shape) {
        if (source == nullptr) return;
        std::shared_ptr<const Spectra> next, dropRetired, dropOffered;      // freed after the lock, on this thread
        if (shape != shapeNow) next = Spectra::get(*source, sourceRate, rate, tiers[0].B, shape);   // built outside the lock
        const juce::SpinLock::ScopedLockType l(offerLock);
        dropRetired = std::move(retired);                    // what the audio thread has swapped out
        if (next != nullptr) { dropOffered = std::move(offered); offered = std::move(next); offerReady = true; shapeNow = shape; }
    }
    const Shape& getShape() const { return shapeNow; }

    void reset() {
        for (auto& t : tiers) {
            std::fill(t.in.begin(), t.in.end(), 0.0f);
            std::fill(t.fdl.begin(), t.fdl.end(), std::complex<float>{});
            t.fill = 0; t.pos = 0; t.step = -1;
        }
        for (auto& r : ring) std::fill(r.begin(), r.end(), 0.0f);
        now = 0;
    }

    /** n samples of mono in, stereo out. outL may be the input buffer. Real-time safe. */
    void process(const float* in, float* outL, float* outR, int n) {
        if (offerReady.load(std::memory_order_acquire)) {        // a reshaped response: swap it in (pointers only)
            const juce::SpinLock::ScopedTryLockType l(offerLock);
            if (l.isLocked() && offerReady && retired == nullptr) { retired = std::move(spec); spec = std::move(offered); offerReady = false; }
        }
        const int mask = ringSize - 1;
        for (int i = 0; i < n; ++i) {
            const float x = in[i];
            for (int t = 0; t < 2; ++t) {
                auto& tier = tiers[t];
                if (tier.segs == 0) continue;
                tier.in[(size_t)(tier.B + tier.fill)] = x;
                if (++tier.fill == tier.B) {
                    if (t == 0) { runBlock(0, now - tier.B + 1); stepTail(); }   // the tail's pending work, one slice
                    else startTail(now - tier.B + 1);
                }
            }
            const int r = (int)((now - latency) & mask);
            outL[i] = ring[0][(size_t)r]; outR[i] = ring[1][(size_t)r];
            ring[0][(size_t)r] = ring[1][(size_t)r] = 0.0f;
            ++now;
        }
    }

private:
    struct Spectra {
        struct TierSpec { int B = 0, order = 0, segs = 0, offset = 0; std::vector<std::complex<float>> h[2]; };
        TierSpec tiers[2];

        static std::shared_ptr<const Spectra> get(const juce::AudioBuffer<float>& ir, double irRate, double sr, int B, const Shape& sh) {
            static std::mutex m;
            using Key = std::tuple<const void*, int, double, int, double, double, double, double>;
            static std::map<Key, std::weak_ptr<const Spectra>> cache;
            const std::lock_guard<std::mutex> l(m);
            for (auto it = cache.begin(); it != cache.end();) it = it->second.expired() ? cache.erase(it) : std::next(it);
            const Key key{(const void*)ir.getReadPointer(0), ir.getNumSamples(), sr, B, sh.decayScale, sh.rt60, sh.lowCutHz, sh.highCutHz};
            if (auto s = cache[key].lock()) return s;
            auto s = std::make_shared<Spectra>(); s->build(ir, irRate, sr, B, sh);
            cache[key] = s;
            return s;
        }

        /** The reshaping: a decay change (an exponential envelope from the start; when longer, the last 0.3 s
         *  fade out, since the response ends there), then 2nd-order Butterworth low / high cuts. */
        static void reshape(juce::AudioBuffer<float>& ir, double sr, const Shape& sh) {
            const int len = ir.getNumSamples();
            if (sh.decayScale != 1.0) {
                const double dbPerSec = 60.0 / sh.rt60 * (1.0 - 1.0 / sh.decayScale);    // > 0 when longer
                const double k = std::pow(10.0, dbPerSec / 20.0 / sr);
                const int fade = sh.decayScale > 1.0 ? std::min(len, juce::roundToInt(0.3 * sr)) : 0;
                for (int c = 0; c < ir.getNumChannels(); ++c) {
                    float* h = ir.getWritePointer(c); double g = 1.0;
                    for (int i = 0; i < len; ++i, g *= k) {
                        double w = g;
                        if (const int left = len - i; left <= fade) w *= 0.5 - 0.5 * std::cos(juce::MathConstants<double>::pi * left / fade);
                        h[i] = (float)(h[i] * w);
                    }
                }
            }
            auto filter = [&](double hz, bool high) {
                const double w0 = 2.0 * juce::MathConstants<double>::pi * std::min(hz, 0.45 * sr) / sr, cw = std::cos(w0);
                const double al = std::sin(w0) / (2.0 * std::sqrt(0.5)), a0 = 1.0 + al;
                const double b0 = (high ? (1.0 + cw) : (1.0 - cw)) / 2.0 / a0, b1 = (high ? -(1.0 + cw) : (1.0 - cw)) / a0, b2 = b0;
                const double a1 = -2.0 * cw / a0, a2 = (1.0 - al) / a0;
                for (int c = 0; c < ir.getNumChannels(); ++c) {
                    float* h = ir.getWritePointer(c); double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
                    for (int i = 0; i < len; ++i) {
                        const double x = h[i], y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                        x2 = x1; x1 = x; y2 = y1; y1 = y; h[i] = (float)y;
                    }
                }
            };
            if (sh.lowCutHz > 0.0) filter(sh.lowCutHz, true);
            if (sh.highCutHz > 0.0 && sh.highCutHz < 0.45 * sr) filter(sh.highCutHz, false);
        }

        void build(const juce::AudioBuffer<float>& source, double irRate, double sr, int B, const Shape& sh) {
            // resampled and scaled exactly as juce::dsp::Convolution did (Normalise::no)
            juce::AudioBuffer<float> ir;
            if (juce::approximatelyEqual(irRate, sr)) ir.makeCopyOf(source);
            else {
                const double factor = irRate / sr;
                juce::AudioBuffer<float> original(source);
                juce::MemoryAudioSource memory(original, false);
                juce::ResamplingAudioSource resampler(&memory, false, source.getNumChannels());
                const int size = juce::roundToInt(juce::jmax(1.0, source.getNumSamples() / factor));
                resampler.setResamplingRatio(factor);
                resampler.prepareToPlay(size, irRate);
                ir.setSize(source.getNumChannels(), size);
                resampler.getNextAudioBlock({&ir, 0, size});
            }
            ir.applyGain((float)(irRate / sr));
            if (sh != Shape()) reshape(ir, sr, sh);
            const int len = ir.getNumSamples();
            const int Bs[2] = {B, 8 * B};
            const int head = std::min(len, 16 * B);
            const int cover[2][2] = {{0, head}, {head, len}};
            for (int t = 0; t < 2; ++t) {
                auto& ts = tiers[t];
                ts.B = Bs[t]; ts.order = juce::roundToInt(std::log2(2.0 * ts.B)); ts.offset = cover[t][0];
                const int n = cover[t][1] - cover[t][0];
                ts.segs = (n + ts.B - 1) / ts.B;
                juce::dsp::FFT fft(ts.order);
                const int N = 2 * ts.B;
                // the scale of a forward + inverse pass, folded into the response
                std::vector<float> w((size_t)(2 * N), 0.0f); w[0] = 1.0f;
                fft.performRealOnlyForwardTransform(w.data(), true);
                fillSymmetric(w.data(), ts.B);
                fft.performRealOnlyInverseTransform(w.data());
                const float scale = 1.0f / w[0];
                for (int c = 0; c < 2; ++c) {
                    const float* h = ir.getReadPointer(std::min(c, ir.getNumChannels() - 1));
                    ts.h[c].assign((size_t)std::max(1, ts.segs) * (size_t)(ts.B + 1), {});
                    for (int k = 0; k < ts.segs; ++k) {
                        std::fill(w.begin(), w.end(), 0.0f);
                        const int from = ts.offset + k * ts.B, count = std::min(ts.B, cover[t][1] - from);
                        for (int j = 0; j < count; ++j) w[(size_t)j] = h[from + j] * scale;
                        fft.performRealOnlyForwardTransform(w.data(), true);
                        auto* dst = &ts.h[c][(size_t)k * (size_t)(ts.B + 1)];
                        for (int j = 0; j <= ts.B; ++j) dst[j] = {w[(size_t)(2 * j)], w[(size_t)(2 * j + 1)]};
                    }
                }
            }
        }
    };

    static void fillSymmetric(float* d, int B) {             // bins B+1 .. 2B-1 are the conjugates of B-1 .. 1
        const int N = 2 * B;
        for (int j = 1; j < B; ++j) { d[2 * (N - j)] = d[2 * j]; d[2 * (N - j) + 1] = -d[2 * j + 1]; }
    }

    /** acc[c] += x * h[c] over n complex bins, for both output channels in one pass over x. Written out on
     *  plain floats: std::complex's operator* carries the C99 inf/NaN recovery, which keeps the compiler from
     *  vectorising the loop. */
    static void multiplyAdd2(std::complex<float>* acc0, std::complex<float>* acc1, const std::complex<float>* x,
                             const std::complex<float>* h0, const std::complex<float>* h1, int n) {
        float* __restrict a0 = reinterpret_cast<float*>(acc0);
        float* __restrict a1 = reinterpret_cast<float*>(acc1);
        const float* __restrict xp = reinterpret_cast<const float*>(x);
        const float* __restrict p0 = reinterpret_cast<const float*>(h0);
        const float* __restrict p1 = reinterpret_cast<const float*>(h1);
        for (int j = 0; j < 2 * n; j += 2) {
            const float xr = xp[j], xi = xp[j + 1];
            a0[j] += xr * p0[j] - xi * p0[j + 1]; a0[j + 1] += xr * p0[j + 1] + xi * p0[j];
            a1[j] += xr * p1[j] - xi * p1[j + 1]; a1[j + 1] += xr * p1[j + 1] + xi * p1[j];
        }
    }

    struct Tier {
        int B = 0, segs = 0, offset = 0, fill = 0, pos = 0;
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> in, work;                           // last 2 B input samples; FFT work space (4 B)
        std::vector<std::complex<float>> fdl, acc[2];          // spectra of the last segs input blocks; each channel's sum
        int step = -1, jobPos = 0; juce::int64 jobBase = 0;    // tail only: the spread-out block in progress (-1: none)
    };
    static constexpr int kSlices = 8;                          // = tail block / head block

    /** A tail block is complete: transform it now, do its multiplications in kSlices slices (stepTail). */
    void startTail(juce::int64 blockStart) {
        auto& tier = tiers[1];
        const int B = tier.B, bins = B + 1;
        float* w = tier.work.data();
        std::copy(tier.in.begin(), tier.in.end(), w);
        std::fill(w + 2 * B, w + 4 * B, 0.0f);
        tier.fft->performRealOnlyForwardTransform(w, true);
        auto* slot = &tier.fdl[(size_t)tier.pos * (size_t)bins];
        for (int j = 0; j < bins; ++j) slot[j] = {w[2 * j], w[2 * j + 1]};
        for (auto& a : tier.acc) std::fill(a.begin(), a.end(), std::complex<float>{});
        tier.jobPos = tier.pos; tier.jobBase = blockStart + tier.offset; tier.step = 0;
        std::copy(tier.in.begin() + B, tier.in.end(), tier.in.begin());
        tier.fill = 0;
        if (++tier.pos == tier.segs) tier.pos = 0;
    }

    /** One slice of the pending tail block; the last one transforms back and adds it into the ring, 16 head
     *  blocks before it is due. */
    void stepTail() {
        auto& tier = tiers[1]; const auto& st = spec->tiers[1];
        if (tier.step < 0) return;
        const int B = tier.B, bins = B + 1;
        const int k0 = tier.segs * tier.step / kSlices, k1 = tier.segs * (tier.step + 1) / kSlices;
        for (int k = k0; k < k1; ++k) {
            int p = tier.jobPos - k; if (p < 0) p += tier.segs;
            const size_t hk = (size_t)k * (size_t)bins;
            multiplyAdd2(tier.acc[0].data(), tier.acc[1].data(), &tier.fdl[(size_t)p * (size_t)bins], &st.h[0][hk], &st.h[1][hk], bins);
        }
        if (++tier.step < kSlices) return;
        tier.step = -1;
        const int mask = ringSize - 1;
        float* w = tier.work.data();
        for (int c = 0; c < 2; ++c) {
            const auto* acc = tier.acc[c].data();
            for (int j = 0; j < bins; ++j) { w[2 * j] = acc[j].real(); w[2 * j + 1] = acc[j].imag(); }
            fillSymmetric(w, B);
            tier.fft->performRealOnlyInverseTransform(w);
            auto& r = ring[c];
            for (int i = 0; i < B; ++i) r[(size_t)((tier.jobBase + i) & mask)] += w[B + i];
        }
    }

    /** A tier's input block is complete: transform it, multiply-accumulate with the response, add the result
     *  into the output ring at the time it belongs to (blockStart is the block's first input sample). */
    void runBlock(int t, juce::int64 blockStart) {
        auto& tier = tiers[t]; const auto& st = spec->tiers[t];
        const int B = tier.B, bins = B + 1;
        float* w = tier.work.data();
        std::copy(tier.in.begin(), tier.in.end(), w);
        std::fill(w + 2 * B, w + 4 * B, 0.0f);
        tier.fft->performRealOnlyForwardTransform(w, true);
        auto* slot = &tier.fdl[(size_t)tier.pos * (size_t)bins];
        for (int j = 0; j < bins; ++j) slot[j] = {w[2 * j], w[2 * j + 1]};
        const int mask = ringSize - 1;
        for (auto& a : tier.acc) std::fill(a.begin(), a.end(), std::complex<float>{});
        for (int k = 0; k < tier.segs; ++k) {
            int p = tier.pos - k; if (p < 0) p += tier.segs;
            const size_t hk = (size_t)k * (size_t)bins;
            multiplyAdd2(tier.acc[0].data(), tier.acc[1].data(), &tier.fdl[(size_t)p * (size_t)bins], &st.h[0][hk], &st.h[1][hk], bins);
        }
        for (int c = 0; c < 2; ++c) {
            const auto* acc = tier.acc[c].data();
            for (int j = 0; j < bins; ++j) { w[2 * j] = acc[j].real(); w[2 * j + 1] = acc[j].imag(); }
            fillSymmetric(w, B);
            tier.fft->performRealOnlyInverseTransform(w);
            auto& r = ring[c];
            const juce::int64 base = blockStart + tier.offset;
            for (int i = 0; i < B; ++i) r[(size_t)((base + i) & mask)] += w[B + i];   // overlap-save: the last B are valid
        }
        std::copy(tier.in.begin() + B, tier.in.end(), tier.in.begin());               // this block becomes the previous one
        tier.fill = 0;
        if (++tier.pos == tier.segs) tier.pos = 0;
    }

    std::shared_ptr<const Spectra> spec;                       // in use by the audio thread
    // handing a reshaped response over: the message thread puts it in `offered`; the audio thread swaps it with
    // `spec`, leaving the old one in `retired` for the message thread to free
    juce::SpinLock offerLock;
    std::shared_ptr<const Spectra> offered, retired;
    std::atomic<bool> offerReady{false};
    const juce::AudioBuffer<float>* source = nullptr; double sourceRate = 44100.0, rate = 44100.0;
    Shape shapeNow;
    Tier tiers[2];
    std::vector<float> ring[2];                                // output, indexed by input time
    int ringSize = 1, latency = 0;
    juce::int64 now = 0;
};
