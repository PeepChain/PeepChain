#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "../dsp/PeepChain.h"
#include "../dsp/SendDelay.h"
#include "../dsp/PreDelay.h"
#include "../dsp/AutoGain.h"
#include "EffectParams.h"
#include "ReverbConvolver.h"
#include "GroupSync.h"
#include <array>
#include <atomic>

/**
 * PeepChain: Lil Peep's GarageBand vocal chain as one plugin.
 * Noise Gate -> Compressor -> AUFilter -> AUGraphicEQ -> AUMultibandCompressor -> AUPeakLimiter,
 * all at Lil Peep's settings. That is the default and it is what was verified.
 *
 * What can be changed:
 *  - input / output level (the chain's thresholds are absolute levels),
 *  - the Noise Gate and Compressor thresholds,
 *  - each of the six effects on / off, and their order,
 *  - how much Delay and Reverb is added,
 *  - Auto Gain (off by default): the output is matched in loudness to the input, so turning the plugin on
 *    and off compares the sound rather than the level.
 *  - Delay sync (off by default): the echo follows the song's tempo (a quarter note) instead of Peep's fixed
 *    500 ms, which is a quarter note at GarageBand's default 120 bpm.
 *  - the effects' own settings (EffectParams.h): all of AUFilter and AUGraphicEQ, AUMultibandCompressor's
 *    crossovers, thresholds, headrooms and times, AUPeakLimiter's pre-gain and release (all checked against
 *    Apple's Audio Units at any value), the Noise Gate's levels and times, the Compressor's times and make-up,
 *    and the sends' time, feedback, filters, pre-delay, length and width. All default to Peep's settings exactly.
 *  - Reverb sync (off by default): the reverb's pre-delay (Peep's is 58 ms) follows the song's tempo, staying
 *    the same part of a beat it is at 120 bpm.
 *  - Group (none by default): PeepChains in the same named group share one sound (GroupSync.h). Everything above
 *    is shared but input, output and Auto Gain, which stay with each track.
 *
 * Every change made in the editor can be undone and redone, and there are two settings slots (A and B) to
 * compare. Both live here rather than in the editor, so closing the window loses neither.
 *
 * Delay and Reverb are the two sends on his vocal track (GarageBand's master Echo and Reverb): the finished
 * vocal is fed to both side by side and their output is added to it. The level knobs default to his send
 * amounts and go down to "off".
 */
class PeepChainProcessor : public juce::AudioProcessor, private GroupHub::Member,
                           private juce::AudioProcessorParameter::Listener, private juce::AsyncUpdater, private juce::Timer {
public:
    static constexpr int kStages = PeepChain::kStages;
    using Order = std::array<int, kStages>;            // order[slot] = stage
    static constexpr float kGateThresholdDefault = -46.0f, kCompThresholdDefault = -26.0f;
    static constexpr float kDelayLevelDefault = -16.5f, kReverbLevelDefault = -6.0f;   // Peep's send amounts, dB
    static constexpr float kSendOffDb = -60.0f;        // bottom of a send knob: off
    /** False until the reverb has been measured from GarageBand and its impulse response built in. */
    static bool hasReverb();
    /** The reverb's impulse response at 44.1 kHz without its silent first kIrTrim samples (decoded once per
     *  process and shared by every instance). Empty without a built-in reverb. */
    static const juce::AudioBuffer<float>& reverbResponse();
    /** The reverb convolution's block size (= its latency) at a sample rate: the largest power of two (64 .. 1024)
     *  that fits in the shortest pre-delay sync can ask for, so the pre-delay line can always absorb it. */
    static int reverbBlockFor(double sampleRate);
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
    double getTailLengthSeconds() const override;      // echoes and reverb ring on after the vocal stops
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
    /** The effects' own settings as they are now (for the editor's pictures of them). */
    PeepChain::Settings effectSettings() const { PeepChain::Settings s; readEffects(s); return s; }
    EffectParams::Sends sendSettings() const { PeepChain::Settings s; EffectParams::Sends d; readEffects(s, &d); return d; }
    /** True when one effect's own settings (EffectParams, stage or EffectParams::kSends) are all Peep's. */
    bool isStageAtPeep(int stage) const;
    /** Puts one effect's own settings back to Peep's (one undo step). */
    void resetStageToPeep(int stage);
    /** True when everything that shapes the sound is exactly as in Peep's preset (levels aside). */
    bool isPeepPreset() const;
    /** Back to Peep's preset: his order, all six effects on, his two thresholds and send amounts, both syncs off.
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

    // Groups (GroupSync.h): none to begin with. Creating one starts it with this instance's sound; joining one
    // takes on its sound. Saved with the session. Message thread, but for getGroupId / getGroupName.
    juce::String getGroupId() const { return groupHub->groupOf(*this); }      // empty: in no group
    juce::String getGroupName() const { return groupHub->nameOf(getGroupId()); }
    void createGroup(const juce::String& name = {});           // empty: GroupHub::suggestName()
    void joinGroup(const juce::String& id);
    void leaveGroup();
    void renameGroup(const juce::String& id, const juce::String& name) { groupHub->rename(id, name); }
    void deleteGroup(const juce::String& id) { groupHub->dissolve(id); }  // its tracks keep their sound
    /** Parameters that stay with each track rather than being shared with its group. */
    static bool isPerTrack(const juce::String& paramId) { return paramId == "input" || paramId == "output" || paramId == "autoGain"; }
    void updateTrackProperties(const TrackProperties&) override;
    GroupHub& getGroupHub() { return *groupHub; }

    /** The echo's current delay time, ms (500 unless synced to the host's tempo). */
    float getDelayMs() const { return delayMs.load(); }
    /** The reverb's current pre-delay, ms (Peep's 58 unless synced to the host's tempo). */
    float getReverbPreDelayMs() const { return reverbPreDelayMs.load(); }
    static constexpr double kPeepBpm = 120.0;          // the tempo both syncs scale from (GarageBand's default)

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
        // For the Compressor and Limiter pictures: the highest value since the editor last took it (take()).
        std::atomic<float> compLevelDb{-120.f}, compGrDb{0.f};          // detector level (RMS dBFS), gain reduction
        std::atomic<float> limInDb{-120.f}, limOutDb{-120.f}, limGrDb{0.f};   // peaks in (after pre-gain) / out, gain reduction
        std::atomic<float> gateInDb{-120.f}, gateGrDb{0.f};                 // peak going into the gate, how far it is closed
        std::atomic<float> bandLevelDb[4];                                  // multiband detector levels (the thresholds' scale)
        static void hold(std::atomic<float>& a, float v) { if (v > a.load(std::memory_order_relaxed)) a.store(v, std::memory_order_relaxed); }
        static float take(std::atomic<float>& a, float rest) { return a.exchange(rest, std::memory_order_relaxed); }
        Meters() { for (auto& b : bandDb) b = 0.f; for (auto& b : bandLevelDb) b = -120.f; }
    } meters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    static uint32_t packOrder(const Order&);
    static Order unpackOrder(uint32_t);
    PeepChain::Settings currentSettings() const;
    void applyControls();
    // Gains are collected as their lowest linear value (no log per sample) and turned into dB reductions once a block.
    struct Peaks {
        double in = 0, out = 0, gateMin = 1, compMin = 1, limMin = 1, bandMin[4] = {1, 1, 1, 1}, bandMs[4] = {0, 0, 0, 0};
        static double reductionDb(double gainMin) { return gainMin < 1.0 ? -20.0 * std::log10(gainMin) : 0.0; }
    };
    void processChunk(float* l, float* r, int numIn, int numOut, int n, Peaks&);

    PeepChain chain;
    std::atomic<float>* inputDb = nullptr; std::atomic<float>* outputDb = nullptr;
    std::atomic<float>* gateThreshold = nullptr; std::atomic<float>* compThreshold = nullptr;
    std::atomic<float>* delayLevel = nullptr; std::atomic<float>* reverbLevel = nullptr;
    std::atomic<float>* autoGainOn = nullptr;
    std::vector<std::atomic<float>*> effectValues;      // EffectParams::all(), in order
    void readEffects(PeepChain::Settings&, EffectParams::Sends* = nullptr) const;
    EffectParams::Sends sends;                         // as last applied (audio thread)
    void applySends(const EffectParams::Sends&, bool force);
    AutoGain autoGain;
    juce::AudioBuffer<double> inPowerBuf;              // K-weighted input power per sample, for Auto Gain
    static float sendGain(float knobDb, float unityAtDb);
    static constexpr double kMaxDelaySeconds = 4.0;    // a half note at 30 bpm
    double songBpm();                                  // the host's tempo, 0 when it doesn't say (audio thread)
    int delaySamplesNow(double bpm);                   // from the host's tempo when synced
    std::atomic<float>* delaySync = nullptr;
    std::atomic<float> delayMs{500.0f};
    // The reverb's impulse response starts kIrOnset samples (58 ms) in: that is its pre-delay. The first
    // kIrTrim samples (all below -150 dB of its peak) are cut off when it's loaded and a delay line of the same
    // length plays them instead, so that gap can follow the tempo. 2499 = 17 x 147, which is also a whole number
    // of samples at 48 kHz (2720) and their multiples, so where JUCE resamples the response nothing shifts.
    static constexpr int kIrOnset = 2573, kIrTrim = 2499;
    static constexpr double kIrRate = 44100.0;
    static constexpr double kMaxPreDelaySeconds = 1.0;  // the longest pre-delay (250 ms) at 30 bpm
    int preDelaySamplesNow(double bpm);
    std::atomic<float>* reverbSync = nullptr;
    std::atomic<float> reverbPreDelayMs{(float)(1000.0 * kIrOnset / kIrRate)};
    PreDelay preDelay;
    double sampleRateNow = 44100.0;
    SendDelay delay;
    ReverbConvolver reverb;                            // runs reverbLatency samples late; the pre-delay line is that much shorter
    int reverbLatency = 0;
    juce::CriticalSection reverbLock;                  // prepareToPlay vs the timer reshaping the reverb
    ReverbConvolver::Shape reverbShape() const;        // from the Reverb settings
    void timerCallback() override;                     // reshapes the reverb when its settings change (message thread)
    juce::SmoothedValue<float> reverbWidth;
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
    void parameterValueChanged(int, float) override { markGroupChanged(); }
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

    // groups
    juce::ValueTree groupSound() const override;
    void applyGroupSound(const juce::ValueTree&) override;
    juce::String groupMemberName() const override { const juce::ScopedLock l(trackLock); return trackName; }
    juce::SharedResourcePointer<GroupHub> groupHub;
    void groupDetailsChanged() override { updateHostDisplay(ChangeDetails().withNonParameterStateChanged(true)); }
    void changeGroup(const std::function<void()>&);
    juce::CriticalSection pendingLock;                  // a loaded session's group, joined on the message thread
    bool groupPending = false;
    juce::String pendingGroupId, pendingGroupName;
    juce::CriticalSection trackLock;
    juce::String trackName;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PeepChainProcessor)
};
