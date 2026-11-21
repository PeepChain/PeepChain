#pragma once
#include "PluginProcessor.h"
#include "UpdateChecker.h"

struct EmbeddedFonts;                                    // the built-in fonts (PluginEditor.cpp)

/** Flat dark knobs with a pink arc from Peep's value to the current one and a white pointer. A slider with the
 *  property "mini" is drawn smaller (the edit panels); a vertical "mini" slider is a fader. */
class PeepLookAndFeel : public juce::LookAndFeel_V4 {
public:
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float startAngle, float endAngle, juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
};

/**
 * One effect's own settings (EffectParams), shown in place of the six big knobs while it is open, with a button
 * that puts them back to Peep's and an X that closes the panel (Escape too). Also the Delay & Reverb panel
 * (stage EffectParams::kSends). Most have a live picture next to their knobs:
 *  - Gate: the level going in and coming out over the last seconds, with its open and close thresholds, which
 *    can be dragged.
 *  - Compressor: its curve with the level on it now, and its gain reduction over the last seconds.
 *  - Multiband: the four bands across the frequency range, each with its level, threshold, headroom and gain
 *    reduction; the crossovers and thresholds can be dragged.
 *  - Limiter: what goes in, what comes out and how much it takes off.
 *  - Delay & Reverb: the echoes' timing and levels, and the reverb's pre-delay and decay.
 */
class StagePanel : public juce::Component, public juce::TooltipClient, private juce::Timer {
public:
    StagePanel(PeepChainProcessor&, int stage);
    ~StagePanel() override;
    int getStage() const { return stage; }
    std::function<void()> onClose;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    juce::String getTooltip() override;
    /** The drawn buttons, and the switches in the group titles (also used by the tests). */
    enum Button { kPeep, kClose, kNumButtons };
    juce::Rectangle<float> buttonBounds(int) const;
    juce::Rectangle<float> choiceBounds(const juce::String& paramId) const;
    juce::Slider* sliderFor(const juce::String& paramId);
    /** Where a draggable line in the picture is (tests): its parameter's id, or empty. */
    juce::String dragTargetAt(juce::Point<float>) const;
    juce::Rectangle<float> getPictureBounds() const { return picture; }
private:
    struct Item { int def; std::unique_ptr<juce::Slider> slider; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; juce::Rectangle<float> area; bool labelled = true; };
    struct Group { juce::String title; std::vector<int> items; int choiceDef = -1; juce::Rectangle<float> area, pic, choiceArea; };
    void timerCallback() override;
    int hitTest(juce::Point<float>) const;                      // a button, 100 + choice def, or -1
    void activate(int);
    float value(int def) const;
    float valueOf(const char* id) const;
    juce::uint32 stateKey() const;
    void updateVisibility();
    int itemIndex(const juce::String& id) const;
    void placeKnob(int item, juce::Rectangle<float> cell, bool labelled);
    void layoutRow(juce::Rectangle<float> body);
    void layoutGrid(juce::Rectangle<float> body, const std::vector<std::vector<const char*>>& rows, float colW);
    void layoutMultiband(juce::Rectangle<float> body);
    void layoutSends(juce::Rectangle<float> body);
    bool hasLivePicture() const;
    void paintKnobTexts(juce::Graphics&);
    void paintGatePicture(juce::Graphics&);
    void paintCompPicture(juce::Graphics&);
    void paintLimitPicture(juce::Graphics&);
    void paintMultiPicture(juce::Graphics&);
    void paintSendsPictures(juce::Graphics&);
    // the pictures' draggable lines: what is under a point, and dragging it
    struct Target { int def = -1; bool vertical = false; };   // vertical: a line dragged up / down
    Target targetAt(juce::Point<float>) const;
    double targetValueAt(const Target&, juce::Point<float>) const;
    float multiX(double hz) const;
    double multiHz(float x) const;
    float levelY(juce::Rectangle<float> plot, float db) const;
    juce::Rectangle<float> multiPlot() const;
    juce::Rectangle<float> gatePlot() const;

    PeepChainProcessor& proc;
    const int stage;
    juce::Rectangle<float> picture;                             // where the live picture goes
    static constexpr int kHistory = 240;                        // 8 s at 30 frames a second
    std::array<float, kHistory> histIn{}, histOut{}, histGr{};  // level in, level out, gain reduction (dB), oldest first from histPos
    int histPos = 0;
    juce::uint32 lastBlocks = 0;
    float levelNow = -120.0f;
    float bandLevel[4] = {-120, -120, -120, -120}, bandGr[4] = {0, 0, 0, 0};
    float shownDelayMs = 0, shownPreDelayMs = 0;
    std::vector<Item> items;
    std::vector<Group> groups;
    int pressed = -1, over = -1;
    Target dragging, hovering;
    juce::uint32 lastKey = 0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StagePanel)
};

/**
 * A newer release is out: its version, its release notes (from GitHub), DOWNLOAD (its page) and LATER (shown
 * again next session; the header's notice reopens it meanwhile). Covers the whole window; clicking outside the
 * card or Escape is LATER.
 */
class UpdatePopup : public juce::Component {
public:
    explicit UpdatePopup(const UpdateChecker::Release&);
    std::function<void()> onDownload, onLater;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    enum Button { kDownload, kLater, kNumButtons };
    juce::Rectangle<float> buttonBounds(int) const;
    juce::Rectangle<float> cardBounds() const;
    juce::String getNotesText() const { return notes.getText(); }
private:
    int buttonAt(juce::Point<float>) const;
    void press(int);
    UpdateChecker::Release release;
    juce::TextEditor notes;
    int pressed = -1, over = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdatePopup)
};

/**
 * Renames a group (from the pencil in the GROUP menu). Covers the whole window; Enter confirms, Escape or
 * clicking outside the card cancels.
 */
class GroupNameDialog : public juce::Component {
public:
    explicit GroupNameDialog(const juce::String& name);
    std::function<void(const juce::String&)> onConfirm;
    std::function<void()> onCancel;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    void visibilityChanged() override;
    enum Button { kConfirm, kCancel, kNumButtons };
    juce::Rectangle<float> buttonBounds(int) const;
    juce::Rectangle<float> cardBounds() const;
    juce::TextEditor& getField() { return field; }
    void confirm();
private:
    int buttonAt(juce::Point<float>) const;
    juce::TextEditor field;
    int pressed = -1, over = -1;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroupNameDialog)
};

/**
 * A group in the GROUP menu: its name (ticked when this track is in it). Clicking it joins the
 * group; while it's under the mouse it shows a pencil (rename) and a trash can (delete) on the right.
 */
class GroupMenuItem : public juce::PopupMenu::CustomComponent {
public:
    enum Action { kJoin, kRename, kDelete };
    GroupMenuItem(const GroupHub::Info&, bool current, std::function<void(Action)> onAction);
    void getIdealSize(int& w, int& h) override;
    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    juce::Rectangle<float> iconBounds(Action) const;            // kRename: the pencil, kDelete: the trash can
    /** Tests: draw it as if hovered, with this icon under the mouse (-1: neither). */
    void previewHover(int icon) { forcedHover = true; overIcon = icon; repaint(); }
private:
    int iconAt(juce::Point<float>) const;
    GroupHub::Info info;
    bool current, forcedHover = false;
    int overIcon = -1;
    std::function<void(Action)> onAction;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroupMenuItem)
};

/**
 * Everything in the window, drawn at 860 x 562; the editor below scales it as a whole.
 *  - Header: logo, version (and a newer one's notice, which opens its release notes), undo / redo, A / B, size.
 *  - GROUP (left of Auto Gain): this PeepChain's group, if any (GroupSync.h). Its menu lists the groups (join one;
 *    rename or delete one from its pencil and trash can), creates one ("Group <n>", with this track's settings) and
 *    leaves this one.
 *  - Signal chain: one card per effect, in processing order. Its power button switches it on or off; clicking
 *    the card (or its EDIT, or right-clicking it) opens that effect's settings; dragging it sideways reorders.
 *  - Controls: six knobs (input, gate threshold, compressor threshold, delay, reverb, output), with SYNC and EDIT
 *    (the Delay & Reverb settings) under the two sends, or the open settings' StagePanel in their place (Esc or
 *    its X closes it).
 *  - Input and output meters.
 */
class PeepChainView : public juce::Component, public juce::TooltipClient, private juce::Timer {
public:
    static constexpr int kWidth = 860, kHeight = 562;
    explicit PeepChainView(PeepChainProcessor&);
    ~PeepChainView() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    /** Pull the latest meter values from the processor (also called by the timer). */
    void updateMeters();

    /** Where things are (also used by the tests). */
    juce::Rectangle<float> slotBounds(int slot) const;
    juce::Rectangle<float> resetBounds() const;
    juce::Rectangle<float> autoGainBounds() const;               // the Auto Gain switch, left of the reset capsule
    juce::Rectangle<float> githubBounds() const;                 // the GitHub button right of the version (empty when no repo is set)
    juce::Rectangle<float> updateBounds() const;                 // the "new version" notice under the title
    juce::Rectangle<float> groupBounds() const;                  // GROUP, left of Auto Gain
    /** Shows the newer release's notes (does nothing unless the update check found one). */
    void showUpdatePopup();
    UpdatePopup* getUpdatePopup() { return updatePopup.get(); }
    /** Asks for a new name for a group. */
    void showRenameDialog(const juce::String& groupId);
    GroupNameDialog* getGroupNameDialog() { return nameDialog.get(); }
    /** Called when GROUP is clicked (shows the group menu; tests may replace it). */
    std::function<void()> onGroupClicked;
    juce::Rectangle<float> editBounds(int slot) const;           // EDIT at the bottom of a card (empty when that effect has no settings)
    juce::Rectangle<float> powerBounds(int slot) const;          // a card's on / off button
    /** Opens an effect's settings (or EffectParams::kSends: Delay & Reverb) in place of the big knobs (-1 closes them). */
    void openEditor(int stage);
    int getEditingStage() const { return panel != nullptr ? panel->getStage() : -1; }
    StagePanel* getPanel() { return panel.get(); }
    juce::String getTooltip() override;                          // for the drawn (non-component) controls
    /** The small drawn buttons: the header toolbar, and SYNC and EDIT under the Delay and Reverb knobs. */
    enum Control { kUndo, kRedo, kSlotA, kSlotB, kCopy, kSize, kDelaySync, kReverbSync, kDelayEdit, kReverbEdit, kNumControls };
    juce::Rectangle<float> controlBounds(int control) const;
    /** Called when SIZE is clicked (the editor shows its menu). */
    std::function<void()> onSizeClicked;
    void setScaleShown(float s) { scaleShown = s; repaint(controlBounds(kSize).expanded(2.0f).toNearestInt()); }
    /** Tests only: hold a segment mid-drag (as if the mouse were still down) so it can be photographed. */
    void previewDrag(int fromSlot, float dx);

    struct Palette {
        static juce::Colour bg()      { return juce::Colour(0xff121015); }   // window
        static juce::Colour header()  { return juce::Colour(0xff19161d); }   // header bar
        static juce::Colour card()    { return juce::Colour(0xff1d1a22); }   // cards and panels
        static juce::Colour edge()    { return juce::Colour(0xff2c2733); }   // their 1 px border
        static juce::Colour well()    { return juce::Colour(0xff0e0c11); }   // displays sunk into a card
        static juce::Colour track()   { return juce::Colour(0xff2e2935); }   // knob / meter tracks
        static juce::Colour pink()    { return juce::Colour(0xffff4fa0); }   // the accent
        static juce::Colour hot()     { return juce::Colour(0xffff7cb8); }   // accent, hovered
        static juce::Colour pale()    { return juce::Colour(0xffeee9f2); }   // main text
        static juce::Colour dim()     { return juce::Colour(0xff4e2a40); }   // accent, inactive
        static juce::Colour grey()    { return juce::Colour(0xffaea8b8); }   // secondary text
    };
    static juce::Font titleFont(float h);                        // bold, for the name
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
    bool keyPressed(const juce::KeyPress&) override;
    // the Filter and EQ cards' pictures, recomputed only when their settings change
    juce::uint64 shapeKey = ~0ull; float filterDb[49] = {}; double geqGains[31] = {};
    bool updateCardShapes();                                     // true when they changed
    void paintLevelMeter(juce::Graphics&, juce::Rectangle<float>, const juce::String& name, float db, bool inputGuide);
    void paintResetCapsule(juce::Graphics&);
    void paintAutoGainCapsule(juce::Graphics&);
    void paintVersion(juce::Graphics&);
    void paintGroupButton(juce::Graphics&);
    void showGroupMenu();
    void closeUpdatePopup();
    void closeGroupNameDialog();
    void paintControls(juce::Graphics&);
    void paintEditMarks(juce::Graphics&);
    void paintMetersAndFooter(juce::Graphics&);
    int controlAt(juce::Point<float>) const;                     // -1 when not on an enabled one
    bool isControlEnabled(int) const;
    void activate(int control);
    juce::uint32 controlsKey() const;

    int slotAt(juce::Point<float>) const;                       // -1 when not on a segment
    PeepChainProcessor::Order displayOrder() const;             // the saved order, or the preview while dragging
    void toggleStage(int stage);
    static bool hasSettings(int stage);

    PeepChainProcessor& proc;
    juce::SharedResourcePointer<EmbeddedFonts> fonts;           // keeps the built-in fonts loaded while an editor is open
    juce::SharedResourcePointer<UpdateChecker> updates;         // one background check per process, shared by all editors
    PeepLookAndFeel lnf;
    juce::Slider input, gateThresh, compThresh, delayLevel, reverbLevel, output;
    juce::AudioProcessorValueTreeState::SliderAttachment inputAtt, gateAtt, compAtt, delayAtt, reverbAtt, outputAtt;
    std::unique_ptr<StagePanel> panel;
    std::unique_ptr<UpdatePopup> updatePopup;
    std::unique_ptr<GroupNameDialog> nameDialog;
    bool updatePopupConsidered = false;                          // the automatic showing was decided on
    juce::String groupShown;                                     // what GROUP says, as last painted
    juce::TooltipWindow tooltips{this, 900};
    juce::Image backdrop, chainLayer, liftedLayer;
    juce::uint64 chainKey = 0, lastPaintedKey = 0;
    juce::uint32 lastControlsKey = 0;
    float imageScale = 2.0f;                                     // pixels per point of the cached pictures
    float scaleShown = 1.0f;

    // segment drag state
    int pressedSlot = -1, dragToSlot = -1;
    bool dragging = false, pressedReset = false, overReset = false;
    bool pressedAutoGain = false, overAutoGain = false, pressedUpdate = false, overUpdate = false;
    bool pressedGithub = false, overGithub = false;
    bool pressedGroup = false, overGroup = false;
    int pressedControl = -1, overControl = -1;
    int pressedEdit = -1, overEdit = -1;                         // slot whose EDIT is pressed / under the mouse
    int overSlot = -1;                                           // card under the mouse
    bool pressedPower = false;                                   // the press began on a card's on / off button
    juce::Point<float> lastMouse;
    float dragDx = 0;

    // smoothed meter state
    juce::uint32 lastBlocks = 0; int idleTicks = 1000;                 // audio-stopped detection for the meters
    float autoGainShownDb = 0, autoGainPaintedDb = 0;
    float gateOpen = 0, compDb = 0, limDb = 0, bandDb[4] = {0, 0, 0, 0}, inDb = -100, outDb = -100;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PeepChainView)
};

/** The plugin window: the view, scaled to the chosen size (75 to 200 %, remembered for new windows). */
class PeepChainEditor : public juce::AudioProcessorEditor {
public:
    explicit PeepChainEditor(PeepChainProcessor&);
    PeepChainView& getView() { return view; }
    float getUiScale() const { return uiScale; }
    void setUiScale(float, bool remember = true);           // remember: also for windows opened later
private:
    void showSizeMenu();
    PeepChainView view;
    float uiScale = 1.0f;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PeepChainEditor)
};
