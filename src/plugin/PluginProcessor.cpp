#include "PluginProcessor.h"
#include "PluginEditor.h"
#if PEEPCHAIN_HAS_REVERB
 #include "BinaryData.h"
#endif

const char* const PeepChainProcessor::stageParamIds[kStages] = {"gateOn", "compOn", "filterOn", "eqOn", "multiOn", "limitOn"};
static const char* const kOrderProperty = "order";

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
   #if PEEPCHAIN_HAS_REVERB
    // The reverb is its measured impulse response, used as it is: no trimming,
    // no level normalisation. JUCE resamples it when the host is not at 44.1 kHz.
    reverb.loadImpulseResponse(BinaryData::reverb_ir_wav, (size_t)BinaryData::reverb_ir_wavSize, juce::dsp::Convolution::Stereo::yes,
                               juce::dsp::Convolution::Trim::no, 0, juce::dsp::Convolution::Normalise::no);
   #endif
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
    return s;
}

void PeepChainProcessor::applyControls() {
    chain.setGateThresholdDb((double)gateThreshold->load());
    chain.setCompressorThresholdDb((double)compThreshold->load());
    for (int i = 0; i < kStages; ++i) chain.setEnabled(i, isStageOn(i));
    const auto o = unpackOrder(order.load());
    chain.setOrder(o.data());
}

void PeepChainProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    chain.prepare(sampleRate, currentSettings());
    // Hosts may hand over more than they promised; processBlock then works in pieces of this size, so the
    // audio thread never allocates.
    maxBlock = juce::jlimit(64, 8192, samplesPerBlock);
    delay.prepare(sampleRate, SendDelay::Settings());
    reverb.prepare({sampleRate, (juce::uint32)maxBlock, 2}); reverb.reset(); reverbIdle = true;
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
    chain.reset(); delay.reset(); reverb.reset(); reverbIdle = true; autoGain.reset();
    bypassDelay.clear(); bypassPos = 0;
}

void PeepChainProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numIn = juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
    const int numOut = juce::jmin(getTotalNumOutputChannels(), buffer.getNumChannels()), total = buffer.getNumSamples();
    if (numIn < 1 || numOut < 1 || total <= 0 || maxBlock <= 0) { buffer.clear(); return; }
    if (wasBypassed) {
        // Coming back from bypass: the effects' state is from before it, so start clean rather than replay it.
        chain.reset(); delay.reset(); reverb.reset(); reverbIdle = true; autoGain.reset(); wasBypassed = false;
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

    float* l = buffer.getWritePointer(0);
    float* r = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    Peaks pk;
    for (int start = 0; start < total; start += maxBlock)
        processChunk(l + start, r != nullptr ? r + start : nullptr, numIn, numOut, juce::jmin(maxBlock, total - start), pk);
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, total);

    meters.inputPeakDb = (float)juce::Decibels::gainToDecibels(pk.in, -100.0);
    meters.outputPeakDb = (float)juce::Decibels::gainToDecibels(pk.out, -100.0);
    meters.gateOpen = (float)pk.gateMin; meters.compressorDb = (float)pk.comp; meters.limiterDb = (float)pk.lim;
    for (int b = 0; b < 4; ++b) meters.bandDb[b] = (float)pk.band[b];
    meters.autoGainOn = autoGainOn->load() > 0.5f;
    meters.autoGainDb = (float)(meters.autoGainOn ? autoGain.getAppliedDb() : autoGain.getMatchDb());
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
        rev[0][i] = rev[1][i] = (float)send;                             // (the reverb turns it into stereo)
        dly[0][i] = dly[1][i] = (float)delay.process(send);
        pk.gateMin = juce::jmin(pk.gateMin, chain.gateGain());
        pk.comp = juce::jmax(pk.comp, chain.compressorReductionDb());
        pk.lim = juce::jmax(pk.lim, chain.limiterReductionDb());
        for (int b = 0; b < 4; ++b) pk.band[b] = juce::jmax(pk.band[b], chain.bandReductionDb(b));
    }

    // 2. reverb, a block at a time. Skipped entirely while its knob is off.
    const bool reverbWanted = hasReverb() && (reverbGain.getTargetValue() > 0.0f || reverbGain.isSmoothing());
    if (reverbWanted) {
        juce::dsp::AudioBlock<float> block(reverbBuf.getArrayOfWritePointers(), 2, (size_t)n);
        reverb.process(juce::dsp::ProcessContextReplacing<float>(block));
        reverbIdle = false;
    } else if (!reverbIdle) { reverb.reset(); reverbIdle = true; }

    // 3. dry + sends, then the output level
    const bool monoOut = !(numOut > 1 && r != nullptr);
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

//==============================================================================
PeepChainProcessor::Order PeepChainProcessor::getOrder() const { return unpackOrder(order.load()); }

void PeepChainProcessor::setOrder(const Order& o) {
    if (!PeepChain::isValidOrder(o.data()) || packOrder(o) == order.load()) return;
    order.store(packOrder(o));
    updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true));     // marks the session as changed
}

bool PeepChainProcessor::isPeepPreset() const {
    if (order.load() != packOrder({0, 1, 2, 3, 4, 5})) return false;
    for (int i = 0; i < kStages; ++i) if (!isStageOn(i)) return false;
    return std::abs(gateThreshold->load() - kGateThresholdDefault) < 0.05f && std::abs(compThreshold->load() - kCompThresholdDefault) < 0.05f
        && std::abs(delayLevel->load() - kDelayLevelDefault) < 0.05f && std::abs(reverbLevel->load() - kReverbLevelDefault) < 0.05f;
}

void PeepChainProcessor::resetToPeep() {
    auto set = [this](const char* id) {
        if (auto* p = apvts.getParameter(id)) {
            if (std::abs(p->getValue() - p->getDefaultValue()) < 1e-6f) return;
            p->beginChangeGesture(); p->setValueNotifyingHost(p->getDefaultValue()); p->endChangeGesture();
        }
    };
    set("gateThresh"); set("compThresh"); set("delayLevel"); set("reverbLevel");
    for (auto* id : stageParamIds) set(id);
    setOrder({0, 1, 2, 3, 4, 5});
}

void PeepChainProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    juce::StringArray parts;
    for (int st : getOrder()) parts.add(juce::String(st));
    state.setProperty(kOrderProperty, parts.joinIntoString(","), nullptr);
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
    apvts.replaceState(state);
}

juce::AudioProcessorEditor* PeepChainProcessor::createEditor() { return new PeepChainEditor(*this); }

#if ! PEEPCHAIN_NO_PLUGIN_ENTRY
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PeepChainProcessor(); }
#endif
