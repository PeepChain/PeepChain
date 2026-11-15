#pragma once
#include "PluginProcessor.h"
#include "UpdateChecker.h"

struct EmbeddedFonts;                                    // the built-in fonts (PluginEditor.cpp)

/** The knobs: round white tablets with a heart for a pointer and a neon ring for the value. */
class PeepLookAndFeel : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
};

/**
 * The chain is drawn as one long scored pill bar, one segment per effect. Click a segment to switch that
 * effect on or off (an off segment is missing from the bar); drag it sideways to change the order.
 * Under it: six tablet knobs (input, gate threshold, compressor threshold, delay, reverb, output) and the level meters.
 */
class PeepChainEditor : public juce::AudioProcessorEditor, public juce::TooltipClient, private juce::Timer {
public:
    explicit PeepChainEditor(PeepChainProcessor&);
    ~PeepChainEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    /** Pull the latest meter values from the processor (also called by the timer). */
    void updateMeters();

    /** Where things are (also used by the tests). */
    juce::Rectangle<float> slotBounds(int slot) const;
    juce::Rectangle<float> resetBounds() const;
    juce::Rectangle<float> autoGainBounds() const;               // the Auto Gain switch, left of the reset capsule
    juce::Rectangle<float> updateBounds() const;                 // the "new version" notice under the title
    juce::String getTooltip() override;                          // for the drawn (non-component) controls
    /** Tests only: hold a segment mid-drag (as if the mouse were still down) so it can be photographed. */
    void previewDrag(int fromSlot, float dx);

    struct Palette {
        static juce::Colour bg()      { return juce::Colour(0xff0a0709); }
        static juce::Colour well()    { return juce::Colour(0xff0d0a0f); }   // the dark windows cut into the pill
        static juce::Colour pink()    { return juce::Colour(0xffff4fa0); }
        static juce::Colour hot()     { return juce::Colour(0xffff8cc4); }
        static juce::Colour pale()    { return juce::Colour(0xffffc4de); }
        static juce::Colour dim()     { return juce::Colour(0xff5c2440); }
        static juce::Colour grey()    { return juce::Colour(0xff8d8791); }
        static juce::Colour pill()    { return juce::Colour(0xfff7f3ec); }   // pill white
        static juce::Colour pillLow() { return juce::Colour(0xffcbc4b6); }   // its shaded side
        static juce::Colour stamp()   { return juce::Colour(0xff8a8274); }   // debossed lettering
    };
    static juce::Font titleFont(float h);
    static juce::Font labelFont(float h);                        // with the lettering's extra letter spacing
    static juce::Font plainLabelFont(float h);
    static juce::Path heart(juce::Rectangle<float>);

    // Where Peep's own raw takes peaked in the loud phrases, for the input guide.
    static constexpr float kPeepPeakLowDb = -24.0f, kPeepPeakHighDb = -6.0f;

private:
    static constexpr int kStages = PeepChainProcessor::kStages;
    void timerCallback() override;
    void renderBackdrop();                                        // everything that never changes, drawn once
    void renderChainLayer();                                      // the bar: redrawn only when something is switched or moved
    juce::Rectangle<float> chainStrip() const;
    juce::uint64 stateKey() const;
    void paintChain(juce::Graphics&);
    void paintSegment(juce::Graphics&, juce::Rectangle<float>, int stage, int slot, bool roundLeft, bool roundRight, bool lifted);
    void paintStageDisplay(juce::Graphics&, juce::Rectangle<float>, int stage, bool on);
    void paintLevelMeter(juce::Graphics&, juce::Rectangle<float>, const juce::String& name, float db, bool inputGuide);
    void paintResetCapsule(juce::Graphics&);
    void paintAutoGainCapsule(juce::Graphics&);
    void paintVersion(juce::Graphics&);

    int slotAt(juce::Point<float>) const;                       // -1 when not on a segment
    PeepChainProcessor::Order displayOrder() const;             // the saved order, or the preview while dragging
    void toggleStage(int stage);

    PeepChainProcessor& proc;
    juce::SharedResourcePointer<EmbeddedFonts> fonts;           // keeps the built-in fonts loaded while an editor is open
    juce::SharedResourcePointer<UpdateChecker> updates;         // one background check per process, shared by all editors
    PeepLookAndFeel lnf;
    juce::Slider input, gateThresh, compThresh, delayLevel, reverbLevel, output;
    juce::AudioProcessorValueTreeState::SliderAttachment inputAtt, gateAtt, compAtt, delayAtt, reverbAtt, outputAtt;
    juce::TooltipWindow tooltips{this, 900};
    juce::Image backdrop, chainLayer, liftedLayer;
    juce::uint64 chainKey = 0, lastPaintedKey = 0;

    // segment drag state
    int pressedSlot = -1, dragToSlot = -1;
    bool dragging = false, pressedReset = false, overReset = false;
    bool pressedAutoGain = false, overAutoGain = false, pressedUpdate = false, overUpdate = false;
    juce::Point<float> lastMouse;
    float dragDx = 0;

    // smoothed meter state
    juce::uint32 lastBlocks = 0; int idleTicks = 1000;                 // audio-stopped detection for the meters
    float autoGainShownDb = 0, autoGainPaintedDb = 0;
    float gateOpen = 0, compDb = 0, limDb = 0, bandDb[4] = {0, 0, 0, 0}, inDb = -100, outDb = -100;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PeepChainEditor)
};
