#include "PluginEditor.h"
#include "BinaryData.h"

using Pal = PeepChainView::Palette;
using RectF = juce::Rectangle<float>;
using PointF = juce::Point<float>;
static constexpr float kPi = juce::MathConstants<float>::pi;

namespace {
const float kW = (float)PeepChainView::kWidth, kH = (float)PeepChainView::kHeight;
const float kPad = 20.0f;                                         // window margin
const float kHeaderH = 52.0f;
const float kChainRowY = 62.0f;                                   // "SIGNAL CHAIN", Auto Gain, Reset
const float kCardY = 92.0f, kCardH = 160.0f, kCardGap = 14.0f;    // the six effect cards
const RectF kControlsArea(kPad, 266.0f, kW - 2.0f * kPad, 206.0f); // the six knobs, or an effect's settings
const RectF kPanelArea = kControlsArea;
const float kKnobSize = 84.0f;
const int kKnobs = 6;
const float kMeterY = 498.0f;                                     // the IN / OUT bars
const float kLayerMargin = 24.0f;                                 // room round the cards for their shadows
const char* const kStageNames[6] = {"GATE", "COMP", "FILTER", "EQ", "MULTI", "LIMIT"};
}

static juce::PropertiesFile::Options settingsFileOptions();        // the editor's own settings file (window size)

// The built-in font (Inter, SIL Open Font License, assets/fonts/), so the window looks the same on every system.
struct EmbeddedFonts {
    juce::Typeface::Ptr bold = juce::Typeface::createSystemTypefaceFor(BinaryData::InterBold_ttf, (size_t)BinaryData::InterBold_ttfSize);
    juce::Typeface::Ptr label = juce::Typeface::createSystemTypefaceFor(BinaryData::InterSemiBold_ttf, (size_t)BinaryData::InterSemiBold_ttfSize);
};
static constexpr float kFontScale = 0.86f;                          // the sizes in this file were set for a narrower font
juce::Font PeepChainView::titleFont(float h) {                       // Inter Bold
    juce::SharedResourcePointer<EmbeddedFonts> f;
    return juce::Font(juce::FontOptions(f->bold).withHeight(h * kFontScale));
}
juce::Font PeepChainView::plainLabelFont(float h) {                  // Inter SemiBold
    juce::SharedResourcePointer<EmbeddedFonts> f;
    return juce::Font(juce::FontOptions(f->label).withHeight(h * kFontScale));
}
juce::Font PeepChainView::labelFont(float h) { return plainLabelFont(h).withExtraKerningFactor(0.03f); }

juce::Path PeepChainView::heart(RectF r) {
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    juce::Path p;
    p.startNewSubPath(x + w * 0.5f, y + h * 0.26f);
    p.cubicTo(x + w * 0.42f, y - h * 0.02f, x, y + h * 0.02f, x, y + h * 0.34f);
    p.cubicTo(x, y + h * 0.62f, x + w * 0.34f, y + h * 0.80f, x + w * 0.5f, y + h);
    p.cubicTo(x + w * 0.66f, y + h * 0.80f, x + w, y + h * 0.62f, x + w, y + h * 0.34f);
    p.cubicTo(x + w, y + h * 0.02f, x + w * 0.58f, y - h * 0.02f, x + w * 0.5f, y + h * 0.26f);
    p.closeSubPath();
    return p;
}

//==============================================================================
// drawing helpers
namespace {
juce::Font labelFont(float h) { return PeepChainView::labelFont(h); }
juce::Font plainLabelFont(float h) { return PeepChainView::plainLabelFont(h); }

/** A card: flat fill with a faint top-light and a 1 px edge. */
void card(juce::Graphics& g, RectF r, float radius = 8.0f, juce::Colour edge = Pal::edge()) {
    g.setGradientFill(juce::ColourGradient(Pal::card().brighter(0.06f), 0.0f, r.getY(), Pal::card().darker(0.12f), 0.0f, r.getBottom(), false));
    g.fillRoundedRectangle(r, radius);
    g.setColour(edge); g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.0f);
}

/** A small button: outlined, or filled with the accent when lit. */
void button(juce::Graphics& g, RectF r, const juce::String& text, bool lit, bool hover, bool enabled = true, float fontH = 12.0f) {
    const float rad = 5.0f;
    if (lit) {
        g.setColour(hover ? Pal::hot() : Pal::pink()); g.fillRoundedRectangle(r, rad);
        g.setColour(juce::Colours::white);
    } else {
        g.setColour(Pal::bg().withAlpha(0.6f)); g.fillRoundedRectangle(r, rad);
        g.setColour(!enabled ? Pal::edge() : hover ? Pal::hot() : Pal::edge().brighter(0.35f)); g.drawRoundedRectangle(r.reduced(0.5f), rad, 1.0f);
        g.setColour(!enabled ? Pal::grey().withAlpha(0.6f) : hover ? Pal::hot() : Pal::pale());
    }
    g.setFont(labelFont(fontH)); g.drawText(text, r.translated(0.0f, 0.5f), juce::Justification::centred, false);
}

/** The power symbol: a broken ring and a bar. */
void powerIcon(juce::Graphics& g, RectF r, bool on, bool hover) {
    const auto c = r.getCentre(); const float rad = r.getWidth() * 0.5f;
    if (on) {
        juce::Path disc; disc.addEllipse(r);
        juce::DropShadow(Pal::pink().withAlpha(0.7f), 8, {}).drawForPath(g, disc);
        g.setColour(hover ? Pal::hot() : Pal::pink()); g.fillPath(disc);
    } else {
        g.setColour(hover ? Pal::hot() : Pal::grey().withAlpha(0.7f)); g.drawEllipse(r.reduced(0.5f), 1.0f);
    }
    const auto ink = on ? juce::Colours::white : hover ? Pal::hot() : Pal::grey();
    juce::Path ring; ring.addCentredArc(c.x, c.y + 0.5f, rad * 0.48f, rad * 0.48f, 0.0f, 0.75f, 2.0f * kPi - 0.75f, true);
    g.setColour(ink); g.strokePath(ring, juce::PathStrokeType(1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.drawLine(c.x, c.y - rad * 0.62f, c.x, c.y - rad * 0.05f, 1.4f);
}

/** Undo (or, mirrored, redo): a curved arrow. */
void undoIcon(juce::Graphics& g, RectF r, bool redo, juce::Colour c) {
    const auto ctr = r.getCentre();
    juce::Path p; p.addCentredArc(0.0f, 1.0f, 6.0f, 6.0f, 0.0f, -2.3f, 1.3f, true);
    const PointF tip(6.0f * std::sin(-2.3f), 1.0f - 6.0f * std::cos(-2.3f));
    juce::Path head; head.addTriangle(tip.x - 3.6f, tip.y - 2.4f, tip.x + 2.6f, tip.y - 3.4f, tip.x - 0.4f, tip.y + 2.8f);
    auto t = juce::AffineTransform::scale(redo ? -1.0f : 1.0f, 1.0f).translated(ctr.x, ctr.y);
    g.setColour(c);
    g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), t);
    g.fillPath(head, t);
}

/** A toggle switch: a track and a sliding knob. */
void toggleSwitch(juce::Graphics& g, RectF r, bool on, bool hover) {
    const float rad = r.getHeight() * 0.5f;
    g.setColour(on ? (hover ? Pal::hot() : Pal::pink()) : (hover ? Pal::track().brighter(0.3f) : Pal::track()));
    g.fillRoundedRectangle(r, rad);
    const auto knob = RectF(r.getHeight() - 4.0f, r.getHeight() - 4.0f).withCentre({on ? r.getRight() - rad : r.getX() + rad, r.getCentreY()});
    g.setColour(on ? juce::Colours::white : Pal::grey()); g.fillEllipse(knob);
}

void brokenHeart(juce::Graphics& g, RectF r, juce::Colour fill, juce::Colour crack) {
    g.setColour(fill); g.fillPath(PeepChainView::heart(r));
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    juce::Path c;
    c.startNewSubPath(x + w * 0.50f, y + h * 0.22f);
    c.lineTo(x + w * 0.40f, y + h * 0.42f); c.lineTo(x + w * 0.58f, y + h * 0.54f);
    c.lineTo(x + w * 0.44f, y + h * 0.72f); c.lineTo(x + w * 0.52f, y + h * 0.98f);
    g.setColour(crack); g.strokePath(c, juce::PathStrokeType(juce::jmax(1.5f, w * 0.07f), juce::PathStrokeType::mitered));
}

juce::String knobValueText(juce::Slider& s) {
    const double v = s.getValue();
    if (s.getProperties()["offAtMinimum"] && v <= s.getMinimum() + 0.05) return "OFF";
    return (v > 0.05 ? "+" : "") + juce::String(v, 1) + " dB";
}
}

//==============================================================================
void PeepLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) {
    const bool mini = s.getProperties()["mini"];
    auto r = RectF((float)x, (float)y, (float)w, (float)h).reduced(2.0f);
    const float d = juce::jmin(r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre(d, d);
    const auto c = r.getCentre(); const float rad = d * 0.5f;
    const float ang = a0 + pos * (a1 - a0);
    const bool hot = s.isMouseOverOrDragging() && s.isEnabled();
    const auto stroke = [](float t) { return juce::PathStrokeType(t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };
    const float ringW = mini ? 3.0f : 4.0f, ringR = rad - ringW * 0.5f - 1.0f;

    // ring: a dim track, and the accent from Peep's value (the double-click value) to this one
    juce::Path track; track.addCentredArc(c.x, c.y, ringR, ringR, 0.0f, a0, a1, true);
    g.setColour(Pal::track()); g.strokePath(track, stroke(ringW));
    const float peep = a0 + (float)s.valueToProportionOfLength(s.getDoubleClickReturnValue()) * (a1 - a0);
    if (std::abs(ang - peep) > 0.01f) {
        juce::Path val; val.addCentredArc(c.x, c.y, ringR, ringR, 0.0f, juce::jmin(peep, ang), juce::jmax(peep, ang), true);
        g.setColour(Pal::pink().withAlpha(0.18f)); g.strokePath(val, stroke(ringW + 5.0f));
        g.setColour(hot ? Pal::hot() : Pal::pink()); g.strokePath(val, stroke(ringW));
    }
    {   // a tick where Peep's value is
        const float r0 = ringR - ringW * 0.5f - 1.5f, r1 = ringR + ringW * 0.5f + 1.5f;
        g.setColour(Pal::pale().withAlpha(0.7f));
        g.drawLine(c.x + std::sin(peep) * r0, c.y - std::cos(peep) * r0, c.x + std::sin(peep) * r1, c.y - std::cos(peep) * r1, 1.2f);
    }

    // the body
    const float br = rad - (mini ? 8.0f : 11.0f);
    juce::Path body; body.addEllipse(c.x - br, c.y - br, br * 2, br * 2);
    juce::DropShadow(juce::Colours::black.withAlpha(0.7f), mini ? 6 : 10, {0, mini ? 2 : 4}).drawForPath(g, body);
    const auto top = juce::Colour(0xff3b3542).brighter(hot ? 0.15f : 0.0f), bottom = juce::Colour(0xff1f1b24);
    g.setGradientFill(juce::ColourGradient(top, c.x, c.y - br, bottom, c.x, c.y + br, false)); g.fillPath(body);
    g.setColour(juce::Colours::white.withAlpha(0.07f)); g.drawEllipse(c.x - br + 0.5f, c.y - br + 0.5f, br * 2 - 1.0f, br * 2 - 1.0f, 1.0f);

    // pointer
    const float p0 = br * 0.30f, p1 = br * 0.82f;
    g.setColour(s.isEnabled() ? juce::Colours::white : Pal::grey());
    g.drawLine(c.x + std::sin(ang) * p0, c.y - std::cos(ang) * p0, c.x + std::sin(ang) * p1, c.y - std::cos(ang) * p1, mini ? 2.0f : 2.6f);
}

void PeepLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                                       juce::Slider::SliderStyle style, juce::Slider& s) {
    if (!s.getProperties()["mini"] || style != juce::Slider::LinearVertical) {
        LookAndFeel_V4::drawLinearSlider(g, x, y, w, h, pos, minPos, maxPos, style, s); return;
    }
    // a fader: a slot, the accent from 0 dB to the value, a tick at Peep's value, and a cap
    const auto r = RectF((float)x, (float)y, (float)w, (float)h);
    const float cx = r.getCentreX();
    const float zeroY = r.getY() + r.getHeight() * (1.0f - (float)s.valueToProportionOfLength(0.0));
    const float peepY = r.getY() + r.getHeight() * (1.0f - (float)s.valueToProportionOfLength(s.getDoubleClickReturnValue()));
    g.setColour(Pal::track()); g.fillRoundedRectangle(cx - 1.5f, r.getY(), 3.0f, r.getHeight(), 1.5f);
    g.setColour(Pal::grey().withAlpha(0.5f)); g.fillRect(cx - 5.0f, zeroY - 0.5f, 10.0f, 1.0f);
    g.setColour(Pal::pale().withAlpha(0.7f)); g.fillRect(cx - 4.0f, peepY - 0.6f, 8.0f, 1.2f);
    if (std::abs(pos - zeroY) > 0.5f) {
        g.setColour(Pal::pink()); g.fillRoundedRectangle(cx - 1.5f, juce::jmin(pos, zeroY), 3.0f, std::abs(pos - zeroY), 1.5f);
    }
    const auto cap = RectF(juce::jmin(16.0f, r.getWidth() - 4.0f), 8.0f).withCentre({cx, pos});
    juce::Path p; p.addRoundedRectangle(cap, 2.5f);
    juce::DropShadow(juce::Colours::black.withAlpha(0.7f), 4, {0, 2}).drawForPath(g, p);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffe6e1ea), 0.0f, cap.getY(), juce::Colour(0xffa9a2b0), 0.0f, cap.getBottom(), false));
    g.fillPath(p);
    if (s.isMouseOverOrDragging()) { g.setColour(Pal::pink()); g.drawRoundedRectangle(cap, 2.5f, 1.2f); }
    else { g.setColour(Pal::bg()); g.fillRect(cap.getX() + 3.0f, cap.getCentreY() - 0.5f, cap.getWidth() - 6.0f, 1.0f); }
}

//==============================================================================
PeepChainView::PeepChainView(PeepChainProcessor& p)
    : proc(p),
      inputAtt(p.apvts, "input", input), gateAtt(p.apvts, "gateThresh", gateThresh), compAtt(p.apvts, "compThresh", compThresh),
      delayAtt(p.apvts, "delayLevel", delayLevel), reverbAtt(p.apvts, "reverbLevel", reverbLevel), outputAtt(p.apvts, "output", output) {
    setLookAndFeel(&lnf);
    juce::Slider* knobs[kKnobs] = {&input, &gateThresh, &compThresh, &delayLevel, &reverbLevel, &output};
    for (int i = 0; i < kKnobs; ++i) {
        auto* s = knobs[i];
        s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s->setRotaryParameters(kPi * 1.25f, kPi * 2.75f, true);
        s->setPopupDisplayEnabled(false, false, nullptr);
        s->onValueChange = [this, i] {                       // the number under the knob
            const float colW = kControlsArea.getWidth() / (float)kKnobs;
            repaint(RectF(kControlsArea.getX() + colW * (float)i, kControlsArea.getY() + 124.0f, colW, 24.0f).toNearestInt());
        };
        addAndMakeVisible(*s);                           // double-click returns to the default (set by the attachment)
    }
    input.setTooltip("How hard the vocal hits the chain. Aim the IN meter's peaks at the marked zone.");
    gateThresh.setTooltip("Noise Gate threshold. Anything quieter is cut. Peep's setting: -46 dB.");
    compThresh.setTooltip("Compressor threshold. Lower squashes more. Peep's setting: -26 dB.");
    delayLevel.setTooltip("How much echo is added. Its time, feedback and tone are under EDIT. Peep's send: -16.5. All the way down is off.");
    reverbLevel.setTooltip("How much reverb is added. Its pre-delay, length, tone and width are under EDIT. Peep's send: -6.0. All the way down is off.");
    for (auto* s : {&delayLevel, &reverbLevel}) s->getProperties().set("offAtMinimum", true);
    output.setTooltip("Level after everything");
    lnf.setColour(juce::ScrollBar::thumbColourId, Pal::grey().withAlpha(0.5f));   // the release notes' scroll bar
    lnf.setColour(juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    onGroupClicked = [this] { showGroupMenu(); };
    setSize((int)kW, (int)kH);
    startTimerHz(30);
}
PeepChainView::~PeepChainView() { stopTimer(); setLookAndFeel(nullptr); }

void PeepChainView::resized() {
    juce::Slider* knobs[kKnobs] = {&input, &gateThresh, &compThresh, &delayLevel, &reverbLevel, &output};
    const float colW = kControlsArea.getWidth() / (float)kKnobs;
    for (int i = 0; i < kKnobs; ++i) {
        const float cx = kControlsArea.getX() + colW * ((float)i + 0.5f);
        knobs[i]->setBounds(RectF(kKnobSize, kKnobSize).withCentre({cx, kControlsArea.getY() + 34.0f + kKnobSize * 0.5f}).toNearestInt());
    }
    if (panel != nullptr) panel->setBounds(kPanelArea.toNearestInt());
    if (updatePopup != nullptr) updatePopup->setBounds(getLocalBounds());
    if (nameDialog != nullptr) nameDialog->setBounds(getLocalBounds());
    backdrop = {}; chainLayer = {};
}

void PeepChainView::updateMeters() {
    auto fall = [](float& state, float target, float rate) { state = target > state ? target : juce::jmax(target, state - rate); };
    auto& m = proc.meters;
    // When the host stops sending audio (transport stopped, track frozen) the last values would stay up for
    // good; after ~150 ms without a new block the meters fall back to rest instead.
    const uint32_t blocks = m.blocks.load(std::memory_order_relaxed);
    if (blocks != lastBlocks) { lastBlocks = blocks; idleTicks = 0; } else if (idleTicks < 1000) ++idleTicks;
    const bool live = idleTicks < 5;
    fall(compDb, live ? m.compressorDb.load() : 0.0f, 0.6f);
    fall(limDb, live ? m.limiterDb.load() : 0.0f, 0.6f);
    for (int b = 0; b < 4; ++b) fall(bandDb[b], live ? m.bandDb[b].load() : 0.0f, 1.2f);
    fall(inDb, live ? m.inputPeakDb.load() : -100.0f, 1.5f);
    fall(outDb, live ? m.outputPeakDb.load() : -100.0f, 1.5f);
    gateOpen += ((live ? m.gateOpen.load() : 0.0f) - gateOpen) * 0.5f;
    autoGainShownDb = m.autoGainDb.load();                                     // shown as it is, it changes slowly anyway
    gateThresh.setAlpha(proc.isStageOn(PeepChain::kGate) ? 1.0f : 0.35f);       // a threshold does nothing while its effect is off
    compThresh.setAlpha(proc.isStageOn(PeepChain::kComp) ? 1.0f : 0.35f);
}

//==============================================================================
// where things are
namespace {
struct CardLayout { RectF body, head, power, display, foot; };
CardLayout layoutCard(RectF slot) {
    CardLayout l;
    l.body = slot;
    l.head = slot.withHeight(30.0f);
    l.power = RectF(16.0f, 16.0f).withCentre({slot.getX() + 17.0f, slot.getY() + 15.0f});
    l.display = RectF(slot.getX() + 8.0f, slot.getY() + 30.0f, slot.getWidth() - 16.0f, slot.getHeight() - 30.0f - 28.0f);
    l.foot = RectF(slot.getX(), slot.getBottom() - 28.0f, slot.getWidth(), 28.0f);
    return l;
}
juce::Font wordmarkFont() { return PeepChainView::titleFont(25.0f).withExtraKerningFactor(0.02f); }
float wordmarkRight() {
    return 46.0f + juce::GlyphArrangement::getStringWidth(wordmarkFont(), "PEEPCHAIN");
}
}

RectF PeepChainView::slotBounds(int slot) const {
    const float w = (kW - 2.0f * kPad - 5.0f * kCardGap) / 6.0f;
    return {kPad + (float)slot * (w + kCardGap), kCardY, w, kCardH};
}
RectF PeepChainView::powerBounds(int slot) const {
    if (slot < 0 || slot >= kStages) return {};
    return layoutCard(slotBounds(slot)).power.expanded(4.0f);
}
RectF PeepChainView::editBounds(int slot) const {
    if (slot < 0 || slot >= kStages || !hasSettings(displayOrder()[(size_t)slot])) return {};
    return RectF(64.0f, 18.0f).withCentre(layoutCard(slotBounds(slot)).foot.getCentre().translated(0.0f, -1.0f));
}
RectF PeepChainView::resetBounds() const { return {kW - kPad - 132.0f, kChainRowY, 132.0f, 22.0f}; }
RectF PeepChainView::autoGainBounds() const { return {resetBounds().getX() - 12.0f - 150.0f, kChainRowY, 150.0f, 22.0f}; }
RectF PeepChainView::groupBounds() const { return {autoGainBounds().getX() - 16.0f - 240.0f, kChainRowY, 240.0f, 22.0f}; }
static constexpr float kVersionY = 18.0f;
static juce::String versionText() { return "v" + UpdateChecker::currentVersion(); }
static float versionX() { return wordmarkRight() + 12.0f; }
RectF PeepChainView::githubBounds() const {
    const float x = versionX() + juce::GlyphArrangement::getStringWidth(labelFont(12.0f), versionText()) + 10.0f;
    if (!UpdateChecker::isConfigured()) return {x, kVersionY, 0.0f, 16.0f};
    return {x, kVersionY, 16.0f + 4.0f + juce::GlyphArrangement::getStringWidth(labelFont(12.0f), "GITHUB"), 16.0f};
}
RectF PeepChainView::updateBounds() const { return {githubBounds().getRight() + 12.0f, kVersionY, 200.0f, 16.0f}; }

RectF PeepChainView::controlBounds(int c) const {
    if (c == kDelaySync || c == kReverbSync || c == kDelayEdit || c == kReverbEdit) {   // under the Delay / Reverb value: SYNC, EDIT
        const float colW = kControlsArea.getWidth() / (float)kKnobs;
        const bool delay = c == kDelaySync || c == kDelayEdit, edit = c == kDelayEdit || c == kReverbEdit;
        const float cx = kControlsArea.getX() + colW * ((delay ? 3.0f : 4.0f) + 0.5f) + (edit ? 28.0f : -28.0f);
        return RectF(52.0f, 18.0f).withCentre({cx, kControlsArea.getY() + 178.0f});
    }
    const float y = (kHeaderH - 24.0f) * 0.5f, h = 24.0f;
    auto textW = [](const char* t) { return juce::GlyphArrangement::getStringWidth(labelFont(12.0f), t) + 20.0f; };
    float x = kW - kPad;
    RectF r[kNumControls];
    auto take = [&](int ctl, float w, float gapBefore) { r[ctl] = {x - w, y, w, h}; x -= w + gapBefore; };
    take(kSize, textW("200% ") + 10.0f, 16.0f);
    take(kCopy, textW("COPY A TO B"), 8.0f);
    take(kSlotB, 26.0f, 0.0f);
    take(kSlotA, 26.0f, 16.0f);
    take(kRedo, 26.0f, 2.0f);
    take(kUndo, 26.0f, 0.0f);
    return c >= 0 && c < kNumControls ? r[c] : RectF();
}

juce::String PeepChainView::getTooltip() {
    if (autoGainBounds().contains(lastMouse))
        return "Auto Gain: turns the output down (or up) to the loudness of what comes in, so switching the plugin on "
               "and off compares the sound, not the volume. The number is the gain it applies. Off by default.";
    if (updateBounds().contains(lastMouse) && updates->getState() == UpdateChecker::State::available)
        return "See what's new in " + updates->getLatestVersion() + " and download it";
    if (groupBounds().contains(lastMouse))
        return "Link PeepChain across tracks so they share settings";
    if (githubBounds().contains(lastMouse)) return "Open PeepChain on GitHub";
    for (int slot = 0; slot < kStages; ++slot) {
        if (!slotBounds(slot).contains(lastMouse)) continue;
        const int stage = proc.getOrder()[(size_t)slot];
        if (powerBounds(slot).contains(lastMouse)) return proc.isStageOn(stage) ? "Switch this effect off" : "Switch this effect on";
        if (!hasSettings(stage)) return "Drag sideways to change the order";
        return getEditingStage() == stage ? juce::String("Click to close its settings. Drag sideways to change the order.")
                                          : "Click for this effect's own settings. Drag sideways to change the order.";
    }
    for (int c = 0; c < kNumControls; ++c) {
        if (!controlBounds(c).contains(lastMouse)) continue;
        const juce::String here = proc.getActiveSlot() == 0 ? "A" : "B", there = proc.getActiveSlot() == 0 ? "B" : "A";
        switch (c) {
            case kUndo: return "Undo the last change";
            case kRedo: return "Redo what was undone";
            case kSlotA: case kSlotB:
                return "Two sets of settings to compare. Click " + juce::String(c == kSlotA ? "A" : "B") + " to switch to it; "
                       "what you leave stays in its slot. The first visit to B starts as a copy of A.";
            case kCopy: return "Copy these settings (" + here + ") over " + there;
            case kSize: return "Window size";
            case kDelaySync: {
                const bool on = proc.apvts.getRawParameterValue("delaySync")->load() > 0.5f;
                return on ? "Echo follows the song's tempo, now " + juce::String(juce::roundToInt(proc.getDelayMs())) + " ms. Click for a fixed time."
                          : "Echo at a fixed time, now " + juce::String(juce::roundToInt(proc.getDelayMs())) + " ms (Peep's: 500 ms, a quarter note at 120 bpm). "
                            "Click to follow the song's tempo instead.";
            }
            case kReverbSync: {
                const bool on = proc.apvts.getRawParameterValue("reverbSync")->load() > 0.5f;
                return on ? "Reverb pre-delay follows the song's tempo: the same part of a beat it is at 120 bpm, now "
                                + juce::String(juce::roundToInt(proc.getReverbPreDelayMs())) + " ms. Click to fix it."
                          : "Reverb pre-delay (the gap before it starts) fixed, now " + juce::String(juce::roundToInt(proc.getReverbPreDelayMs()))
                                + " ms. Click to make it follow the song's tempo instead.";
            }
            case kDelayEdit: case kReverbEdit: return "Delay and Reverb settings: time, feedback, tone, pre-delay, length, width";
            default: break;
        }
    }
    if (resetBounds().contains(lastMouse)) return "Put the order, switches, thresholds, sends and every effect setting back to Peep's";
    return {};
}

int PeepChainView::slotAt(PointF p) const {
    for (int i = 0; i < kStages; ++i) if (slotBounds(i).contains(p)) return i;
    return -1;
}

PeepChainProcessor::Order PeepChainView::displayOrder() const {
    auto o = proc.getOrder();
    if (!dragging || pressedSlot < 0 || dragToSlot < 0 || dragToSlot == pressedSlot) return o;
    const int stage = o[(size_t)pressedSlot];
    std::vector<int> v(o.begin(), o.end());
    v.erase(v.begin() + pressedSlot);
    v.insert(v.begin() + dragToSlot, stage);
    for (int i = 0; i < kStages; ++i) o[(size_t)i] = v[(size_t)i];
    return o;
}

void PeepChainView::toggleStage(int stage) {
    if (auto* p = proc.apvts.getParameter(PeepChainProcessor::stageParamIds[stage])) {
        p->beginChangeGesture(); p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.0f : 1.0f); p->endChangeGesture();
    }
}

bool PeepChainView::hasSettings(int stage) {                      // the six effects, and EffectParams::kSends
    static const auto has = [] { std::array<bool, PeepChain::kStages + 1> h{}; for (const auto& d : EffectParams::all()) h[(size_t)d.stage] = true; return h; }();
    return stage >= 0 && stage <= kStages && has[(size_t)stage];
}

bool PeepChainView::keyPressed(const juce::KeyPress& k) {          // Esc closes the settings
    if (k != juce::KeyPress::escapeKey || panel == nullptr) return false;
    openEditor(-1);
    return true;
}

void PeepChainView::openEditor(int stage) {
    if (!hasSettings(stage)) stage = -1;
    if (getEditingStage() == stage) return;
    panel.reset();
    if (stage >= 0) {
        panel = std::make_unique<StagePanel>(proc, stage);
        panel->onClose = [safe = juce::Component::SafePointer<PeepChainView>(this)] {     // not from inside the panel's own click
            juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->openEditor(-1); });
        };
        addAndMakeVisible(*panel);
        panel->setBounds(kPanelArea.toNearestInt());
        if (updatePopup != nullptr) updatePopup->toFront(true);
    }
    for (auto* s : {&input, &gateThresh, &compThresh, &delayLevel, &reverbLevel, &output}) s->setVisible(panel == nullptr);
    repaint();
}

//==============================================================================
// mouse
void PeepChainView::mouseDown(const juce::MouseEvent& e) {
    pressedEdit = -1; pressedPower = false;
    for (int slot = 0; slot < kStages; ++slot) if (editBounds(slot).contains(e.position)) pressedEdit = slot;
    if (pressedEdit < 0 && e.mods.isPopupMenu())                                    // right-click a card: its settings
        if (const int slot = slotAt(e.position); slot >= 0 && hasSettings(proc.getOrder()[(size_t)slot])) pressedEdit = slot;
    if (pressedEdit >= 0) {
        pressedSlot = dragToSlot = -1; dragging = false; dragDx = 0; pressedControl = -1;
        pressedReset = pressedAutoGain = pressedUpdate = pressedGithub = pressedGroup = false;
        return;
    }
    pressedSlot = slotAt(e.position); dragToSlot = pressedSlot; dragging = false; dragDx = 0;
    pressedPower = pressedSlot >= 0 && powerBounds(pressedSlot).contains(e.position);
    pressedReset = pressedSlot < 0 && resetBounds().contains(e.position);
    pressedAutoGain = pressedSlot < 0 && autoGainBounds().contains(e.position);
    pressedUpdate = pressedSlot < 0 && updateBounds().contains(e.position) && updates->getState() == UpdateChecker::State::available;
    pressedGithub = pressedSlot < 0 && githubBounds().contains(e.position);
    pressedGroup = pressedSlot < 0 && groupBounds().contains(e.position);
    pressedControl = pressedSlot < 0 ? controlAt(e.position) : -1;
}

void PeepChainView::mouseDrag(const juce::MouseEvent& e) {
    if (pressedSlot < 0) return;
    dragDx = (float)e.getDistanceFromDragStartX();
    if (!dragging && std::abs(dragDx) > 6.0f) dragging = true;
    if (dragging) {
        dragToSlot = juce::jlimit(0, kStages - 1, pressedSlot + juce::roundToInt(dragDx / (slotBounds(1).getX() - slotBounds(0).getX())));
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        repaint(chainStrip().toNearestInt());
    }
}

void PeepChainView::previewDrag(int fromSlot, float dx) {
    pressedSlot = fromSlot; dragging = true; dragDx = dx;
    dragToSlot = juce::jlimit(0, kStages - 1, pressedSlot + juce::roundToInt(dragDx / (slotBounds(1).getX() - slotBounds(0).getX())));
}

void PeepChainView::mouseUp(const juce::MouseEvent& e) {
    if (pressedEdit >= 0) {
        if (slotAt(e.position) == pressedEdit) {
            const int stage = proc.getOrder()[(size_t)pressedEdit];
            openEditor(getEditingStage() == stage ? -1 : stage);
        }
    } else if (pressedSlot >= 0) {
        if (dragging) proc.setOrder(displayOrder());                               // drop: one reorder, however far it moved
        else if (slotAt(e.position) == pressedSlot) {
            const int stage = proc.getOrder()[(size_t)pressedSlot];
            if (pressedPower) toggleStage(stage);                                   // the power button
            else if (hasSettings(stage)) openEditor(getEditingStage() == stage ? -1 : stage);   // the card: its settings
        }
    } else if (pressedReset && resetBounds().contains(e.position)) {
        proc.resetToPeep();
    } else if (pressedAutoGain && autoGainBounds().contains(e.position)) {
        if (auto* p = proc.apvts.getParameter("autoGain")) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.0f : 1.0f); p->endChangeGesture();
        }
    } else if (pressedUpdate && updateBounds().contains(e.position)) {
        showUpdatePopup();
    } else if (pressedGroup && groupBounds().contains(e.position)) {
        if (onGroupClicked) onGroupClicked();
    } else if (pressedGithub && githubBounds().contains(e.position)) {
        UpdateChecker::repoPage().launchInDefaultBrowser();
    } else if (pressedControl >= 0 && controlAt(e.position) == pressedControl) {
        activate(pressedControl);
    }
    pressedControl = -1; pressedEdit = -1; pressedPower = false;
    pressedSlot = dragToSlot = -1; dragging = false; pressedReset = false; pressedAutoGain = false; pressedUpdate = false; pressedGithub = false; pressedGroup = false; dragDx = 0;
    mouseMove(e);
    repaint();
}

void PeepChainView::mouseMove(const juce::MouseEvent& e) {
    lastMouse = e.position;
    const bool wasReset = overReset, wasAutoGain = overAutoGain;
    overReset = resetBounds().contains(e.position) && !proc.isPeepPreset();
    overAutoGain = autoGainBounds().contains(e.position);
    if (overReset != wasReset || overAutoGain != wasAutoGain) repaint(autoGainBounds().getUnion(resetBounds()).expanded(4.0f).toNearestInt());
    overUpdate = updateBounds().contains(e.position) && updates->getState() == UpdateChecker::State::available;
    const bool wasOverGithub = std::exchange(overGithub, githubBounds().contains(e.position));
    if (overGithub != wasOverGithub) repaint(githubBounds().expanded(2.0f).toNearestInt());
    if (std::exchange(overGroup, groupBounds().contains(e.position)) != overGroup) repaint(groupBounds().expanded(2.0f).toNearestInt());
    int edit = -1;
    for (int slot = 0; slot < kStages; ++slot) if (editBounds(slot).contains(e.position)) edit = slot;
    overEdit = edit;
    if (const int was = std::exchange(overSlot, slotAt(e.position)); was != overSlot)
        for (int s : {was, overSlot}) if (s >= 0) repaint(slotBounds(s).expanded(6.0f).toNearestInt());
    if (overSlot >= 0) repaint(slotBounds(overSlot).expanded(6.0f).toNearestInt());       // power / EDIT hover inside the card
    const int wasOverControl = std::exchange(overControl, controlAt(e.position));
    if (overControl != wasOverControl) for (int c : {wasOverControl, overControl}) if (c >= 0) repaint(controlBounds(c).expanded(3.0f).toNearestInt());
    const bool clickable = (overSlot >= 0 && (hasSettings(proc.getOrder()[(size_t)overSlot]) || powerBounds(overSlot).contains(e.position)))
                        || overReset || overAutoGain || overUpdate || overGithub || overGroup || overControl >= 0;
    setMouseCursor(clickable ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void PeepChainView::mouseExit(const juce::MouseEvent&) {
    for (int s : {std::exchange(overSlot, -1)}) if (s >= 0) repaint(slotBounds(s).expanded(6.0f).toNearestInt());
    if (std::exchange(overGroup, false)) repaint(groupBounds().expanded(2.0f).toNearestInt());
    overEdit = -1;
}

//==============================================================================
// the parts of the window that never change, drawn once
void PeepChainView::renderBackdrop() {
    const float scale = imageScale;
    backdrop = juce::Image(juce::Image::ARGB, (int)(kW * scale), (int)(kH * scale), true);
    juce::Graphics g(backdrop);
    g.addTransform(juce::AffineTransform::scale(scale));
    const RectF b(0.0f, 0.0f, kW, kH);

    g.fillAll(Pal::bg());
    g.setGradientFill(juce::ColourGradient(Pal::pink().withAlpha(0.07f), 0.0f, 0.0f, Pal::pink().withAlpha(0.0f), 0.0f, kH * 0.7f, true)); g.fillRect(b);

    // header bar
    const RectF head(0.0f, 0.0f, kW, kHeaderH);
    g.setGradientFill(juce::ColourGradient(Pal::header().brighter(0.05f), 0.0f, 0.0f, Pal::header(), 0.0f, kHeaderH, false)); g.fillRect(head);
    g.setColour(Pal::edge()); g.fillRect(0.0f, kHeaderH - 1.0f, kW, 1.0f);
    const RectF hr(20.0f, 16.0f, 20.0f, 18.5f);
    juce::DropShadow(Pal::pink().withAlpha(0.6f), 10, {}).drawForPath(g, heart(hr));
    g.setGradientFill(juce::ColourGradient(Pal::hot(), 0.0f, hr.getY(), Pal::pink().darker(0.2f), 0.0f, hr.getBottom(), false));
    g.fillPath(heart(hr));
    const auto wm = wordmarkFont();
    const float peepW = juce::GlyphArrangement::getStringWidth(wm, "PEEP");
    g.setFont(wm);
    g.setColour(Pal::pink()); g.drawText("PEEP", RectF(46.0f, 12.0f, peepW + 2.0f, 28.0f), juce::Justification::centredLeft, false);
    g.setColour(Pal::pale()); g.drawText("CHAIN", RectF(46.0f + peepW, 12.0f, 120.0f, 28.0f), juce::Justification::centredLeft, false);
    {   // toolbar group separators
        g.setColour(Pal::edge());
        for (float x : {controlBounds(kSlotA).getX() - 8.0f, controlBounds(kSize).getX() - 8.0f}) g.fillRect(x, 14.0f, 1.0f, kHeaderH - 28.0f);
    }

    // "SIGNAL CHAIN" and its hint
    g.setColour(Pal::grey()); g.setFont(labelFont(13.0f).withExtraKerningFactor(0.06f));
    g.drawText("SIGNAL CHAIN", RectF(kPad + 2.0f, kChainRowY, 120.0f, 22.0f), juce::Justification::centredLeft, false);

    // the controls card and its column labels
    card(g, kControlsArea, 10.0f);

    // the meters' card
    card(g, RectF(kPad, kMeterY - 18.0f, kW - 2.0f * kPad, 62.0f), 10.0f);

}

//==============================================================================
static void reductionBar(juce::Graphics& g, RectF r, float db, float maxDb) {
    g.setColour(Pal::track().withAlpha(0.7f)); g.fillRoundedRectangle(r, 2.0f);
    const float frac = juce::jlimit(0.0f, 1.0f, db / maxDb);
    if (frac > 0.005f) {
        const auto f = r.withHeight(r.getHeight() * frac);
        g.setGradientFill(juce::ColourGradient(Pal::hot(), 0.0f, f.getY(), Pal::pink().darker(0.3f), 0.0f, f.getBottom(), false));
        g.fillRoundedRectangle(f, 2.0f);
    }
}

static void readout(juce::Graphics& g, RectF r, float db) {
    g.setColour(db > 0.3f ? Pal::pale() : Pal::grey());
    g.setFont(labelFont(12.0f));
    g.drawText(db > 0.05f ? "-" + juce::String(db, 1) : "0.0", r, juce::Justification::centred, false);
}

void PeepChainView::paintStageDisplay(juce::Graphics& g, RectF in, int stage, bool on) {
    const float gOpen = on ? gateOpen : 0.0f, cDb = on ? compDb : 0.0f, lDb = on ? limDb : 0.0f;
    if (stage == PeepChain::kGate) {                              // a heart that lights up while the gate is open
        const auto hr = RectF(40.0f, 37.0f).withCentre(in.getCentre().translated(0.0f, -6.0f));
        if (gOpen > 0.05f) juce::DropShadow(Pal::pink().withAlpha(0.8f * gOpen), 14, {}).drawForPath(g, heart(hr));
        brokenHeart(g, hr, Pal::dim().interpolatedWith(Pal::pink(), gOpen), Pal::well());
        g.setColour(gOpen > 0.5f ? Pal::pale() : Pal::grey()); g.setFont(labelFont(12.0f));
        g.drawText(gOpen > 0.5f ? "OPEN" : "CLOSED", in.withTop(in.getBottom() - 14.0f), juce::Justification::centred, false);
    } else if (stage == PeepChain::kComp || stage == PeepChain::kLimit) {
        const float db = stage == PeepChain::kComp ? cDb : lDb;
        auto col = in.reduced(0.0f, 2.0f);
        readout(g, col.removeFromBottom(14.0f), db);
        reductionBar(g, col.withTrimmedBottom(3.0f).withSizeKeepingCentre(12.0f, col.getHeight() - 3.0f), db, stage == PeepChain::kComp ? 20.0f : 18.0f);
        g.setColour(Pal::grey().withAlpha(0.35f));                // scale ticks either side
        for (int i = 0; i <= 4; ++i) {
            const float y = col.getY() + (col.getHeight() - 3.0f) * (float)i / 4.0f;
            g.fillRect(in.getCentreX() - 18.0f, y, 6.0f, 1.0f); g.fillRect(in.getCentreX() + 12.0f, y, 6.0f, 1.0f);
        }
    } else if (stage == PeepChain::kFilter) {                     // AUFilter: its real response, 20 Hz .. 20 kHz
        juce::Path p; auto a = in.reduced(2.0f, 4.0f);
        if (shapeKey == ~0ull) updateCardShapes();                 // the timer keeps them up to date
        const float* dbs = filterDb;
        float lo = 0.0f, hi = 13.0f;
        for (int k = 0; k <= 48; ++k) { lo = juce::jmin(lo, dbs[k]); hi = juce::jmax(hi, dbs[k]); }
        g.setColour(Pal::grey().withAlpha(0.15f));
        for (int k = 1; k < 3; ++k) g.fillRect(a.getX() + a.getWidth() * (float)k / 3.0f, a.getY(), 1.0f, a.getHeight());   // decades
        for (int k = 0; k <= 48; ++k) {
            const float px = a.getX() + a.getWidth() * (float)k / 48.0f, py = a.getBottom() - 4.0f - (a.getHeight() - 8.0f) * (dbs[k] - lo) / (hi - lo);
            if (k == 0) p.startNewSubPath(px, py); else p.lineTo(px, py);
        }
        juce::Path fill(p); fill.lineTo(a.getRight(), a.getBottom()); fill.lineTo(a.getX(), a.getBottom()); fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(Pal::pink().withAlpha(0.35f), 0.0f, a.getY(), Pal::pink().withAlpha(0.0f), 0.0f, a.getBottom(), false)); g.fillPath(fill);
        g.setColour(on ? Pal::hot() : Pal::grey()); g.strokePath(p, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    } else if (stage == PeepChain::kGeq) {                        // the 31 graphic EQ sliders, as they are set
        if (shapeKey == ~0ull) updateCardShapes();                 // the timer keeps them up to date
        const double* gains = geqGains;
        float lo = 0.0f, hi = 13.0f;
        for (int k = 0; k < 31; ++k) { lo = juce::jmin(lo, (float)gains[k]); hi = juce::jmax(hi, (float)gains[k]); }
        auto a = in.reduced(2.0f, 4.0f); const float bw = a.getWidth() / 31.0f;
        const float zeroY = a.getBottom() - (a.getHeight() - 2.0f) * (0.0f - lo) / (hi - lo);
        for (int k = 0; k < 31; ++k) {
            const float v = (float)gains[k] / (hi - lo), x = a.getX() + bw * (float)k, h = juce::jmax(2.0f, (a.getHeight() - 2.0f) * std::abs(v));
            const float top = v >= 0.0f ? zeroY - h : zeroY;
            g.setColour(v != 0.0f ? (on ? Pal::pink() : Pal::grey()) : Pal::track());
            g.fillRoundedRectangle(x + 0.4f, v == 0.0f ? zeroY - 2.0f : top, juce::jmax(1.2f, bw - 0.9f), h, 0.8f);
        }
    } else {                                                      // multiband: the four bands' gain reduction
        auto bars = in.reduced(0.0f, 2.0f).withSizeKeepingCentre(72.0f, in.getHeight() - 4.0f);
        const float bw = 11.0f, sp = (bars.getWidth() - bw * 4.0f) / 3.0f;
        for (int b = 0; b < 4; ++b) reductionBar(g, {bars.getX() + (float)b * (bw + sp), bars.getY(), bw, bars.getHeight()}, on ? bandDb[b] : 0.0f, 40.0f);
    }
}

bool PeepChainView::updateCardShapes() {
    const auto s = proc.effectSettings();
    juce::uint64 k = 1469598103934665603ull;                      // FNV-1a over the Filter's and EQ's settings
    auto mix = [&k](const void* data, size_t n) { auto* b = (const juce::uint8*)data; for (size_t i = 0; i < n; ++i) k = (k ^ b[i]) * 1099511628211ull; };
    const auto& f = s.filter;
    const double fv[] = {f.lowIsCut ? 1.0 : 0.0, f.lowFreq, f.lowGain, f.highIsCut ? 1.0 : 0.0, f.highFreq, f.highGain,
                         f.freq[0], f.gain[0], f.bw[0], f.freq[1], f.gain[1], f.bw[1], f.freq[2], f.gain[2], f.bw[2]};
    mix(fv, sizeof(fv)); mix(s.geqGains, sizeof(s.geqGains));
    if (k == shapeKey) return false;
    shapeKey = k;
    AUFilterEQ eq; eq.prepare(44100.0, f);
    for (int i = 0; i <= 48; ++i)
        filterDb[i] = juce::jlimit(-30.0f, 30.0f, (float)eq.magnitudeDb(std::pow(10.0, std::log10(20.0) + 3.0 * i / 48.0), 44100.0));
    std::copy(s.geqGains, s.geqGains + 31, geqGains);
    return true;
}

//==============================================================================
// the effect cards
void PeepChainView::timerCallback() {
    updateMeters();
    // A newer version: its notes pop up by themselves once per DAW session (in the first window to see it).
    if (!updatePopupConsidered && updates->getState() == UpdateChecker::State::available) {
        updatePopupConsidered = true;
        if (updates->claimPopup()) showUpdatePopup();
    }
    {   // GROUP shows its name and how many tracks are in it, which other tracks can change
        const auto id = proc.getGroupId();
        const auto key = id + ":" + proc.getGroupName() + ":" + juce::String(proc.getGroupHub().memberCount(id));
        if (key != groupShown) { groupShown = key; repaint(groupBounds().expanded(2.0f).toNearestInt()); }
    }
    const auto key = stateKey();
    if (key != lastPaintedKey) { lastPaintedKey = key; lastControlsKey = controlsKey(); repaint(); return; }   // something was switched or moved: everything
    if (const auto ck = controlsKey(); ck != lastControlsKey) {                       // undo / redo available, A/B, sync
        lastControlsKey = ck;
        for (int c = 0; c < kNumControls; ++c) repaint(controlBounds(c).expanded(3.0f).toNearestInt());
    }
    const bool shapes = updateCardShapes();
    if (dragging) repaint(chainStrip().toNearestInt());                               // otherwise just the moving parts:
    else for (int slot = 0; slot < kStages; ++slot) {                                 // the meters' displays, the Filter's and
        const int st = proc.getOrder()[(size_t)slot];                                 // EQ's when they changed, the level meters
        if (shapes || (st != PeepChain::kFilter && st != PeepChain::kGeq)) repaint(layoutCard(slotBounds(slot)).display.toNearestInt());
    }
    repaint(0, (int)kMeterY - 20, getWidth(), 66);
    // the Auto Gain switch shows the gain it is applying
    if (std::abs(autoGainShownDb - autoGainPaintedDb) >= 0.05f) { autoGainPaintedDb = autoGainShownDb; repaint(autoGainBounds().expanded(4.0f).toNearestInt()); }
}

/** One card, without the moving display in its window (that is drawn on top every frame). */
void PeepChainView::paintSegment(juce::Graphics& g, RectF slot, int stage, int slotIndex, bool, bool, bool lifted) {
    const bool on = proc.isStageOn(stage);
    const auto l = layoutCard(slot);
    juce::Path shape; shape.addRoundedRectangle(l.body, 8.0f);
    juce::DropShadow(juce::Colours::black.withAlpha(lifted ? 0.8f : 0.45f), lifted ? 22 : 10, {0, lifted ? 10 : 4}).drawForPath(g, shape);
    card(g, l.body, 8.0f, lifted ? Pal::pink() : Pal::edge());
    if (on) {                                                     // a thin accent line along the top of a working effect
        juce::Graphics::ScopedSaveState s(g); g.reduceClipRegion(shape);
        g.setColour(Pal::pink().withAlpha(0.9f)); g.fillRect(l.body.withHeight(2.0f));
    }
    g.setColour(on ? Pal::pale() : Pal::grey()); g.setFont(labelFont(15.0f).withExtraKerningFactor(0.06f));
    g.drawText(kStageNames[stage], l.head.withTrimmedLeft(32.0f).translated(0.0f, 1.0f), juce::Justification::centredLeft, false);
    g.setColour(Pal::grey()); g.setFont(labelFont(13.0f));
    g.drawText(juce::String(slotIndex + 1), l.head.withTrimmedRight(10.0f).translated(0.0f, 1.0f), juce::Justification::centredRight, false);
    g.setColour(Pal::well()); g.fillRoundedRectangle(l.display, 5.0f);
    g.setColour(juce::Colours::black.withAlpha(0.35f)); g.drawRoundedRectangle(l.display.reduced(0.5f), 5.0f, 1.0f);
    if (!on) {                                                    // switched off: the display greyed and BYPASSED over it
        g.beginTransparencyLayer(0.25f); paintStageDisplay(g, l.display.reduced(6.0f, 5.0f), stage, false); g.endTransparencyLayer();
        g.setColour(Pal::grey()); g.setFont(labelFont(12.0f).withExtraKerningFactor(0.06f));
        g.drawText("BYPASSED", l.display, juce::Justification::centred, false);
    }
}

juce::uint64 PeepChainView::stateKey() const {
    juce::uint64 k = 0;
    for (int st : displayOrder()) k = k * 8 + (juce::uint64)st;
    for (int i = 0; i < kStages; ++i) k = k * 2 + (proc.isStageOn(i) ? 1u : 0u);
    k = k * 64 + (juce::uint64)(dragging ? pressedSlot + 1 : 0) * 8 + (juce::uint64)(proc.isPeepPreset() ? 1 : 0) + (overReset ? 2u : 0u);
    const bool agOn = proc.apvts.getRawParameterValue("autoGain")->load() > 0.5f;
    return k * 16 + (agOn ? 1u : 0u) + (overAutoGain ? 2u : 0u) + (updates->getState() == UpdateChecker::State::available ? 4u : 0u) + (overUpdate ? 8u : 0u);
}

/** The cards change only when something is switched, moved or dragged, so they are drawn once and kept. */
void PeepChainView::renderChainLayer() {
    const float scale = imageScale;
    const auto shown = displayOrder();
    const int lifted = dragging && pressedSlot >= 0 ? proc.getOrder()[(size_t)pressedSlot] : -1;
    const auto area = chainStrip();
    chainLayer = juce::Image(juce::Image::ARGB, (int)(area.getWidth() * scale), (int)(area.getHeight() * scale), true);
    {
        juce::Graphics g(chainLayer);
        g.addTransform(juce::AffineTransform::translation(-area.getX(), -area.getY()).scaled(scale));
        for (int slot = 0; slot < kStages; ++slot) {
            const auto r = slotBounds(slot);
            if (slot > 0) {                                          // the signal flows left to right
                const float x = r.getX() - kCardGap * 0.5f, y = r.getCentreY();
                juce::Path chev; chev.startNewSubPath(x - 2.5f, y - 5.0f); chev.lineTo(x + 2.5f, y); chev.lineTo(x - 2.5f, y + 5.0f);
                g.setColour(Pal::grey().withAlpha(0.5f)); g.strokePath(chev, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            if (shown[(size_t)slot] == lifted) {                     // where the dragged card will land
                juce::Path dashed; const float dashes[2] = {6.0f, 5.0f};
                juce::Path outline; outline.addRoundedRectangle(r.reduced(1.0f), 8.0f);
                juce::PathStrokeType(1.4f).createDashedStroke(dashed, outline, dashes, 2);
                g.setColour(Pal::pink().withAlpha(0.8f)); g.fillPath(dashed);
            } else {
                paintSegment(g, r, shown[(size_t)slot], slot, false, false, false);
            }
        }
    }
    liftedLayer = {};
    if (lifted >= 0) {                                               // the card in the hand, drawn once and then just moved
        const auto r = slotBounds(0).expanded(kLayerMargin);
        liftedLayer = juce::Image(juce::Image::ARGB, (int)(r.getWidth() * scale), (int)(r.getHeight() * scale), true);
        juce::Graphics g(liftedLayer);
        g.addTransform(juce::AffineTransform::translation(-r.getX(), -r.getY()).scaled(scale));
        paintSegment(g, slotBounds(0), lifted, dragToSlot, false, false, true);
    }
    chainKey = stateKey() * 8 + (juce::uint64)(dragToSlot + 1);
}

RectF PeepChainView::chainStrip() const { return RectF(kPad, kCardY, kW - 2.0f * kPad, kCardH).expanded(kLayerMargin - 6.0f, kLayerMargin); }

void PeepChainView::paintChain(juce::Graphics& g) {
    if (chainLayer.isNull() || chainKey != stateKey() * 8 + (juce::uint64)(dragToSlot + 1)) renderChainLayer();
    g.drawImage(chainLayer, chainStrip());
    const auto shown = displayOrder();
    const int lifted = dragging && pressedSlot >= 0 ? proc.getOrder()[(size_t)pressedSlot] : -1;
    auto display = [&](RectF slot, int stage) {
        if (!proc.isStageOn(stage)) return;                          // an off stage's faded display is part of the kept picture
        paintStageDisplay(g, layoutCard(slot).display.reduced(6.0f, 5.0f), stage, true);
    };
    for (int slot = 0; slot < kStages; ++slot)
        if (shown[(size_t)slot] != lifted) display(slotBounds(slot), shown[(size_t)slot]);
    if (lifted >= 0 && !liftedLayer.isNull()) {
        auto r = slotBounds(pressedSlot).translated(dragDx, -6.0f);
        r.setX(juce::jlimit(slotBounds(0).getX() - 10.0f, slotBounds(kStages - 1).getX() + 10.0f, r.getX()));
        g.drawImage(liftedLayer, r.expanded(kLayerMargin));
        display(r, lifted);
    }
}

/** What changes with the mouse on top of the kept cards: the power button, the hover edge, EDIT, and the open one. */
void PeepChainView::paintEditMarks(juce::Graphics& g) {
    if (dragging) {                                               // mid-drag: just the power buttons, where the cards are shown
        const auto shown = displayOrder();
        const int lifted = pressedSlot >= 0 ? proc.getOrder()[(size_t)pressedSlot] : -1;
        for (int slot = 0; slot < kStages; ++slot)
            if (shown[(size_t)slot] != lifted) powerIcon(g, layoutCard(slotBounds(slot)).power, proc.isStageOn(shown[(size_t)slot]), false);
        if (lifted >= 0) {
            auto r = slotBounds(pressedSlot).translated(dragDx, -6.0f);
            r.setX(juce::jlimit(slotBounds(0).getX() - 10.0f, slotBounds(kStages - 1).getX() + 10.0f, r.getX()));
            powerIcon(g, layoutCard(r).power, proc.isStageOn(lifted), false);
        }
        return;
    }
    for (int slot = 0; slot < kStages; ++slot) {
        const int stage = proc.getOrder()[(size_t)slot];
        const auto l = layoutCard(slotBounds(slot));
        const bool open = getEditingStage() == stage, hover = overSlot == slot;
        if (open) {                                               // a pink edge with a soft halo outside it
            for (int i = 3; i >= 1; --i) { g.setColour(Pal::pink().withAlpha(0.10f)); g.drawRoundedRectangle(l.body.expanded((float)i), 8.0f + (float)i, 1.5f); }
            g.setColour(Pal::pink()); g.drawRoundedRectangle(l.body.reduced(0.5f), 8.0f, 1.5f);
        } else if (hover) {
            g.setColour(Pal::edge().brighter(0.6f)); g.drawRoundedRectangle(l.body.reduced(0.5f), 8.0f, 1.0f);
        }
        powerIcon(g, l.power, proc.isStageOn(stage), hover && powerBounds(slot).contains(lastMouse));
        const auto e = editBounds(slot);
        if (e.isEmpty()) continue;
        const bool overE = overEdit == slot || (hover && !powerBounds(slot).contains(lastMouse));
        if (open) button(g, e, "EDITING", true, false, true, 11.0f);
        else button(g, e, "EDIT", false, overE, true, 11.0f);
    }
}

//==============================================================================
// meters and the header controls
void PeepChainView::paintLevelMeter(juce::Graphics& g, RectF r, const juce::String& name, float db, bool inputGuide) {
    g.setColour(Pal::pale()); g.setFont(labelFont(14.0f).withExtraKerningFactor(0.06f));
    g.drawText(name, r.removeFromLeft(34.0f), juce::Justification::centredLeft, false);
    g.setColour(db > -0.2f ? Pal::pink() : Pal::pale().withAlpha(0.85f)); g.setFont(labelFont(13.0f));
    g.drawText(db <= -59.9f ? juce::String("-INF") : juce::String(db, 1), r.removeFromRight(46.0f), juce::Justification::centredRight, false);
    auto bar = r.withSizeKeepingCentre(r.getWidth() - 8.0f, 8.0f);
    auto xAt = [&](float d) { return bar.getX() + bar.getWidth() * juce::jlimit(0.0f, 1.0f, (d + 60.0f) / 60.0f); };
    g.setColour(Pal::well()); g.fillRoundedRectangle(bar, 2.0f);
    const float frac = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    if (frac > 0.005f) {
        const auto fill = bar.withWidth(bar.getWidth() * frac);
        g.setGradientFill(juce::ColourGradient(Pal::pink().darker(0.6f), bar.getX(), 0.0f, db > -0.2f ? juce::Colours::white : Pal::hot(), bar.getRight(), 0.0f, false));
        g.fillRoundedRectangle(fill, 2.0f);
    }
    g.setColour(Pal::grey()); g.setFont(plainLabelFont(11.0f));                   // scale
    for (int d : {-48, -36, -24, -12, -6, 0}) {
        const float x = xAt((float)d);
        g.fillRect(x - 0.5f, bar.getBottom() + 2.0f, 1.0f, 2.5f);
        g.drawText(juce::String(d), RectF(x - 14.0f, bar.getBottom() + 6.0f, 28.0f, 10.0f), juce::Justification::centred, false);
    }
    if (!inputGuide) return;
    // where Peep's own takes peaked: aim the loud phrases here with the INPUT knob
    const auto zone = RectF(xAt(kPeepPeakLowDb), bar.getY() - 4.0f, xAt(kPeepPeakHighDb) - xAt(kPeepPeakLowDb), bar.getHeight() + 8.0f);
    const bool inZone = db >= kPeepPeakLowDb && db <= kPeepPeakHighDb;
    g.setColour(Pal::pale().withAlpha(inZone ? 0.95f : 0.45f)); g.drawRoundedRectangle(zone, 3.0f, inZone ? 1.4f : 1.0f);
    g.setFont(labelFont(11.0f));
    g.drawText("PEEP'S PEAKS", zone.withY(zone.getY() - 12.0f).withHeight(11.0f).expanded(30.0f, 0.0f), juce::Justification::centred, false);
    // the gate threshold, when the gate is the first thing the input meets
    if (proc.getOrder()[0] == PeepChain::kGate && proc.isStageOn(PeepChain::kGate)) {
        const float x = xAt((float)gateThresh.getValue());
        g.setColour(Pal::pink()); g.fillRect(x - 0.75f, bar.getY() - 4.0f, 1.5f, bar.getHeight() + 8.0f);
        g.setFont(labelFont(11.0f)); g.drawText("GATE", RectF(x - 20.0f, bar.getY() - 15.0f, 40.0f, 11.0f), juce::Justification::centred, false);
    }
}

void PeepChainView::paintResetCapsule(juce::Graphics& g) {
    const auto r = resetBounds();
    const bool peep = proc.isPeepPreset();
    if (peep) button(g, r, "PEEP'S SETTINGS", false, false, false);
    else button(g, r, "RESET TO PEEP", false, overReset, true);
    const auto hr = RectF(10.0f, 9.0f).withCentre({r.getX() + 12.0f, r.getCentreY() + 0.5f});
    g.setColour(peep ? Pal::grey().withAlpha(0.4f) : Pal::pink()); g.fillPath(heart(hr));
}

/** Two links of a chain, for GROUP. */
static void linkIcon(juce::Graphics& g, RectF r, juce::Colour c) {
    juce::Path p;
    const float w = r.getWidth() * 0.62f, h = r.getHeight() * 0.36f;
    p.addRoundedRectangle(-w * 0.8f, -h * 0.5f, w, h, h * 0.5f);
    p.addRoundedRectangle(-w * 0.2f, -h * 0.5f, w, h, h * 0.5f);
    g.setColour(c);
    g.strokePath(p, juce::PathStrokeType(1.4f), juce::AffineTransform::rotation(-kPi * 0.25f).translated(r.getCentreX(), r.getCentreY()));
}

void PeepChainView::paintGroupButton(juce::Graphics& g) {
    const auto r = groupBounds();
    const auto id = proc.getGroupId();
    const bool in = id.isNotEmpty();
    const auto ink = in ? (overGroup ? Pal::hot() : Pal::pink()) : overGroup ? Pal::hot() : Pal::grey();
    g.setColour(Pal::bg().withAlpha(0.6f)); g.fillRoundedRectangle(r, 5.0f);
    g.setColour(in ? ink.withAlpha(overGroup ? 1.0f : 0.7f) : overGroup ? Pal::hot() : Pal::edge().brighter(0.35f));
    g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.0f);
    linkIcon(g, RectF(14.0f, 14.0f).withCentre({r.getX() + 15.0f, r.getCentreY()}), ink);
    auto text = r.withTrimmedLeft(28.0f).withTrimmedRight(22.0f).translated(0.0f, 0.5f);
    g.setFont(labelFont(12.0f));
    if (in) {                                                     // its name, and how many tracks are in it
        const int n = proc.getGroupHub().memberCount(id);
        const auto count = juce::String(n) + (n == 1 ? " TRACK" : " TRACKS");
        const float countW = juce::GlyphArrangement::getStringWidth(labelFont(12.0f), count);
        g.setColour(Pal::grey());
        g.drawText(count, text.removeFromRight(countW), juce::Justification::centredRight, false);
        g.setColour(ink);
        g.drawText(proc.getGroupName().toUpperCase(), text.withTrimmedRight(10.0f), juce::Justification::centredLeft, true);
    } else {
        g.setColour(overGroup ? Pal::hot() : Pal::pale());
        g.drawText("NO GROUP", text, juce::Justification::centredLeft, false);
    }
    juce::Path tri; const float tx = r.getRight() - 12.0f, ty = r.getCentreY();
    tri.addTriangle(tx - 3.5f, ty - 1.8f, tx + 3.5f, ty - 1.8f, tx, ty + 2.6f);
    g.setColour(overGroup ? Pal::hot() : Pal::grey()); g.fillPath(tri);
}

void PeepChainView::showGroupMenu() {
    const auto current = proc.getGroupId();
    const auto groups = proc.getGroupHub().list();
    enum { kNew = 1, kLeave, kFirstGroup = 100 };
    juce::PopupMenu m;
    if (groups.empty()) m.addItem(-1, "No groups", false);
    // A group's own clicks (join, pencil, trash) are acted on here, once the menu has closed.
    auto act = [safe = juce::Component::SafePointer<PeepChainView>(this)](juce::String id, GroupMenuItem::Action a) {
        juce::MessageManager::callAsync([safe, id, a] {
            if (safe == nullptr) return;
            if (a == GroupMenuItem::kJoin) safe->proc.joinGroup(id);              // takes on its settings
            else if (a == GroupMenuItem::kRename) safe->showRenameDialog(id);
            else safe->proc.deleteGroup(id);
            safe->repaint();
        });
    };
    for (size_t i = 0; i < groups.size(); ++i)
        m.addCustomItem(kFirstGroup + (int)i,
                        std::make_unique<GroupMenuItem>(groups[i], groups[i].id == current,
                                                        [act, id = groups[i].id](GroupMenuItem::Action a) { act(id, a); }),
                        nullptr, groups[i].name);
    m.addSeparator();
    m.addItem(kNew, "New group");
    if (current.isNotEmpty()) m.addItem(kLeave, "Leave group");
    const auto area = localAreaToGlobal(groupBounds()).toNearestInt();
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(area),
                    [safe = juce::Component::SafePointer<PeepChainView>(this)](int result) {
                        if (safe == nullptr) return;
                        if (result == kNew) safe->proc.createGroup();
                        else if (result == kLeave) safe->proc.leaveGroup();
                        safe->repaint();
                    });
}

//==============================================================================
// a group in the GROUP menu
GroupMenuItem::GroupMenuItem(const GroupHub::Info& i, bool isCurrent, std::function<void(Action)> f)
    : CustomComponent(false), info(i), current(isCurrent), onAction(std::move(f)) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void GroupMenuItem::getIdealSize(int& w, int& h) {
    const auto font = getLookAndFeel().getPopupMenuFont();
    w = juce::jlimit(180, 360, (int)juce::GlyphArrangement::getStringWidth(font, info.name) + 30 + 64);
    h = juce::roundToInt(font.getHeight() * 1.9f);
}

RectF GroupMenuItem::iconBounds(Action a) const {
    const auto b = getLocalBounds().toFloat();
    const float s = b.getHeight() - 6.0f;
    const float x = b.getRight() - 8.0f - (a == kDelete ? s : 2.0f * s + 4.0f);
    return {x, b.getCentreY() - s * 0.5f, s, s};
}

int GroupMenuItem::iconAt(PointF p) const {
    for (auto a : {kRename, kDelete}) if (iconBounds(a).contains(p)) return a;
    return -1;
}

void GroupMenuItem::paint(juce::Graphics& g) {
    auto& lf = getLookAndFeel();
    const bool hot = isItemHighlighted() || forcedHover;
    const auto b = getLocalBounds().toFloat();
    if (hot) { g.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId)); g.fillRect(b.reduced(1.0f, 0.0f)); }
    const auto text = hot ? findColour(juce::PopupMenu::highlightedTextColourId) : findColour(juce::PopupMenu::textColourId);
    if (current) {                                                // ticked, as a normal menu item would be
        g.setColour(text);
        const auto tick = lf.getTickShape(1.0f);
        g.fillPath(tick, tick.getTransformToScaleToFit(RectF(b.getHeight() * 0.42f, b.getHeight() * 0.42f).withCentre({b.getX() + 12.0f, b.getCentreY()}), true));
    }
    const auto font = lf.getPopupMenuFont();
    auto area = b.withTrimmedLeft(26.0f).withTrimmedRight(hot ? b.getRight() - iconBounds(kRename).getX() + 6.0f : 8.0f);
    g.setFont(font); g.setColour(text);
    g.drawText(info.name, area, juce::Justification::centredLeft, true);
    if (!hot) return;
    for (auto a : {kRename, kDelete}) {                           // the pencil and the trash can
        const auto r = iconBounds(a);
        const bool over = overIcon == a;
        if (over) { g.setColour(text.withAlpha(0.18f)); g.fillRoundedRectangle(r, 4.0f); }
        const auto ink = over ? (a == kDelete ? juce::Colour(0xffff6b6b) : text) : text.withAlpha(0.75f);
        const auto ic = r.reduced(r.getWidth() * 0.22f);
        juce::Path p;
        if (a == kRename) {
            p.startNewSubPath(ic.getX(), ic.getBottom());          // a pencil leaning right: tip at the bottom left
            p.lineTo(ic.getX() + ic.getWidth() * 0.12f, ic.getBottom() - ic.getHeight() * 0.34f);
            p.lineTo(ic.getRight() - ic.getWidth() * 0.22f, ic.getY());
            p.lineTo(ic.getRight(), ic.getY() + ic.getHeight() * 0.22f);
            p.lineTo(ic.getX() + ic.getWidth() * 0.34f, ic.getBottom() - ic.getHeight() * 0.12f);
            p.closeSubPath();
            p.startNewSubPath(ic.getRight() - ic.getWidth() * 0.36f, ic.getY() + ic.getHeight() * 0.14f);
            p.lineTo(ic.getRight() - ic.getWidth() * 0.14f, ic.getY() + ic.getHeight() * 0.36f);
        } else {
            const float x0 = ic.getX(), x1 = ic.getRight(), w = ic.getWidth(), y = ic.getY(), h = ic.getHeight();
            p.startNewSubPath(x0, y + h * 0.18f); p.lineTo(x1, y + h * 0.18f);                     // the lid
            p.startNewSubPath(x0 + w * 0.36f, y + h * 0.18f); p.lineTo(x0 + w * 0.36f, y);          // its handle
            p.lineTo(x0 + w * 0.64f, y); p.lineTo(x0 + w * 0.64f, y + h * 0.18f);
            p.startNewSubPath(x0 + w * 0.12f, y + h * 0.3f); p.lineTo(x0 + w * 0.2f, y + h);         // the can
            p.lineTo(x0 + w * 0.8f, y + h); p.lineTo(x0 + w * 0.88f, y + h * 0.3f);
            p.startNewSubPath(x0 + w * 0.4f, y + h * 0.42f); p.lineTo(x0 + w * 0.4f, y + h * 0.84f);
            p.startNewSubPath(x0 + w * 0.6f, y + h * 0.42f); p.lineTo(x0 + w * 0.6f, y + h * 0.84f);
        }
        g.setColour(ink);
        g.strokePath(p, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
}

void GroupMenuItem::mouseMove(const juce::MouseEvent& e) {
    if (const int was = std::exchange(overIcon, iconAt(e.position)); was != overIcon) repaint();
}

void GroupMenuItem::mouseExit(const juce::MouseEvent&) { if (std::exchange(overIcon, -1) >= 0) repaint(); }

void GroupMenuItem::mouseUp(const juce::MouseEvent& e) {
    if (!getLocalBounds().toFloat().contains(e.position)) return;
    const int icon = iconAt(e.position);
    const auto action = icon == kRename ? kRename : icon == kDelete ? kDelete : kJoin;
    auto f = onAction;                                            // the menu closes (and this goes) when it's triggered
    triggerMenuItem();
    if (f) f(action);
}

void PeepChainView::showRenameDialog(const juce::String& groupId) {
    const auto current = proc.getGroupHub().nameOf(groupId);
    if (current.isEmpty()) return;                                // (deleted meanwhile)
    nameDialog = std::make_unique<GroupNameDialog>(current);
    auto close = [safe = juce::Component::SafePointer<PeepChainView>(this)] {        // not from inside the dialog's own click
        juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->closeGroupNameDialog(); });
    };
    nameDialog->onCancel = close;
    nameDialog->onConfirm = [this, groupId, close](const juce::String& name) {
        proc.renameGroup(groupId, name);                          // does nothing if it was deleted meanwhile
        close();
    };
    addAndMakeVisible(*nameDialog);
    nameDialog->setBounds(getLocalBounds());
}

void PeepChainView::closeGroupNameDialog() { nameDialog.reset(); repaint(); }

//==============================================================================
// naming a group
namespace { const float kNameW = 400.0f, kNameH = 178.0f; }

GroupNameDialog::GroupNameDialog(const juce::String& name) {
    setWantsKeyboardFocus(true);
    field.setColour(juce::TextEditor::backgroundColourId, Pal::well());
    field.setColour(juce::TextEditor::outlineColourId, Pal::edge().brighter(0.3f));
    field.setColour(juce::TextEditor::focusedOutlineColourId, Pal::pink());
    field.setColour(juce::TextEditor::textColourId, Pal::pale());
    field.setColour(juce::TextEditor::highlightColourId, Pal::pink().withAlpha(0.35f));
    field.setColour(juce::TextEditor::highlightedTextColourId, Pal::pale());
    field.setColour(juce::CaretComponent::caretColourId, Pal::pink());
    field.setFont(plainLabelFont(16.0f));
    field.setIndents(10, 7);
    field.setInputRestrictions(GroupHub::kMaxNameLength);
    field.setText(name, false);
    field.onReturnKey = [this] { confirm(); };
    field.onEscapeKey = [this] { if (onCancel) onCancel(); };
    addAndMakeVisible(field);
}

void GroupNameDialog::visibilityChanged() {
    if (!isShowing()) return;
    juce::MessageManager::callAsync([safe = juce::Component::SafePointer<GroupNameDialog>(this)] {
        if (safe != nullptr && safe->isShowing()) { safe->field.grabKeyboardFocus(); safe->field.selectAll(); }
    });
}

RectF GroupNameDialog::cardBounds() const { return RectF(kNameW, kNameH).withCentre(getLocalBounds().toFloat().getCentre()); }

RectF GroupNameDialog::buttonBounds(int b) const {
    const auto c = cardBounds();
    const float y = c.getBottom() - 20.0f - 28.0f;
    auto textW = [](const char* t) { return juce::GlyphArrangement::getStringWidth(labelFont(12.0f), t) + 28.0f; };
    const float ok = textW("RENAME"), no = textW("CANCEL");
    switch (b) {
        case kCancel:  return {c.getRight() - 22.0f - no, y, no, 28.0f};
        case kConfirm: return {c.getRight() - 22.0f - no - 8.0f - ok, y, ok, 28.0f};
        default:       return {};
    }
}

void GroupNameDialog::resized() {
    const auto c = cardBounds();
    field.setBounds(RectF(c.getX() + 22.0f, c.getY() + 70.0f, c.getWidth() - 44.0f, 34.0f).toNearestInt());
}

void GroupNameDialog::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black.withAlpha(0.6f));
    const auto c = cardBounds();
    juce::Path shape; shape.addRoundedRectangle(c, 10.0f);
    juce::DropShadow(juce::Colours::black.withAlpha(0.8f), 30, {0, 12}).drawForPath(g, shape);
    card(g, c, 10.0f, Pal::edge().brighter(0.2f));
    {
        juce::Graphics::ScopedSaveState s(g); g.reduceClipRegion(shape);
        g.setColour(Pal::pink()); g.fillRect(c.withHeight(3.0f));
    }
    g.setColour(Pal::pink()); g.setFont(labelFont(12.0f).withExtraKerningFactor(0.08f));
    g.drawText("RENAME GROUP", RectF(c.getX() + 22.0f, c.getY() + 20.0f, 300.0f, 14.0f), juce::Justification::centredLeft, false);
    g.setColour(Pal::grey()); g.setFont(plainLabelFont(13.0f));
    g.drawFittedText("Every track in the group shows the new name.",
                     RectF(c.getX() + 22.0f, c.getY() + 42.0f, c.getWidth() - 44.0f, 20.0f).toNearestInt(), juce::Justification::topLeft, 1, 1.0f);
    button(g, buttonBounds(kConfirm), "RENAME", true, over == kConfirm);
    button(g, buttonBounds(kCancel), "CANCEL", false, over == kCancel);
}

int GroupNameDialog::buttonAt(PointF p) const {
    for (int b = 0; b < kNumButtons; ++b) if (buttonBounds(b).contains(p)) return b;
    return -1;
}

void GroupNameDialog::mouseDown(const juce::MouseEvent& e) {
    pressed = buttonAt(e.position);
    if (pressed < 0 && !cardBounds().contains(e.position)) pressed = 100;          // outside the card: cancel
}

void GroupNameDialog::mouseUp(const juce::MouseEvent& e) {
    const int was = std::exchange(pressed, -1);
    if (was == 100 && !cardBounds().contains(e.position)) { if (onCancel) onCancel(); }
    else if (was == kConfirm && buttonAt(e.position) == kConfirm) confirm();
    else if (was == kCancel && buttonAt(e.position) == kCancel && onCancel) onCancel();
}

void GroupNameDialog::mouseMove(const juce::MouseEvent& e) {
    if (const int was = std::exchange(over, buttonAt(e.position)); was != over) repaint();
    setMouseCursor(over >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void GroupNameDialog::mouseExit(const juce::MouseEvent&) { if (std::exchange(over, -1) >= 0) repaint(); }

bool GroupNameDialog::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) { if (onCancel) onCancel(); return true; }
    if (k == juce::KeyPress::returnKey) { confirm(); return true; }
    return false;
}

void GroupNameDialog::confirm() { if (onConfirm) onConfirm(GroupHub::cleanName(field.getText())); }

void PeepChainView::showUpdatePopup() {
    if (updates->getState() != UpdateChecker::State::available) return;
    if (updatePopup == nullptr) {
        updatePopup = std::make_unique<UpdatePopup>(updates->getLatest());
        auto later = [safe = juce::Component::SafePointer<PeepChainView>(this)] {        // not from inside the popup's own click
            juce::MessageManager::callAsync([safe] { if (safe != nullptr) safe->closeUpdatePopup(); });
        };
        updatePopup->onLater = later;
        updatePopup->onDownload = [this, later] { updates->latestPage().launchInDefaultBrowser(); later(); };
        addAndMakeVisible(*updatePopup);
        updatePopup->setBounds(getLocalBounds());
    }
    updatePopup->toFront(true);
}

void PeepChainView::closeUpdatePopup() { updatePopup.reset(); repaint(); }

//==============================================================================
// the newer release's notes
namespace { const float kPopupW = 540.0f, kPopupH = 400.0f; }

UpdatePopup::UpdatePopup(const UpdateChecker::Release& r) : release(r) {
    setWantsKeyboardFocus(true);
    notes.setMultiLine(true, true);
    notes.setReadOnly(true);
    notes.setCaretVisible(false);
    notes.setScrollbarsShown(true);
    notes.setPopupMenuEnabled(true);
    notes.setColour(juce::TextEditor::backgroundColourId, Pal::well());
    notes.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    notes.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    notes.setColour(juce::TextEditor::textColourId, Pal::pale());
    notes.setColour(juce::TextEditor::highlightColourId, Pal::pink().withAlpha(0.35f));
    notes.setColour(juce::TextEditor::highlightedTextColourId, Pal::pale());
    notes.setFont(plainLabelFont(14.0f));
    notes.setIndents(12, 10);
    notes.setLineSpacing(1.25f);
    const auto text = UpdateChecker::plainNotes(release.notes);
    notes.setText(text.isNotEmpty() ? text : juce::String("No release notes were published for this version."), false);
    notes.moveCaretToTop(false);
    addAndMakeVisible(notes);
}

RectF UpdatePopup::cardBounds() const { return RectF(kPopupW, kPopupH).withCentre(getLocalBounds().toFloat().getCentre()); }

RectF UpdatePopup::buttonBounds(int b) const {
    const auto c = cardBounds();
    const float y = c.getBottom() - 20.0f - 28.0f;
    auto textW = [](const char* t) { return juce::GlyphArrangement::getStringWidth(labelFont(12.0f), t) + 28.0f; };
    const float dl = textW("DOWNLOAD"), lt = textW("LATER");
    switch (b) {
        case kLater:     return {c.getRight() - 22.0f - lt, y, lt, 28.0f};
        case kDownload:  return {c.getRight() - 22.0f - lt - 8.0f - dl, y, dl, 28.0f};
        default:        return {};
    }
}

void UpdatePopup::resized() {
    const auto c = cardBounds();
    notes.setBounds(RectF(c.getX() + 22.0f, c.getY() + 92.0f, c.getWidth() - 44.0f, c.getHeight() - 92.0f - 68.0f).toNearestInt());
}

void UpdatePopup::paint(juce::Graphics& g) {
    g.fillAll(juce::Colours::black.withAlpha(0.6f));
    const auto c = cardBounds();
    juce::Path shape; shape.addRoundedRectangle(c, 10.0f);
    juce::DropShadow(juce::Colours::black.withAlpha(0.8f), 30, {0, 12}).drawForPath(g, shape);
    card(g, c, 10.0f, Pal::edge().brighter(0.2f));
    {
        juce::Graphics::ScopedSaveState s(g); g.reduceClipRegion(shape);
        g.setColour(Pal::pink()); g.fillRect(c.withHeight(3.0f));
    }
    g.setColour(Pal::pink()); g.setFont(labelFont(12.0f).withExtraKerningFactor(0.08f));
    g.drawText("NEW VERSION", RectF(c.getX() + 22.0f, c.getY() + 20.0f, 300.0f, 14.0f), juce::Justification::centredLeft, false);
    g.setColour(Pal::pale()); g.setFont(PeepChainView::titleFont(26.0f));
    auto tag = release.tag.trim();
    if (!tag.startsWithIgnoreCase("v")) tag = "v" + tag;
    g.drawText(tag, RectF(c.getX() + 22.0f, c.getY() + 36.0f, c.getWidth() - 44.0f, 30.0f), juce::Justification::centredLeft, false);
    g.setColour(Pal::grey()); g.setFont(plainLabelFont(13.0f));
    g.drawText("You have v" + UpdateChecker::currentVersion(), RectF(c.getX() + 22.0f, c.getY() + 66.0f, c.getWidth() - 44.0f, 16.0f), juce::Justification::centredLeft, true);
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawRoundedRectangle(notes.getBounds().toFloat().expanded(0.5f), 4.0f, 1.0f);
    button(g, buttonBounds(kLater), "LATER", false, over == kLater);
    button(g, buttonBounds(kDownload), "DOWNLOAD", true, over == kDownload);
}

int UpdatePopup::buttonAt(PointF p) const {
    for (int b = 0; b < kNumButtons; ++b) if (buttonBounds(b).contains(p)) return b;
    return -1;
}

void UpdatePopup::mouseDown(const juce::MouseEvent& e) {
    pressed = buttonAt(e.position);
    if (pressed < 0 && !cardBounds().contains(e.position)) pressed = 100;          // outside the card: LATER
}

void UpdatePopup::mouseUp(const juce::MouseEvent& e) {
    const int was = std::exchange(pressed, -1);
    if (was == 100 && !cardBounds().contains(e.position)) press(kLater);
    else if (was >= 0 && buttonAt(e.position) == was) press(was);
}

void UpdatePopup::mouseMove(const juce::MouseEvent& e) {
    if (const int was = std::exchange(over, buttonAt(e.position)); was != over) repaint();
    setMouseCursor(over >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void UpdatePopup::mouseExit(const juce::MouseEvent&) { if (std::exchange(over, -1) >= 0) repaint(); }

bool UpdatePopup::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) { press(kLater); return true; }
    if (k == juce::KeyPress::returnKey) { press(kDownload); return true; }
    return false;
}

void UpdatePopup::press(int b) {
    auto& f = b == kDownload ? onDownload : onLater;
    if (f) f();
}

void PeepChainView::paintAutoGainCapsule(juce::Graphics& g) {
    const auto r = autoGainBounds();
    const bool on = proc.apvts.getRawParameterValue("autoGain")->load() > 0.5f;
    toggleSwitch(g, RectF(r.getX() + 2.0f, r.getCentreY() - 7.0f, 26.0f, 14.0f), on, overAutoGain);
    const float db = autoGainShownDb;
    g.setColour(on ? Pal::pale() : overAutoGain ? Pal::hot() : Pal::grey()); g.setFont(labelFont(12.0f));
    g.drawText(on ? "AUTO GAIN " + juce::String(db > 0.05f ? "+" : "") + juce::String(db, 1) + " DB" : juce::String("AUTO GAIN"),
               r.withTrimmedLeft(36.0f), juce::Justification::centredLeft, false);
}

// GitHub's mark (Octicons "mark-github", MIT), in a 16 x 16 box.
static const juce::Path& githubMark() {
    static const juce::Path p = juce::Drawable::parseSVGPath(
        "M8 0c4.42 0 8 3.58 8 8a8.013 8.013 0 0 1-5.45 7.59c-.4.08-.55-.17-.55-.38 0-.27.01-1.13.01-2.2 0-.75-.25-1.23-.54-1.48 "
        "1.78-.2 3.65-.88 3.65-3.95 0-.88-.31-1.59-.82-2.15.08-.2.36-1.02-.08-2.12 0 0-.67-.22-2.2.82-.64-.18-1.32-.27-2-.27"
        "-.68 0-1.36.09-2 .27-1.53-1.03-2.2-.82-2.2-.82-.44 1.1-.16 1.92-.08 2.12-.51.56-.82 1.28-.82 2.15 0 3.06 1.86 3.75 "
        "3.64 3.95-.23.2-.44.55-.51 1.07-.46.21-1.61.55-2.33-.66-.15-.24-.6-.83-1.23-.82-.67.01-.27.38.01.53.34.19.73.9.82 "
        "1.13.16.45.68 1.31 2.69.94 0 .67.01 1.3.01 1.49 0 .21-.15.45-.55.38A7.995 7.995 0 0 1 0 8c0-4.42 3.58-8 8-8Z");
    return p;
}

void PeepChainView::paintVersion(juce::Graphics& g) {
    g.setColour(Pal::grey()); g.setFont(labelFont(13.0f));
    g.drawText(versionText(), RectF(versionX(), kVersionY, 60.0f, 16.0f), juce::Justification::centredLeft, false);
    if (UpdateChecker::isConfigured()) {
        const auto r = githubBounds();
        g.setColour(overGithub ? Pal::hot() : Pal::grey());
        const auto icon = RectF(r.getX(), r.getY(), 12.0f, 12.0f).withCentre({r.getX() + 6.0f, r.getCentreY() - 0.5f});
        g.fillPath(githubMark(), githubMark().getTransformToScaleToFit(icon, true));
        g.drawText("GITHUB", r.withTrimmedLeft(16.0f), juce::Justification::centredLeft, false);
    }
    if (updates->getState() == UpdateChecker::State::available) {
        const auto r = updateBounds();
        g.setColour(overUpdate ? Pal::hot() : Pal::pink()); g.setFont(labelFont(12.0f));
        g.drawText("NEW VERSION " + updates->getLatestVersion().toUpperCase() + "  -  WHAT'S NEW", r, juce::Justification::centredLeft, false);
    }
}

bool PeepChainView::isControlEnabled(int c) const {
    if (c == kUndo) return proc.canUndo();
    if (c == kRedo) return proc.canRedo();
    if (c == kSlotA || c == kSlotB) return proc.getActiveSlot() != (c == kSlotA ? 0 : 1);
    if (c == kDelaySync || c == kReverbSync || c == kDelayEdit || c == kReverbEdit) return panel == nullptr && (c == kDelaySync || c == kDelayEdit || PeepChainProcessor::hasReverb());
    return c >= 0 && c < kNumControls;
}

int PeepChainView::controlAt(PointF p) const {
    for (int c = 0; c < kNumControls; ++c) if (controlBounds(c).contains(p) && isControlEnabled(c)) return c;
    return -1;
}

juce::uint32 PeepChainView::controlsKey() const {
    return (proc.canUndo() ? 1u : 0u) | (proc.canRedo() ? 2u : 0u) | (proc.getActiveSlot() == 1 ? 4u : 0u)
         | (proc.apvts.getRawParameterValue("delaySync")->load() > 0.5f ? 8u : 0u)
         | (proc.apvts.getRawParameterValue("reverbSync")->load() > 0.5f ? 16u : 0u);
}

void PeepChainView::activate(int c) {
    switch (c) {
        case kUndo: proc.undo(); break;
        case kRedo: proc.redo(); break;
        case kSlotA: proc.selectSlot(0); break;
        case kSlotB: proc.selectSlot(1); break;
        case kCopy: proc.copyToOtherSlot(); break;
        case kSize: if (onSizeClicked) onSizeClicked(); break;
        case kDelayEdit: case kReverbEdit: openEditor(EffectParams::kSends); break;
        case kDelaySync: case kReverbSync:
            if (auto* p = proc.apvts.getParameter(c == kDelaySync ? "delaySync" : "reverbSync")) {
                p->beginChangeGesture(); p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.0f : 1.0f); p->endChangeGesture();
            }
            break;
        default: break;
    }
    repaint();
}

void PeepChainView::paintControls(juce::Graphics& g) {
    // undo / redo
    for (int c : {(int)kUndo, (int)kRedo}) {
        const auto r = controlBounds(c);
        if (overControl == c) { g.setColour(Pal::track()); g.fillRoundedRectangle(r, 5.0f); }
        undoIcon(g, r, c == kRedo, !isControlEnabled(c) ? Pal::grey().withAlpha(0.3f) : overControl == c ? Pal::hot() : Pal::pale().withAlpha(0.85f));
    }
    // A | B: one segmented switch
    {
        const auto a = controlBounds(kSlotA), b = controlBounds(kSlotB), both = a.getUnion(b);
        g.setColour(Pal::bg().withAlpha(0.6f)); g.fillRoundedRectangle(both, 5.0f);
        g.setColour(Pal::edge().brighter(0.35f)); g.drawRoundedRectangle(both.reduced(0.5f), 5.0f, 1.0f);
        const auto active = proc.getActiveSlot() == 0 ? a : b;
        g.setColour(Pal::pink()); g.fillRoundedRectangle(active.reduced(2.0f), 3.5f);
        g.setFont(labelFont(13.0f));
        for (int c : {(int)kSlotA, (int)kSlotB}) {
            const bool on = (c == kSlotA) == (proc.getActiveSlot() == 0);
            g.setColour(on ? juce::Colours::white : overControl == c ? Pal::hot() : Pal::grey());
            g.drawText(c == kSlotA ? "A" : "B", controlBounds(c).translated(0.0f, 0.5f), juce::Justification::centred, false);
        }
    }
    button(g, controlBounds(kCopy), proc.getActiveSlot() == 0 ? "COPY A TO B" : "COPY B TO A", false, overControl == kCopy);
    {   // window size, with a little arrow for its menu
        const auto r = controlBounds(kSize);
        button(g, r, juce::String(juce::roundToInt(scaleShown * 100.0f)) + "%    ", false, overControl == kSize);
        juce::Path tri; const float tx = r.getRight() - 13.0f, ty = r.getCentreY();
        tri.addTriangle(tx - 3.5f, ty - 1.8f, tx + 3.5f, ty - 1.8f, tx, ty + 2.6f);
        g.setColour(overControl == kSize ? Pal::hot() : Pal::grey()); g.fillPath(tri);
    }
    if (panel != nullptr) return;                                 // the syncs belong to the knobs the panel covers
    button(g, controlBounds(kDelaySync), "SYNC", proc.apvts.getRawParameterValue("delaySync")->load() > 0.5f, overControl == kDelaySync, true, 11.0f);
    button(g, controlBounds(kDelayEdit), "EDIT", false, overControl == kDelayEdit, true, 11.0f);
    if (PeepChainProcessor::hasReverb()) {
        button(g, controlBounds(kReverbSync), "SYNC", proc.apvts.getRawParameterValue("reverbSync")->load() > 0.5f, overControl == kReverbSync, true, 11.0f);
        button(g, controlBounds(kReverbEdit), "EDIT", false, overControl == kReverbEdit, true, 11.0f);
    }
}

void PeepChainView::paint(juce::Graphics& g) {
    // The cached pictures are drawn at (at least) the screen's own resolution, so a bigger window stays sharp.
    const float want = juce::jmax(2.0f, std::ceil(g.getInternalContext().getPhysicalPixelScaleFactor() - 0.01f));
    if (want != imageScale) { imageScale = want; backdrop = {}; chainLayer = {}; liftedLayer = {}; }
    if (backdrop.isNull()) renderBackdrop();
    g.drawImage(backdrop, getLocalBounds().toFloat());

    paintResetCapsule(g);
    paintAutoGainCapsule(g);
    paintGroupButton(g);
    paintVersion(g);
    paintControls(g);
    paintChain(g);
    paintEditMarks(g);
    if (panel == nullptr) {                                       // the six knobs' labels, values and Peep's values
        const char* names[kKnobs] = {"INPUT", "GATE THRESHOLD", "COMP THRESHOLD", "DELAY", "REVERB", "OUTPUT"};
        const char* hints[kKnobs] = {"", "PEEP: -46.0", "PEEP: -26.0", "PEEP: -16.5", "PEEP: -6.0", ""};
        juce::Slider* knobs[kKnobs] = {&input, &gateThresh, &compThresh, &delayLevel, &reverbLevel, &output};
        const bool live[kKnobs] = {true, proc.isStageOn(PeepChain::kGate), proc.isStageOn(PeepChain::kComp), true, true, true};
        const float colW = kControlsArea.getWidth() / (float)kKnobs;
        for (int i = 0; i < kKnobs; ++i) {
            const auto col = RectF(kControlsArea.getX() + colW * (float)i, kControlsArea.getY(), colW, kControlsArea.getHeight());
            const float a = live[i] ? 1.0f : 0.4f;
            g.setColour(Pal::pale().withAlpha(0.9f * a)); g.setFont(labelFont(13.0f).withExtraKerningFactor(0.06f));
            g.drawText(names[i], col.withY(col.getY() + 12.0f).withHeight(16.0f), juce::Justification::centred, false);
            g.setColour(Pal::pale().withAlpha(a)); g.setFont(labelFont(17.0f));
            g.drawText(knobValueText(*knobs[i]), RectF(col.getX(), col.getY() + 124.0f, colW, 22.0f), juce::Justification::centred, false);
            g.setColour(Pal::grey().withAlpha(a)); g.setFont(labelFont(13.0f));
            g.drawText(hints[i], RectF(col.getX(), col.getY() + 146.0f, colW, 14.0f), juce::Justification::centred, false);
            if (i > 0) { g.setColour(Pal::edge()); g.fillRect(col.getX(), col.getY() + 18.0f, 1.0f, col.getHeight() - 36.0f); }
        }
    }
    paintMetersAndFooter(g);
}

void PeepChainView::paintMetersAndFooter(juce::Graphics& g) {
    const float W = (float)getWidth();
    paintLevelMeter(g, {kPad + 16.0f, kMeterY - 4.0f, W * 0.5f - kPad - 28.0f, 22.0f}, "IN", inDb, true);
    paintLevelMeter(g, {W * 0.5f + 12.0f, kMeterY - 4.0f, W * 0.5f - kPad - 28.0f, 22.0f}, "OUT", outDb, false);
}

//==============================================================================
namespace {
using EffectParams::Unit;
const char* const kStageTitles[7] = {"NOISE GATE", "COMPRESSOR", "FILTER", "GRAPHIC EQ", "MULTIBAND", "LIMITER", "DELAY & REVERB"};
const float kColW = 55.0f, kGroupGap = 18.0f, kMiniKnob = 46.0f;   // the row layout (Filter)
const float kGridKnob = 42.0f, kCellH = 70.0f;                       // the grid layouts: a knob with its label over it, value under
const float kPanelHead = 40.0f;
const char* const kBandNames[4] = {"LOW", "LOW MID", "HIGH MID", "HIGH"};
const float kGateLo = -80.0f, kMultiLo = -90.0f;                     // bottoms of the gate's and multiband's level scales (top: 0 dB)

/** An X, for closing. */
void closeIcon(juce::Graphics& g, RectF r, juce::Colour c) {
    const auto b = r.withSizeKeepingCentre(9.0f, 9.0f);
    g.setColour(c);
    juce::Path p; p.startNewSubPath(b.getTopLeft()); p.lineTo(b.getBottomRight()); p.startNewSubPath(b.getTopRight()); p.lineTo(b.getBottomLeft());
    g.strokePath(p, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

/** A dark display sunk into the panel. */
void well(juce::Graphics& g, RectF r) {
    g.setColour(Pal::well()); g.fillRoundedRectangle(r, 6.0f);
    g.setColour(juce::Colours::black.withAlpha(0.35f)); g.drawRoundedRectangle(r.reduced(0.5f), 6.0f, 1.0f);
}

/** A level history as a filled shape from the bottom: h[(start + i) % n], oldest first. */
juce::Path historyShape(RectF plot, const float* h, int n, int start, const std::function<float(float)>& y) {
    const float step = plot.getWidth() / (float)(n - 1);
    juce::Path p; p.startNewSubPath(plot.getX(), plot.getBottom());
    for (int i = 0; i < n; ++i) p.lineTo(plot.getX() + step * (float)i, y(h[(start + i) % n]));
    p.lineTo(plot.getRight(), plot.getBottom()); p.closeSubPath();
    return p;
}

/** dB grid lines with their numbers in the left margin of `area`. */
void dbGrid(juce::Graphics& g, RectF area, RectF plot, float lo, float hi, float step, const std::function<float(float)>& y) {
    g.setFont(plainLabelFont(11.0f));
    for (float d = hi - step; d > lo + 0.5f; d -= step) {
        g.setColour(Pal::edge()); g.fillRect(plot.getX(), y(d), plot.getWidth(), 1.0f);
        g.setColour(Pal::grey()); g.drawText(juce::String((int)d), RectF(area.getX() + 4.0f, y(d) - 6.0f, 26.0f, 12.0f), juce::Justification::centredLeft, false);
    }
}

/** A small key: a swatch and a word. Moves x on. */
void legendKey(juce::Graphics& g, float& x, float y, juce::Colour c, const juce::String& t) {
    g.setColour(c); g.fillRoundedRectangle(x, y + 3.0f, 10.0f, 8.0f, 2.0f);
    g.setColour(Pal::grey()); g.setFont(plainLabelFont(11.0f));
    g.drawText(t, RectF(x + 14.0f, y, 80.0f, 14.0f), juce::Justification::centredLeft, false);
    x += 18.0f + juce::GlyphArrangement::getStringWidth(plainLabelFont(11.0f), t) + 10.0f;
}

/** Text on a dark pill, so it stays readable over a picture. align: -1 left, 0 centre, +1 right of `at`. */
void pillText(juce::Graphics& g, PointF at, int align, const juce::String& t, juce::Colour ink, float fontH = 11.0f) {
    const auto f = labelFont(fontH);
    const float w = juce::GlyphArrangement::getStringWidth(f, t) + 10.0f, h = fontH + 4.0f;
    const float x = align < 0 ? at.x : align > 0 ? at.x - w : at.x - w * 0.5f;
    const RectF r(x, at.y, w, h);
    g.setColour(Pal::well().withAlpha(0.85f)); g.fillRoundedRectangle(r, 3.0f);
    g.setColour(ink); g.setFont(f); g.drawText(t, r, juce::Justification::centred, false);
}

void setPlainValue(juce::RangedAudioParameter* p, float plain) {
    if (p == nullptr) return;
    p->setValueNotifyingHost(p->convertTo0to1(plain));
}
}

StagePanel::StagePanel(PeepChainProcessor& p, int st) : proc(p), stage(st) {
    const auto& defs = EffectParams::all();
    for (int i : EffectParams::ofStage(stage)) {
        const auto& d = defs[(size_t)i];
        if (groups.empty() || groups.back().title != d.group) groups.push_back({d.group, {}, -1, {}, {}, {}});
        if (d.unit == Unit::Choice) { groups.back().choiceDef = i; continue; }
        Item it; it.def = i; it.slider = std::make_unique<juce::Slider>();
        auto& sl = *it.slider;
        if (stage == PeepChain::kGeq) sl.setSliderStyle(juce::Slider::LinearVertical);
        else { sl.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); sl.setRotaryParameters(kPi * 1.25f, kPi * 2.75f, true); }
        sl.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        sl.setPopupDisplayEnabled(false, false, nullptr);
        sl.getProperties().set("mini", true);
        sl.setTooltip(d.name + ". Peep's: " + EffectParams::format(d, d.peep, true) + ". Double-click for Peep's.");
        sl.onValueChange = [this] { repaint(); };
        addAndMakeVisible(sl);
        it.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, d.id, sl);
        groups.back().items.push_back((int)items.size());
        items.push_back(std::move(it));
    }
    histIn.fill(-120.0f); histOut.fill(-120.0f); histGr.fill(0.0f);
    lastBlocks = proc.meters.blocks.load();
    lastKey = stateKey();
    setWantsKeyboardFocus(true);
    updateVisibility();
    startTimerHz(hasLivePicture() ? 30 : 10);
}
StagePanel::~StagePanel() { stopTimer(); }

bool StagePanel::hasLivePicture() const {
    return stage == PeepChain::kGate || stage == PeepChain::kComp || stage == PeepChain::kMulti || stage == PeepChain::kLimit;
}

float StagePanel::value(int def) const {
    const auto& d = EffectParams::all()[(size_t)def];
    return (float)EffectParams::snap(d, proc.apvts.getRawParameterValue(d.id)->load());
}
float StagePanel::valueOf(const char* id) const { return value(EffectParams::indexOf(id)); }

int StagePanel::itemIndex(const juce::String& id) const {
    for (size_t k = 0; k < items.size(); ++k) if (EffectParams::all()[(size_t)items[k].def].id == id) return (int)k;
    return -1;
}

juce::Slider* StagePanel::sliderFor(const juce::String& id) {
    const int k = itemIndex(id);
    return k >= 0 ? items[(size_t)k].slider.get() : nullptr;
}

void StagePanel::updateVisibility() {
    if (stage != EffectParams::kSends) return;
    const bool synced = valueOf("delaySync") > 0.5f;            // TIME: milliseconds, or a note of the song when synced
    if (auto* s = sliderFor("delayTime")) s->setVisible(!synced);
    if (auto* s = sliderFor("delayNote")) s->setVisible(synced);
}

//------------------------------------------------------------------------------
// layout
void StagePanel::placeKnob(int item, RectF cell, bool labelled) {
    auto& it = items[(size_t)item];
    it.area = cell; it.labelled = labelled;
    const float cy = labelled ? cell.getY() + 15.0f + kGridKnob * 0.5f : cell.getY() + kGridKnob * 0.5f;
    it.slider->setBounds(RectF(kGridKnob, kGridKnob).withCentre({cell.getCentreX(), cy}).toNearestInt());
}

void StagePanel::resized() {
    const auto body = getLocalBounds().toFloat().withTrimmedTop(kPanelHead).reduced(16.0f, 8.0f);
    picture = {};
    switch (stage) {
        case PeepChain::kGate:  layoutGrid(body, {{"gateThresh", "gateReduction"}, {"gateAttack", "gateHold", "gateRelease"}}, 74.0f); break;
        case PeepChain::kComp:  layoutGrid(body, {{"compThresh", "compMakeup"}, {"compAttack", "compRelease"}}, 74.0f); break;
        case PeepChain::kLimit: layoutGrid(body, {{"limPregain", "limRelease"}}, 74.0f); break;
        case PeepChain::kMulti: layoutMultiband(body); break;
        case EffectParams::kSends: layoutSends(body); break;
        case PeepChain::kGeq: {                                   // 31 faders: value on top, frequency under
            const float w = body.getWidth() / (float)items.size();
            for (size_t k = 0; k < items.size(); ++k) {
                items[k].area = {body.getX() + w * (float)k, body.getY(), w, body.getHeight()};
                items[k].slider->setBounds(items[k].area.withTrimmedTop(14.0f).withTrimmedBottom(16.0f).toNearestInt());
            }
            if (!groups.empty()) groups[0].area = body;
            break;
        }
        default: layoutRow(body); break;
    }
    updateVisibility();
}

/** Groups of knobs side by side, each under its title (or its type switch): the Filter. */
void StagePanel::layoutRow(RectF body) {
    float total = 0.0f;
    for (auto& gr : groups) total += (float)gr.items.size() * kColW;
    total += kGroupGap * (float)(groups.size() - 1);
    float x = body.getCentreX() - total * 0.5f;
    const float h = 18.0f + 14.0f + kMiniKnob + 16.0f, top = body.getY() + (body.getHeight() - h) * 0.5f;
    for (auto& gr : groups) {
        gr.area = {x, top, (float)gr.items.size() * kColW, h};
        gr.choiceArea = gr.choiceDef >= 0 ? gr.area.withHeight(16.0f).reduced(4.0f, 0.0f) : RectF();
        for (int idx : gr.items) {
            auto& it = items[(size_t)idx];
            it.area = {x, top + 18.0f, kColW, h - 18.0f}; it.labelled = true;
            it.slider->setBounds(RectF(kMiniKnob, kMiniKnob).withCentre({x + kColW * 0.5f, top + 32.0f + kMiniKnob * 0.5f}).toNearestInt());
            x += kColW;
        }
        x += kGroupGap;
    }
}

/** Knobs in rows on the left (each with its name over it), the picture on the right. */
void StagePanel::layoutGrid(RectF body, const std::vector<std::vector<const char*>>& rows, float colW) {
    size_t cols = 0;
    for (auto& r : rows) cols = std::max(cols, r.size());
    const float gap = 8.0f, h = (float)rows.size() * kCellH + (float)(rows.size() - 1) * gap;
    const float top = body.getY() + (body.getHeight() - h) * 0.5f, x0 = body.getX();
    for (size_t r = 0; r < rows.size(); ++r) {
        const float indent = colW * (float)(cols - rows[r].size()) * 0.5f;   // a shorter row is centred over the others
        for (size_t c = 0; c < rows[r].size(); ++c)
            if (const int k = itemIndex(rows[r][c]); k >= 0)
                placeKnob(k, {x0 + indent + colW * (float)c, top + (kCellH + gap) * (float)r, colW, kCellH}, true);
    }
    picture = body.withTrimmedLeft(colW * (float)cols + 20.0f);
}

/** The four bands' thresholds and headrooms in a grid (a column a band, as in the picture), the times beside
 *  them, the picture on the right. */
void StagePanel::layoutMultiband(RectF body) {
    const float gutter = 70.0f, colW = 54.0f, head = 16.0f, rowH = 60.0f, gap = 8.0f;
    const float total = head + 2.0f * rowH + gap, top = body.getY() + (body.getHeight() - total) * 0.5f;
    const float x0 = body.getX() + gutter;
    for (int b = 0; b < 4; ++b) {
        placeKnob(itemIndex("mbThresh" + juce::String(b + 1)), {x0 + colW * (float)b, top + head, colW, rowH}, false);
        placeKnob(itemIndex("mbHeadroom" + juce::String(b + 1)), {x0 + colW * (float)b, top + head + rowH + gap, colW, rowH}, false);
    }
    for (int k = 1; k <= 3; ++k) if (auto* s = sliderFor("mbSplit" + juce::String(k))) s->setVisible(false);   // dragged in the picture
    const float tx = x0 + colW * 4.0f + 16.0f, tw = 62.0f, th = (total - gap) * 0.5f;
    placeKnob(itemIndex("mbAttack"), {tx, top, tw, th}, true);
    placeKnob(itemIndex("mbRelease"), {tx, top + th + gap, tw, th}, true);
    picture = body.withTrimmedLeft(tx + tw + 18.0f - body.getX());
}

/** Delay and Reverb side by side: each a title (with its SYNC), a picture, and a row of knobs. */
void StagePanel::layoutSends(RectF body) {
    const float colW = 62.0f, gap = 30.0f, titleH = 18.0f, picH = 46.0f;
    float total = 0.0f;
    for (auto& gr : groups) total += colW * (float)(gr.items.size() - (gr.title == "DELAY" ? 1 : 0));   // TIME and NOTE share a place
    total += gap * (float)(groups.size() - 1);
    const float h = titleH + 4.0f + picH + 8.0f + kCellH, top = body.getY() + (body.getHeight() - h) * 0.5f;
    float x = body.getCentreX() - total * 0.5f;
    for (auto& gr : groups) {
        float cx = x;
        for (int idx : gr.items) {
            const auto& id = EffectParams::all()[(size_t)items[(size_t)idx].def].id;
            if (id == "delayNote") { placeKnob(idx, items[(size_t)itemIndex("delayTime")].area, true); continue; }
            placeKnob(idx, {cx, top + titleH + 4.0f + picH + 8.0f, colW, kCellH}, true);
            cx += colW;
        }
        gr.area = {x, top, cx - x, h};
        gr.pic = {x + 4.0f, top + titleH + 4.0f, cx - x - 8.0f, picH};
        gr.choiceArea = gr.choiceDef >= 0 ? RectF(gr.area.getRight() - 58.0f, top, 54.0f, titleH) : RectF();
        x = cx + gap;
    }
}

RectF StagePanel::buttonBounds(int b) const {
    const float right = (float)getWidth() - 12.0f;
    if (b == kClose) return {right - 26.0f, 8.0f, 26.0f, 26.0f};
    if (b == kPeep) return {right - 26.0f - 10.0f - 116.0f, 10.0f, 116.0f, 22.0f};
    return {};
}

RectF StagePanel::choiceBounds(const juce::String& id) const {
    for (auto& gr : groups)
        if (gr.choiceDef >= 0 && EffectParams::all()[(size_t)gr.choiceDef].id == id) return gr.choiceArea;
    return {};
}

//------------------------------------------------------------------------------
// the pictures' draggable lines
RectF StagePanel::gatePlot() const { return picture.reduced(10.0f, 8.0f).withTrimmedLeft(24.0f); }
RectF StagePanel::multiPlot() const { return picture.reduced(10.0f, 8.0f).withTrimmedLeft(24.0f).withTrimmedBottom(14.0f); }
float StagePanel::levelY(RectF plot, float db) const {
    const float lo = stage == PeepChain::kMulti ? kMultiLo : kGateLo;
    return plot.getBottom() - plot.getHeight() * juce::jlimit(0.0f, 1.0f, (db - lo) / (0.0f - lo));
}
float StagePanel::multiX(double hz) const {
    const auto plot = multiPlot();
    return plot.getX() + plot.getWidth() * (float)juce::jlimit(0.0, 1.0, std::log(hz / 20.0) / std::log(1000.0));
}
double StagePanel::multiHz(float x) const {
    const auto plot = multiPlot();
    return 20.0 * std::pow(1000.0, (double)juce::jlimit(0.0f, 1.0f, (x - plot.getX()) / plot.getWidth()));
}

StagePanel::Target StagePanel::targetAt(PointF p) const {
    if (stage == PeepChain::kGate) {
        const auto plot = gatePlot();
        if (!plot.expanded(0.0f, 4.0f).contains(p)) return {};
        if (std::abs(p.y - levelY(plot, valueOf("gateThresh"))) < 5.0f) return {EffectParams::indexOf("gateThresh"), true};
        return {};
    }
    if (stage == PeepChain::kMulti) {
        const auto plot = multiPlot();
        if (!plot.expanded(4.0f).contains(p)) return {};
        double split[3];
        for (int k = 0; k < 3; ++k) split[k] = valueOf(("mbSplit" + juce::String(k + 1)).toRawUTF8());
        int best = -1; float bestD = 6.0f;
        for (int k = 0; k < 3; ++k) if (const float d = std::abs(p.x - multiX(split[k])); d < bestD) { bestD = d; best = k; }
        if (best >= 0) return {EffectParams::indexOf("mbSplit" + juce::String(best + 1)), false};
        int band = 0;
        while (band < 3 && p.x > multiX(split[band])) ++band;
        const auto id = "mbThresh" + juce::String(band + 1);
        if (std::abs(p.y - levelY(plot, valueOf(id.toRawUTF8()))) < 6.0f) return {EffectParams::indexOf(id), true};
    }
    return {};
}

double StagePanel::targetValueAt(const Target& t, PointF p) const {
    const auto& d = EffectParams::all()[(size_t)t.def];
    if (stage == PeepChain::kGate) {
        const auto plot = gatePlot();
        const double db = kGateLo + (plot.getBottom() - p.y) / plot.getHeight() * (0.0 - kGateLo);
        return juce::jlimit(d.min, d.max, db);
    }
    if (!t.vertical) {                                            // a crossover, kept between its neighbours
        const int k = d.id.getTrailingIntValue() - 1;
        const double lo = k > 0 ? valueOf(("mbSplit" + juce::String(k)).toRawUTF8()) : d.min;
        const double hi = k < 2 ? valueOf(("mbSplit" + juce::String(k + 2)).toRawUTF8()) : d.max;
        return juce::jlimit(lo, hi, multiHz(p.x));
    }
    const auto plot = multiPlot();
    return juce::jlimit(d.min, d.max, kMultiLo + (plot.getBottom() - p.y) / plot.getHeight() * (0.0 - kMultiLo));
}

juce::String StagePanel::dragTargetAt(PointF p) const {
    const auto t = targetAt(p);
    return t.def >= 0 ? EffectParams::all()[(size_t)t.def].id : juce::String();
}

//------------------------------------------------------------------------------
// mouse and keys
int StagePanel::hitTest(PointF p) const {
    for (int b = 0; b < kNumButtons; ++b)
        if (buttonBounds(b).contains(p) && (b != kPeep || !proc.isStageAtPeep(stage))) return b;
    for (auto& gr : groups)
        if (gr.choiceDef >= 0 && gr.choiceArea.contains(p)) return 100 + gr.choiceDef;
    return -1;
}

void StagePanel::activate(int what) {
    if (what == kPeep) proc.resetStageToPeep(stage);
    else if (what == kClose) { if (onClose) onClose(); return; }
    else if (what >= 100)
        if (auto* p = proc.apvts.getParameter(EffectParams::all()[(size_t)(what - 100)].id)) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.0f : 1.0f); p->endChangeGesture();
        }
    updateVisibility();
    repaint();
}

void StagePanel::mouseDown(const juce::MouseEvent& e) {
    pressed = hitTest(e.position);
    if (pressed >= 0) return;
    dragging = targetAt(e.position);
    if (dragging.def >= 0)
        if (auto* p = proc.apvts.getParameter(EffectParams::all()[(size_t)dragging.def].id)) p->beginChangeGesture();
}
void StagePanel::mouseDrag(const juce::MouseEvent& e) {
    if (dragging.def < 0) return;
    setPlainValue(proc.apvts.getParameter(EffectParams::all()[(size_t)dragging.def].id), (float)targetValueAt(dragging, e.position));
    repaint(picture.expanded(2.0f).toNearestInt());
}
void StagePanel::mouseUp(const juce::MouseEvent& e) {
    if (dragging.def >= 0) {
        if (auto* p = proc.apvts.getParameter(EffectParams::all()[(size_t)dragging.def].id)) p->endChangeGesture();
        dragging = {};
        mouseMove(e);
        return;
    }
    const int was = std::exchange(pressed, -1);
    if (was >= 0 && hitTest(e.position) == was) activate(was);
}
void StagePanel::mouseDoubleClick(const juce::MouseEvent& e) {          // a line back to Peep's
    const auto t = targetAt(e.position);
    if (t.def < 0) return;
    if (auto* p = proc.apvts.getParameter(EffectParams::all()[(size_t)t.def].id)) {
        p->beginChangeGesture(); p->setValueNotifyingHost(p->getDefaultValue()); p->endChangeGesture();
    }
}
void StagePanel::mouseMove(const juce::MouseEvent& e) {
    const int h = hitTest(e.position);
    const auto t = targetAt(e.position);
    if (std::exchange(over, h) != h || std::exchange(hovering, t).def != t.def) repaint();
    setMouseCursor(t.def >= 0 ? (t.vertical ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::LeftRightResizeCursor)
                   : h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}
void StagePanel::mouseExit(const juce::MouseEvent&) {
    if (std::exchange(over, -1) != -1 || std::exchange(hovering, Target()).def >= 0) repaint();
}
bool StagePanel::keyPressed(const juce::KeyPress& k) {
    if (k != juce::KeyPress::escapeKey) return false;
    if (onClose) onClose();
    return true;
}

juce::String StagePanel::getTooltip() {
    const auto p = getMouseXYRelative().toFloat();
    const int h = hitTest(p);
    if (h == kPeep) return "Put the " + juce::String(kStageTitles[stage]).toLowerCase() + "'s settings back to Peep's";
    if (h == kClose) return "Close (Esc)";
    if (h >= 100) {
        const auto& id = EffectParams::all()[(size_t)(h - 100)].id;
        if (id == "delaySync") return valueOf("delaySync") > 0.5f ? "The echo follows the song's tempo: TIME is a note. Click for a time in ms."
                                                                   : "Make the echo follow the song's tempo (TIME becomes a note). Peep's doesn't: 500 ms.";
        if (id == "reverbSync") return "Make the pre-delay follow the song's tempo: the same part of a beat it is at 120 bpm. Peep's doesn't.";
        return EffectParams::all()[(size_t)(h - 100)].name.upToLastOccurrenceOf(" Type", false, false)
               + ": shelf, or a cut (where GAIN sets the resonance). Click to switch.";
    }
    if (const auto t = targetAt(p); t.def >= 0)
        return "Drag to change the " + EffectParams::all()[(size_t)t.def].name.toLowerCase() + ". Double-click for Peep's.";
    if (stage == PeepChain::kGate && picture.contains(p))
        return "Grey: the level going in. Pink: what comes out. The gate opens above the solid line and closes below the dashed one.";
    if (stage == PeepChain::kMulti && picture.contains(p))
        return "Each band: its level (bar), threshold (line), headroom (shaded) and gain reduction (from the top).";
    return {};
}

juce::uint32 StagePanel::stateKey() const {
    juce::uint32 k = proc.isStageAtPeep(stage) ? 1u : 0u;
    for (auto& gr : groups) if (gr.choiceDef >= 0) k = k * 2 + (value(gr.choiceDef) > 0.5f ? 1u : 0u);
    return k;
}

void StagePanel::timerCallback() {                                // undo, A/B and the host can change what's shown
    if (const auto k = stateKey(); k != lastKey) { lastKey = k; updateVisibility(); repaint(); }
    if (stage == EffectParams::kSends) {                          // synced times follow the song
        if (std::exchange(shownDelayMs, proc.getDelayMs()) != shownDelayMs || std::exchange(shownPreDelayMs, proc.getReverbPreDelayMs()) != shownPreDelayMs)
            repaint();
        return;
    }
    if (!hasLivePicture()) return;
    // one step of the picture: the loudest since the last frame, or nothing when no audio came in
    auto& m = proc.meters;
    const auto blocks = m.blocks.load(std::memory_order_relaxed);
    const bool live = blocks != lastBlocks; lastBlocks = blocks;
    if (stage == PeepChain::kMulti) {
        for (int b = 0; b < 4; ++b) {
            const float lv = live ? m.take(m.bandLevelDb[b], -120.0f) : -120.0f, gr = live ? m.bandDb[b].load() : 0.0f;
            bandLevel[b] = lv > bandLevel[b] ? lv : juce::jmax(lv, bandLevel[b] - 1.5f);
            bandGr[b] = gr > bandGr[b] ? gr : juce::jmax(gr, bandGr[b] - 0.8f);
        }
        repaint(picture.expanded(2.0f).toNearestInt());
        return;
    }
    float in, out = -120.0f, gr;
    if (stage == PeepChain::kLimit) { in = m.take(m.limInDb, -120.0f); out = m.take(m.limOutDb, -120.0f); gr = m.take(m.limGrDb, 0.0f); }
    else if (stage == PeepChain::kGate) { in = m.take(m.gateInDb, -120.0f); gr = juce::jmin(120.0f, m.take(m.gateGrDb, 0.0f)); out = in - gr; }
    else { in = m.take(m.compLevelDb, -120.0f); gr = m.take(m.compGrDb, 0.0f); }
    if (!live) { in = out = -120.0f; gr = 0.0f; }
    histIn[(size_t)histPos] = in; histOut[(size_t)histPos] = out; histGr[(size_t)histPos] = gr;
    histPos = (histPos + 1) % kHistory;
    levelNow = in;
    repaint(picture.expanded(2.0f).toNearestInt());
}

//------------------------------------------------------------------------------
// pictures
void StagePanel::paintGatePicture(juce::Graphics& g) {
    well(g, picture);
    const auto plot = gatePlot();
    auto y = [&](float db) { return levelY(plot, db); };
    dbGrid(g, picture, plot, kGateLo, 0.0f, 20.0f, y);
    g.setColour(Pal::grey().withAlpha(0.35f)); g.fillPath(historyShape(plot, histIn.data(), kHistory, histPos, y));
    g.setGradientFill(juce::ColourGradient(Pal::hot().withAlpha(0.9f), 0.0f, plot.getY(), Pal::pink().withAlpha(0.35f), 0.0f, plot.getBottom(), false));
    g.fillPath(historyShape(plot, histOut.data(), kHistory, histPos, y));
    // the two thresholds: opens above the solid line, closes below the dashed one
    const float thr = valueOf("gateThresh"), close = thr + (float)NoiseGate::Settings().hysteresisDb;   // Peep's hysteresis, fixed
    const bool hotT = hovering.def == EffectParams::indexOf("gateThresh") || dragging.def == EffectParams::indexOf("gateThresh");
    g.setFont(labelFont(11.0f));
    g.setColour(Pal::pale().withAlpha(0.7f));
    const float dash[2] = {4.0f, 3.0f};
    g.drawDashedLine({plot.getX(), y(close), plot.getRight(), y(close)}, dash, 2, 1.0f);
    g.setColour(hotT ? Pal::hot() : Pal::pink()); g.fillRect(plot.getX(), y(thr) - (hotT ? 1.0f : 0.75f), plot.getWidth(), hotT ? 2.0f : 1.5f);
    pillText(g, {plot.getRight() - 4.0f, y(close) + 3.0f}, 1, "CLOSE " + juce::String(close, 1), Pal::pale());
    pillText(g, {plot.getRight() - 4.0f, y(thr) - 18.0f}, 1, "OPEN " + juce::String(thr, 1), hotT ? Pal::hot() : Pal::pink());
    // open or closed now
    const float grNow = histGr[(size_t)((histPos + kHistory - 1) % kHistory)];
    const bool open = histIn[(size_t)((histPos + kHistory - 1) % kHistory)] > -119.0f && grNow < 1.0f;
    g.setColour(open ? Pal::pink() : Pal::grey()); g.setFont(labelFont(12.0f));
    g.drawText(open ? "OPEN" : "CLOSED", RectF(plot.getX() + 6.0f, plot.getY() + 2.0f, 80.0f, 14.0f), juce::Justification::centredLeft, false);
    float lx = plot.getX() + 6.0f;
    legendKey(g, lx, plot.getBottom() - 15.0f, Pal::grey().withAlpha(0.6f), "IN");
    legendKey(g, lx, plot.getBottom() - 15.0f, Pal::pink(), "OUT");
}

void StagePanel::paintCompPicture(juce::Graphics& g) {
    auto area = picture;
    // its curve: level in (RMS) across, level out up, with where the level is now
    const auto box = area.removeFromLeft(area.getHeight());
    area.removeFromLeft(14.0f);
    well(g, box);
    const auto plot = box.reduced(8.0f);
    Compressor::Settings s; s.thresholdDb = valueOf("compThresh");
    Compressor c; c.prepare(44100.0, s, Compressor::Fit());
    auto px = [&](double db) { return plot.getX() + plot.getWidth() * (float)((db + 60.0) / 60.0); };
    auto py = [&](double db) { return plot.getBottom() - plot.getHeight() * (float)((db + 60.0) / 60.0); };
    g.setColour(Pal::edge());
    for (int d = -48; d < 0; d += 12) { g.fillRect(px(d), plot.getY(), 1.0f, plot.getHeight()); g.fillRect(plot.getX(), py(d), plot.getWidth(), 1.0f); }
    g.setColour(Pal::grey().withAlpha(0.5f));                     // 1:1
    const float dash[2] = {3.0f, 3.0f};
    g.drawDashedLine({px(-60), py(-60), px(0), py(0)}, dash, 2, 1.0f);
    juce::Path curve;
    for (int k = 0; k <= 60; ++k) {
        const double in = -60.0 + k, out = in + c.curve(in);
        if (k == 0) curve.startNewSubPath(px(in), py(out)); else curve.lineTo(px(in), juce::jmin(plot.getBottom(), py(out)));
    }
    g.setColour(Pal::hot()); g.strokePath(curve, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (levelNow > -60.0f) {
        const double in = juce::jmin(0.0, (double)levelNow);
        const PointF dot(px(in), py(in + c.curve(in)));
        juce::Path d; d.addEllipse(RectF(9.0f, 9.0f).withCentre(dot));
        juce::DropShadow(Pal::pink(), 8, {}).drawForPath(g, d);
        g.setColour(juce::Colours::white); g.fillPath(d);
    }
    g.setColour(Pal::grey()); g.setFont(plainLabelFont(11.0f));
    g.drawText("IN", RectF(plot.getRight() - 20.0f, plot.getBottom() - 14.0f, 20.0f, 12.0f), juce::Justification::centredRight, false);
    g.drawText("OUT", RectF(plot.getX() + 2.0f, plot.getY(), 30.0f, 12.0f), juce::Justification::centredLeft, false);

    // the history: level, and gain reduction hanging from the top
    well(g, area);
    const auto hp = area.reduced(10.0f, 8.0f).withTrimmedLeft(24.0f);
    auto y = [&](float db) { return hp.getBottom() - hp.getHeight() * juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f); };
    dbGrid(g, area, hp, -60.0f, 0.0f, 12.0f, y);
    g.setColour(Pal::grey().withAlpha(0.35f)); g.fillPath(historyShape(hp, histIn.data(), kHistory, histPos, y));
    picture = area; paintLimitPicture(g); picture = area.getUnion(box);   // the shared gain-reduction overlay
}

/** The Limiter's history (in, out, gain reduction); also the Compressor's gain-reduction overlay. */
void StagePanel::paintLimitPicture(juce::Graphics& g) {
    const bool lim = stage == PeepChain::kLimit;
    const auto area = picture;
    const auto plot = area.reduced(10.0f, 8.0f).withTrimmedLeft(24.0f);
    const float step = plot.getWidth() / (float)(kHistory - 1);
    if (lim) {
        well(g, area);
        auto y = [&](float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit(0.0f, 1.0f, (db + 36.0f) / 48.0f); };
        g.setFont(plainLabelFont(11.0f));
        for (float d = -24.0f; d <= 12.0f; d += 12.0f) {          // dB grid; 0 dB is the ceiling
            const bool ceiling = d == 0.0f;
            g.setColour(ceiling ? Pal::pale().withAlpha(0.45f) : Pal::edge()); g.fillRect(plot.getX(), y(d), plot.getWidth(), 1.0f);
            g.setColour(ceiling ? Pal::pale() : Pal::grey());
            g.drawText(juce::String((int)d), RectF(area.getX() + 4.0f, y(d) - 6.0f, 26.0f, 12.0f), juce::Justification::centredLeft, false);
        }
        g.setColour(Pal::grey().withAlpha(0.35f)); g.fillPath(historyShape(plot, histIn.data(), kHistory, histPos, y));
        g.setGradientFill(juce::ColourGradient(Pal::hot().withAlpha(0.9f), 0.0f, plot.getY(), Pal::pink().withAlpha(0.35f), 0.0f, plot.getBottom(), false));
        g.fillPath(historyShape(plot, histOut.data(), kHistory, histPos, y));
    }
    // gain reduction, hanging from the top: 0 at the top, 12 dB at the middle
    juce::Path grLine;
    const float grH = plot.getHeight() * 0.5f;
    for (int i = 0; i < kHistory; ++i) {
        const float v = histGr[(size_t)((histPos + i) % kHistory)];
        const PointF pt(plot.getX() + step * (float)i, plot.getY() + grH * juce::jlimit(0.0f, 1.0f, v / 12.0f));
        if (i == 0) grLine.startNewSubPath(pt); else grLine.lineTo(pt);
    }
    { juce::Path fill(grLine); fill.lineTo(plot.getRight(), plot.getY()); fill.lineTo(plot.getX(), plot.getY()); fill.closeSubPath();
      g.setColour(Pal::pink().withAlpha(0.25f)); g.fillPath(fill); }
    g.setColour(Pal::pink()); g.strokePath(grLine, juce::PathStrokeType(1.6f));
    const float grNow = histGr[(size_t)((histPos + kHistory - 1) % kHistory)];
    pillText(g, {plot.getRight() - 2.0f, plot.getY() + 2.0f}, 1, "GAIN REDUCTION " + juce::String(grNow > 0.05f ? -grNow : 0.0f, 1) + " dB", Pal::pale(), 12.0f);
    float lx = plot.getX() + 6.0f;
    legendKey(g, lx, plot.getBottom() - 15.0f, Pal::grey().withAlpha(0.6f), lim ? "IN" : "LEVEL");
    if (lim) legendKey(g, lx, plot.getBottom() - 15.0f, Pal::pink(), "OUT");
}

void StagePanel::paintMultiPicture(juce::Graphics& g) {
    well(g, picture);
    const auto plot = multiPlot();
    auto y = [&](float db) { return levelY(plot, db); };
    dbGrid(g, picture, plot, kMultiLo, 0.0f, 20.0f, y);
    g.setFont(plainLabelFont(11.0f));                              // frequency grid
    for (double f : {100.0, 1000.0, 10000.0}) {
        const float x = multiX(f);
        g.setColour(Pal::edge().withAlpha(0.6f)); g.fillRect(x, plot.getY(), 1.0f, plot.getHeight());
        g.setColour(Pal::grey()); g.drawText(f >= 1000.0 ? juce::String((int)(f / 1000.0)) + "K" : juce::String((int)f),
                                             RectF(x - 20.0f, plot.getBottom() + 2.0f, 40.0f, 12.0f), juce::Justification::centred, false);
    }
    double split[3];
    for (int k = 0; k < 3; ++k) split[k] = valueOf(("mbSplit" + juce::String(k + 1)).toRawUTF8());
    const auto mouse = getMouseXYRelative().toFloat();
    for (int b = 0; b < 4; ++b) {
        const float x0 = b == 0 ? plot.getX() : multiX(split[b - 1]), x1 = b == 3 ? plot.getRight() : multiX(split[b]);
        const RectF reg(x0, plot.getY(), juce::jmax(0.0f, x1 - x0), plot.getHeight());
        if (reg.getWidth() < 1.0f) continue;
        if (isMouseOver() && reg.contains(mouse)) { g.setColour(juce::Colours::white.withAlpha(0.03f)); g.fillRect(reg); }
        const auto tid = "mbThresh" + juce::String(b + 1);
        const float thr = valueOf(tid.toRawUTF8()), hr = valueOf(("mbHeadroom" + juce::String(b + 1)).toRawUTF8());
        // headroom: where the band's level is squeezed into (above the threshold, up to threshold + headroom)
        g.setColour(Pal::pink().withAlpha(0.08f)); g.fillRect(reg.reduced(1.0f, 0.0f).withTop(y(juce::jmin(0.0f, thr + hr))).withBottom(y(thr)));
        // the band's level now: grey up to the threshold, pink above it
        const float lv = bandLevel[b], bw = juce::jmin(20.0f, reg.getWidth() * 0.4f);
        if (lv > kMultiLo) {
            const auto bar = RectF(bw, 0.0f).withCentre({reg.getCentreX(), 0.0f}).withTop(y(lv)).withBottom(plot.getBottom());
            g.setColour(Pal::pale().withAlpha(0.22f)); g.fillRect(bar.withTop(juce::jmax(bar.getY(), y(thr))));
            if (lv > thr) { g.setColour(Pal::pink().withAlpha(0.75f)); g.fillRect(bar.withBottom(y(thr))); }
        }
        // gain reduction, hanging from the top (24 dB = 40 % of the height)
        if (bandGr[b] > 0.05f) {
            const float h = plot.getHeight() * 0.4f * juce::jmin(1.0f, bandGr[b] / 24.0f);
            g.setColour(Pal::pink().withAlpha(0.28f)); g.fillRect(reg.reduced(3.0f, 0.0f).withHeight(h));
            g.setColour(Pal::pink()); g.fillRect(reg.reduced(3.0f, 0.0f).withY(plot.getY() + h - 1.0f).withHeight(1.5f));
        }
        if (reg.getWidth() > 30.0f)
            pillText(g, {reg.getCentreX(), plot.getY() + 2.0f}, 0, bandGr[b] > 0.05f ? juce::String(-bandGr[b], 1) : juce::String("0.0"),
                     bandGr[b] > 0.3f ? Pal::pale() : Pal::grey());
        // the threshold: a line to drag
        const bool hot = hovering.def == EffectParams::indexOf(tid) || dragging.def == EffectParams::indexOf(tid);
        g.setColour(hot ? Pal::hot() : Pal::pink()); g.fillRect(reg.reduced(2.0f, 0.0f).withY(y(thr) - (hot ? 1.25f : 0.75f)).withHeight(hot ? 2.5f : 1.5f));
    }
    // the crossovers: lines to drag sideways, with their frequency on a tag at the top
    for (int k = 0; k < 3; ++k) {
        const float x = multiX(split[k]);
        const auto sid = "mbSplit" + juce::String(k + 1);
        const bool hot = hovering.def == EffectParams::indexOf(sid) || dragging.def == EffectParams::indexOf(sid);
        g.setColour(hot ? Pal::hot() : Pal::pale().withAlpha(0.45f)); g.fillRect(x - (hot ? 1.0f : 0.5f), plot.getY(), hot ? 2.0f : 1.0f, plot.getHeight());
        const auto& d = EffectParams::all()[(size_t)EffectParams::indexOf(sid)];
        const auto t = EffectParams::format(d, split[k], false);
        const float tw = juce::GlyphArrangement::getStringWidth(labelFont(11.0f), t) + 10.0f;
        const auto tag = RectF(tw, 14.0f).withCentre({x, plot.getBottom() - 10.0f});
        g.setColour(hot ? Pal::pink() : Pal::track()); g.fillRoundedRectangle(tag, 3.0f);
        g.setColour(hot ? juce::Colours::white : Pal::pale()); g.setFont(labelFont(11.0f)); g.drawText(t, tag, juce::Justification::centred, false);
    }
}

void StagePanel::paintSendsPictures(juce::Graphics& g) {
    const auto s = proc.sendSettings();
    for (auto& gr : groups) {
        const auto r = gr.pic;
        well(g, r);
        const auto plot = r.reduced(8.0f, 6.0f).withTrimmedTop(12.0f);
        g.setFont(labelFont(11.0f));
        if (gr.title == "DELAY") {                                // the echoes: when they come and how loud
            const bool synced = valueOf("delaySync") > 0.5f;
            const double t = synced && shownDelayMs > 0.0f ? shownDelayMs * 1e-3 : s.delayTime;
            const double span = juce::jmax(2.0, t * 4.2);
            const double fb = SendDelay::Settings().feedback * s.delayFeedbackPct / 38.0;
            g.setColour(Pal::grey().withAlpha(0.5f)); g.fillRoundedRectangle(plot.getX(), plot.getY(), 3.0f, plot.getHeight(), 1.5f);   // the voice
            double db = 0.0;
            for (int k = 1; k * t <= span && db > -48.0; ++k, db += 20.0 * std::log10(juce::jmax(1e-6, fb))) {
                const float x = plot.getX() + plot.getWidth() * (float)(k * t / span);
                const float h = plot.getHeight() * 0.8f * (float)(1.0 + db / 48.0);
                g.setColour(Pal::pink().withAlpha(k == 1 ? 1.0f : 0.75f)); g.fillRoundedRectangle(x - 1.5f, plot.getBottom() - h, 3.0f, h, 1.5f);
            }
            g.setColour(Pal::pale());
            const auto label = synced ? EffectParams::noteNames()[s.delayNote] + "  " + juce::String(juce::roundToInt(t * 1000.0)) + " ms"
                                      : juce::String(juce::roundToInt(t * 1000.0)) + " ms";
            g.drawText(label, r.reduced(8.0f, 3.0f).withHeight(13.0f), juce::Justification::centredRight, false);
        } else {                                                  // the reverb: a gap, then its decay
            const bool synced = valueOf("reverbSync") > 0.5f;
            const double pre = synced && shownPreDelayMs > 0.0f ? shownPreDelayMs * 1e-3 : s.reverbPreDelay, len = s.reverbLength;
            const double span = 3.0;
            auto x = [&](double sec) { return plot.getX() + plot.getWidth() * (float)juce::jlimit(0.0, 1.0, sec / span); };
            auto yy = [&](double db) { return plot.getBottom() - plot.getHeight() * 0.9f * (float)juce::jlimit(0.0, 1.0, 1.0 + db / 60.0); };
            juce::Path env; env.startNewSubPath(x(pre), plot.getBottom()); env.lineTo(x(pre), yy(0.0));
            env.lineTo(x(pre + len), yy(-60.0)); env.lineTo(x(pre + len), plot.getBottom()); env.closeSubPath();
            g.setGradientFill(juce::ColourGradient(Pal::pink().withAlpha(0.55f), 0.0f, plot.getY(), Pal::pink().withAlpha(0.08f), 0.0f, plot.getBottom(), false));
            g.fillPath(env);
            g.setColour(Pal::hot()); g.drawLine(x(pre), yy(0.0), x(pre + len), yy(-60.0), 1.5f);
            g.setColour(Pal::grey().withAlpha(0.5f)); g.fillRoundedRectangle(plot.getX(), plot.getY(), 3.0f, plot.getHeight(), 1.5f);
            g.setColour(Pal::pale());
            g.drawText(juce::String(juce::roundToInt(pre * 1000.0)) + " ms  " + juce::String(len, 2) + " s", r.reduced(8.0f, 3.0f).withHeight(13.0f),
                       juce::Justification::centredRight, false);
        }
    }
}

//------------------------------------------------------------------------------
void StagePanel::paintKnobTexts(juce::Graphics& g) {
    const auto& defs = EffectParams::all();
    auto valueColour = [&](int def) { return std::abs(value(def) - (float)defs[(size_t)def].peep) > 1e-6f ? Pal::hot() : Pal::pale(); };
    for (auto& gr : groups) {
        const bool cut = gr.choiceDef >= 0 && value(gr.choiceDef) > 0.5f;
        if (gr.choiceDef >= 0 && stage == PeepChain::kFilter) {   // the Filter's group title is its type switch
            const auto& d = defs[(size_t)gr.choiceDef];
            const auto r = gr.choiceArea;                         // looks like a little dropdown
            button(g, r, d.choices[cut ? 1 : 0].toUpperCase() + "    ", false, over == 100 + gr.choiceDef, true, 11.0f);
            juce::Path tri; const float tx = r.getRight() - 11.0f, ty = r.getCentreY();
            tri.addTriangle(tx - 3.0f, ty - 1.5f, tx + 3.0f, ty - 1.5f, tx, ty + 2.2f);
            g.setColour(over == 100 + gr.choiceDef ? Pal::hot() : Pal::grey()); g.fillPath(tri);
        } else if (stage == EffectParams::kSends) {               // DELAY / REVERB, and its SYNC
            g.setColour(Pal::pale()); g.setFont(labelFont(13.0f).withExtraKerningFactor(0.06f));
            g.drawText(gr.title, gr.area.withHeight(18.0f).translated(4.0f, 0.0f), juce::Justification::centredLeft, false);
            if (gr.choiceDef >= 0) button(g, gr.choiceArea, "SYNC", value(gr.choiceDef) > 0.5f, over == 100 + gr.choiceDef, true, 11.0f);
        } else if (stage == PeepChain::kFilter && groups.size() > 1) {
            g.setColour(Pal::pale().withAlpha(0.8f)); g.setFont(labelFont(12.0f).withExtraKerningFactor(0.06f));
            g.drawText(gr.title, gr.area.withHeight(16.0f), juce::Justification::centred, false);
            g.setColour(Pal::edge().brighter(0.2f)); g.fillRect(gr.area.getX() + 6.0f, gr.area.getY() + 16.0f, gr.area.getWidth() - 12.0f, 1.0f);
        }
        for (int idx : gr.items) {
            const auto& it = items[(size_t)idx];
            if (!it.slider->isVisible()) continue;
            const auto& d = defs[(size_t)it.def];
            if (it.labelled) {
                const juce::String label = cut && d.label == "GAIN" ? juce::String("RESO") : d.label;
                g.setColour(Pal::grey()); g.setFont(labelFont(12.0f));
                g.drawText(label, it.area.withHeight(13.0f).expanded(6.0f, 0.0f), juce::Justification::centred, false);
            }
            g.setColour(valueColour(it.def)); g.setFont(labelFont(13.0f));
            g.drawText(EffectParams::format(d, value(it.def), d.unit != Unit::dB), it.area.withTop(it.area.getBottom() - 15.0f).expanded(6.0f, 0.0f),
                       juce::Justification::centred, false);
        }
    }
    if (stage == PeepChain::kMulti) {                             // the grid's band names and row names
        const auto& t1 = items[(size_t)itemIndex("mbThresh1")].area, &h1 = items[(size_t)itemIndex("mbHeadroom1")].area;
        g.setColour(Pal::grey()); g.setFont(labelFont(12.0f));
        g.drawText("THRESHOLD", RectF(t1.getX() - 72.0f, t1.getY() + 12.0f, 68.0f, 16.0f), juce::Justification::centredRight, false);
        g.drawText("HEADROOM", RectF(h1.getX() - 72.0f, h1.getY() + 12.0f, 68.0f, 16.0f), juce::Justification::centredRight, false);
        const auto mouse = getMouseXYRelative().toFloat();
        double split[3];
        for (int k = 0; k < 3; ++k) split[k] = valueOf(("mbSplit" + juce::String(k + 1)).toRawUTF8());
        int bandUnder = -1;                                      // the band under the mouse in the picture, lit in the grid too
        if (isMouseOver() && multiPlot().contains(mouse)) { bandUnder = 0; while (bandUnder < 3 && mouse.x > multiX(split[bandUnder])) ++bandUnder; }
        for (int b = 0; b < 4; ++b) {
            const auto& a = items[(size_t)itemIndex("mbThresh" + juce::String(b + 1))].area;
            g.setColour(b == bandUnder ? Pal::hot() : Pal::pale().withAlpha(0.85f)); g.setFont(labelFont(12.0f));
            g.drawText(kBandNames[b], RectF(a.getX() - 6.0f, a.getY() - 16.0f, a.getWidth() + 12.0f, 14.0f), juce::Justification::centred, false);
        }
    }
}

void StagePanel::paint(juce::Graphics& g) {
    const auto& defs = EffectParams::all();
    const auto b = getLocalBounds().toFloat();
    card(g, b, 10.0f, Pal::pink().withAlpha(0.45f));

    // header: what this is, PEEP'S and X
    g.setColour(Pal::pink()); g.fillRoundedRectangle(16.0f, 14.0f, 3.0f, 14.0f, 1.5f);
    g.setColour(Pal::pale()); g.setFont(labelFont(16.0f).withExtraKerningFactor(0.06f));
    g.drawText(kStageTitles[stage], RectF(26.0f, 10.0f, 300.0f, 22.0f), juce::Justification::centredLeft, false);
    g.setColour(Pal::edge()); g.fillRect(14.0f, kPanelHead, b.getWidth() - 28.0f, 1.0f);
    const bool atPeep = proc.isStageAtPeep(stage);
    button(g, buttonBounds(kPeep), atPeep ? "AT PEEP'S" : "BACK TO PEEP'S", false, over == kPeep, !atPeep);
    {
        const auto r = buttonBounds(kClose);
        if (over == kClose) { g.setColour(Pal::track()); g.fillRoundedRectangle(r, 5.0f); }
        closeIcon(g, r, over == kClose ? Pal::hot() : Pal::pale().withAlpha(0.85f));
    }

    switch (stage) {
        case PeepChain::kGate:  paintGatePicture(g); break;
        case PeepChain::kComp:  paintCompPicture(g); break;
        case PeepChain::kLimit: paintLimitPicture(g); break;
        case PeepChain::kMulti: paintMultiPicture(g); break;
        case EffectParams::kSends: paintSendsPictures(g); break;
        default: break;
    }
    if (stage == PeepChain::kGeq) {
        auto valueColour = [&](int def) { return std::abs(value(def) - (float)defs[(size_t)def].peep) > 1e-6f ? Pal::hot() : Pal::pale(); };
        for (auto& it : items) {
            const auto& d = defs[(size_t)it.def];
            const float v = value(it.def);
            g.setColour(v == 0.0f ? Pal::grey() : valueColour(it.def)); g.setFont(plainLabelFont(12.0f));
            g.drawText(v == 0.0f ? juce::String("0") : EffectParams::format(d, v, false), it.area.withHeight(13.0f), juce::Justification::centred, false);
            g.setColour(Pal::pale().withAlpha(0.8f)); g.setFont(plainLabelFont(12.0f));
            g.drawText(d.label, it.area.withTop(it.area.getBottom() - 14.0f), juce::Justification::centred, false);
        }
        return;
    }
    paintKnobTexts(g);
}

//==============================================================================
static juce::PropertiesFile::Options settingsFileOptions() {
    juce::PropertiesFile::Options o;
    o.applicationName = "PeepChain"; o.filenameSuffix = ".settings";
    o.folderName = "PeepChain"; o.osxLibrarySubFolder = "Application Support";
    return o;
}
static constexpr int kScalePercents[] = {75, 100, 125, 150, 175, 200};

PeepChainEditor::PeepChainEditor(PeepChainProcessor& p) : AudioProcessorEditor(&p), view(p) {
    addAndMakeVisible(view);
    view.onSizeClicked = [this] { showSizeMenu(); };
    juce::PropertiesFile settings(settingsFileOptions());
    setUiScale((float)settings.getDoubleValue("uiScale", 1.0), false);
}

void PeepChainEditor::setUiScale(float s, bool remember) {
    s = juce::jlimit(0.75f, 2.0f, s);
    const bool changed = std::abs(s - uiScale) > 1e-4f;
    uiScale = s;
    view.setTransform(juce::AffineTransform::scale(s));
    view.setScaleShown(s);
    setSize(juce::roundToInt((float)PeepChainView::kWidth * s), juce::roundToInt((float)PeepChainView::kHeight * s));
    if (changed && remember) { juce::PropertiesFile settings(settingsFileOptions()); settings.setValue("uiScale", (double)s); settings.saveIfNeeded(); }
}

void PeepChainEditor::showSizeMenu() {
    juce::PopupMenu m;
    m.addSectionHeader("Window size");
    for (int pct : kScalePercents) m.addItem(pct, juce::String(pct) + " %", true, juce::roundToInt(uiScale * 100.0f) == pct);
    const auto area = view.localAreaToGlobal(view.controlBounds(PeepChainView::kSize)).toNearestInt();
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&view).withTargetScreenArea(area),
                    [safe = juce::Component::SafePointer<PeepChainEditor>(this)](int result) {
                        if (safe != nullptr && result > 0) safe->setUiScale((float)result / 100.0f);
                    });
}
