#include "PluginEditor.h"
#include "BinaryData.h"

using Pal = PeepChainView::Palette;
using RectF = juce::Rectangle<float>;
using PointF = juce::Point<float>;
static constexpr float kPi = juce::MathConstants<float>::pi;

namespace {
const float kW = (float)PeepChainView::kWidth, kH = (float)PeepChainView::kHeight;
const float kBarX = 28.0f, kBarY = 160.0f, kBarH = 142.0f, kSeam = 2.0f;
const float kKnobSize = 112.0f, kKnobY = 352.0f;
const int kKnobs = 6;
const float kMeterY = 528.0f;
const char* const kStageNames[6] = {"GATE", "COMP", "FILTER", "EQ", "MULTI", "LIMIT"};
}

// Built-in fonts (SIL Open Font License, assets/fonts/). The design was drawn with macOS's Luminari and DIN Condensed,
// which other systems don't have; these are open look-alikes. Their ascent / descent are overridden (in em) so
// that, at the same font height, the lettering is as wide as the original's (measured over the plugin's own
// words) and the capitals sit at the same height in their box, so everything lines up where it was drawn.
struct EmbeddedFonts {
    juce::Typeface::Ptr title = juce::Typeface::createSystemTypefaceFor(BinaryData::AlmendraRegular_ttf, (size_t)BinaryData::AlmendraRegular_ttfSize);
    juce::Typeface::Ptr label = juce::Typeface::createSystemTypefaceFor(BinaryData::BarlowCondensedSemiBold_ttf, (size_t)BinaryData::BarlowCondensedSemiBold_ttfSize);
};
juce::Font PeepChainView::titleFont(float h) {                     // Almendra in place of Luminari
    juce::SharedResourcePointer<EmbeddedFonts> f;
    return juce::Font(juce::FontOptions(f->title).withAscentOverride(0.8547f).withDescentOverride(0.3256f).withHeight(h));
}
juce::Font PeepChainView::plainLabelFont(float h) {                // Barlow Condensed SemiBold in place of DIN Condensed Bold
    juce::SharedResourcePointer<EmbeddedFonts> f;
    return juce::Font(juce::FontOptions(f->label).withAscentOverride(0.7467f).withDescentOverride(0.3675f).withHeight(h));
}
juce::Font PeepChainView::labelFont(float h) { return plainLabelFont(h).withExtraKerningFactor(0.12f); }

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
static juce::Path textPath(const juce::String& text, const juce::Font& font, RectF area, juce::Justification j) {
    juce::GlyphArrangement ga;
    ga.addFittedText(font, text, area.getX(), area.getY(), area.getWidth(), area.getHeight(), j, 1, 1.0f);
    juce::Path p; ga.createPath(p);
    return p;
}

/** Neon tube: soft coloured halo, a saturated body and a hot, almost white core. */
static void neonPath(juce::Graphics& g, const juce::Path& p, juce::Colour c, float glow) {
    const auto b = p.getBounds();
    juce::DropShadow(c.withAlpha(0.75f), (int)(glow * 2.6f), {}).drawForPath(g, p);
    juce::DropShadow(c.withAlpha(0.95f), (int)glow, {}).drawForPath(g, p);
    juce::DropShadow(c, (int)(glow * 0.4f), {}).drawForPath(g, p);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xffffe3f0), 0.0f, b.getY(), c.brighter(0.25f), 0.0f, b.getBottom(), false));
    g.fillPath(p);
}

/** Lettering pressed into a pill: a dark face with a light lip underneath. */
static void stampText(juce::Graphics& g, const juce::String& t, RectF r, const juce::Font& f, juce::Justification j, float strength = 1.0f) {
    g.setFont(f);
    g.setColour(juce::Colours::white.withAlpha(0.85f * strength)); g.drawText(t, r.translated(0.0f, 1.1f), j, false);
    g.setColour(Pal::stamp().withMultipliedAlpha(strength)); g.drawText(t, r, j, false);
}

/** Pill material: off-white, lit from above, with a darker rim. */
static void pillFill(juce::Graphics& g, const juce::Path& shape, RectF b, juce::Colour tint = Pal::pill()) {
    g.setGradientFill(juce::ColourGradient(tint.brighter(0.6f), b.getX(), b.getY(), tint.interpolatedWith(Pal::pillLow(), 0.85f), b.getX(), b.getBottom(), false));
    g.fillPath(shape);
    juce::Graphics::ScopedSaveState s(g);
    g.reduceClipRegion(shape);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.75f), b.getX(), b.getY(), juce::Colours::white.withAlpha(0.0f), b.getX(), b.getY() + b.getHeight() * 0.34f, false));
    g.fillRect(b);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0x00000000), b.getX(), b.getBottom() - b.getHeight() * 0.22f, juce::Colour(0x38000000), b.getX(), b.getBottom(), false));
    g.fillRect(b);
    g.setColour(juce::Colour(0x55685f52)); g.strokePath(shape, juce::PathStrokeType(1.6f));
}

/** A little four-part bar seen from above, centred on c and turned by angle. */
static void miniBar(juce::Graphics& g, PointF c, float len, float angle, float alpha, juce::Colour tint = Pal::pill(), bool lettered = true) {
    juce::Graphics::ScopedSaveState s(g);
    g.addTransform(juce::AffineTransform::rotation(angle).translated(c.x, c.y));
    g.setOpacity(1.0f);
    const float th = len * 0.29f;
    const RectF b(-len * 0.5f, -th * 0.5f, len, th);
    juce::Path shape; shape.addRoundedRectangle(b, th * 0.30f);
    if (alpha < 1.0f) g.beginTransparencyLayer(alpha);
    juce::DropShadow(juce::Colours::black.withAlpha(0.7f), (int)(th * 0.5f), {0, (int)(th * 0.22f)}).drawForPath(g, shape);
    pillFill(g, shape, b, tint);
    for (int i = 1; i <= 3; ++i) {                                // the three score lines, notched at both edges
        const float x = b.getX() + len * (float)i / 4.0f;
        g.setColour(juce::Colour(0x66554c40)); g.fillRect(x - 0.6f, b.getY() + 1.0f, 1.2f, th - 2.0f);
        g.setColour(juce::Colours::white.withAlpha(0.7f)); g.fillRect(x + 0.6f, b.getY() + 1.5f, 0.8f, th - 3.0f);
        juce::Path n; n.addTriangle(x - th * 0.14f, b.getY() - 0.5f, x + th * 0.14f, b.getY() - 0.5f, x, b.getY() + th * 0.16f);
        n.addTriangle(x - th * 0.14f, b.getBottom() + 0.5f, x + th * 0.14f, b.getBottom() + 0.5f, x, b.getBottom() - th * 0.16f);
        g.setColour(juce::Colour(0xff0b080a)); g.fillPath(n);
    }
    if (lettered && len >= 40.0f) {
        const char* letters[4] = {"X", "AN", "A", "X"};
        const auto f = PeepChainView::plainLabelFont(th * 0.62f);
        for (int i = 0; i < 4; ++i) stampText(g, letters[i], {b.getX() + len * (float)i / 4.0f, b.getY() + th * 0.08f, len / 4.0f, th}, f, juce::Justification::centred);
    }
    if (alpha < 1.0f) g.endTransparencyLayer();
}

static void roundTablet(juce::Graphics& g, PointF c, float r, float alpha) {
    juce::Path shape; shape.addEllipse(c.x - r, c.y - r, r * 2, r * 2);
    if (alpha < 1.0f) g.beginTransparencyLayer(alpha);
    juce::DropShadow(juce::Colours::black.withAlpha(0.7f), (int)(r * 0.6f), {0, (int)(r * 0.3f)}).drawForPath(g, shape);
    pillFill(g, shape, {c.x - r, c.y - r, r * 2, r * 2});
    g.setColour(juce::Colour(0x66554c40)); g.fillRect(c.x - r * 0.72f, c.y - 0.6f, r * 1.44f, 1.2f);
    g.setColour(juce::Colours::white.withAlpha(0.7f)); g.fillRect(c.x - r * 0.72f, c.y + 0.6f, r * 1.44f, 0.8f);
    if (alpha < 1.0f) g.endTransparencyLayer();
}

static juce::Path sparkle(float cx, float cy, float r) {
    juce::Path p;
    for (int i = 0; i < 8; ++i) {
        const float a = (float)i * kPi / 4.0f, rr = (i % 2 == 0) ? r : r * 0.22f;
        const PointF pt(cx + std::sin(a) * rr, cy - std::cos(a) * rr);
        if (i == 0) p.startNewSubPath(pt); else p.lineTo(pt);
    }
    p.closeSubPath();
    return p;
}

static juce::Path crackPath(RectF r) {
    const float x = r.getX(), y = r.getY(), w = r.getWidth(), h = r.getHeight();
    juce::Path c;
    c.startNewSubPath(x + w * 0.50f, y + h * 0.22f);
    c.lineTo(x + w * 0.40f, y + h * 0.42f); c.lineTo(x + w * 0.58f, y + h * 0.54f);
    c.lineTo(x + w * 0.44f, y + h * 0.72f); c.lineTo(x + w * 0.52f, y + h * 0.98f);
    return c;
}

static void brokenHeart(juce::Graphics& g, RectF r, juce::Colour fill, juce::Colour crack) {
    g.setColour(fill); g.fillPath(PeepChainView::heart(r));
    g.setColour(crack); g.strokePath(crackPath(r), juce::PathStrokeType(juce::jmax(1.5f, r.getWidth() * 0.07f), juce::PathStrokeType::mitered));
}

static void barbedWire(juce::Graphics& g, float x0, float x1, float y, juce::Colour c) {
    juce::Path a, b;
    for (float x = x0; x <= x1; x += 2.0f) {
        const float ph = (x - x0) * 0.16f;
        const float ya = y + std::sin(ph) * 2.2f, yb = y - std::sin(ph) * 2.2f;
        if (x == x0) { a.startNewSubPath(x, ya); b.startNewSubPath(x, yb); } else { a.lineTo(x, ya); b.lineTo(x, yb); }
    }
    g.setColour(c);
    g.strokePath(a, juce::PathStrokeType(1.1f)); g.strokePath(b, juce::PathStrokeType(1.1f));
    for (float x = x0 + 26.0f; x < x1 - 10.0f; x += 58.0f) {            // barbs
        g.drawLine(x - 5.0f, y - 6.5f, x + 5.0f, y + 6.5f, 1.4f);
        g.drawLine(x - 5.0f, y + 6.5f, x + 5.0f, y - 6.5f, 1.4f);
        g.fillEllipse(x - 2.2f, y - 2.2f, 4.4f, 4.4f);
    }
}

//==============================================================================
void PeepLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) {
    auto r = RectF((float)x, (float)y, (float)w, (float)h).reduced(5.0f);
    const float d = juce::jmin(r.getWidth(), r.getHeight());
    r = r.withSizeKeepingCentre(d, d);
    const auto c = r.getCentre(); const float rad = d * 0.5f;
    const float ang = a0 + pos * (a1 - a0);
    const bool hot = s.isMouseOverOrDragging();
    const auto stroke = [](float t) { return juce::PathStrokeType(t, juce::PathStrokeType::curved, juce::PathStrokeType::rounded); };

    // ring: a dim track, and a neon arc from the default (0 dB, or Peep's own threshold) to the value
    juce::Path track; track.addCentredArc(c.x, c.y, rad - 5.0f, rad - 5.0f, 0.0f, a0, a1, true);
    g.setColour(juce::Colour(0xff3d2334)); g.strokePath(track, stroke(5.0f));
    const float zero = a0 + (float)s.valueToProportionOfLength(s.getDoubleClickReturnValue()) * (a1 - a0);
    if (std::abs(ang - zero) > 0.01f) {
        juce::Path val; val.addCentredArc(c.x, c.y, rad - 5.0f, rad - 5.0f, 0.0f, juce::jmin(zero, ang), juce::jmax(zero, ang), true);
        g.setColour(Pal::pink().withAlpha(0.22f)); g.strokePath(val, stroke(13.0f));
        g.setColour(Pal::pink()); g.strokePath(val, stroke(5.0f));
        g.setColour(juce::Colours::white.withAlpha(0.6f)); g.strokePath(val, stroke(1.4f));
    }
    {   // tick where the default sits
        const PointF a(c.x + std::sin(zero) * (rad - 10.0f), c.y - std::cos(zero) * (rad - 10.0f)), b(c.x + std::sin(zero) * rad, c.y - std::cos(zero) * rad);
        g.setColour(Pal::pale()); g.drawLine({a, b}, 1.6f);
    }

    // the tablet
    const float tr = rad - 15.0f;
    juce::Path face; face.addEllipse(c.x - tr, c.y - tr, tr * 2, tr * 2);
    juce::DropShadow(juce::Colours::black.withAlpha(0.85f), 14, {0, 7}).drawForPath(g, face);
    if (hot) juce::DropShadow(Pal::pink().withAlpha(0.5f), 14, {}).drawForPath(g, face);
    pillFill(g, face, {c.x - tr, c.y - tr, tr * 2, tr * 2});
    const float br = tr - 7.0f;                                   // bevel: the flat face inside the rounded edge
    g.setGradientFill(juce::ColourGradient(juce::Colour(0x50685f52), c.x, c.y - br, juce::Colours::white.withAlpha(0.9f), c.x, c.y + br, false));
    g.drawEllipse(c.x - br, c.y - br, br * 2, br * 2, 1.3f);

    // pointer: a pink heart pressed into the face near the rim
    const PointF hp(c.x + std::sin(ang) * (br - 11.0f), c.y - std::cos(ang) * (br - 11.0f));
    const auto hr = RectF(13.0f, 12.0f).withCentre(hp);
    const auto turn = juce::AffineTransform::rotation(ang, hp.x, hp.y);
    g.setColour(juce::Colours::white.withAlpha(0.9f)); g.fillPath(PeepChainView::heart(hr.translated(0.0f, 1.0f)), turn);
    g.setColour(Pal::pink().darker(0.1f)); g.fillPath(PeepChainView::heart(hr), turn);

    const double v = s.getValue();
    const bool off = s.getProperties()["offAtMinimum"] && v <= s.getMinimum() + 0.05;
    stampText(g, off ? juce::String("OFF") : (v > 0.05 ? "+" : "") + juce::String(v, 1), RectF(br * 2, br).withCentre(c.translated(0.0f, 3.0f)),
              PeepChainView::labelFont(juce::jmax(16.0f, br * 0.52f)).withExtraKerningFactor(0.04f), juce::Justification::centred);
}

//==============================================================================
PeepChainView::PeepChainView(PeepChainProcessor& p)
    : proc(p),
      inputAtt(p.apvts, "input", input), gateAtt(p.apvts, "gateThresh", gateThresh), compAtt(p.apvts, "compThresh", compThresh),
      delayAtt(p.apvts, "delayLevel", delayLevel), reverbAtt(p.apvts, "reverbLevel", reverbLevel), outputAtt(p.apvts, "output", output) {
    setLookAndFeel(&lnf);
    for (auto* s : {&input, &gateThresh, &compThresh, &delayLevel, &reverbLevel, &output}) {
        s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s->setRotaryParameters(kPi * 1.25f, kPi * 2.75f, true);
        s->setPopupDisplayEnabled(false, false, nullptr);
        addAndMakeVisible(*s);                           // double-click returns to the default (set by the attachment)
    }
    input.setTooltip("How hard the vocal hits the chain. Aim the IN meter's peaks at the marked zone.");
    gateThresh.setTooltip("Noise Gate threshold. Anything quieter is cut. Peep's setting: -46 dB.");
    compThresh.setTooltip("Compressor threshold. Lower squashes more. Peep's setting: -26 dB.");
    delayLevel.setTooltip("How much echo is added (a 500 ms repeat, or a quarter note of the song with SYNC on). Peep's send: -16.5. All the way down is off.");
    reverbLevel.setTooltip("How much reverb is added. Peep's send: -6.0. All the way down is off.");
    for (auto* s : {&delayLevel, &reverbLevel}) s->getProperties().set("offAtMinimum", true);
    output.setTooltip("Level after everything");
    setSize((int)kW, (int)kH);
    startTimerHz(30);
}
PeepChainView::~PeepChainView() { stopTimer(); setLookAndFeel(nullptr); }

void PeepChainView::resized() {
    juce::Slider* knobs[kKnobs] = {&input, &gateThresh, &compThresh, &delayLevel, &reverbLevel, &output};
    for (int i = 0; i < kKnobs; ++i) {
        const float cx = (float)getWidth() * (2.0f * (float)i + 1.0f) / (2.0f * (float)kKnobs);
        knobs[i]->setBounds(RectF(cx - kKnobSize * 0.5f, kKnobY, kKnobSize, kKnobSize).toNearestInt());
    }
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
RectF PeepChainView::slotBounds(int slot) const {
    const float w = ((float)getWidth() - 2.0f * kBarX) / 6.0f;
    return {kBarX + (float)slot * w, kBarY, w, kBarH};
}
RectF PeepChainView::resetBounds() const { return {(float)getWidth() - kBarX - 172.0f, 122.0f, 172.0f, 28.0f}; }
RectF PeepChainView::autoGainBounds() const { return {resetBounds().getX() - 14.0f - 156.0f, 122.0f, 156.0f, 28.0f}; }
static constexpr float kVersionX = 100.0f, kVersionY = 91.0f;
static juce::String versionText() { return "V" + UpdateChecker::currentVersion(); }
RectF PeepChainView::githubBounds() const {
    if (!UpdateChecker::isConfigured()) return {kVersionX + juce::GlyphArrangement::getStringWidth(labelFont(12.0f), versionText()), kVersionY, 0.0f, 16.0f};
    const float x = kVersionX + juce::GlyphArrangement::getStringWidth(labelFont(12.0f), versionText()) + 6.0f;
    return {x, kVersionY, 16.0f + 4.0f + juce::GlyphArrangement::getStringWidth(labelFont(12.0f), "GITHUB"), 16.0f};
}
RectF PeepChainView::updateBounds() const { return {githubBounds().getRight() + 12.0f, kVersionY, 250.0f, 16.0f}; }

juce::String PeepChainView::getTooltip() {
    if (autoGainBounds().contains(lastMouse))
        return "Auto Gain: turns the output down (or up) to the loudness of what comes in, so switching the plugin on "
               "and off compares the sound, not the volume. The number is the gain it applies. Off by default.";
    if (updateBounds().contains(lastMouse) && updates->getState() == UpdateChecker::State::available)
        return "Open the download page for " + updates->getLatestVersion();
    if (githubBounds().contains(lastMouse)) return "Open PeepChain on GitHub";
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
            case kSync: {
                const bool on = proc.apvts.getRawParameterValue("delaySync")->load() > 0.5f;
                return on ? "Echo follows the song's tempo: a quarter note, now " + juce::String(juce::roundToInt(proc.getDelayMs())) + " ms. Click for Peep's fixed 500 ms."
                          : "Echo at Peep's fixed 500 ms (a quarter note at 120 bpm). Click to follow the song's tempo instead.";
            }
            default: break;
        }
    }
    if (resetBounds().contains(lastMouse)) return "Put the order, switches, thresholds and sends back to Peep's settings";
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

void PeepChainView::mouseDown(const juce::MouseEvent& e) {
    pressedSlot = slotAt(e.position); dragToSlot = pressedSlot; dragging = false; dragDx = 0;
    pressedReset = pressedSlot < 0 && resetBounds().contains(e.position);
    pressedAutoGain = pressedSlot < 0 && autoGainBounds().contains(e.position);
    pressedUpdate = pressedSlot < 0 && updateBounds().contains(e.position) && updates->getState() == UpdateChecker::State::available;
    pressedGithub = pressedSlot < 0 && githubBounds().contains(e.position);
    pressedControl = pressedSlot < 0 ? controlAt(e.position) : -1;
}

void PeepChainView::mouseDrag(const juce::MouseEvent& e) {
    if (pressedSlot < 0) return;
    dragDx = (float)e.getDistanceFromDragStartX();
    if (!dragging && std::abs(dragDx) > 6.0f) dragging = true;
    if (dragging) {
        dragToSlot = juce::jlimit(0, kStages - 1, pressedSlot + juce::roundToInt(dragDx / slotBounds(0).getWidth()));
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        repaint(chainStrip().expanded(14.0f, 0.0f).toNearestInt());
    }
}

void PeepChainView::previewDrag(int fromSlot, float dx) {
    pressedSlot = fromSlot; dragging = true; dragDx = dx;
    dragToSlot = juce::jlimit(0, kStages - 1, pressedSlot + juce::roundToInt(dragDx / slotBounds(0).getWidth()));
}

void PeepChainView::mouseUp(const juce::MouseEvent& e) {
    if (pressedSlot >= 0) {
        if (dragging) proc.setOrder(displayOrder());                               // drop: one reorder, however far it moved
        else if (slotAt(e.position) == pressedSlot) toggleStage(proc.getOrder()[(size_t)pressedSlot]);
    } else if (pressedReset && resetBounds().contains(e.position)) {
        proc.resetToPeep();
    } else if (pressedAutoGain && autoGainBounds().contains(e.position)) {
        if (auto* p = proc.apvts.getParameter("autoGain")) {
            p->beginChangeGesture(); p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.0f : 1.0f); p->endChangeGesture();
        }
    } else if (pressedUpdate && updateBounds().contains(e.position)) {
        UpdateChecker::releasesPage().launchInDefaultBrowser();
    } else if (pressedGithub && githubBounds().contains(e.position)) {
        UpdateChecker::repoPage().launchInDefaultBrowser();
    } else if (pressedControl >= 0 && controlAt(e.position) == pressedControl) {
        activate(pressedControl);
    }
    pressedControl = -1;
    pressedSlot = dragToSlot = -1; dragging = false; pressedReset = false; pressedAutoGain = false; pressedUpdate = false; pressedGithub = false; dragDx = 0;
    mouseMove(e);
    repaint();
}

void PeepChainView::mouseMove(const juce::MouseEvent& e) {
    lastMouse = e.position;
    overReset = resetBounds().contains(e.position) && !proc.isPeepPreset();
    overAutoGain = autoGainBounds().contains(e.position);
    overUpdate = updateBounds().contains(e.position) && updates->getState() == UpdateChecker::State::available;
    const bool wasOverGithub = std::exchange(overGithub, githubBounds().contains(e.position));
    if (overGithub != wasOverGithub) repaint(githubBounds().expanded(2.0f).toNearestInt());
    const int wasOverControl = std::exchange(overControl, controlAt(e.position));
    if (overControl != wasOverControl) for (int c : {wasOverControl, overControl}) if (c >= 0) repaint(controlBounds(c).expanded(3.0f).toNearestInt());
    setMouseCursor(slotAt(e.position) >= 0 || overReset || overAutoGain || overUpdate || overGithub || overControl >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

//==============================================================================
void PeepChainView::renderBackdrop() {
    const float scale = imageScale;                                     // drawn once at retina size, then just blitted
    backdrop = juce::Image(juce::Image::ARGB, (int)(kW * scale), (int)(kH * scale), true);
    juce::Graphics g(backdrop);
    g.addTransform(juce::AffineTransform::scale(scale));
    const RectF b(0.0f, 0.0f, kW, kH);

    // night: black, with pink light leaking in from the title and from under the bar
    g.fillAll(Pal::bg());
    g.setGradientFill(juce::ColourGradient(Pal::pink().withAlpha(0.30f), 190.0f, 30.0f, Pal::pink().withAlpha(0.0f), 190.0f, 330.0f, true)); g.fillRect(b);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0x38b0206a), kW * 0.5f, kBarY + kBarH + 40.0f, juce::Colour(0x00b0206a), kW * 0.5f, kBarY + kBarH + 330.0f, true)); g.fillRect(b);

    juce::Random rnd(1996);                                       // fixed seed: the same mess every time
    // a couple of loose pills in the bottom corners
    struct Loose { float x, y, len, angle; int kind; };           // kind 0: bar, 1: pink bar, 2: round tablet
    const Loose loose[] = {{kW - 60.0f, kH - 30.0f, 60.0f, 0.2f, 1}, {64.0f, kH - 24.0f, 60.0f, -0.15f, 0}};
    for (const auto& l : loose) {
        if (l.kind == 2) roundTablet(g, {l.x, l.y}, 11.0f, 0.42f);
        else miniBar(g, {l.x, l.y}, l.len, l.angle, 0.42f, l.kind == 1 ? Pal::pale() : Pal::pill(), true);
    }
    for (int i = 0; i < 34; ++i) {
        const float x = rnd.nextFloat() * kW, y = rnd.nextFloat() * kH, r = 2.0f + rnd.nextFloat() * 4.5f;
        g.setColour((i % 3 == 0 ? Pal::pink() : Pal::pale()).withAlpha(0.10f + rnd.nextFloat() * 0.25f));
        g.fillPath(sparkle(x, y, r));
    }

    // film grain and a soft vignette, under the title and the bottle so those stay bright
    for (int i = 0; i < 5200; ++i) {
        g.setColour((rnd.nextBool() ? juce::Colours::white : juce::Colours::black).withAlpha(0.035f + rnd.nextFloat() * 0.05f));
        g.fillRect(rnd.nextFloat() * kW, rnd.nextFloat() * kH, 0.8f, 0.8f);
    }
    g.setGradientFill(juce::ColourGradient(juce::Colour(0x00000000), kW * 0.5f, kH * 0.5f, juce::Colour(0xa0000000), 0.0f, kH, true)); g.fillRect(b);

    // title: broken heart and neon name
    {
        const RectF hr(32.0f, 30.0f, 54.0f, 50.0f);
        neonPath(g, heart(hr), Pal::pink(), 12.0f);
        g.setColour(Pal::bg()); g.strokePath(crackPath(hr), juce::PathStrokeType(4.0f, juce::PathStrokeType::mitered));
        for (int i = 0; i < 2; ++i) {                             // teardrops
            juce::Path t; const float tx = 46.0f + (float)i * 22.0f, ty = 88.0f + (float)i * 8.0f;
            t.startNewSubPath(tx, ty); t.quadraticTo(tx + 5.5f, ty + 9.0f, tx, ty + 12.0f); t.quadraticTo(tx - 5.5f, ty + 9.0f, tx, ty); t.closeSubPath();
            g.setColour(Pal::pale().withAlpha(0.85f)); g.fillPath(t);
        }
        neonPath(g, textPath("PeepChain", titleFont(70.0f), {98.0f, 10.0f, 440.0f, 80.0f}, juce::Justification::centredLeft), Pal::pink(), 11.0f);
    }

    // loose bars across the top right, one snapped in half with crumbs
    miniBar(g, {kW - 76.0f, 70.0f}, 62.0f, 0.42f, 1.0f);
    miniBar(g, {kW - 150.0f, 40.0f}, 62.0f, -0.30f, 1.0f);
    miniBar(g, {kW - 214.0f, 80.0f}, 62.0f, 0.08f, 1.0f, Pal::pale());
    miniBar(g, {kW - 296.0f, 52.0f}, 62.0f, 1.15f, 1.0f);
    miniBar(g, {kW - 372.0f, 84.0f}, 62.0f, -0.12f, 1.0f);
    miniBar(g, {kW - 78.0f, 28.0f}, 31.0f, 0.9f, 1.0f, Pal::pill(), false);
    miniBar(g, {kW - 44.0f, 36.0f}, 31.0f, -0.5f, 1.0f, Pal::pill(), false);
    g.setColour(Pal::pill().withAlpha(0.8f));
    for (int i = 0; i < 9; ++i) { const float r = 0.6f + rnd.nextFloat() * 1.2f; g.fillEllipse(kW - 74.0f + rnd.nextFloat() * 30.0f, 24.0f + rnd.nextFloat() * 24.0f, r * 2, r * 2); }

    barbedWire(g, 22.0f, kW - 22.0f, 503.0f, Pal::grey().withAlpha(0.32f));

    g.setColour(Pal::dim()); g.drawRect(b.reduced(5.0f), 1.0f);
}

//==============================================================================
static void reductionBar(juce::Graphics& g, RectF r, float db, float maxDb) {
    g.setColour(juce::Colour(0xff1f1722)); g.fillRoundedRectangle(r, r.getWidth() * 0.5f);
    const float frac = juce::jlimit(0.0f, 1.0f, db / maxDb);
    if (frac > 0.002f) {
        auto f = r.withHeight(juce::jmax(r.getWidth(), r.getHeight() * frac));          // grows downward, like gain reduction
        g.setColour(Pal::pink().withAlpha(0.22f)); g.fillRoundedRectangle(f.expanded(3.0f), r.getWidth() * 0.5f + 3.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colours::white, f.getX(), r.getY(), Pal::pink(), f.getX(), r.getY() + juce::jmax(20.0f, f.getHeight()), false));
        g.fillRoundedRectangle(f, r.getWidth() * 0.5f);
    }
}

static void readout(juce::Graphics& g, RectF r, float db) {
    g.setColour(db > 0.3f ? Pal::pale() : Pal::dim().brighter(0.4f));
    g.setFont(PeepChainView::labelFont(13.0f).withExtraKerningFactor(0.04f));
    g.drawText(db > 0.05f ? "-" + juce::String(db, 1) : "0.0", r, juce::Justification::centred, false);
}

void PeepChainView::paintStageDisplay(juce::Graphics& g, RectF in, int stage, bool on) {
    const float gOpen = on ? gateOpen : 0.0f, cDb = on ? compDb : 0.0f, lDb = on ? limDb : 0.0f;
    if (stage == PeepChain::kGate) {                              // a heart that lights up while the gate is open
        const auto hr = RectF(46.0f, 42.0f).withCentre(in.getCentre().translated(0.0f, 1.0f));
        if (gOpen > 0.05f) juce::DropShadow(Pal::pink().withAlpha(0.8f * gOpen), 14, {}).drawForPath(g, heart(hr));
        brokenHeart(g, hr, juce::Colour(0xff3a1b2c).interpolatedWith(Pal::pink(), gOpen), Pal::well());
    } else if (stage == PeepChain::kComp || stage == PeepChain::kLimit) {
        const float db = stage == PeepChain::kComp ? cDb : lDb;
        auto col = in.reduced(0.0f, 4.0f);
        readout(g, col.removeFromBottom(14.0f), db);
        reductionBar(g, col.withTrimmedBottom(3.0f).withSizeKeepingCentre(15.0f, col.getHeight() - 3.0f), db, stage == PeepChain::kComp ? 20.0f : 18.0f);
        g.setColour(Pal::dim().brighter(0.2f));                   // scale ticks either side
        for (int i = 0; i <= 4; ++i) {
            const float y = col.getY() + (col.getHeight() - 3.0f) * (float)i / 4.0f;
            g.fillRect(in.getCentreX() - 20.0f, y, 6.0f, 1.0f); g.fillRect(in.getCentreX() + 14.0f, y, 6.0f, 1.0f);
        }
    } else if (stage == PeepChain::kFilter) {                     // AUFilter: low shelf + two gentle bumps
        juce::Path p; auto a = in.reduced(4.0f, 6.0f);
        for (int k = 0; k <= 48; ++k) {
            const float f = std::pow(10.0f, std::log10(20.0f) + 3.0f * (float)k / 48.0f);       // 20 Hz .. 20 kHz
            float dbv = 12.3f / (1.0f + std::pow(f / 134.0f, 2.0f));
            dbv += 3.1f * std::exp(-std::pow(std::log2(f / 602.0f) / 1.2f, 2.0f)) + 1.6f * std::exp(-std::pow(std::log2(f / 2801.0f) / 1.2f, 2.0f));
            const float px = a.getX() + a.getWidth() * (float)k / 48.0f, py = a.getBottom() - 6.0f - (a.getHeight() - 10.0f) * dbv / 13.0f;
            if (k == 0) p.startNewSubPath(px, py); else p.lineTo(px, py);
        }
        juce::Path fill(p); fill.lineTo(a.getRight(), a.getBottom()); fill.lineTo(a.getX(), a.getBottom()); fill.closeSubPath();
        g.setGradientFill(juce::ColourGradient(Pal::pink().withAlpha(0.45f), 0.0f, a.getY(), Pal::pink().withAlpha(0.0f), 0.0f, a.getBottom(), false)); g.fillPath(fill);
        const auto st = juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour(Pal::pink().withAlpha(0.3f)); g.strokePath(p, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(Pal::hot()); g.strokePath(p, st);
    } else if (stage == PeepChain::kGeq) {                        // the 31 graphic EQ sliders, as they are in the preset
        const double* gains = GraphicEQ31::peepGains();
        auto a = in.reduced(3.0f, 6.0f); const float bw = a.getWidth() / 31.0f;
        for (int k = 0; k < 31; ++k) {
            const float v = (float)gains[k] / 13.0f, x = a.getX() + bw * (float)k, h = juce::jmax(2.0f, (a.getHeight() - 2.0f) * v);
            g.setColour(v > 0.0f ? Pal::pink() : Pal::dim());
            if (v > 0.0f) g.setGradientFill(juce::ColourGradient(Pal::hot(), 0.0f, a.getBottom() - h, Pal::pink().darker(0.25f), 0.0f, a.getBottom(), false));
            g.fillRoundedRectangle(x + 0.4f, a.getBottom() - h, juce::jmax(1.2f, bw - 0.9f), h, 0.8f);
        }
    } else {                                                      // multiband: four bands, like the four parts of a bar
        auto bars = in.reduced(0.0f, 5.0f).withSizeKeepingCentre(80.0f, in.getHeight() - 10.0f);
        const float bw = 13.0f, sp = (bars.getWidth() - bw * 4.0f) / 3.0f;
        for (int b = 0; b < 4; ++b) reductionBar(g, {bars.getX() + (float)b * (bw + sp), bars.getY(), bw, bars.getHeight()}, on ? bandDb[b] : 0.0f, 40.0f);
    }
}

static juce::Path segmentShape(RectF r, bool roundLeft, bool roundRight) {
    const float R = 34.0f, c = 9.0f;                              // big round ends; small chamfers where two parts meet (the score)
    const float x = r.getX(), y = r.getY(), x2 = r.getRight(), y2 = r.getBottom();
    const float k = 0.5523f;
    juce::Path p;
    p.startNewSubPath(x + (roundLeft ? R : c), y);
    p.lineTo(x2 - (roundRight ? R : c), y);
    if (roundRight) p.cubicTo(x2 - R + R * k, y, x2, y + R - R * k, x2, y + R); else p.lineTo(x2, y + c);
    p.lineTo(x2, y2 - (roundRight ? R : c));
    if (roundRight) p.cubicTo(x2, y2 - R + R * k, x2 - R + R * k, y2, x2 - R, y2); else p.lineTo(x2 - c, y2);
    p.lineTo(x + (roundLeft ? R : c), y2);
    if (roundLeft) p.cubicTo(x + R - R * k, y2, x, y2 - R + R * k, x, y2 - R); else p.lineTo(x, y2 - c);
    p.lineTo(x, y + (roundLeft ? R : c));
    if (roundLeft) p.cubicTo(x, y + R - R * k, x + R - R * k, y, x + R, y); else p.lineTo(x + c, y);
    p.closeSubPath();
    return p;
}

namespace {
struct SegmentLayout { RectF body, head, well, light; };
SegmentLayout layoutSegment(RectF slot, bool roundLeft, bool roundRight) {
    SegmentLayout l;
    l.body = slot.reduced(kSeam * 0.5f, 0.0f);
    const auto inner = l.body.withTrimmedLeft(roundLeft ? 8.0f : 0.0f).withTrimmedRight(roundRight ? 8.0f : 0.0f);   // keep clear of the round ends
    l.head = inner.withHeight(34.0f).reduced(14.0f, 0.0f);
    l.well = inner.withTrimmedTop(36.0f).reduced(15.0f, 0.0f).withTrimmedBottom(16.0f);
    l.light = RectF(13.0f, 12.0f).withPosition(l.head.getX(), l.head.getCentreY() - 5.0f);
    return l;
}
const float kLayerMargin = 46.0f;                                 // room round the bar for its shadow and glow
}

void PeepChainView::timerCallback() {
    updateMeters();
    const auto key = stateKey();
    if (key != lastPaintedKey) { lastPaintedKey = key; lastControlsKey = controlsKey(); repaint(); return; }   // something was switched or moved: everything
    if (const auto ck = controlsKey(); ck != lastControlsKey) {                       // undo / redo available, A/B, sync
        lastControlsKey = ck;
        for (int c = 0; c < kNumControls; ++c) repaint(controlBounds(c).expanded(3.0f).toNearestInt());
    }
    if (dragging) repaint(chainStrip().expanded(14.0f, 0.0f).toNearestInt());         // otherwise just the moving parts:
    else for (int slot = 0; slot < kStages; ++slot)                                   // the six little windows and the level meters
        repaint(layoutSegment(slotBounds(slot), slot == 0, slot == kStages - 1).well.toNearestInt());
    repaint(0, (int)kMeterY - 22, getWidth(), 60);
    // the Auto Gain switch shows the gain it is applying
    if (std::abs(autoGainShownDb - autoGainPaintedDb) >= 0.05f) { autoGainPaintedDb = autoGainShownDb; repaint(autoGainBounds().expanded(14.0f).toNearestInt()); }
}

/** One part of the bar, without the moving display in its window (that is drawn on top every frame). */
void PeepChainView::paintSegment(juce::Graphics& g, RectF slot, int stage, int slotIndex, bool roundLeft, bool roundRight, bool lifted) {
    const bool on = proc.isStageOn(stage);
    const auto l = layoutSegment(slot, roundLeft, roundRight);
    const auto& r = l.body; const auto& head = l.head; const auto& well = l.well; const auto& light = l.light;
    const auto shape = segmentShape(r, roundLeft, roundRight);
    const auto nameFont = labelFont(19.0f);

    if (!on && !lifted) {
        // switched off: this part of the bar has been taken. An empty outline and a few crumbs are left.
        g.setColour(juce::Colour(0x66100c12)); g.fillPath(shape);
        juce::Path dashed; const float dashes[2] = {5.0f, 5.0f};
        juce::PathStrokeType(1.2f).createDashedStroke(dashed, shape, dashes, 2);
        g.setColour(Pal::pill().withAlpha(0.30f)); g.fillPath(dashed);
        g.setColour(Pal::grey().withAlpha(0.8f)); g.strokePath(heart(light), juce::PathStrokeType(1.1f));
        g.setColour(Pal::grey().withAlpha(0.75f)); g.setFont(nameFont);
        g.drawText(juce::String(kStageNames[stage]), head.withTrimmedLeft(20.0f), juce::Justification::centredLeft, false);
        g.setColour(Pal::pink().withAlpha(0.8f)); g.setFont(labelFont(13.0f));
        g.drawText("OFF", head, juce::Justification::centredRight, false);
        g.setColour(juce::Colour(0x33000000)); g.fillRoundedRectangle(well, 9.0f);
        g.beginTransparencyLayer(0.16f); paintStageDisplay(g, well.reduced(8.0f, 6.0f), stage, false); g.endTransparencyLayer();
        juce::Random rnd(stage * 7 + 3);
        g.setColour(Pal::pill().withAlpha(0.55f));
        for (int i = 0; i < 7; ++i) { const float cr = 0.7f + rnd.nextFloat() * 1.3f; g.fillEllipse(r.getX() + 14.0f + rnd.nextFloat() * (r.getWidth() - 28.0f), r.getBottom() - 12.0f + rnd.nextFloat() * 8.0f, cr * 2, cr * 2); }
        return;
    }

    juce::DropShadow(juce::Colours::black.withAlpha(0.85f), lifted ? 26 : 16, {0, lifted ? 16 : 9}).drawForPath(g, shape);
    if (lifted) juce::DropShadow(Pal::pink().withAlpha(0.7f), 18, {}).drawForPath(g, shape);
    if (!on) g.beginTransparencyLayer(0.55f);
    pillFill(g, shape, r);

    // on/off light, the name pressed into the pill, and which place in the chain this is
    juce::DropShadow(Pal::pink().withAlpha(0.6f), 5, {}).drawForPath(g, heart(light));
    g.setColour(Pal::pink()); g.fillPath(heart(light));
    stampText(g, kStageNames[stage], head.withTrimmedLeft(20.0f).translated(0.0f, 1.0f), nameFont, juce::Justification::centredLeft, 1.25f);
    stampText(g, juce::String(slotIndex + 1), head.translated(0.0f, 1.0f), nameFont, juce::Justification::centredRight, 0.8f);

    // the window: a dark well sunk into the pill
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xaa4f473c), 0.0f, well.getY() - 2.0f, juce::Colours::white, 0.0f, well.getBottom() + 2.0f, false));
    g.fillRoundedRectangle(well.expanded(2.0f), 11.0f);
    g.setColour(Pal::well()); g.fillRoundedRectangle(well, 9.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0x70000000), 0.0f, well.getY(), juce::Colour(0x00000000), 0.0f, well.getY() + 12.0f, false));
    g.fillRoundedRectangle(well, 9.0f);
    if (!on) { paintStageDisplay(g, well.reduced(8.0f, 6.0f), stage, false); g.endTransparencyLayer(); }
}

juce::uint64 PeepChainView::stateKey() const {
    juce::uint64 k = 0;
    for (int st : displayOrder()) k = k * 8 + (juce::uint64)st;
    for (int i = 0; i < kStages; ++i) k = k * 2 + (proc.isStageOn(i) ? 1u : 0u);
    k = k * 64 + (juce::uint64)(dragging ? pressedSlot + 1 : 0) * 8 + (juce::uint64)(proc.isPeepPreset() ? 1 : 0) + (overReset ? 2u : 0u);
    const bool agOn = proc.apvts.getRawParameterValue("autoGain")->load() > 0.5f;
    return k * 16 + (agOn ? 1u : 0u) + (overAutoGain ? 2u : 0u) + (updates->getState() == UpdateChecker::State::available ? 4u : 0u) + (overUpdate ? 8u : 0u);
}

/** The bar itself changes only when something is switched, moved or dragged, so it is drawn once and kept. */
void PeepChainView::renderChainLayer() {
    const float scale = imageScale;
    const auto shown = displayOrder();
    const int lifted = dragging && pressedSlot >= 0 ? proc.getOrder()[(size_t)pressedSlot] : -1;
    const auto area = chainStrip();
    chainLayer = juce::Image(juce::Image::ARGB, (int)(area.getWidth() * scale), (int)(area.getHeight() * scale), true);
    {
        juce::Graphics g(chainLayer);
        g.addTransform(juce::AffineTransform::translation(-area.getX(), -area.getY()).scaled(scale));
        juce::Path whole; whole.addRoundedRectangle(RectF(kBarX, kBarY, (float)getWidth() - 2.0f * kBarX, kBarH), 34.0f);
        juce::DropShadow(Pal::pink().withAlpha(0.30f), 34, {0, 10}).drawForPath(g, whole);     // pink light under the whole bar
        for (int slot = 0; slot < kStages; ++slot) {
            const auto r = slotBounds(slot);
            const bool first = slot == 0, last = slot == kStages - 1;
            if (shown[(size_t)slot] == lifted) {                     // where the dragged part will land
                juce::Path dashed; const float dashes[2] = {6.0f, 5.0f};
                juce::PathStrokeType(1.6f).createDashedStroke(dashed, segmentShape(r.reduced(kSeam * 0.5f, 0.0f), first, last), dashes, 2);
                g.setColour(Pal::pink().withAlpha(0.9f)); g.fillPath(dashed);
            } else {
                paintSegment(g, r, shown[(size_t)slot], slot, first, last, false);
            }
        }
    }
    liftedLayer = {};
    if (lifted >= 0) {                                               // the part in the hand, drawn once and then just moved
        const auto r = slotBounds(0).expanded(kLayerMargin);
        liftedLayer = juce::Image(juce::Image::ARGB, (int)(r.getWidth() * scale), (int)(r.getHeight() * scale), true);
        juce::Graphics g(liftedLayer);
        g.addTransform(juce::AffineTransform::translation(-r.getX(), -r.getY()).scaled(scale));
        paintSegment(g, slotBounds(0), lifted, dragToSlot, false, false, true);
    }
    chainKey = stateKey() * 8 + (juce::uint64)(dragToSlot + 1);
}

RectF PeepChainView::chainStrip() const { return RectF(kBarX, kBarY, (float)getWidth() - 2.0f * kBarX, kBarH).expanded(kLayerMargin - 18.0f, kLayerMargin); }

void PeepChainView::paintChain(juce::Graphics& g) {
    if (chainLayer.isNull() || chainKey != stateKey() * 8 + (juce::uint64)(dragToSlot + 1)) renderChainLayer();
    g.drawImage(chainLayer, chainStrip());
    const auto shown = displayOrder();
    const int lifted = dragging && pressedSlot >= 0 ? proc.getOrder()[(size_t)pressedSlot] : -1;
    auto display = [&](RectF slot, int stage, bool first, bool last) {
        if (!proc.isStageOn(stage)) return;                          // an off stage's faded display is part of the kept picture
        const auto well = layoutSegment(slot, first, last).well;
        paintStageDisplay(g, well.reduced(8.0f, 6.0f), stage, true);
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.07f), well.getX(), well.getY(), juce::Colours::white.withAlpha(0.0f), well.getX() + well.getWidth() * 0.6f, well.getBottom(), false));
        g.fillRoundedRectangle(well, 9.0f);                          // a little glass reflection
    };
    for (int slot = 0; slot < kStages; ++slot)
        if (shown[(size_t)slot] != lifted) display(slotBounds(slot), shown[(size_t)slot], slot == 0, slot == kStages - 1);
    if (lifted >= 0 && !liftedLayer.isNull()) {
        auto r = slotBounds(pressedSlot).translated(dragDx, -6.0f);
        r.setX(juce::jlimit(slotBounds(0).getX() - 12.0f, slotBounds(kStages - 1).getX() + 12.0f, r.getX()));
        g.drawImage(liftedLayer, r.expanded(kLayerMargin));
        display(r, lifted, false, false);
    }
}

void PeepChainView::paintLevelMeter(juce::Graphics& g, RectF r, const juce::String& name, float db, bool inputGuide) {
    g.setColour(Pal::pale()); g.setFont(labelFont(16.0f));
    g.drawText(name, r.removeFromLeft(38.0f), juce::Justification::centredLeft, false);
    auto bar = r.withSizeKeepingCentre(r.getWidth(), 10.0f);
    auto xAt = [&](float d) { return bar.getX() + bar.getWidth() * juce::jlimit(0.0f, 1.0f, (d + 60.0f) / 60.0f); };
    g.setColour(juce::Colour(0xff1c1420)); g.fillRoundedRectangle(bar, 5.0f);
    g.setColour(juce::Colour(0xff3a2233)); g.drawRoundedRectangle(bar, 5.0f, 1.0f);
    const float frac = juce::jlimit(0.0f, 1.0f, (db + 60.0f) / 60.0f);
    if (frac > 0.01f) {
        const auto fill = bar.withWidth(juce::jmax(10.0f, bar.getWidth() * frac));
        g.setColour(Pal::pink().withAlpha(0.20f)); g.fillRoundedRectangle(fill.expanded(3.0f), 8.0f);
        g.setGradientFill(juce::ColourGradient(Pal::dim(), bar.getX(), 0.0f, db > -0.2f ? juce::Colours::white : Pal::hot(), bar.getRight(), 0.0f, false));
        g.fillRoundedRectangle(fill, 5.0f);
    }
    if (!inputGuide) return;
    // where Peep's own takes peaked: aim the loud phrases here with the INPUT knob
    const auto zone = RectF(xAt(kPeepPeakLowDb), bar.getY() - 5.0f, xAt(kPeepPeakHighDb) - xAt(kPeepPeakLowDb), bar.getHeight() + 10.0f);
    const bool inZone = db >= kPeepPeakLowDb && db <= kPeepPeakHighDb;
    g.setColour(Pal::pill().withAlpha(inZone ? 1.0f : 0.6f)); g.drawRoundedRectangle(zone, 5.0f, inZone ? 1.6f : 1.1f);
    g.setFont(labelFont(11.0f));
    g.drawText("PEEP'S TAKES PEAKED HERE", zone.withY(zone.getY() - 14.0f).withHeight(12.0f).expanded(40.0f, 0.0f), juce::Justification::centred, false);
    // the gate threshold, when the gate is the first thing the input meets
    if (proc.getOrder()[0] == PeepChain::kGate && proc.isStageOn(PeepChain::kGate)) {
        const float x = xAt((float)gateThresh.getValue());
        g.setColour(Pal::pale()); g.fillRect(x - 0.75f, bar.getY() - 5.0f, 1.5f, bar.getHeight() + 10.0f);
        g.setColour(Pal::grey()); g.drawText("GATE", RectF(x - 30.0f, bar.getBottom() + 6.0f, 60.0f, 11.0f), juce::Justification::centred, false);
    }
}

void PeepChainView::paintResetCapsule(juce::Graphics& g) {
    // a two-tone capsule: pink half, white half
    const auto r = resetBounds(); const float rad = r.getHeight() * 0.5f;
    const bool peep = proc.isPeepPreset();
    juce::Path shape; shape.addRoundedRectangle(r, rad);
    if (peep) g.beginTransparencyLayer(0.55f);
    juce::DropShadow(juce::Colours::black.withAlpha(0.8f), 10, {0, 5}).drawForPath(g, shape);
    if (!peep) juce::DropShadow(Pal::pink().withAlpha(overReset ? 0.9f : 0.5f), 12, {}).drawForPath(g, shape);
    pillFill(g, shape, r);
    {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(shape);
        const auto half = r.withWidth(34.0f);
        g.setGradientFill(juce::ColourGradient(Pal::hot(), 0.0f, r.getY(), Pal::pink().darker(0.25f), 0.0f, r.getBottom(), false)); g.fillRect(half);
        g.setColour(juce::Colours::white.withAlpha(0.45f)); g.fillRoundedRectangle(half.getX() + 8.0f, r.getY() + 3.5f, 22.0f, 3.0f, 1.5f);
        g.setColour(juce::Colour(0x50000000)); g.fillRect(half.getRight() - 0.6f, r.getY(), 1.2f, r.getHeight());
    }
    g.setColour(juce::Colours::white); g.fillPath(heart(RectF(12.0f, 11.0f).withCentre({r.getX() + 18.0f, r.getCentreY() + 0.5f})));
    stampText(g, peep ? "PEEP'S SETTINGS" : "RESET TO PEEP", r.withTrimmedLeft(34.0f).translated(0.0f, 1.5f), labelFont(15.0f), juce::Justification::centred, 1.4f);
    if (peep) g.endTransparencyLayer();
}

void PeepChainView::paintAutoGainCapsule(juce::Graphics& g) {
    // same two-tone capsule as the reset one; its pink end is lit while Auto Gain is on
    const auto r = autoGainBounds(); const float rad = r.getHeight() * 0.5f;
    const bool on = proc.apvts.getRawParameterValue("autoGain")->load() > 0.5f;
    juce::Path shape; shape.addRoundedRectangle(r, rad);
    juce::DropShadow(juce::Colours::black.withAlpha(0.8f), 10, {0, 5}).drawForPath(g, shape);
    if (on || overAutoGain) juce::DropShadow(Pal::pink().withAlpha(on ? 0.8f : 0.45f), 12, {}).drawForPath(g, shape);
    pillFill(g, shape, r);
    {
        juce::Graphics::ScopedSaveState s(g);
        g.reduceClipRegion(shape);
        const auto half = r.withWidth(34.0f);
        if (on) g.setGradientFill(juce::ColourGradient(Pal::hot(), 0.0f, r.getY(), Pal::pink().darker(0.25f), 0.0f, r.getBottom(), false));
        else    g.setGradientFill(juce::ColourGradient(Pal::dim().brighter(0.2f), 0.0f, r.getY(), Pal::dim().darker(0.5f), 0.0f, r.getBottom(), false));
        g.fillRect(half);
        g.setColour(juce::Colours::white.withAlpha(on ? 0.45f : 0.15f)); g.fillRoundedRectangle(half.getX() + 8.0f, r.getY() + 3.5f, 22.0f, 3.0f, 1.5f);
        g.setColour(juce::Colour(0x50000000)); g.fillRect(half.getRight() - 0.6f, r.getY(), 1.2f, r.getHeight());
    }
    // an "=" sign for level match on the coloured end
    g.setColour(juce::Colours::white.withAlpha(on ? 1.0f : 0.5f));
    const float cx = r.getX() + 18.0f, cy = r.getCentreY() + 0.5f;
    g.fillRoundedRectangle(cx - 6.0f, cy - 4.0f, 12.0f, 2.6f, 1.3f); g.fillRoundedRectangle(cx - 6.0f, cy + 1.4f, 12.0f, 2.6f, 1.3f);
    const float db = autoGainShownDb;
    const juce::String text = on ? "AUTO GAIN " + juce::String(db > 0.05f ? "+" : "") + juce::String(db, 1) : juce::String("AUTO GAIN OFF");
    stampText(g, text, r.withTrimmedLeft(34.0f).translated(0.0f, 1.5f), labelFont(15.0f), juce::Justification::centred, on ? 1.4f : 1.0f);
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
    g.setColour(Pal::grey().withAlpha(0.8f)); g.setFont(labelFont(12.0f));
    g.drawText(versionText(), RectF(kVersionX, kVersionY, 50.0f, 16.0f), juce::Justification::centredLeft, false);
    if (UpdateChecker::isConfigured()) {
        const auto r = githubBounds();
        g.setColour(overGithub ? Pal::hot() : Pal::grey().withAlpha(0.8f));
        const auto icon = RectF(r.getX(), r.getY(), 12.0f, 12.0f).withCentre({r.getX() + 6.0f, r.getCentreY() - 0.5f});
        g.fillPath(githubMark(), githubMark().getTransformToScaleToFit(icon, true));
        g.drawText("GITHUB", r.withTrimmedLeft(16.0f), juce::Justification::centredLeft, false);
    }
    if (updates->getState() == UpdateChecker::State::available) {
        const auto r = updateBounds();
        g.setColour(overUpdate ? Pal::hot() : Pal::pink()); g.setFont(labelFont(12.0f));
        g.drawText("NEW VERSION " + updates->getLatestVersion().toUpperCase() + "  -  GET IT", r, juce::Justification::centredLeft, false);
    }
}

RectF PeepChainView::controlBounds(int c) const {
    if (c == kSync) {                                              // beside the DELAY caption
        const float cx = kW * 7.0f / 12.0f;
        return {cx + 27.0f, kKnobY - 19.0f, 38.0f, 15.0f};
    }
    auto textW = [](const char* t) { return juce::GlyphArrangement::getStringWidth(labelFont(12.0f), t) + 6.0f; };
    const float y = 101.0f, h = 16.0f, pill = 20.0f;
    float x = kW - kBarX;                                          // right-aligned with the capsules below
    RectF r[kNumControls];
    auto take = [&](int ctl, float w, float gapBefore) { r[ctl] = {x - w, y, w, h}; x -= w + gapBefore; };
    take(kSize, textW("SIZE 200%"), 14.0f);
    take(kCopy, textW("COPY A TO B"), 6.0f);
    take(kSlotB, pill, 4.0f);
    take(kSlotA, pill, 18.0f);
    take(kRedo, textW("REDO"), 6.0f);
    take(kUndo, textW("UNDO"), 0.0f);
    return c >= 0 && c < kNumControls ? r[c] : RectF();
}

bool PeepChainView::isControlEnabled(int c) const {
    if (c == kUndo) return proc.canUndo();
    if (c == kRedo) return proc.canRedo();
    if (c == kSlotA || c == kSlotB) return proc.getActiveSlot() != (c == kSlotA ? 0 : 1);
    return c >= 0 && c < kNumControls;
}

int PeepChainView::controlAt(PointF p) const {
    for (int c = 0; c < kNumControls; ++c) if (controlBounds(c).contains(p) && isControlEnabled(c)) return c;
    return -1;
}

juce::uint32 PeepChainView::controlsKey() const {
    return (proc.canUndo() ? 1u : 0u) | (proc.canRedo() ? 2u : 0u) | (proc.getActiveSlot() == 1 ? 4u : 0u)
         | (proc.apvts.getRawParameterValue("delaySync")->load() > 0.5f ? 8u : 0u);
}

void PeepChainView::activate(int c) {
    switch (c) {
        case kUndo: proc.undo(); break;
        case kRedo: proc.redo(); break;
        case kSlotA: proc.selectSlot(0); break;
        case kSlotB: proc.selectSlot(1); break;
        case kCopy: proc.copyToOtherSlot(); break;
        case kSize: if (onSizeClicked) onSizeClicked(); break;
        case kSync:
            if (auto* p = proc.apvts.getParameter("delaySync")) {
                p->beginChangeGesture(); p->setValueNotifyingHost(p->getValue() > 0.5f ? 0.0f : 1.0f); p->endChangeGesture();
            }
            break;
        default: break;
    }
    repaint();
}

void PeepChainView::paintControls(juce::Graphics& g) {
    auto colourFor = [&](int c) {
        if (!isControlEnabled(c)) return Pal::grey().withAlpha(0.3f);
        return overControl == c ? Pal::hot() : Pal::grey().withAlpha(0.85f);
    };
    auto text = [&](int c, const juce::String& t, juce::Justification j) {
        g.setColour(colourFor(c)); g.setFont(labelFont(12.0f)); g.drawText(t, controlBounds(c), j, false);
    };
    // a small capsule: lit pink when on, an outline otherwise
    auto pill = [&](int c, const juce::String& t, bool lit, float fontH) {
        const auto r = controlBounds(c); const float rad = r.getHeight() * 0.5f;
        if (lit) {
            juce::Path shape; shape.addRoundedRectangle(r, rad);
            juce::DropShadow(Pal::pink().withAlpha(0.6f), 8, {}).drawForPath(g, shape);
            g.setGradientFill(juce::ColourGradient(Pal::hot(), 0.0f, r.getY(), Pal::pink().darker(0.25f), 0.0f, r.getBottom(), false));
            g.fillPath(shape);
            g.setColour(juce::Colours::white);
        } else {
            g.setColour(overControl == c ? Pal::hot() : Pal::grey().withAlpha(0.6f));
            g.drawRoundedRectangle(r.reduced(0.5f), rad - 0.5f, 1.0f);
        }
        g.setFont(labelFont(fontH)); g.drawText(t, r.translated(lit ? 0.5f : 0.5f, 0.5f), juce::Justification::centred, false);
    };
    text(kUndo, "UNDO", juce::Justification::centred);
    text(kRedo, "REDO", juce::Justification::centred);
    pill(kSlotA, "A", proc.getActiveSlot() == 0, 12.0f);
    pill(kSlotB, "B", proc.getActiveSlot() == 1, 12.0f);
    text(kCopy, proc.getActiveSlot() == 0 ? "COPY A TO B" : "COPY B TO A", juce::Justification::centred);
    text(kSize, "SIZE " + juce::String(juce::roundToInt(scaleShown * 100.0f)) + "%", juce::Justification::centredRight);
    pill(kSync, "SYNC", proc.apvts.getRawParameterValue("delaySync")->load() > 0.5f, 11.0f);
}

void PeepChainView::paint(juce::Graphics& g) {
    // The cached pictures are drawn at (at least) the screen's own resolution, so a bigger window stays sharp.
    const float want = juce::jmax(2.0f, std::ceil(g.getInternalContext().getPhysicalPixelScaleFactor() - 0.01f));
    if (want != imageScale) { imageScale = want; backdrop = {}; chainLayer = {}; liftedLayer = {}; }
    if (backdrop.isNull()) renderBackdrop();
    g.drawImage(backdrop, getLocalBounds().toFloat());
    const float W = (float)getWidth();

    g.setColour(Pal::grey()); g.setFont(labelFont(13.0f));
    g.drawText("CLICK A PIECE TO TAKE IT OUT  /  DRAG IT TO CHANGE THE ORDER", RectF(kBarX + 8.0f, 128.0f, 420.0f, 18.0f), juce::Justification::centredLeft, false);
    paintResetCapsule(g);
    paintAutoGainCapsule(g);
    paintVersion(g);
    paintControls(g);
    paintChain(g);

    // knob captions
    const char* names[kKnobs] = {"INPUT", "GATE THRESHOLD", "COMP THRESHOLD", "DELAY", "REVERB", "OUTPUT"};
    const char* hints[kKnobs] = {"HOW HARD IT HITS", "PEEP -46", "PEEP -26", "PEEP -16.5", "PEEP -6.0", "LEVEL AFTER"};
    const bool live[kKnobs] = {true, proc.isStageOn(PeepChain::kGate), proc.isStageOn(PeepChain::kComp), true, true, true};
    for (int i = 0; i < kKnobs; ++i) {
        const float cx = W * (2.0f * (float)i + 1.0f) / (2.0f * (float)kKnobs);
        g.setColour(Pal::pale().withAlpha(live[i] ? 1.0f : 0.4f)); g.setFont(labelFont(16.0f).withExtraKerningFactor(0.07f));
        g.drawText(names[i], RectF(cx - 71.0f, kKnobY - 22.0f, 142.0f, 22.0f), juce::Justification::centred, false);
        g.setColour(Pal::grey().withAlpha(live[i] ? 1.0f : 0.4f)); g.setFont(labelFont(13.0f));
        g.drawText(hints[i], RectF(cx - 71.0f, kKnobY + kKnobSize, 142.0f, 16.0f), juce::Justification::centred, false);
    }

    paintLevelMeter(g, {40.0f, kMeterY, W * 0.5f - 60.0f, 22.0f}, "IN", inDb, true);
    paintLevelMeter(g, {W * 0.5f + 20.0f, kMeterY, W * 0.5f - 60.0f, 22.0f}, "OUT", outDb, false);
    g.setColour(Pal::grey().withAlpha(0.75f)); g.setFont(labelFont(12.0f));
    g.drawText("DOUBLE-CLICK A KNOB FOR PEEP'S VALUE", getLocalBounds().toFloat().withTop(kH - 28.0f).withHeight(16.0f), juce::Justification::centred, false);
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
