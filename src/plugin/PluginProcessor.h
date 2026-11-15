#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "../dsp/PeepChain.h"
#include "../dsp/SendDelay.h"
#include "../dsp/AutoGain.h"
#include <array>
#include <atomic>

/**
 * PeepChain: Lil Peep's GarageBand vocal chain as one plugin.
 * Noise Gate -> Compressor -> AUFilter -> AUGraphicEQ -> AUMultibandCompressor -> AUPeakLimiter,
 * all at Lil Peep's settings. That is the default and it is what was verified.
 *
 * What can be changed is only what depends on the take rather than on the sound:
 *  - input / output level (the chain's thresholds are absolute levels),
 *  - the Noise Gate and Compressor thresholds,
 *  - each of the six effects on / off, and their order,
 *  - how much Delay and Reverb is added,
 *  - Auto Gain (off by default): the output is matched in loudness to the input, so turning the plugin on
 *    and off compares the sound rather than the level.
 *
 * Delay and Reverb are the two sends on his vocal track (GarageBand's master Echo and Reverb): the finished
 * vocal is fed to both side by side and their output is added to it. The level knobs default to his send
 * amounts and go down to "off".
 */
class PeepChainProcessor : public juce::AudioProcessor {
public:
    static constexpr int kStages = PeepChain::kStages;
    using Order = std::array<int, kStages>;            // order[slot] = stage
    static constexpr float kGateThresholdDefault = -46.0f, kCompThresholdDefault = -26.0f;
    static constexpr float kDelayLevelDefault = -16.5f, kReverbLevelDefault = -6.0f;   // Peep's send amounts, dB
    static constexpr float kSendOffDb = -60.0f;        // bottom of a send knob: off
    /** False until the reverb has been measured from GarageBand and its impulse response built in. */
    static bool hasReverb();
    static const char* const stageParamIds[kStages];   // on/off parameter of each stage

    PeepChainProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void reset() override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "PeepChain"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }       // echoes and reverb ring on after the vocal stops
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Peep"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    /** Processing order of the six effects. Saved with the session; not a host-automatable parameter. */
    Order getOrder() const;
    void setOrder(const Order&);
    bool isStageOn(int stage) const { return stageOn[(size_t)stage]->load() > 0.5f; }
    /** True when everything that shapes the sound is exactly as in Peep's preset (levels aside). */
    bool isPeepPreset() const;
    /** Back to Peep's preset: his order, all six effects on, his two thresholds and send amounts. Leaves input / output alone. */
    void resetToPeep();

    juce::AudioProcessorValueTreeState apvts;

    /** Meter values for the editor (written on the audio thread). */
    struct Meters {
        std::atomic<float> inputPeakDb{-100.f}, outputPeakDb{-100.f};
        std::atomic<float> gateOpen{0.f};            // 0..1
        std::atomic<float> compressorDb{0.f};        // gain reduction, positive dB
        std::atomic<float> bandDb[4];                // multiband gain reduction per band, positive dB
        std::atomic<float> limiterDb{0.f};
        std::atomic<float> autoGainDb{0.f};          // gain Auto Gain applies now (or would apply, while off), dB
        std::atomic<bool> autoGainOn{false};
        std::atomic<uint32_t> blocks{0};             // counts processed blocks, so the editor can tell when audio has stopped
        Meters() { for (auto& b : bandDb) b = 0.f; }
    } meters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static uint32_t packOrder(const Order&);
    static Order unpackOrder(uint32_t);
    PeepChain::Settings currentSettings() const;
    void applyControls();
    struct Peaks { double in = 0, out = 0, gateMin = 1, comp = 0, lim = 0, band[4] = {0, 0, 0, 0}; };
    void processChunk(float* l, float* r, int numIn, int numOut, int n, Peaks&);

    PeepChain chain;
    std::atomic<float>* inputDb = nullptr; std::atomic<float>* outputDb = nullptr;
    std::atomic<float>* gateThreshold = nullptr; std::atomic<float>* compThreshold = nullptr;
    std::atomic<float>* delayLevel = nullptr; std::atomic<float>* reverbLevel = nullptr;
    std::atomic<float>* autoGainOn = nullptr;
    AutoGain autoGain;
    juce::AudioBuffer<double> inPowerBuf;              // K-weighted input power per sample, for Auto Gain
    static float sendGain(float knobDb, float unityAtDb);
    SendDelay delay;
    juce::dsp::Convolution reverb{juce::dsp::Convolution::NonUniform{1024}};   // zero latency
    juce::AudioBuffer<double> dryBuf; juce::AudioBuffer<float> delayBuf, reverbBuf;
    juce::SmoothedValue<float> delayGain, reverbGain;
    bool reverbIdle = true;
    std::array<std::atomic<float>*, kStages> stageOn{};
    std::atomic<uint32_t> order;
    juce::SmoothedValue<double> inGain, outGain;
    int maxBlock = 0;                                  // the most samples processChunk handles at once (buffers are this big)
    // The input delayed by the reported latency, kept up to date all the time so that bypass is seamless.
    juce::AudioBuffer<float> bypassDelay; int bypassPos = 0;
    bool wasBypassed = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PeepChainProcessor)
};
