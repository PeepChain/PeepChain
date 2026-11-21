#pragma once
#include <juce_core/juce_core.h>
#include "../dsp/PeepChain.h"
#include <cmath>
#include <vector>

/**
 * The settings shown in the effects' own panels. Every one defaults to Peep's value, exactly: the parameters are
 * floats, so a value within a hair of Peep's is read as his double-precision value (see snap()). There are three
 * kinds:
 *
 *  1. Checked against Apple's real Audio Units at ANY value (AUFilter, AUGraphicEQ, AUMultibandCompressor,
 *     AUPeakLimiter can be hosted offline and compared at random settings). Ranges are the Audio Units' own,
 *     except two, cut to where the clone was checked to still match:
 *      - Multiband release 10-40 ms (Apple's goes to 3 s): with longer releases and heavy compression the clone's
 *        release drifts from Apple's (several dB at 100 ms with 0.3 dB of headroom, on a vocal; simple steps still
 *        match exactly). Up to 40 ms the worst case is 0.2 dB rms.
 *      - Limiter pre-gain up to +24 dB (Apple's goes to +40): above that the clone's limiting drifts (1.5 dB at
 *        +30, 8 dB at +40). Up to +24 it stays within 0.2 dB.
 *  2. GarageBand-only effects (Noise Gate, Compressor), measured at Peep's settings: their times and the gate's
 *     levels. The models take these as GarageBand's knobs mean them (the gate's attack is a linear ramp, its
 *     release 20 dB per release time; the compressor's times are to 90 %), but away from Peep's values they could
 *     not be checked against GarageBand.
 *  3. The sends (Delay and Reverb), which are measured models: their time, feedback and filters, and shaping of
 *     the measured reverb (its length, cuts and width). Feedback is scaled from Peep's 38 %: GarageBand's knob was
 *     only measured there.
 * Plus the four that were already on the main screen (gate and compressor thresholds, the two send levels) and
 * the two syncs: those are "core" parameters, made by the processor itself, listed here so the panels can show them.
 *
 * Left out on purpose:
 *  - Compressor ratio and knee: its curve was measured at Peep's 8:1 and 0.7 only.
 *  - Noise Gate look-ahead and AUPeakLimiter attack: both are look-ahead, so changing them would change the
 *    plugin's latency.
 *  - AUMultibandCompressor pre/post gain and band EQ: not modelled.
 */
namespace EffectParams {

enum class Unit { dB, Hz, Octaves, Seconds, Percent, Choice, Note };
constexpr int kSends = PeepChain::kStages;           // the Delay & Reverb panel, after the six effects

struct Def {
    juce::String id, name;       // parameter id and the name hosts show
    juce::String label, group;   // in the editor: under the control, and the group it sits in
    int stage;                   // PeepChain::Stage, or kSends
    Unit unit;
    double min, max, peep;       // range, and Peep's value
    bool log = false;            // log-scaled knob (frequencies, times)
    juce::StringArray choices;   // Unit::Choice / Note: the names (Choice: index 0 = off / Peep's)
    bool core = false;           // made by the processor (an older parameter), not by the loop over these
    int version = 7;             // parameter version hint
    int offAt = 0;               // -1: the minimum reads "OFF", +1: the maximum does
};

/** The send settings (kind 3). */
struct Sends {
    double delayTime = 0.5;            // s, when not synced
    int delayNote = 8;                 // index into noteNames() / noteBeats(), when synced: 1/4
    double delayFeedbackPct = 38.0;    // GarageBand's figure; the loop gain is scaled from Peep's
    double delayLowCut = 16.863, delayHighCut = 9851.8;     // the fitted filters round the echo loop (SendDelay)
    double reverbPreDelay = 2573.0 / 44100.0;               // s
    double reverbLength = 1.65;        // s, the measured response's decay time (RT60)
    double reverbLowCut = 20.0, reverbHighCut = 20000.0;    // at these ends: off
    double reverbWidthPct = 100.0;
};
constexpr double kReverbRt60 = 1.65;   // decay time of Peep's reverb (energy decay fit, -5 to -65 dB)

inline const juce::StringArray& noteNames() {
    static const juce::StringArray n{"1/32", "1/16T", "1/16", "1/16D", "1/8T", "1/8", "1/8D", "1/4T", "1/4", "1/4D", "1/2T", "1/2"};
    return n;
}
inline double noteBeats(int i) {
    static const double b[] = {0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0, 1.0, 1.5, 4.0 / 3.0, 2.0};
    return b[juce::jlimit(0, 11, i)];
}

inline const std::vector<Def>& all() {
    static const std::vector<Def> defs = [] {
        const PeepChain::Settings p;
        const Sends sp;
        std::vector<Def> d;
        auto add = [&](juce::String id, juce::String name, juce::String label, juce::String group, int stage, Unit u,
                       double lo, double hi, double peep, bool log = false, juce::StringArray ch = {}) -> Def& {
            Def x; x.id = id; x.name = name; x.label = label; x.group = group; x.stage = stage; x.unit = u;
            x.min = lo; x.max = hi; x.peep = peep; x.log = log; x.choices = ch;
            d.push_back(x);
            return d.back();
        };
        // Noise Gate: its levels and times (not the look-ahead, and not the hysteresis: Peep's -6 dB keeps a held
        // note from chattering, and few people would know what to do with it)
        const auto& gt = p.gate;
        add("gateThresh", "Gate Threshold", "THRESHOLD", "", PeepChain::kGate, Unit::dB, -80.0, -10.0, gt.thresholdDb).core = true;
        add("gateReduction", "Gate Reduction", "REDUCTION", "", PeepChain::kGate, Unit::dB, -100.0, 0.0, gt.reductionDb).version = 8;
        add("gateAttack", "Gate Attack", "ATTACK", "", PeepChain::kGate, Unit::Seconds, 0.0001, 0.2, gt.attackMs * 1e-3, true).version = 8;
        add("gateHold", "Gate Hold", "HOLD", "", PeepChain::kGate, Unit::Seconds, 0.001, 2.0, gt.holdMs * 1e-3, true).version = 8;
        add("gateRelease", "Gate Release", "RELEASE", "", PeepChain::kGate, Unit::Seconds, 0.002, 2.0, gt.releaseMs * 1e-3, true).version = 8;

        // Compressor: threshold, times, make-up (not ratio or knee)
        const auto& c = p.comp;
        add("compThresh", "Compressor Threshold", "THRESHOLD", "", PeepChain::kComp, Unit::dB, -50.0, 0.0, c.thresholdDb).core = true;
        add("compAttack", "Comp Attack", "ATTACK", "", PeepChain::kComp, Unit::Seconds, 0.0001, 0.2, c.attackMs * 1e-3, true).version = 8;
        add("compRelease", "Comp Release", "RELEASE", "", PeepChain::kComp, Unit::Seconds, 0.005, 2.0, c.releaseMs * 1e-3, true).version = 8;
        add("compMakeup", "Comp Make-Up", "MAKE-UP", "", PeepChain::kComp, Unit::dB, -20.0, 30.0, c.makeupDb);

        // AUFilter: everything
        const auto& f = p.filter;
        const double fTop = 21829.5;                                  // the AU's own range
        add("filtLowType", "Filter Low Type", "TYPE", "LOW", PeepChain::kFilter, Unit::Choice, 0, 1, f.lowIsCut ? 1 : 0, false, {"Low Shelf", "High Pass"});
        add("filtLowFreq", "Filter Low Freq", "FREQ", "LOW", PeepChain::kFilter, Unit::Hz, 10.0, fTop, f.lowFreq, true);
        add("filtLowGain", "Filter Low Gain", "GAIN", "LOW", PeepChain::kFilter, Unit::dB, -18.0, 18.0, f.lowGain);
        for (int i = 0; i < 3; ++i) {
            const juce::String n(i + 1), grp = "MID " + n;
            add("filtMid" + n + "Freq", "Filter Mid " + n + " Freq", "FREQ", grp, PeepChain::kFilter, Unit::Hz, 10.0, fTop, f.freq[i], true);
            add("filtMid" + n + "Gain", "Filter Mid " + n + " Gain", "GAIN", grp, PeepChain::kFilter, Unit::dB, -18.0, 18.0, f.gain[i]);
            add("filtMid" + n + "Width", "Filter Mid " + n + " Width", "WIDTH", grp, PeepChain::kFilter, Unit::Octaves, 0.05, 3.0, f.bw[i], true);
        }
        add("filtHighType", "Filter High Type", "TYPE", "HIGH", PeepChain::kFilter, Unit::Choice, 0, 1, f.highIsCut ? 1 : 0, false, {"High Shelf", "Low Pass"});
        add("filtHighFreq", "Filter High Freq", "FREQ", "HIGH", PeepChain::kFilter, Unit::Hz, 10.0, fTop, f.highFreq, true);
        add("filtHighGain", "Filter High Gain", "GAIN", "HIGH", PeepChain::kFilter, Unit::dB, -18.0, 18.0, f.highGain);

        // AUGraphicEQ (31 bands): every band
        for (int i = 0; i < GraphicEQ31::kBands; ++i) {
            const double hz = GraphicEQ31::frequencies()[i];
            const juce::String shown = hz >= 1000.0 ? juce::String(hz / 1000.0, hz < 10000.0 && std::fmod(hz, 1000.0) != 0.0 ? 1 : 0) + "K"
                                                    : juce::String(hz, std::fmod(hz, 1.0) != 0.0 ? 1 : 0);
            add("geq" + juce::String(i + 1).paddedLeft('0', 2), "EQ " + juce::String(hz, std::fmod(hz, 1.0) != 0.0 ? 1 : 0) + " Hz",
                shown, "BANDS", PeepChain::kGeq, Unit::dB, -20.0, 20.0, p.geqGains[i]);
        }

        // AUMultibandCompressor: crossovers, thresholds, headrooms, attack, release
        const auto& m = p.multiband;
        const char* const bandNames[4] = {"LOW", "LOW MID", "HIGH MID", "HIGH"};
        for (int i = 0; i < 3; ++i)
            add("mbSplit" + juce::String(i + 1), "Multiband Crossover " + juce::String(i + 1), juce::String(i + 1) + " | " + juce::String(i + 2),
                "SPLITS", PeepChain::kMulti, Unit::Hz, 20.0, 22050.0, m.crossover[i], true);
        for (int i = 0; i < 4; ++i)
            add("mbThresh" + juce::String(i + 1), "Multiband Threshold " + juce::String(i + 1), bandNames[i], "THRESHOLD",
                PeepChain::kMulti, Unit::dB, -100.0, 0.0, m.threshold[i]);
        for (int i = 0; i < 4; ++i)
            add("mbHeadroom" + juce::String(i + 1), "Multiband Headroom " + juce::String(i + 1), bandNames[i], "HEADROOM",
                PeepChain::kMulti, Unit::dB, 0.1, 40.0, m.headroom[i]);
        add("mbAttack", "Multiband Attack", "ATTACK", "TIMES", PeepChain::kMulti, Unit::Seconds, 0.001, 0.2, m.attackTime, true);
        add("mbRelease", "Multiband Release", "RELEASE", "TIMES", PeepChain::kMulti, Unit::Seconds, 0.01, 0.04, m.releaseTime, true);   // see above

        // AUPeakLimiter: pre-gain and release
        add("limPregain", "Limiter Pre-Gain", "PRE-GAIN", "LIMITER", PeepChain::kLimit, Unit::dB, -40.0, 24.0, p.limiterPregainDb);   // see above
        add("limRelease", "Limiter Release", "RELEASE", "LIMITER", PeepChain::kLimit, Unit::Seconds, 0.001, 0.04, p.limiterRelease, true);

        // The sends
        add("delayLevel", "Delay", "LEVEL", "DELAY", kSends, Unit::dB, -60.0, 6.0, -16.5).core = true;
        d.back().offAt = -1;
        add("delaySync", "Delay Sync", "SYNC", "DELAY", kSends, Unit::Choice, 0, 1, 0, false, {"Off", "On"}).core = true;
        add("delayTime", "Delay Time", "TIME", "DELAY", kSends, Unit::Seconds, 0.01, 2.0, sp.delayTime, true).version = 8;
        add("delayNote", "Delay Note", "TIME", "DELAY", kSends, Unit::Note, 0, noteNames().size() - 1, sp.delayNote, false, noteNames()).version = 8;
        add("delayFeedback", "Delay Feedback", "FEEDBACK", "DELAY", kSends, Unit::Percent, 0.0, 100.0, sp.delayFeedbackPct).version = 8;
        add("delayLowCut", "Delay Low Cut", "LOW CUT", "DELAY", kSends, Unit::Hz, 10.0, 2000.0, sp.delayLowCut, true).version = 8;
        add("delayHighCut", "Delay High Cut", "HIGH CUT", "DELAY", kSends, Unit::Hz, 1000.0, 20000.0, sp.delayHighCut, true).version = 8;
        add("reverbLevel", "Reverb", "LEVEL", "REVERB", kSends, Unit::dB, -60.0, 6.0, -6.0).core = true;
        d.back().offAt = -1;
        add("reverbSync", "Reverb Sync", "SYNC", "REVERB", kSends, Unit::Choice, 0, 1, 0, false, {"Off", "On"}).core = true;
        add("reverbPreDelay", "Reverb Pre-Delay", "PRE-DELAY", "REVERB", kSends, Unit::Seconds, 0.025, 0.25, sp.reverbPreDelay, true).version = 8;
        add("reverbLength", "Reverb Length", "LENGTH", "REVERB", kSends, Unit::Seconds, 0.4, 2.45, sp.reverbLength, true).version = 8;
        add("reverbLowCut", "Reverb Low Cut", "LOW CUT", "REVERB", kSends, Unit::Hz, 20.0, 1000.0, sp.reverbLowCut, true).version = 8;
        d.back().offAt = -1;
        add("reverbHighCut", "Reverb High Cut", "HIGH CUT", "REVERB", kSends, Unit::Hz, 1000.0, 20000.0, sp.reverbHighCut, true).version = 8;
        d.back().offAt = 1;
        add("reverbWidth", "Reverb Width", "WIDTH", "REVERB", kSends, Unit::Percent, 0.0, 200.0, sp.reverbWidthPct).version = 8;
        return d;
    }();
    return defs;
}

/** Index into all() of a parameter id (-1 if it isn't one). */
inline int indexOf(const juce::String& id) {
    const auto& d = all();
    for (int i = 0; i < (int)d.size(); ++i) if (d[(size_t)i].id == id) return i;
    return -1;
}

/** Peep's value when v is within float rounding of it, so the defaults run at exactly his settings. */
inline double snap(const Def& d, float v) {
    const double x = (double)v;
    return std::abs(x - d.peep) <= 1e-5 * std::max(1.0, std::abs(d.peep)) ? d.peep : x;
}

/** Writes the panel settings into s (and the send settings into sends, if given). value(i) is the raw (plain)
 *  value of all()[i]. The core parameters are read by the processor itself and are skipped here. */
template <typename ValueOf>
void read(PeepChain::Settings& s, Sends* sends, ValueOf&& value) {
    const auto& defs = all();
    std::size_t i = 0;
    auto next = [&] { while (defs[i].core) ++i; const auto& d = defs[i]; return snap(d, value(i++)); };
    auto& gt = s.gate;
    gt.reductionDb = next();
    gt.attackMs = next() * 1e3; gt.holdMs = next() * 1e3; gt.releaseMs = next() * 1e3;
    s.comp.attackMs = next() * 1e3; s.comp.releaseMs = next() * 1e3; s.comp.makeupDb = next();
    auto& f = s.filter;
    f.lowIsCut = next() > 0.5; f.lowFreq = next(); f.lowGain = next();
    for (int k = 0; k < 3; ++k) { f.freq[k] = next(); f.gain[k] = next(); f.bw[k] = next(); }
    f.highIsCut = next() > 0.5; f.highFreq = next(); f.highGain = next();
    for (int k = 0; k < GraphicEQ31::kBands; ++k) s.geqGains[k] = next();
    auto& m = s.multiband;
    for (int k = 0; k < 3; ++k) m.crossover[k] = next();
    m.crossover[1] = std::max(m.crossover[1], m.crossover[0]);      // the splits stay in order
    m.crossover[2] = std::max(m.crossover[2], m.crossover[1]);
    for (int k = 0; k < 4; ++k) m.threshold[k] = next();
    for (int k = 0; k < 4; ++k) m.headroom[k] = next();
    m.attackTime = next(); m.releaseTime = next();
    s.limiterPregainDb = next(); s.limiterRelease = next();
    Sends sd;
    sd.delayTime = next(); sd.delayNote = juce::roundToInt(next()); sd.delayFeedbackPct = next();
    sd.delayLowCut = next(); sd.delayHighCut = next();
    sd.reverbPreDelay = next(); sd.reverbLength = next(); sd.reverbLowCut = next(); sd.reverbHighCut = next();
    sd.reverbWidthPct = next();
    while (i < defs.size() && defs[i].core) ++i;
    jassert(i == defs.size());
    if (sends != nullptr) *sends = sd;
}
template <typename ValueOf>
void read(PeepChain::Settings& s, ValueOf&& value) { read(s, nullptr, value); }

/** How a value reads: "134 Hz", "2.8 kHz", "+12.3 dB", "2.0 oct", "20 ms", "1.20 s", "38 %", "1/4", "OFF"
 *  (withUnit false drops the unit). */
inline juce::String format(const Def& d, double v, bool withUnit) {
    auto unit = [&](const char* u) { return withUnit ? juce::String(" ") + u : juce::String(); };
    if ((d.offAt < 0 && v <= d.min + 1e-6 * std::max(1.0, std::abs(d.min))) || (d.offAt > 0 && v >= d.max * (1.0 - 1e-6)))
        return "OFF";
    switch (d.unit) {
        case Unit::Choice: case Unit::Note: return d.choices[juce::jlimit(0, d.choices.size() - 1, juce::roundToInt(v))];
        case Unit::Hz:
            if (v >= 1000.0) return juce::String(v / 1000.0, v < 9995.0 ? 2 : 1) + (withUnit ? " kHz" : "K");
            return juce::String(juce::roundToInt(v)) + unit("Hz");
        case Unit::Octaves: return juce::String(v, 2) + unit("oct");
        case Unit::Percent: return juce::String(juce::roundToInt(v)) + unit("%");
        case Unit::Seconds:
            if (v < 0.9995) return juce::String(v * 1000.0, v < 0.0995 ? 1 : 0) + unit("ms");
            return juce::String(v, 2) + unit("s");
        case Unit::dB: default:
            return (v > 0.049 && d.min < 0.0 && d.max > 0.0 ? "+" : "") + juce::String(v, 1) + unit("dB");   // a + only where it can go either way
    }
}

/** Defs of one stage (or kSends), in order (indices into all()). */
inline std::vector<int> ofStage(int stage) {
    std::vector<int> v;
    for (int i = 0; i < (int)all().size(); ++i) if (all()[(size_t)i].stage == stage) v.push_back(i);
    return v;
}

} // namespace EffectParams
