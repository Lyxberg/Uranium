#include "PluginEditor.h"

#include "BinaryData.h"

//==============================================================================
namespace
{
    constexpr int kWidth  = 800;
    constexpr int kHeight = 600;

    const char* kTitle = "Uranium";

    const juce::Colour kBg      (0xff1ed3a3);   // bright green
    const juce::Colour kPanel   (0xff12b98d);   // spectrum well
    const juce::Colour kPanelHi (0xff16c396);
    const juce::Colour kMint    (0xffa9f1da);   // controls
    const juce::Colour kInk     (0xff2f3b38);   // type

    const char* kIds[6]   { "depth", "sharp", "attack", "release", "mix", "output" };
    const char* kNames[6] { "Depth", "Sharp", "Attack", "Release", "MIX", "Output" };

    const float kKnobX[5] { 65.0f, 196.0f, 327.0f, 458.0f, 589.0f };
    constexpr float kKnobY = 514.0f;
    constexpr float kKnobR = 40.0f;
    constexpr float kMixX  = 711.0f;
    constexpr float kMixY  = 127.0f;
    constexpr float kMixR  = 62.0f;
    constexpr float kMsX   = 711.0f;
    constexpr float kMsY   = 291.0f;

    const juce::Rectangle<int> kDeltaRect  (657, 378, 116, 50);
    const juce::Rectangle<int> kLinkRect   (657, 495, 116, 50);
    const juce::Rectangle<int> kPresetRect (250, 12, 312, 32);
    const juce::Rectangle<int> kSaveRect   (574, 12, 92, 32);
    const juce::Rectangle<int> kScRect     (676, 12, 104, 32);

    juce::Font titleFont (float h) { return juce::Font ("Arial Black", h, juce::Font::plain); }
    juce::Font labelFont (float h) { return juce::Font ("Segoe UI", h, juce::Font::bold); }
    juce::Font heavyFont (float h) { return juce::Font ("Arial Black", h, juce::Font::plain); }
    juce::Font valueFont (float h) { return juce::Font ("Segoe UI", h, juce::Font::plain); }

    // a tileable speckle, so the flat green does not look like plastic
    const juce::Image& grain()
    {
        static juce::Image img;

        if (img.isNull())
        {
            img = juce::Image (juce::Image::ARGB, 128, 128, true);

            juce::Random rnd (7);
            juce::Image::BitmapData d (img, juce::Image::BitmapData::writeOnly);

            for (int y = 0; y < 128; ++y)
                for (int x = 0; x < 128; ++x)
                    d.setPixelColour (x, y, juce::Colours::white.withAlpha ((float) rnd.nextInt (24) / 255.0f));
        }

        return img;
    }

    void softShadow (juce::Graphics& g, juce::Rectangle<float> r, float radius, float alpha)
    {
        for (int i = 5; i >= 1; --i)
        {
            g.setColour (juce::Colours::black.withAlpha (alpha));
            g.fillRoundedRectangle (r.expanded ((float) i * 2.0f).translated (0.0f, 2.0f), radius);
        }
    }

    void strokeGlow (juce::Graphics& g, const juce::Path& p, juce::Colour c, float width)
    {
        g.setColour (c.withAlpha (0.12f));
        g.strokePath (p, juce::PathStrokeType (width + 6.0f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
        g.setColour (c.withAlpha (0.25f));
        g.strokePath (p, juce::PathStrokeType (width + 2.5f, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (width, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }
}

//==============================================================================
TameAudioProcessorEditor::TameAudioProcessorEditor (TameAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setSize (kWidth, kHeight);
    installFactoryPresets();
    refreshPresets();
    startTimerHz (30);
}

//==============================================================================
void TameAudioProcessorEditor::timerCallback()
{
    scopeValid = processorRef.readScope (scope);
    repaint();
}

//==============================================================================
juce::Rectangle<int> TameAudioProcessorEditor::scopeBounds() const
{
    return { 22, 58, 604, 389 };
}

juce::Point<float> TameAudioProcessorEditor::knobCentre (int index) const
{
    if (index == 4)
        return { kMixX, kMixY };

    return { kKnobX[juce::jlimit (0, 4, index == 5 ? 4 : index)], kKnobY };
}

float TameAudioProcessorEditor::knobRadius (int index) const
{
    return index == 4 ? kMixR : kKnobR;
}

float TameAudioProcessorEditor::knobNorm (int index) const
{
    if (auto* p = processorRef.apvts.getParameter (kIds[juce::jlimit (0, 5, index)]))
        return p->getValue();

    return 0.0f;
}

void TameAudioProcessorEditor::setKnobNorm (int index, float norm)
{
    if (auto* p = processorRef.apvts.getParameter (kIds[juce::jlimit (0, 5, index)]))
        p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, norm));
}

float TameAudioProcessorEditor::freqToX (float f) const
{
    const auto r = scopeBounds().reduced (14, 16);

    const float u = std::log (juce::jmax (20.0f, f) / 20.0f) / std::log (1000.0f);

    return (float) r.getX() + juce::jlimit (0.0f, 1.0f, u) * (float) r.getWidth();
}

float TameAudioProcessorEditor::xToFreq (float x) const
{
    const auto r = scopeBounds().reduced (14, 16);

    const float u = juce::jlimit (0.0f, 1.0f, (x - (float) r.getX()) / (float) r.getWidth());

    return 20.0f * std::pow (1000.0f, u);
}

//==============================================================================
void TameAudioProcessorEditor::pointParams (int index, float& f, float& g) const
{
    const int i = juce::jlimit (0, 4, index);

    f = processorRef.apvts.getRawParameterValue ("p" + juce::String (i + 1) + "f")->load();
    g = processorRef.apvts.getRawParameterValue ("p" + juce::String (i + 1) + "g")->load();
}

void TameAudioProcessorEditor::setPoint (int index, float f, float g)
{
    const int i = juce::jlimit (0, 4, index);

    if (auto* p = processorRef.apvts.getParameter ("p" + juce::String (i + 1) + "f"))
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (20.0f, 20000.0f, f)));

    if (auto* p = processorRef.apvts.getParameter ("p" + juce::String (i + 1) + "g"))
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (-18.0f, 18.0f, g)));
}

//==============================================================================
void TameAudioProcessorEditor::paint (juce::Graphics& g)
{
    // base + speckle
    juce::ColourGradient base (kBg.brighter (0.06f), 0.0f, 0.0f,
                               kBg.darker (0.06f), 0.0f, (float) kHeight, false);
    g.setGradientFill (base);
    g.fillAll();

    g.setTiledImageFill (grain(), 0, 0, 0.75f);
    g.fillAll();

    // title, with a whisper of a shadow
    g.setFont (titleFont (34.0f));
    g.setColour (juce::Colours::black.withAlpha (0.16f));
    g.drawText (kTitle, 21, 8, 400, 46, juce::Justification::centredLeft, false);
    g.setColour (kInk);
    g.drawText (kTitle, 20, 6, 400, 46, juce::Justification::centredLeft, false);

    drawPresetBar (g);

    // spectrum well
    const auto sb = scopeBounds().toFloat();

    softShadow (g, sb, 10.0f, 0.030f);

    {
        juce::ColourGradient pg (kPanel.darker (0.26f), 0.0f, sb.getY(),
                                 kPanel.darker (0.12f), 0.0f, sb.getBottom(), false);
        g.setGradientFill (pg);
        g.fillRect (sb);
    }

    // inner shadow along the top edge
    {
        juce::ColourGradient in (juce::Colours::black.withAlpha (0.20f), 0.0f, sb.getY(),
                                 juce::Colours::transparentBlack, 0.0f, sb.getY() + 18.0f, false);
        g.setGradientFill (in);
        g.fillRect (sb.withHeight (18.0f));
    }

    g.setColour (juce::Colours::white.withAlpha (0.10f));
    g.drawLine (sb.getX(), sb.getY() + 0.5f, sb.getRight(), sb.getY() + 0.5f, 1.0f);
    g.setColour (juce::Colours::black.withAlpha (0.12f));
    g.drawLine (sb.getX(), sb.getBottom() - 0.5f, sb.getRight(), sb.getBottom() - 0.5f, 1.0f);

    drawSpectrum (g);

    for (int i = 0; i < 6; ++i)
        drawKnob (g, i);

    drawLabels (g);

    // buttons
    {
        const auto drawButton = [&] (juce::Rectangle<int> r, const juce::String& text, bool on)
        {
            const auto rf = r.toFloat();

            softShadow (g, rf, 4.0f, on ? 0.022f : 0.030f);

            juce::ColourGradient bg (on ? kPanel.brighter (0.06f) : kMint.brighter (0.10f), 0.0f, rf.getY(),
                                     on ? kPanel.darker (0.10f) : kMint.darker (0.10f), 0.0f, rf.getBottom(), false);
            g.setGradientFill (bg);
            g.fillRect (rf);

            g.setColour (juce::Colours::white.withAlpha (on ? 0.06f : 0.35f));
            g.drawLine (rf.getX() + 1.0f, rf.getY() + 0.5f, rf.getRight() - 1.0f, rf.getY() + 0.5f, 1.0f);

            g.setColour (on ? kMint : kPanel);
            g.setFont (labelFont (18.0f));
            g.drawText (text, r, juce::Justification::centred, false);
        };

        drawButton (kDeltaRect, "Delta", processorRef.apvts.getRawParameterValue ("delta")->load() > 0.5f);
        drawButton (kLinkRect, "Link", processorRef.apvts.getRawParameterValue ("link")->load() > 0.5f);

        // sidechain: lamp lights when the host has the bus connected
        {
            auto* sc = processorRef.getBus (true, 1);
            const bool connected = sc != nullptr && sc->isEnabled();
            const bool on = processorRef.apvts.getRawParameterValue ("sc")->load() > 0.5f;
            const bool live = connected && on;

            const auto sr = kScRect.toFloat();

            juce::ColourGradient bg (live ? kPanel.brighter (0.06f) : kMint.brighter (0.10f), 0.0f, sr.getY(),
                                     live ? kPanel.darker (0.10f) : kMint.darker (0.10f), 0.0f, sr.getBottom(), false);
            g.setGradientFill (bg);
            g.fillRect (sr);

            g.setColour (juce::Colours::white.withAlpha (live ? 0.06f : 0.30f));
            g.drawLine (sr.getX() + 1.0f, sr.getY() + 0.5f, sr.getRight() - 1.0f, sr.getY() + 0.5f, 1.0f);

            g.setColour (live ? kMint : kPanel.withAlpha (connected ? 0.45f : 0.22f));
            g.fillEllipse (kScRect.getX() + 10.0f, kScRect.getCentreY() - 4.0f, 8.0f, 8.0f);

            g.setColour (live ? kMint : kPanel.withAlpha (connected ? 0.85f : 0.45f));
            g.setFont (labelFont (13.0f));
            g.drawText (connected ? (on ? "SC ON" : "SC OFF") : "SC -",
                        kScRect.withTrimmedLeft (22), juce::Justification::centred, false);
        }
    }

    // M/S monitor
    {
        const juce::Point<float> c (kMsX, kMsY);
        const float ring = 56.0f;

        const bool on = processorRef.apvts.getRawParameterValue ("ms")->load() > 0.5f;
        const float v = processorRef.apvts.getRawParameterValue ("msw")->load();
        const float amount = std::abs (v) * 0.01f;

        juce::Path arc;
        arc.addCentredArc (c.x, c.y, ring, ring, 0.0f,
                           -juce::MathConstants<float>::pi * 0.75f,
                           juce::MathConstants<float>::pi * 0.75f, true);

        g.setColour (juce::Colours::black.withAlpha (0.10f));
        g.strokePath (arc, juce::PathStrokeType (11.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        g.setColour (kMint.withAlpha (on ? 1.0f : 0.45f));
        g.strokePath (arc, juce::PathStrokeType (9.0f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));

        // a hair of light along the outer edge
        {
            juce::Path hi;
            hi.addCentredArc (c.x, c.y, ring + 3.0f, ring + 3.0f, 0.0f, -2.2f, -0.9f, true);
            g.setColour (juce::Colours::white.withAlpha (on ? 0.30f : 0.12f));
            g.strokePath (hi, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved));
        }

        const float a = juce::jlimit (-1.0f, 1.0f, v * 0.01f)
                          * juce::MathConstants<float>::pi * 0.75f;

        if (on)
        {
            const float dx = c.x + std::sin (a) * ring;
            const float dy = c.y - std::cos (a) * ring;

            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillEllipse (dx - 7.0f, dy - 6.4f, 14.0f, 14.0f);

            g.setColour (kInk);
            g.fillEllipse (dx - 6.0f, dy - 6.0f, 12.0f, 12.0f);
        }

        g.setColour (kInk);
        g.setFont (labelFont (14.0f));
        g.drawText ("M/S", (int) c.x - 44, (int) c.y - 21, 88, 18,
                    juce::Justification::centred, false);

        juce::String state;

        if (! on)
            state = "stereo";
        else if (amount < 0.02f)
            state = "mid";
        else
            state = "side " + juce::String (juce::roundToInt (amount * 100.0f)) + "%";

        g.setColour (kInk.withAlpha (0.80f));
        g.setFont (labelFont (10.5f));
        g.drawText (state, (int) c.x - 46, (int) c.y + 0, 92, 14,
                    juce::Justification::centred, false);
    }
}

//==============================================================================
void TameAudioProcessorEditor::drawPresetBar (juce::Graphics& g)
{
    const auto rf = kPresetRect.toFloat();

    // field
    {
        juce::ColourGradient bg (kMint.brighter (0.06f), 0.0f, rf.getY(),
                                 kMint.darker (0.08f), 0.0f, rf.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRect (rf);

        g.setColour (juce::Colours::white.withAlpha (0.30f));
        g.drawLine (rf.getX() + 1.0f, rf.getY() + 0.5f, rf.getRight() - 1.0f, rf.getY() + 0.5f, 1.0f);

        g.setColour (kPanel);
        g.setFont (labelFont (14.0f));
        g.drawText ("PRESET", (int) rf.getX() + 12, (int) rf.getY(), 70, (int) rf.getHeight(),
                    juce::Justification::centredLeft, false);

        g.setColour (kInk);
        g.setFont (labelFont (14.0f));
        g.drawText (currentPresetName, (int) rf.getX() + 88, (int) rf.getY(),
                    (int) rf.getWidth() - 118, (int) rf.getHeight(),
                    juce::Justification::centredLeft, false);

        // caret
        juce::Path caret;
        const float cx = rf.getRight() - 18.0f;
        const float cy = rf.getCentreY();
        caret.startNewSubPath (cx - 5.0f, cy - 2.5f);
        caret.lineTo (cx, cy + 3.0f);
        caret.lineTo (cx + 5.0f, cy - 2.5f);

        g.setColour (kPanel);
        g.strokePath (caret, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // save button
    {
        const auto sr = kSaveRect.toFloat();

        softShadow (g, sr, 4.0f, 0.030f);

        juce::ColourGradient bg (kMint.brighter (0.10f), 0.0f, sr.getY(),
                                 kMint.darker (0.10f), 0.0f, sr.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRect (sr);

        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawLine (sr.getX() + 1.0f, sr.getY() + 0.5f, sr.getRight() - 1.0f, sr.getY() + 0.5f, 1.0f);

        g.setColour (kPanel);
        g.setFont (labelFont (14.0f));
        g.drawText ("Save", kSaveRect, juce::Justification::centred, false);
    }
}

//==============================================================================
void TameAudioProcessorEditor::drawSpectrum (juce::Graphics& g)
{
    const auto r = scopeBounds().reduced (14, 16);

    g.setColour (kBg.withAlpha (0.10f));

    for (int i = 1; i < 8; ++i)
        g.drawVerticalLine (r.getX() + r.getWidth() * i / 8, (float) r.getY(), (float) r.getBottom());

    for (int i = 1; i < 4; ++i)
        g.drawHorizontalLine (r.getY() + r.getHeight() * i / 4, (float) r.getX(), (float) r.getRight());

    scopeValid = processorRef.readScope (scope);

    if (! scopeValid || scope.inDb.empty())
        return;

    const int w = juce::jmax (2, r.getWidth());
    const double binHz = processorRef.getSampleRate() > 0.0
                            ? processorRef.getSampleRate() / 2048.0 : 48000.0 / 2048.0;

    const int nb = (int) scope.inDb.size();

    std::vector<float> aIn ((size_t) w), aOut ((size_t) w), aSm ((size_t) w);

    for (int px = 0; px < w; ++px)
    {
        const float u0 = (float) px / (float) w;
        const float u1 = (float) (px + 1) / (float) w;

        const float f0 = 20.0f * std::pow (1000.0f, u0);
        const float f1 = 20.0f * std::pow (1000.0f, u1);

        const int b0 = juce::jlimit (1, nb - 1, (int) (f0 / binHz));
        const int b1 = juce::jlimit (b0, nb - 1, (int) std::ceil (f1 / binHz));

        float mIn = -100.0f, mSm = -100.0f, mRed = 0.0f;

        for (int b = b0; b <= b1; ++b)
        {
            mIn  = juce::jmax (mIn,  scope.inDb[(size_t) b]);
            mSm  = juce::jmax (mSm,  scope.smoothDb[(size_t) b]);
            mRed = juce::jmin (mRed, scope.redDb[(size_t) b]);
        }

        aIn[(size_t) px]  = mIn;
        aSm[(size_t) px]  = mSm;
        aOut[(size_t) px] = mIn + mRed;
    }

    for (int pass = 0; pass < 2; ++pass)
    {
        for (int px = 1; px < w - 1; ++px)
        {
            aIn[(size_t) px]  = 0.25f * aIn[(size_t) (px - 1)] + 0.5f * aIn[(size_t) px] + 0.25f * aIn[(size_t) (px + 1)];
            aOut[(size_t) px] = 0.25f * aOut[(size_t) (px - 1)] + 0.5f * aOut[(size_t) px] + 0.25f * aOut[(size_t) (px + 1)];
            aSm[(size_t) px]  = 0.25f * aSm[(size_t) (px - 1)] + 0.5f * aSm[(size_t) px] + 0.25f * aSm[(size_t) (px + 1)];
        }
    }

    const auto yFor = [&] (float db)
    {
        const float u = juce::jlimit (0.0f, 1.0f, (juce::jlimit (-90.0f, 0.0f, db) + 90.0f) / 90.0f);
        return (float) r.getBottom() - u * (float) r.getHeight();
    };

    juce::Path inPath, outPath, smoothPath;

    for (int px = 0; px < w; ++px)
    {
        const float x = (float) r.getX() + (float) px;

        const float yIn  = yFor (aIn[(size_t) px]);
        const float yOut = yFor (aOut[(size_t) px]);
        const float ySm  = yFor (aSm[(size_t) px]);

        if (px == 0)
        {
            inPath.startNewSubPath (x, yIn);
            outPath.startNewSubPath (x, yOut);
            smoothPath.startNewSubPath (x, ySm);
        }
        else
        {
            inPath.lineTo (x, yIn);
            outPath.lineTo (x, yOut);
            smoothPath.lineTo (x, ySm);
        }
    }

    {
        juce::Path fill (inPath);
        fill.lineTo ((float) r.getRight(), (float) r.getBottom());
        fill.lineTo ((float) r.getX(), (float) r.getBottom());
        fill.closeSubPath();

        juce::ColourGradient fg (kBg.withAlpha (0.16f), 0.0f, (float) r.getY(),
                                 kBg.withAlpha (0.03f), 0.0f, (float) r.getBottom(), false);
        g.setGradientFill (fg);
        g.fillPath (fill);
    }

    g.setColour (kMint.withAlpha (0.20f));
    g.strokePath (smoothPath, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));

    strokeGlow (g, outPath, kMint, 1.8f);

    //--- sensitivity curve ---------------------------------------------------
    {
        const float zeroY = (float) r.getCentreY();
        const float scale = (float) r.getHeight() * 0.45f / 18.0f;

        g.setColour (kMint.withAlpha (0.16f));
        g.drawHorizontalLine ((int) zeroY, (float) r.getX(), (float) r.getRight());

        float pf[5], pg[5];

        for (int i = 0; i < 5; ++i)
            pointParams (i, pf[i], pg[i]);

        juce::Path biasCurve;

        const int steps = 260;

        for (int s = 0; s <= steps; ++s)
        {
            const float u = (float) s / (float) steps;
            const float f = 20.0f * std::pow (1000.0f, u);

            float bias = pg[4];

            if (f <= pf[0])
            {
                bias = pg[0];
            }
            else
            {
                for (int i = 0; i < 4; ++i)
                {
                    if (f <= pf[i + 1])
                    {
                        const double lg = (double) std::log2 (f) - (double) std::log2 (pf[i]);
                        const double span = juce::jmax (1.0e-6, (double) std::log2 (pf[i + 1]) - (double) std::log2 (pf[i]));
                        const double t = juce::jlimit (0.0, 1.0, lg / span);
                        const double sm = t * t * (3.0 - 2.0 * t);
                        bias = (float) (pg[i] + (pg[i + 1] - pg[i]) * sm);
                        break;
                    }
                }
            }

            const float x = (float) r.getX() + u * (float) r.getWidth();
            const float y = zeroY - bias * scale;

            if (s == 0) biasCurve.startNewSubPath (x, y);
            else        biasCurve.lineTo (x, y);
        }

        strokeGlow (g, biasCurve, kBg.brighter (0.10f), 2.2f);

        for (int i = 0; i < 5; ++i)
        {
            const float x = freqToX (pf[i]);
            const float y = zeroY - pg[i] * scale;

            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillEllipse (x - 8.0f, y - 7.0f, 16.0f, 16.0f);

            g.setColour (kPanel);
            g.fillEllipse (x - 7.5f, y - 7.5f, 15.0f, 15.0f);

            juce::ColourGradient dot (kMint.brighter (0.15f), x - 2.0f, y - 3.0f,
                                      kMint.darker (0.10f), x + 3.0f, y + 4.0f, true);
            g.setGradientFill (dot);
            g.fillEllipse (x - 5.5f, y - 5.5f, 11.0f, 11.0f);
        }

        if (pointDrag >= 0 && pointDrag < 5)
        {
            const float x = freqToX (pf[pointDrag]);
            const float y = zeroY - pg[pointDrag] * scale;

            juce::String txt = pf[pointDrag] >= 999.5f
                                 ? juce::String (pf[pointDrag] / 1000.0f, 2) + " kHz"
                                 : juce::String (juce::roundToInt (pf[pointDrag])) + " Hz";

            txt << "   " << (pg[pointDrag] >= 0.0f ? "+" : "") << juce::String (pg[pointDrag], 1) << " dB";

            const int tw = 150;
            auto box = juce::Rectangle<int> ((int) x - tw / 2, (int) y - 36, tw, 23)
                         .constrainedWithin (scopeBounds().expanded (0, 6));

            g.setColour (kInk.withAlpha (0.90f));
            g.fillRoundedRectangle (box.toFloat(), 4.0f);

            g.setColour (kMint);
            g.setFont (labelFont (11.0f));
            g.drawText (txt, box, juce::Justification::centred, false);
        }
    }

    // ruler
    g.setColour (kMint.withAlpha (0.42f));
    g.setFont (valueFont (9.0f));

    const int marks[7] { 100, 300, 1000, 3000, 6000, 12000, 18000 };

    for (int m : marks)
        g.drawText (m >= 1000 ? juce::String (m / 1000) + "k" : juce::String (m),
                    juce::Rectangle<int> ((int) freqToX ((float) m) - 16, r.getBottom() - 14, 32, 12),
                    juce::Justification::centred, false);
}

//==============================================================================
void TameAudioProcessorEditor::drawKnob (juce::Graphics& g, int index)
{
    const auto c = knobCentre (index);
    const float radius = knobRadius (index);
    const float norm = knobNorm (index);

    // shadow
    {
        juce::ColourGradient sh (juce::Colours::black.withAlpha (0.22f), c.x, c.y + 4.0f,
                                 juce::Colours::transparentBlack, c.x, c.y + radius + 12.0f, true);
        g.setGradientFill (sh);
        g.fillEllipse (c.x - radius - 12.0f, c.y - radius - 8.0f,
                       (radius + 12.0f) * 2.0f, (radius + 12.0f) * 2.0f);
    }

    // body
    {
        juce::ColourGradient body (kMint.brighter (0.14f), c.x - radius * 0.35f, c.y - radius * 0.45f,
                                   kMint.darker (0.14f), c.x + radius * 0.25f, c.y + radius * 0.6f, true);
        g.setGradientFill (body);
        g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
    }

    // rim light
    {
        juce::Path hl;
        hl.addCentredArc (c.x, c.y, radius - 1.2f, radius - 1.2f, 0.0f, -2.45f, -0.65f, true);
        g.setColour (juce::Colours::white.withAlpha (0.40f));
        g.strokePath (hl, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
    }

    // pointer: a dot travelling round the rim
    const float a = (-0.75f + 1.5f * norm) * juce::MathConstants<float>::pi;
    const float dr = radius - 9.0f;
    const float px = c.x + std::sin (a) * dr;
    const float py = c.y - std::cos (a) * dr;

    g.setColour (juce::Colours::black.withAlpha (0.18f));
    g.fillEllipse (px - 5.0f, py - 4.2f, 10.0f, 10.0f);

    g.setColour (kInk);
    g.fillEllipse (px - 4.5f, py - 4.5f, 9.0f, 9.0f);

    juce::String valueText;

    if (auto* p = processorRef.apvts.getParameter (kIds[juce::jlimit (0, 5, index)]))
        valueText = p->getCurrentValueAsText();

    g.setColour (kInk.withAlpha (0.80f));
    g.setFont (labelFont (index == 4 ? 12.0f : 10.5f));
    g.drawText (valueText,
                juce::Rectangle<int> ((int) c.x - 40, (int) c.y + (index == 4 ? 22 : 14), 80, 14),
                juce::Justification::centred, false);
}

//==============================================================================
void TameAudioProcessorEditor::drawLabels (juce::Graphics& g)
{
    g.setFont (heavyFont (14.0f));

    for (int i = 0; i < 6; ++i)
    {
        const auto c = knobCentre (i);
        const float y = c.y + knobRadius (i) + 4.0f;

        const auto box = juce::Rectangle<int> ((int) c.x - 62, (int) y, 124, 22);

        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.drawText (kNames[i], box.translated (0, 1), juce::Justification::centred, false);

        g.setColour (kInk);
        g.drawText (kNames[i], box, juce::Justification::centred, false);
    }
}

//==============================================================================
juce::File TameAudioProcessorEditor::presetFolder() const
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile ("URANIUM")
             .getChildFile ("Presets");
}

void TameAudioProcessorEditor::installFactoryPresets()
{
    auto dir = presetFolder();

    if (dir.isDirectory() && dir.getNumberOfChildFiles (juce::File::findFiles, "*.xml") > 0)
        return;

    dir.createDirectory();

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        int size = 0;
        const char* data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);

        if (data != nullptr && size > 0)
            dir.getChildFile (BinaryData::originalFilenames[i]).replaceWithData (data, (size_t) size);
    }
}

void TameAudioProcessorEditor::refreshPresets()
{
    juce::StringArray found;

    auto dir = presetFolder();

    if (dir.isDirectory())
    {
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.xml"))
            found.add (f.getFileNameWithoutExtension());
    }

    found.sort (true);

    presetNames.clearQuick();
    presetNames.add ("init");
    presetNames.addArray (found);
}

void TameAudioProcessorEditor::showPresetMenu()
{
    juce::PopupMenu menu;

    for (int i = 0; i < presetNames.size(); ++i)
        menu.addItem (i + 1, presetNames[i], true, presetNames[i] == currentPresetName);

    menu.addSeparator();
    menu.addItem (9000, "Reveal folder");

    juce::Component::SafePointer<TameAudioProcessorEditor> safe (this);

    menu.showMenuAsync (juce::PopupMenu::Options()
                          .withTargetScreenArea (localAreaToGlobal (kPresetRect)),
                        [safe] (int result)
    {
        auto* self = safe.getComponent();

        if (self == nullptr)
            return;

        if (result == 0)
            return;

        if (result == 9000)
        {
            self->presetFolder().revealToUser();
            return;
        }

        const auto name = self->presetNames[result - 1];

        if (name == "init")
        {
            for (auto* p : self->processorRef.getParameters())
                p->setValueNotifyingHost (p->getDefaultValue());

            self->currentPresetName = "init";
        }
        else
        {
            const auto file = self->presetFolder().getChildFile (name + ".xml");

            if (auto xml = juce::XmlDocument::parse (file))
            {
                auto tree = juce::ValueTree::fromXml (*xml);

                if (tree.isValid())
                {
                    self->processorRef.apvts.replaceState (tree);
                    self->currentPresetName = name;
                }
            }
        }

        self->repaint();
    });
}

void TameAudioProcessorEditor::savePreset()
{
    auto* aw = new juce::AlertWindow ("Save preset",
                                      "Name for this preset",
                                      juce::MessageBoxIconType::NoIcon,
                                      this);

    aw->addTextEditor ("name", currentPresetName == "init" ? juce::String ("my preset")
                                                           : currentPresetName);
    aw->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<TameAudioProcessorEditor> safe (this);

    aw->enterModalState (true, juce::ModalCallbackFunction::create ([safe, aw] (int result)
    {
        if (result != 1)
            return;

        auto* self = safe.getComponent();

        if (self == nullptr)
            return;

        auto name = aw->getTextEditorContents ("name").trim();

        if (name.isEmpty())
            return;

        auto dir = self->presetFolder();

        if (! dir.isDirectory())
            dir.createDirectory();

        auto file = dir.getChildFile (juce::File::createLegalFileName (name) + ".xml");

        if (auto xml = self->processorRef.apvts.copyState().createXml())
            xml->writeTo (file);

        self->currentPresetName = file.getFileNameWithoutExtension();
        self->refreshPresets();
        self->repaint();
    }), true);
}

//==============================================================================
void TameAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    dragKnob = -1;
    pointDrag = -1;
    msDrag = false;

    if (kPresetRect.contains (e.getPosition()))
    {
        refreshPresets();
        showPresetMenu();
        return;
    }

    if (kSaveRect.contains (e.getPosition()))
    {
        savePreset();
        return;
    }

    if (kScRect.contains (e.getPosition()))
    {
        if (auto* p = processorRef.apvts.getParameter ("sc"))
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);

        repaint();
        return;
    }

    if (kDeltaRect.contains (e.getPosition()))
    {
        if (auto* p = processorRef.apvts.getParameter ("delta"))
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);

        repaint();
        return;
    }

    if (kLinkRect.contains (e.getPosition()))
    {
        if (auto* p = processorRef.apvts.getParameter ("link"))
            p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);

        repaint();
        return;
    }

    if (e.position.getDistanceFrom ({ kMsX, kMsY }) < 70.0f)
    {
        msDrag = true;
        msMoved = false;
        msStart = processorRef.apvts.getRawParameterValue ("msw")->load();
        return;
    }

    for (int i = 0; i < 6; ++i)
    {
        if (e.position.getDistanceFrom (knobCentre (i)) < knobRadius (i) + 10.0f)
        {
            dragKnob = i;
            dragStartNorm = knobNorm (i);
            return;
        }
    }

    if (! scopeBounds().contains (e.getPosition()))
        return;

    //--- sensitivity points -------------------------------------------------
    {
        auto rr = scopeBounds().reduced (14, 16);
        const float zeroY = (float) rr.getCentreY();
        const float scale = (float) rr.getHeight() * 0.45f / 18.0f;

        for (int i = 0; i < 5; ++i)
        {
            float f = 0.0f, gg = 0.0f;
            pointParams (i, f, gg);

            const juce::Point<float> pos (freqToX (f), zeroY - gg * scale);

            if (e.position.getDistanceFrom (pos) < 16.0f)
            {
                pointDrag = i;
                return;
            }
        }
    }
}

void TameAudioProcessorEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragKnob >= 0)
    {
        setKnobNorm (dragKnob, dragStartNorm - (float) e.getDistanceFromDragStartY() * 0.006f);
        repaint();
        return;
    }

    if (msDrag)
    {
        if (std::abs (e.getDistanceFromDragStartX()) > 2)
            msMoved = true;

        const float v = juce::jlimit (-100.0f, 100.0f,
                                      msStart + (float) e.getDistanceFromDragStartX() * 1.5f);

        if (auto* p = processorRef.apvts.getParameter ("msw"))
            p->setValueNotifyingHost (p->convertTo0to1 (v));

        if (auto* p = processorRef.apvts.getParameter ("ms"))
            p->setValueNotifyingHost (1.0f);

        repaint();
        return;
    }

    if (pointDrag >= 0)
    {
        auto rr = scopeBounds().reduced (14, 16);

        const float f = xToFreq (e.position.x);
        const float gg = juce::jlimit (-18.0f, 18.0f,
                                       ((float) rr.getCentreY() - e.position.y)
                                         / ((float) rr.getHeight() * 0.45f) * 18.0f);

        setPoint (pointDrag, f, gg);
        repaint();
    }
}

void TameAudioProcessorEditor::mouseUp (const juce::MouseEvent&)
{
    dragKnob = -1;
    pointDrag = -1;

    if (msDrag)
    {
        msDrag = false;

        if (! msMoved)
        {
            if (auto* p = processorRef.apvts.getParameter ("ms"))
                p->setValueNotifyingHost (p->getValue() > 0.5f ? 0.0f : 1.0f);
        }

        repaint();
    }
}
