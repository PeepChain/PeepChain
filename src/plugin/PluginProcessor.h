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
 *  - Delay sync (off by default): the echo follows the song's tempo (a quarter note) instead of Peep's fixed
 *    500 ms, which is a quarter note at GarageBand's default 120 bpm.
 *
 * Every change made in the editor can be undone and redone, and there are two settings slots (A and B) to
 * compare. Both live here rather than in the editor, so closing the window loses neither.
 *
 * Delay and Reverb are the two sends on his vocal track (GarageBand's master Echo and Reverb): the finished
 * vocal is fed to both side by side and their output is added to it. The level knobs default to his send
 * amounts and go down to "off".
 */
class PeepChainProcessor : public juce::AudioProcessor,
                           private juce::AudioProcessorParameter::Listener, private juce::AsyncUpdater {
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
    ~PeepChainProcessor() override;

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
    /** Back to Peep's preset: his order, all six effects on, his two thresholds and send amounts, delay sync off.
        Leaves input / output and Auto Gain alone. One undo step. */
    void resetToPeep();

    // Undo / redo. A step is recorded whenever a change made in the editor is finished (a knob let go, a
    // piece clicked or dropped, Reset, an A/B switch...). Message thread only.
    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    void undo();
    void redo();
    /** Records a step if anything changed since the last one (called by the processor itself after each change). */
    void commitUndoStep();

    // A/B: two sets of settings. The live settings are the active slot; the other is kept here.
    int getActiveSlot() const { const juce::ScopedLock l(slotLock); return activeSlot; }
    /** Puts the live settings away in the active slot and loads the other (a copy of the live ones the first time). */
    void selectSlot(int slot);
    /** Copies the live settings over the other slot. */
    void copyToOtherSlot();

    /** The echo's current delay time, ms (500 unless synced to the host's tempo). */
    float getDelayMs() const { return delayMs.load(); }

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
    static constexpr double kMaxDelaySeconds = 2.0;    // a quarter note at 30 bpm
    int delaySamplesNow();                             // from the host's tempo when synced (audio thread)
    std::atomic<float>* delaySync = nullptr;
    std::atomic<float> delayMs{500.0f};
    double sampleRateNow = 44100.0;
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

    // undo / A-B
    juce::ValueTree soundState() const;               // every parameter and the order
    void applySound(const juce::ValueTree&);
    juce::ValueTree makeStep() const;                  // the sound plus the A/B slots
    void restoreStep(const juce::ValueTree&);
    void parameterValueChanged(int, float) override {}
    void parameterGestureChanged(int, bool starting) override;
    void handleAsyncUpdate() override;
    std::vector<juce::ValueTree> undoStack, redoStack;
    juce::ValueTree committed;
    int suspendCommits = 0;
    std::atomic<bool> commitPending{false}, historyResetPending{false};
    struct CommitGroup {                               // several parameter changes that are one step
        explicit CommitGroup(PeepChainProcessor& p) : proc(p) { ++proc.suspendCommits; }
        ~CommitGroup() { if (--proc.suspendCommits == 0) proc.commitUndoStep(); }
        PeepChainProcessor& proc;
    };
    juce::CriticalSection slotLock;                    // getStateInformation may come from any thread
    int activeSlot = 0;
    juce::ValueTree otherSlot;                         // the inactive slot's sound (invalid until first used)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PeepChainProcessor)
};
