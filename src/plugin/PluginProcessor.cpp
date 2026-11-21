#include "PluginProcessor.h"
#include "PluginEditor.h"
#if PEEPCHAIN_HAS_REVERB
 #include "BinaryData.h"
#endif

const char* const PeepChainProcessor::stageParamIds[kStages] = {"gateOn", "compOn", "filterOn", "eqOn", "multiOn", "limitOn"};
static const char* const kOrderProperty = "order";
static const juce::Identifier kSlotProperty("abSlot"), kOtherSlotType("OtherSlot"), kSoundType("Sound"), kStepType("Step");
static constexpr size_t kMaxUndoSteps = 100;

uint32_t PeepChainProcessor::packOrder(const Order& o) {
    uint32_t v = 0;
    for (int i = 0; i < kStages; ++i) v |= (uint32_t)o[(size_t)i] << (3 * i);
    return v;
}
PeepChainProcessor::Order PeepChainProcessor::unpackOrder(uint32_t v) {
    Order o;
    for (int i = 0; i < kStages; ++i) o[(size_t)i] = (int)((v >> (3 * i)) & 7u);
    return o;
}

PeepChainProcessor::PeepChainProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PeepChain", createLayout()),
      order(packOrder({0, 1, 2, 3, 4, 5})) {
    inputDb = apvts.getRawParameterValue("input");
    outputDb = apvts.getRawParameterValue("output");
    gateThreshold = apvts.getRawParameterValue("gateThresh");
    compThreshold = apvts.getRawParameterValue("compThresh");
    for (int i = 0; i < kStages; ++i) stageOn[(size_t)i] = apvts.getRawParameterValue(stageParamIds[i]);
    delayLevel = apvts.getRawParameterValue("delayLevel");
    reverbLevel = apvts.getRawParameterValue("reverbLevel");
    autoGainOn = apvts.getRawParameterValue("autoGain");
    delaySync = apvts.getRawParameterValue("delaySync");
    reverbSync = apvts.getRawParameterValue("reverbSync");
    for (const auto& d : EffectParams::all()) effectValues.push_back(apvts.getRawParameterValue(d.id));
    for (auto* prm : getParameters()) prm->addListener(this);
    committed = makeStep();
}

PeepChainProcessor::~PeepChainProcessor() {
    stopTimer();
    cancelPendingUpdate();
    for (auto* prm : getParameters()) prm->removeListener(this);
}

const juce::AudioBuffer<float>& PeepChainProcessor::reverbResponse() {
    // The reverb is its measured impulse response, used as it is: no level normalisation, and nothing cut but its
    // silent first kIrTrim samples, which the pre-delay line plays instead.
    static const juce::AudioBuffer<float> response = [] {
        juce::AudioBuffer<float> b;
       #if PEEPCHAIN_HAS_REVERB
        juce::AudioFormatManager formats; formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> ir(formats.createReaderFor(
            std::make_unique<juce::MemoryInputStream>(BinaryData::reverb_ir_wav, (size_t)BinaryData::reverb_ir_wavSize, false)));
        jassert(ir != nullptr && ir->sampleRate == kIrRate && ir->lengthInSamples > kIrOnset);
        if (ir != nullptr) {
            const int len = (int)ir->lengthInSamples - kIrTrim;
            b.setSize((int)ir->numChannels, len);
            ir->read(&b, 0, len, kIrTrim, true, true);
        }
       #endif
        return b;
    }();
    return response;
}

int PeepChainProcessor::reverbBlockFor(double sampleRate) {
    const double shortest = (kIrOnset * kPeepBpm / 300.0 - (kIrOnset - kIrTrim)) / kIrRate * sampleRate;   // sync at 300 bpm
    int b = 64;
    while (b * 2 <= 1024 && b * 2 <= shortest) b *= 2;
    return b;
}

bool PeepChainProcessor::hasReverb() {
   #if PEEPCHAIN_HAS_REVERB
    return true;
   #else
    return false;
   #endif
}

float PeepChainProcessor::sendGain(float knobDb, float unityAtDb) {
    return knobDb <= kSendOffDb + 0.05f ? 0.0f : juce::Decibels::decibelsToGain(knobDb - unityAtDb);
}

juce::AudioProcessorValueTreeState::ParameterLayout PeepChainProcessor::createLayout() {
    using P = juce::AudioParameterFloat;
    auto dbText = juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction(
        [](float v, int) { return (v > 0 ? "+" : "") + juce::String(v, 1) + " dB"; });
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add(std::make_unique<P>(juce::ParameterID{"input", 1}, "Input", juce::NormalisableRange<float>(-24.f, 24.f, 0.1f), 0.f, dbText));
    layout.add(std::make_unique<P>(juce::ParameterID{"output", 1}, "Output", juce::NormalisableRange<float>(-24.f, 12.f, 0.1f), 0.f, dbText));
    // Added after the first build, hence version hint 2 (keeps the AU parameter order stable).
    layout.add(std::make_unique<P>(juce::ParameterID{"gateThresh", 2}, "Gate Threshold", juce::NormalisableRange<float>(-80.f, -10.f, 0.1f), kGateThresholdDefault, dbText));
    layout.add(std::make_unique<P>(juce::ParameterID{"compThresh", 2}, "Compressor Threshold", juce::NormalisableRange<float>(-50.f, 0.f, 0.1f), kCompThresholdDefault, dbText));
    const char* names[kStages] = {"Noise Gate", "Compressor", "Filter", "Graphic EQ", "Multiband", "Limiter"};
    for (int i = 0; i < kStages; ++i)
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{stageParamIds[i], 2}, names[i], true));
    // The two sends (version hint 3). The bottom of the range reads "Off".
    auto sendText = juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction(
        [](float v, int) { return v <= kSendOffDb + 0.05f ? juce::String("Off") : (v > 0 ? "+" : "") + juce::String(v, 1) + " dB"; });
    layout.add(std::make_unique<P>(juce::ParameterID{"delayLevel", 3}, "Delay", juce::NormalisableRange<float>(kSendOffDb, 6.f, 0.1f, 2.5f), kDelayLevelDefault, sendText));
    layout.add(std::make_unique<P>(juce::ParameterID{"reverbLevel", 3}, "Reverb", juce::NormalisableRange<float>(kSendOffDb, 6.f, 0.1f, 2.5f), kReverbLevelDefault, sendText));
    // Loudness match at the very end (version hint 4). Off by default: the plugin opens at Peep's real level.
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"autoGain", 4}, "Auto Gain", false));
    // Echo follows the host's tempo (version hint 5). Off by default: Peep's echo is always 500 ms.
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"delaySync", 5}, "Delay Sync", false));
    // The reverb's pre-delay follows the host's tempo (version hint 6). Off by default: Peep's is always 58 ms.
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{"reverbSync", 6}, "Reverb Sync", false));
    // The effects' own settings, each defaulting to Peep's (version hint 7; 8 for the gate, compressor times and
    // sends, added later). See EffectParams.h. The core ones (thresholds, send levels, syncs) are made above.
    for (int version : {7, 8})
    for (const auto& d : EffectParams::all()) {
        if (d.core || d.version != version) continue;
        if (d.unit == EffectParams::Unit::Choice || d.unit == EffectParams::Unit::Note) {
            layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{d.id, version}, d.name, d.choices, (int)d.peep));
            continue;
        }
        juce::NormalisableRange<float> range((float)d.min, (float)d.max);
        if (d.log) range.setSkewForCentre((float)std::sqrt(d.min * d.max));
        const auto unit = d.unit;
        auto attrs = juce::AudioParameterFloatAttributes()
            .withLabel(unit == EffectParams::Unit::Hz ? "Hz" : unit == EffectParams::Unit::Octaves ? "oct" : unit == EffectParams::Unit::Seconds ? "s"
                       : unit == EffectParams::Unit::Percent ? "%" : "dB")
            .withStringFromValueFunction([def = d](float v, int) { return EffectParams::format(def, EffectParams::snap(def, v), true); })
            .withValueFromStringFunction([unit](const juce::String& t) {
                double v = t.getDoubleValue();
                if (unit == EffectParams::Unit::Hz && t.containsIgnoreCase("k")) v *= 1000.0;
                if (unit == EffectParams::Unit::Seconds && t.containsIgnoreCase("ms")) v /= 1000.0;
                return (float)v;
            });
        layout.add(std::make_unique<P>(juce::ParameterID{d.id, version}, d.name, range, (float)d.peep, attrs));
    }
    return layout;
}

bool PeepChainProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk && out.size() >= in.size();        // mono->mono, mono->stereo, stereo->stereo
}

PeepChain::Settings PeepChainProcessor::currentSettings() const {
    PeepChain::Settings s;
    s.liveSwitching = true;                                  // constant latency, click-free switching
    s.gate.thresholdDb = (double)gateThreshold->load();
    s.comp.thresholdDb = (double)compThreshold->load();
    s.comp.matchThresholdDb = kCompThresholdDefault;         // gain match: loud passages leave the Compressor as they would at Peep's threshold
    const auto o = unpackOrder(order.load());
    for (int i = 0; i < kStages; ++i) { s.enable[i] = isStageOn(i); s.order[i] = o[(size_t)i]; }
    readEffects(s);
    return s;
}

void PeepChainProcessor::readEffects(PeepChain::Settings& s, EffectParams::Sends* sd) const {
    EffectParams::read(s, sd, [this](std::size_t i) { return effectValues[i]->load(); });
}

void PeepChainProcessor::applySends(const EffectParams::Sends& d, bool force) {
    const EffectParams::Sends peep;
    if (force || d.delayFeedbackPct != sends.delayFeedbackPct)       // scaled from Peep's 38 %, exactly his at 38
        delay.setFeedback(d.delayFeedbackPct == peep.delayFeedbackPct ? SendDelay::Settings().feedback
                                                                      : SendDelay::Settings().feedback * d.delayFeedbackPct / peep.delayFeedbackPct);
    if (force || d.delayLowCut != sends.delayLowCut || d.delayHighCut != sends.delayHighCut) delay.setFilters(d.delayHighCut, d.delayLowCut);
    reverbWidth.setTargetValue((float)(d.reverbWidthPct / 100.0));
    sends = d;
}

double PeepChainProcessor::getTailLengthSeconds() const {
    // How long the echoes (down 60 dB) and the reverb (its pre-delay and 1.5 x its decay time) ring on. At least
    // 6 s, as before the send settings existed; long echoes with a lot of feedback can need up to 30.
    PeepChain::Settings s; EffectParams::Sends d; readEffects(s, &d);
    const double loop = SendDelay::Settings().feedback * d.delayFeedbackPct / EffectParams::Sends().delayFeedbackPct;
    const double repeats = loop > 1e-3 ? std::ceil(60.0 / -juce::Decibels::gainToDecibels(loop)) : 1.0;
    const double delaySec = juce::jmax((double)delayMs.load() * 1e-3, d.delayTime);
    const double tail = juce::jmax(delaySec * (repeats + 1.0), reverbPreDelayMs.load() * 1e-3 + 1.5 * d.reverbLength);
    return juce::jlimit(6.0, 30.0, tail);
}

ReverbConvolver::Shape PeepChainProcessor::reverbShape() const {
    PeepChain::Settings s; EffectParams::Sends d; readEffects(s, &d);
    ReverbConvolver::Shape sh;
    sh.rt60 = EffectParams::kReverbRt60;
    sh.decayScale = d.reverbLength / EffectParams::kReverbRt60;
    sh.lowCutHz = d.reverbLowCut > 20.0 ? d.reverbLowCut : 0.0;         // the ends of the ranges are off
    sh.highCutHz = d.reverbHighCut < 20000.0 ? d.reverbHighCut : 0.0;
    return sh;
}

void PeepChainProcessor::timerCallback() {
    if (!hasReverb()) return;
    const juce::ScopedLock l(reverbLock);
    reverb.setShape(reverbShape());                                  // a no-op (but for tidying up) when unchanged
}

void PeepChainProcessor::applyControls() {
    chain.setGateThresholdDb((double)gateThreshold->load());
    chain.setCompressorThresholdDb((double)compThreshold->load());
    for (int i = 0; i < kStages; ++i) chain.setEnabled(i, isStageOn(i));
    const auto o = unpackOrder(order.load());
    chain.setOrder(o.data());
    PeepChain::Settings s; EffectParams::Sends d;
    readEffects(s, &d);
    chain.setEffectSettings(s);                              // only what changed is recomputed
    applySends(d, false);
}

void PeepChainProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    chain.prepare(sampleRate, currentSettings());
    // Hosts may hand over more than they promised; processBlock then works in pieces of this size, so the
    // audio thread never allocates.
    maxBlock = juce::jlimit(64, 8192, samplesPerBlock);
    sampleRateNow = sampleRate;
    delay.prepare(sampleRate, SendDelay::Settings(), kMaxDelaySeconds);
    { PeepChain::Settings s; EffectParams::Sends d; readEffects(s, &d); reverbWidth.reset(sampleRate, 0.05); applySends(d, true);
      reverbWidth.setCurrentAndTargetValue((float)(d.reverbWidthPct / 100.0)); }
    const double bpm = songBpm();
    delay.setDelaySamples(delaySamplesNow(bpm)); delay.reset();
    reverbLatency = reverbBlockFor(sampleRate);
    preDelay.prepare(sampleRate, kMaxPreDelaySeconds);
    preDelay.setDelaySamples(preDelaySamplesNow(bpm)); preDelay.reset();
    if (hasReverb()) {
        const juce::ScopedLock l(reverbLock);
        reverb.prepare(reverbResponse(), kIrRate, sampleRate, reverbLatency, reverbShape());
    }
    startTimerHz(15);
    reverbIdle = true;
    dryBuf.setSize(2, maxBlock); delayBuf.setSize(2, maxBlock); reverbBuf.setSize(2, maxBlock);
    inPowerBuf.setSize(1, maxBlock); autoGain.prepare(sampleRate);
    delayGain.reset(sampleRate, 0.02); reverbGain.reset(sampleRate, 0.02);
    delayGain.setCurrentAndTargetValue(sendGain(delayLevel->load(), kDelayLevelDefault));
    reverbGain.setCurrentAndTargetValue(hasReverb() ? sendGain(reverbLevel->load(), kReverbLevelDefault) : 0.0f);
    setLatencySamples(chain.getLatency());
    inGain.reset(sampleRate, 0.02); outGain.reset(sampleRate, 0.02);
    inGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain((double)inputDb->load()));
    outGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain((double)outputDb->load()));
    bypassDelay.setSize(2, juce::jmax(1, chain.getLatency())); bypassDelay.clear(); bypassPos = 0;
    wasBypassed = false;
}

void PeepChainProcessor::reset() {
    // Hosts call this from the audio thread too (Logic resets an Audio Unit whenever playback starts), so
    // nothing here allocates.
    chain.reset(); delay.reset(); preDelay.reset(); reverb.reset(); reverbIdle = true; autoGain.reset();
    bypassDelay.clear(); bypassPos = 0;
}

void PeepChainProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numIn = juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
    const int numOut = juce::jmin(getTotalNumOutputChannels(), buffer.getNumChannels()), total = buffer.getNumSamples();
    if (numIn < 1 || numOut < 1 || total <= 0 || maxBlock <= 0) { buffer.clear(); return; }
    if (wasBypassed) {
        // Coming back from bypass: the effects' state is from before it, so start clean rather than replay it.
        chain.reset(); delay.reset(); preDelay.reset(); reverb.reset(); reverbIdle = true; autoGain.reset(); wasBypassed = false;
    }
    // Keep the bypass line fed with the input (before anything below overwrites the buffer).
    {
        const int len = bypassDelay.getNumSamples();
        for (int i = 0; i < total; ++i) {
            for (int c = 0; c < 2; ++c) bypassDelay.setSample(c, bypassPos, buffer.getSample(c < numIn ? c : 0, i));
            if (++bypassPos >= len) bypassPos = 0;
        }
    }
    inGain.setTargetValue(juce::Decibels::decibelsToGain((double)inputDb->load()));
    outGain.setTargetValue(juce::Decibels::decibelsToGain((double)outputDb->load()));
    // Both sends were measured at Peep's own send amounts, so each knob is unity gain at its default.
    delayGain.setTargetValue(sendGain(delayLevel->load(), kDelayLevelDefault));
    reverbGain.setTargetValue(hasReverb() ? sendGain(reverbLevel->load(), kReverbLevelDefault) : 0.0f);
    applyControls();
    const double bpm = songBpm();
    delay.setDelaySamples(delaySamplesNow(bpm));
    preDelay.setDelaySamples(preDelaySamplesNow(bpm));

    float* l = buffer.getWritePointer(0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    Peaks pk;
    for (int start = 0; start < total; start += maxBlock)
        processChunk(l + start, r != nullptr ? r + start : nullptr, numIn, numOut, juce::jmin(maxBlock, total - start), pk);
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, total);

    meters.inputPeakDb = (float)juce::Decibels::gainToDecibels(pk.in, -100.0);
    meters.outputPeakDb = (float)juce::Decibels::gainToDecibels(pk.out, -100.0);
    const double compDb = Peaks::reductionDb(pk.compMin), limDb = Peaks::reductionDb(pk.limMin);
    meters.gateOpen = (float)pk.gateMin; meters.compressorDb = (float)compDb; meters.limiterDb = (float)limDb;
    for (int b = 0; b < 4; ++b) meters.bandDb[b] = (float)Peaks::reductionDb(pk.bandMin[b]);
    meters.autoGainOn = autoGainOn->load() > 0.5f;
    meters.autoGainDb = (float)(meters.autoGainOn ? autoGain.getAppliedDb() : autoGain.getMatchDb());
    Meters::hold(meters.compLevelDb, (float)chain.compressorLevelDb());   // a slow RMS: once a block is plenty
    Meters::hold(meters.compGrDb, (float)compDb);
    Meters::hold(meters.gateInDb, (float)juce::Decibels::gainToDecibels(chain.gateInPeak(), -120.0));
    Meters::hold(meters.gateGrDb, (float)-juce::Decibels::gainToDecibels(pk.gateMin, -120.0));
    chain.resetGatePeak();
    for (int b = 0; b < 4; ++b) Meters::hold(meters.bandLevelDb[b], (float)(10.0 * std::log10(2.0 * pk.bandMs[b] + 1e-12)));
    Meters::hold(meters.limInDb, (float)juce::Decibels::gainToDecibels(chain.limiterInPeak(), -120.0));
    Meters::hold(meters.limOutDb, (float)juce::Decibels::gainToDecibels(chain.limiterOutPeak(), -120.0));
    Meters::hold(meters.limGrDb, (float)limDb);
    chain.resetLimiterPeaks();
    meters.blocks.fetch_add(1, std::memory_order_relaxed);
}

void PeepChainProcessor::processChunk(float* l, float* r, int numIn, int numOut, int n, Peaks& pk) {
    double* dry[2] = {dryBuf.getWritePointer(0), dryBuf.getWritePointer(1)};
    float* dly[2] = {delayBuf.getWritePointer(0), delayBuf.getWritePointer(1)};
    float* rev[2] = {reverbBuf.getWritePointer(0), reverbBuf.getWritePointer(1)};
    double* inPow = inPowerBuf.getWritePointer(0);
    const bool agOn = autoGainOn->load() > 0.5f;
    auto clean = [](double v) { return std::isfinite(v) ? v : 0.0; };   // a NaN or inf from the host would poison every filter for good

    // 1. the chain; its output is the dry vocal and also what both sends are fed
    for (int i = 0; i < n; ++i) {
        const double gi = inGain.getNextValue();
        const double x0 = clean(l[i]), x1 = (numIn > 1 && r != nullptr) ? clean(r[i]) : x0;   // mono in: feed both sides
        inPow[i] = autoGain.inputPower(x0, x1);                          // what arrived, before the Input knob
        double f[2] = {x0 * gi, x1 * gi};
        pk.in = juce::jmax(pk.in, std::abs(f[0]), std::abs(f[1]));
        chain.processFrame(f);
        dry[0][i] = f[0]; dry[1][i] = f[1];
        const double send = 0.5 * (f[0] + f[1]);                         // the sends were measured from a mono track
        rev[0][i] = (float)preDelay.process(send);                       // mono; the reverb turns it into stereo
        dly[0][i] = dly[1][i] = (float)delay.process(send);
        pk.gateMin = juce::jmin(pk.gateMin, chain.gateGain());
        pk.compMin = juce::jmin(pk.compMin, chain.compressorGain());
        pk.limMin = juce::jmin(pk.limMin, chain.limiterGain());
        for (int b = 0; b < 4; ++b) { pk.bandMin[b] = juce::jmin(pk.bandMin[b], chain.bandGain(b)); pk.bandMs[b] = juce::jmax(pk.bandMs[b], chain.bandMeanSquare(b)); }
    }

    // 2. reverb, a block at a time. Skipped entirely while its knob is off.
    const bool reverbWanted = hasReverb() && (reverbGain.getTargetValue() > 0.0f || reverbGain.isSmoothing());
    if (reverbWanted) {
        reverb.process(rev[0], rev[0], rev[1], n);
        reverbIdle = false;
    } else if (!reverbIdle) { reverb.reset(); reverbIdle = true; }

    // 3. dry + sends, then the output level
    const bool monoOut = !(numOut > 1 && r != nullptr);
    if (reverbWanted && (reverbWidth.isSmoothing() || reverbWidth.getTargetValue() != 1.0f))   // width: the reverb's sides scaled
        for (int i = 0; i < n; ++i) {
            const float w = reverbWidth.getNextValue(), m = 0.5f * (rev[0][i] + rev[1][i]), sd = 0.5f * (rev[0][i] - rev[1][i]) * w;
            rev[0][i] = m + sd; rev[1][i] = m - sd;
        }
    for (int i = 0; i < n; ++i) {
        const double go = outGain.getNextValue();
        const float gd = delayGain.getNextValue(), gr = reverbWanted ? reverbGain.getNextValue() : (reverbGain.getNextValue(), 0.0f);
        double f[2] = {dry[0][i], dry[1][i]};
        if (gd != 0.0f) { f[0] += gd * dly[0][i]; f[1] += gd * dly[1][i]; }
        if (gr != 0.0f) { f[0] += gr * rev[0][i]; f[1] += gr * rev[1][i]; }
        if (monoOut && (gd != 0.0f || gr != 0.0f)) f[0] = f[1] = 0.5 * (f[0] + f[1]);   // the sends are stereo; a mono output gets both sides
        autoGain.process(f, inPow[i], agOn);                                // loudness match (exactly 1 while off)
        f[0] *= go; f[1] *= go;
        pk.out = juce::jmax(pk.out, std::abs(f[0]), monoOut ? 0.0 : std::abs(f[1]));
        l[i] = (float)f[0];
        if (!monoOut) r[i] = (float)f[1];
    }
}

void PeepChainProcessor::processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    // The dry input, delayed by the reported latency so it stays in time with everything else. The line is
    // fed in processBlock too, so switching bypass on carries straight on from the last input.
    const int numIn = juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
    const int len = bypassDelay.getNumSamples(), n = buffer.getNumSamples(), chans = juce::jmin(2, buffer.getNumChannels());
    if (numIn < 1 || len < 1) { buffer.clear(); return; }
    for (int i = 0; i < n; ++i) {
        float in[2];
        for (int c = 0; c < 2; ++c) in[c] = buffer.getSample(c < numIn ? c : 0, i);   // mono in: both sides get it
        for (int c = 0; c < chans; ++c) buffer.setSample(c, i, bypassDelay.getSample(c, bypassPos));
        for (int c = 0; c < 2; ++c) bypassDelay.setSample(c, bypassPos, in[c]);
        if (++bypassPos >= len) bypassPos = 0;
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, n);
    wasBypassed = true;
    meters.gateOpen = 1.f; meters.compressorDb = 0.f; meters.limiterDb = 0.f;
    for (auto& b : meters.bandDb) b = 0.f;
    meters.blocks.fetch_add(1, std::memory_order_relaxed);
}

double PeepChainProcessor::songBpm() {
    if (auto* head = getPlayHead())
        if (const auto pos = head->getPosition())
            if (const auto bpm = pos->getBpm(); bpm.hasValue() && *bpm > 0.0)
                return juce::jlimit(30.0, 300.0, *bpm);
    return 0.0;
}

int PeepChainProcessor::delaySamplesNow(double bpm) {
    double seconds = sends.delayTime;                                    // Peep's: 500 ms, a quarter note at 120 bpm
    if (delaySync->load() > 0.5f && bpm > 0.0) seconds = EffectParams::noteBeats(sends.delayNote) * 60.0 / bpm;   // a note at the song's tempo
    const int n = juce::jlimit(1, (int)std::ceil(kMaxDelaySeconds * sampleRateNow), (int)std::lround(seconds * sampleRateNow));
    delayMs = (float)(1000.0 * n / sampleRateNow);
    return n;
}

int PeepChainProcessor::preDelaySamplesNow(double bpm) {
    // The pre-delay (Peep's 58 ms unless changed), scaled with the tempo when synced. The response itself still
    // holds the last kIrOnset - kIrTrim samples of it, so the line plays the rest. The convolution runs
    // reverbLatency late, so the line can't be shorter than that (only reachable synced to a fast tempo).
    const double scale = reverbSync->load() > 0.5f && bpm > 0.0 ? kPeepBpm / bpm : 1.0;
    const double held = (kIrOnset - kIrTrim) / kIrRate;
    const int n = juce::jlimit(reverbLatency, (int)std::ceil(kMaxPreDelaySeconds * sampleRateNow),
                               (int)std::lround((sends.reverbPreDelay * scale - held) * sampleRateNow));
    reverbPreDelayMs = (float)(1000.0 * ((double)n / sampleRateNow + held));
    return n - reverbLatency;
}

//==============================================================================
PeepChainProcessor::Order PeepChainProcessor::getOrder() const { return unpackOrder(order.load()); }

void PeepChainProcessor::setOrder(const Order& o) {
    if (!PeepChain::isValidOrder(o.data()) || packOrder(o) == order.load()) return;
    order.store(packOrder(o));
    updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));     // marks the session as changed
    if (suspendCommits == 0 && juce::MessageManager::existsAndIsCurrentThread()) commitUndoStep();
}

bool PeepChainProcessor::isPeepPreset() const {
    if (order.load() != packOrder({0, 1, 2, 3, 4, 5})) return false;
    for (int i = 0; i < kStages; ++i) if (!isStageOn(i)) return false;
    for (int i = 0; i <= kStages; ++i) if (!isStageAtPeep(i)) return false;     // and the sends' (EffectParams::kSends)
    return std::abs(gateThreshold->load() - kGateThresholdDefault) < 0.05f && std::abs(compThreshold->load() - kCompThresholdDefault) < 0.05f
        && std::abs(delayLevel->load() - kDelayLevelDefault) < 0.05f && std::abs(reverbLevel->load() - kReverbLevelDefault) < 0.05f
        && delaySync->load() < 0.5f && reverbSync->load() < 0.5f;
}

bool PeepChainProcessor::isStageAtPeep(int stage) const {
    const auto& defs = EffectParams::all();
    for (std::size_t i = 0; i < defs.size(); ++i)
        if (defs[i].stage == stage && EffectParams::snap(defs[i], effectValues[i]->load()) != defs[i].peep) return false;
    return true;
}

void PeepChainProcessor::resetStageToPeep(int stage) {
    CommitGroup step(*this);
    for (const auto& d : EffectParams::all())
        if (auto* p = apvts.getParameter(d.id); p != nullptr && d.stage == stage && std::abs(p->getValue() - p->getDefaultValue()) > 1e-7f) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->getDefaultValue()); p->endChangeGesture();
        }
}

void PeepChainProcessor::resetToPeep() {
    CommitGroup step(*this);
    auto set = [this](const char* id) {
        if (auto* p = apvts.getParameter(id)) {
            if (std::abs(p->getValue() - p->getDefaultValue()) < 1e-6f) return;
            p->beginChangeGesture(); p->setValueNotifyingHost(p->getDefaultValue()); p->endChangeGesture();
        }
    };
    set("gateThresh"); set("compThresh"); set("delayLevel"); set("reverbLevel"); set("delaySync"); set("reverbSync");
    for (auto* id : stageParamIds) set(id);
    for (const auto& d : EffectParams::all()) set(d.id.toRawUTF8());
    setOrder({0, 1, 2, 3, 4, 5});
}

void PeepChainProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    juce::StringArray parts;
    for (int st : getOrder()) parts.add(juce::String(st));
    state.setProperty(kOrderProperty, parts.joinIntoString(","), nullptr);
    {
        const juce::ScopedLock l(slotLock);
        state.setProperty(kSlotProperty, activeSlot, nullptr);
        if (otherSlot.isValid()) { juce::ValueTree o(kOtherSlotType); o.addChild(otherSlot.createCopy(), -1, nullptr); state.addChild(o, -1, nullptr); }
    }
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void PeepChainProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size);
    if (xml == nullptr || !xml->hasTagName(apvts.state.getType())) return;
    auto state = juce::ValueTree::fromXml(*xml);
    Order o{0, 1, 2, 3, 4, 5};                       // sessions saved before reordering existed have no order: Peep's
    const auto parts = juce::StringArray::fromTokens(state.getProperty(kOrderProperty).toString(), ",", "");
    if (parts.size() == kStages) {
        Order read;
        for (int i = 0; i < kStages; ++i) read[(size_t)i] = parts[i].getIntValue();
        if (PeepChain::isValidOrder(read.data())) o = read;
    }
    order.store(packOrder(o));
    {
        const juce::ScopedLock l(slotLock);
        activeSlot = (int)state.getProperty(kSlotProperty, 0) == 1 ? 1 : 0;
        const auto other = state.getChildWithName(kOtherSlotType);
        otherSlot = other.isValid() && other.getChild(0).hasType(kSoundType) ? other.getChild(0).createCopy() : juce::ValueTree();
        if (other.isValid()) state.removeChild(other, nullptr);
    }
    state.removeProperty(kSlotProperty, nullptr);
    apvts.replaceState(state);
    // A loaded session starts a fresh undo history.
    historyResetPending = true;
    if (juce::MessageManager::existsAndIsCurrentThread()) handleAsyncUpdate(); else triggerAsyncUpdate();
}

//==============================================================================
juce::ValueTree PeepChainProcessor::soundState() const {
    juce::ValueTree t(kSoundType);
    for (auto* prm : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*>(prm)) t.setProperty(wp->paramID, prm->getValue(), nullptr);
    juce::StringArray parts;
    for (int st : getOrder()) parts.add(juce::String(st));
    t.setProperty(kOrderProperty, parts.joinIntoString(","), nullptr);
    return t;
}

void PeepChainProcessor::applySound(const juce::ValueTree& t) {
    CommitGroup step(*this);
    for (auto* prm : getParameters())
        if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*>(prm); wp != nullptr && t.hasProperty(wp->paramID)) {
            const float v = (float)t.getProperty(wp->paramID);
            if (std::abs(prm->getValue() - v) < 1e-7f) continue;
            prm->beginChangeGesture(); prm->setValueNotifyingHost(v); prm->endChangeGesture();
        }
    const auto parts = juce::StringArray::fromTokens(t.getProperty(kOrderProperty).toString(), ",", "");
    if (parts.size() == kStages) {
        Order o;
        for (int i = 0; i < kStages; ++i) o[(size_t)i] = parts[i].getIntValue();
        setOrder(o);
    }
}

juce::ValueTree PeepChainProcessor::makeStep() const {
    juce::ValueTree step(kStepType);
    step.addChild(soundState(), -1, nullptr);
    const juce::ScopedLock l(slotLock);
    step.setProperty(kSlotProperty, activeSlot, nullptr);
    if (otherSlot.isValid()) { juce::ValueTree o(kOtherSlotType); o.addChild(otherSlot.createCopy(), -1, nullptr); step.addChild(o, -1, nullptr); }
    return step;
}

void PeepChainProcessor::restoreStep(const juce::ValueTree& step) {
    ++suspendCommits;
    {
        const juce::ScopedLock l(slotLock);
        activeSlot = (int)step.getProperty(kSlotProperty, 0);
        const auto other = step.getChildWithName(kOtherSlotType);
        otherSlot = other.isValid() ? other.getChild(0).createCopy() : juce::ValueTree();
    }
    applySound(step.getChildWithName(kSoundType));
    --suspendCommits;
    committed = makeStep();
}

void PeepChainProcessor::commitUndoStep() {
    JUCE_ASSERT_MESSAGE_THREAD
    if (suspendCommits > 0) return;
    auto now = makeStep();
    if (now.isEquivalentTo(committed)) return;
    undoStack.push_back(committed);
    if (undoStack.size() > kMaxUndoSteps) undoStack.erase(undoStack.begin());
    redoStack.clear();
    committed = now;
}

void PeepChainProcessor::undo() {
    commitUndoStep();                                          // anything not yet recorded (e.g. host automation) first
    if (undoStack.empty()) return;
    redoStack.push_back(committed);
    const auto step = undoStack.back(); undoStack.pop_back();
    restoreStep(step);
}

void PeepChainProcessor::redo() {
    if (redoStack.empty()) return;
    undoStack.push_back(committed);
    const auto step = redoStack.back(); redoStack.pop_back();
    restoreStep(step);
}

void PeepChainProcessor::parameterGestureChanged(int, bool starting) {
    if (starting || suspendCommits > 0) return;
    if (juce::MessageManager::existsAndIsCurrentThread()) commitUndoStep();
    else { commitPending = true; triggerAsyncUpdate(); }
}

void PeepChainProcessor::handleAsyncUpdate() {
    if (historyResetPending.exchange(false)) { undoStack.clear(); redoStack.clear(); committed = makeStep(); commitPending = false; }
    if (commitPending.exchange(false)) commitUndoStep();
}

void PeepChainProcessor::selectSlot(int slot) {
    slot = slot == 1 ? 1 : 0;
    if (slot == getActiveSlot()) return;
    CommitGroup step(*this);
    const auto live = soundState();
    juce::ValueTree load;
    {
        const juce::ScopedLock l(slotLock);
        load = otherSlot.isValid() ? otherSlot : live.createCopy();      // first visit: start from a copy
        otherSlot = live; activeSlot = slot;
    }
    applySound(load);
}

void PeepChainProcessor::copyToOtherSlot() {
    CommitGroup step(*this);
    const auto live = soundState();
    const juce::ScopedLock l(slotLock);
    otherSlot = live;
}

juce::AudioProcessorEditor* PeepChainProcessor::createEditor() { return new PeepChainEditor(*this); }

#if ! PEEPCHAIN_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PeepChainProcessor(); }
#endif
