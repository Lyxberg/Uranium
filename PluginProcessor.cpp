#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
TameAudioProcessor::TameAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",     juce::AudioChannelSet::stereo(), true)
                          .withInput  ("Sidechain", juce::AudioChannelSet::stereo(), false)
                          .withOutput ("Output",    juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout TameAudioProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    const auto fmt = [] (int dp, const juce::String& suffix)
    {
        return juce::AudioParameterFloatAttributes()
                   .withStringFromValueFunction ([dp, suffix] (float v, int)
                                                 { return juce::String (v, dp) + suffix; });
    };

    static const float pointF[5] { 400.0f, 1000.0f, 4000.0f, 8000.0f, 12000.0f };
    static const float pointG[5] { -18.0f, -3.0f, 10.0f, -2.0f, -18.0f };

    for (int i = 0; i < 5; ++i)
    {
        layout.add (std::make_unique<juce::AudioParameterFloat>
        (
            juce::ParameterID { "p" + juce::String (i + 1) + "f", 1 },
            "Point " + juce::String (i + 1) + " Freq",
            juce::NormalisableRange<float> (20.0f, 20000.0f, 0.0f, 0.3f),
            pointF[i], fmt (0, " Hz")
        ));

        layout.add (std::make_unique<juce::AudioParameterFloat>
        (
            juce::ParameterID { "p" + juce::String (i + 1) + "g", 1 },
            "Point " + juce::String (i + 1) + " Bias",
            juce::NormalisableRange<float> (-18.0f, 18.0f, 0.1f),
            pointG[i], fmt (1, " dB")
        ));
    }

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "depth", 1 }, "Depth",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, fmt (0, " %")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "sharp", 1 }, "Sharpness",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 46.8f, fmt (0, " %")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "attack", 1 }, "Attack",
        juce::NormalisableRange<float> (0.5f, 30.0f, 0.01f, 0.35f), 1.0f, fmt (1, " ms")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "release", 1 }, "Release",
        juce::NormalisableRange<float> (20.0f, 800.0f, 1.0f, 0.35f), 20.0f, fmt (0, " ms")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, fmt (0, " %")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "output", 1 }, "Output",
        juce::NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, fmt (1, " dB")));

    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "delta", 1 }, "Delta", false));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "link", 1 }, "Stereo Link", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "sc", 1 }, "Sidechain", true));
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "ms", 1 }, "M/S Monitor", false));
    layout.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "msw", 1 }, "M/S Blend",
        juce::NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, fmt (0, " %")));

    return layout;
}

//==============================================================================
void TameAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;

    window.resize ((size_t) fftSize);

    for (int n = 0; n < fftSize; ++n)
        window[(size_t) n] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi
                                                                     * (double) n / (double) fftSize));

    for (auto& c : chans)
    {
        c.inRing.assign ((size_t) fftSize, 0.0f);
        c.scRing.assign ((size_t) fftSize, 0.0f);
        c.scBuf.assign ((size_t) fftSize * 2, 0.0f);
        c.ola.assign ((size_t) fftSize, 0.0f);
        c.hopOut.assign ((size_t) hopSize, 0.0f);
        c.dryDelay.assign ((size_t) fftSize, 0.0f);
        c.fftBuf.assign ((size_t) fftSize * 2, 0.0f);
        c.gainDb.assign ((size_t) fftSize / 2, 0.0f);

        c.inPos = c.scPos = c.outIndex = c.dryPos = 0;
    }

    {
        const juce::SpinLock::ScopedLockType sl (scopeLock);
        scope.inDb.assign (kScopeBins, -100.0f);
        scope.smoothDb.assign (kScopeBins, -100.0f);
        scope.redDb.assign (kScopeBins, 0.0f);
        scope.valid = false;
    }

    setLatencySamples (latency);
}

bool TameAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();

    if (in != out)
        return false;

    if (! (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo()))
        return false;

    // the sidechain bus is optional, but must be mono or stereo when it is used
    if (layouts.inputBuses.size() > 1)
    {
        const auto& sc = layouts.inputBuses[1];

        if (! sc.isDisabled() && ! (sc == juce::AudioChannelSet::mono()
                                     || sc == juce::AudioChannelSet::stereo()))
            return false;
    }

    return true;
}

//==============================================================================
void TameAudioProcessor::analyseSpectrum (int channel)
{
    auto& c = chans[channel];
    const int halfBins = fftSize / 2;
    const float norm = 4.0f / (float) fftSize;
    const double dt = (double) hopSize / currentSampleRate;

    // the detector follows the attack setting, but never gets slower than 8 ms
    const double detTau = juce::jlimit (0.001, 0.008,
                                        (double) apvts.getRawParameterValue ("attack")->load() * 0.0005);

    for (int n = 0; n < fftSize; ++n)
        c.fftBuf[(size_t) n] = c.inRing[(size_t) ((c.inPos + n) & (fftSize - 1))] * window[(size_t) n];

    std::fill (c.fftBuf.begin() + fftSize, c.fftBuf.end(), 0.0f);
    fft.performRealOnlyForwardTransform (c.fftBuf.data(), true);

    // an external sidechain drives the detector, the main signal is what gets tamed
    if (sidechainActive)
    {
        for (int n = 0; n < fftSize; ++n)
            c.scBuf[(size_t) n] = c.scRing[(size_t) ((c.scPos + n) & (fftSize - 1))] * window[(size_t) n];

        std::fill (c.scBuf.begin() + fftSize, c.scBuf.end(), 0.0f);
        fft.performRealOnlyForwardTransform (c.scBuf.data(), true);
    }

    const float* src = sidechainActive ? c.scBuf.data() : c.fftBuf.data();

    if ((int) c.mags.size() < halfBins)
    {
        c.mags.assign ((size_t) halfBins, 0.0f);
        c.det.assign ((size_t) halfBins, 0.0f);
        c.gainS.assign ((size_t) halfBins, 0.0f);
    }

    for (int k = 1; k < halfBins; ++k)
    {
        const float re = src[2 * k];
        const float im = src[2 * k + 1];

        c.mags[(size_t) k] = std::sqrt (re * re + im * im) * norm;
    }

    c.mags[0] = 0.0f;
    c.det[0] = 0.0f;

    // detector magnitude: smoothed across frequency first (one noisy bin must
    // not own a whole gain), then smoothed a little in time
    const float detCoef = (float) (1.0 - std::exp (-dt / detTau));

    for (int k = 1; k < halfBins; ++k)
    {
        const int km2 = juce::jmax (1, k - 2);
        const int km1 = juce::jmax (1, k - 1);
        const int kp1 = juce::jmin (halfBins - 1, k + 1);
        const int kp2 = juce::jmin (halfBins - 1, k + 2);

        const float sm = (c.mags[(size_t) km2] + 4.0f * c.mags[(size_t) km1]
                          + 6.0f * c.mags[(size_t) k]
                          + 4.0f * c.mags[(size_t) kp1] + c.mags[(size_t) kp2]) * (1.0f / 16.0f);

        c.det[(size_t) k] += (sm - c.det[(size_t) k]) * detCoef;
    }
}

//==============================================================================
void TameAudioProcessor::computeGains (int channel, const float* detSpectrum)
{
    auto& c = chans[channel];
    const int halfBins = fftSize / 2;

    const float depth = apvts.getRawParameterValue ("depth")->load() * 0.01f;
    const float sharp = apvts.getRawParameterValue ("sharp")->load() * 0.01f;
    const float attackMs  = apvts.getRawParameterValue ("attack")->load();
    const float releaseMs = apvts.getRawParameterValue ("release")->load();

    float pointF[5], pointG[5];

    for (int i = 0; i < 5; ++i)
    {
        pointF[i] = apvts.getRawParameterValue ("p" + juce::String (i + 1) + "f")->load();
        pointG[i] = apvts.getRawParameterValue ("p" + juce::String (i + 1) + "g")->load();
    }

    const double dt  = (double) hopSize / currentSampleRate;
    const double atk = juce::jmax (0.0002, (double) attackMs * 0.001);
    const double rel = juce::jmax (0.005, (double) releaseMs * 0.001);
    const float  aC  = (float) (1.0 - std::exp (-dt / atk));
    const float  rC  = (float) (1.0 - std::exp (-dt / rel));
    const double binHz = currentSampleRate / (double) fftSize;

    if ((int) prefix.size() < halfBins)
        prefix.assign ((size_t) halfBins, 0.0f);

    prefix[0] = 0.0f;

    for (int k = 1; k < halfBins; ++k)
        prefix[(size_t) k] = prefix[(size_t) (k - 1)] + detSpectrum[k];

    // a bin is only touched when it sits clearly above its own neighbourhood
    const double winOct = 0.05 + (1.0 - (double) sharp) * 0.95;

    // SHARP is the whole precision control: a tight setting keeps a surgical
    // gain curve (and the comb that goes with it), a wide one smooths it out
    const double gOct = 1.0 / 12.0 - (double) sharp * (1.0 / 12.0 - 1.0 / 256.0);

    for (int k = 1; k < halfBins; ++k)
    {
        const float dmag = detSpectrum[k];

        const int w = juce::jmax (3, (int) std::round ((double) k * (std::pow (2.0, winOct * 0.5) - 1.0)));
        const int a = juce::jmax (1, k - w);
        const int b = juce::jmin (halfBins - 1, k + w);

        const float sum = prefix[(size_t) b] - (a > 1 ? prefix[(size_t) (a - 1)] : 0.0f);
        const float smooth = sum / (float) (b - a + 1);

        const float excessDb = 20.0f * std::log10 (juce::jmax (1.0e-9f, dmag)
                                                    / juce::jmax (1.0e-9f, smooth));

        const double f = (double) k * binHz;

        // sensitivity curve: smooth interpolation between the five points
        float bias = pointG[4];

        if (f <= (double) pointF[0])
        {
            bias = pointG[0];
        }
        else
        {
            for (int i = 0; i < 4; ++i)
            {
                if (f <= (double) pointF[i + 1])
                {
                    const double u = (std::log2 (f) - std::log2 ((double) pointF[i]))
                                       / juce::jmax (1.0e-6, std::log2 ((double) pointF[i + 1])
                                                             - std::log2 ((double) pointF[i]));
                    const double s = u * u * (3.0 - 2.0 * u);
                    bias = (float) ((double) pointG[i] + ((double) pointG[i + 1] - (double) pointG[i]) * s);
                    break;
                }
            }
        }

        // positive bias lowers the knee: more of that band gets tamed
        const float knee = juce::jlimit (-8.0f, 24.0f, 1.5f - bias * 1.2f);

        float want = 0.0f;

        if (excessDb > knee)
            want = -(excessDb - knee) * depth;

        want = juce::jlimit (-30.0f, 0.0f, want);

        float& g = c.gainDb[(size_t) k];
        const float coef = want < g ? aC : rC;

        // slew limit in dB per second, so it stays musical at any hop size
        const float slewDown = 2000.0f * (float) dt;
        const float slewUp   = 700.0f * (float) dt;
        const float next = juce::jlimit (g - slewDown, g + slewUp, g + (want - g) * coef);
        g = juce::jlimit (-30.0f, 0.0f, next);

        if (channel == 0)
        {
            scope.inDb[(size_t) k]     = 20.0f * std::log10 (juce::jmax (1.0e-9f, c.mags[(size_t) k]));
            scope.smoothDb[(size_t) k] = 20.0f * std::log10 (juce::jmax (1.0e-9f, smooth));
            scope.redDb[(size_t) k]    = g;
        }
    }

    // the gain curve has to move smoothly across frequency too: a per-bin comb
    // is exactly what makes a spectral suppressor sound grainy
    if ((int) gPrefix.size() < halfBins)
        gPrefix.assign ((size_t) halfBins, 0.0f);

    gPrefix[0] = 0.0f;

    for (int k = 1; k < halfBins; ++k)
        gPrefix[(size_t) k] = gPrefix[(size_t) (k - 1)] + c.gainDb[(size_t) k];

    for (int k = 1; k < halfBins; ++k)
    {
        const int w = juce::jmax (1, (int) std::round ((double) k
                                   * (std::pow (2.0, 0.5 * gOct) - 1.0)));
        const int a = juce::jmax (1, k - w);
        const int b = juce::jmin (halfBins - 1, k + w);

        const float gs = (gPrefix[(size_t) b] - gPrefix[(size_t) (a - 1)]) / (float) (b - a + 1);

        c.gainS[(size_t) k] = gs;

        if (channel == 0)
            scope.redDb[(size_t) k] = gs;
    }

    if (channel == 0)
    {
        scope.inDb[0] = -100.0f;
        scope.smoothDb[0] = -100.0f;
        scope.redDb[0] = 0.0f;
        scope.valid = true;
    }
}

//==============================================================================
void TameAudioProcessor::synthesise (int channel)
{
    auto& c = chans[channel];
    const int halfBins = fftSize / 2;

    for (int k = 1; k < halfBins; ++k)
    {
        const float gg = juce::Decibels::decibelsToGain (c.gainS[(size_t) k]);

        c.fftBuf[(size_t) (2 * k)]     *= gg;
        c.fftBuf[(size_t) (2 * k + 1)] *= gg;
    }

    fft.performRealOnlyInverseTransform (c.fftBuf.data());

    for (int n = 0; n < fftSize; ++n)
        c.ola[(size_t) n] += c.fftBuf[(size_t) n] * window[(size_t) n] * 0.125f;

    std::copy (c.ola.begin(), c.ola.begin() + hopSize, c.hopOut.begin());
    std::move (c.ola.begin() + hopSize, c.ola.end(), c.ola.begin());
    std::fill (c.ola.end() - hopSize, c.ola.end(), 0.0f);
}

//==============================================================================
void TameAudioProcessor::renderHop (int numChannels)
{
    const int halfBins = fftSize / 2;
    const bool link = apvts.getRawParameterValue ("link")->load() > 0.5f;

    for (int ch = 0; ch < numChannels; ++ch)
        analyseSpectrum (ch);

    if (link)
    {
        if ((int) comb.size() < halfBins)
            comb.assign ((size_t) halfBins, 0.0f);

        for (int k = 1; k < halfBins; ++k)
            comb[(size_t) k] = numChannels > 1
                                 ? juce::jmax (chans[0].det[(size_t) k], chans[1].det[(size_t) k])
                                 : chans[0].det[(size_t) k];

        // one gain curve for the whole image, or the phantom centre moves
        computeGains (0, comb.data());

        for (int ch = 1; ch < numChannels; ++ch)
        {
            chans[ch].gainDb = chans[0].gainDb;
            chans[ch].gainS  = chans[0].gainS;
        }
    }
    else
    {
        for (int ch = 0; ch < numChannels; ++ch)
            computeGains (ch, chans[ch].det.data());
    }

    for (int ch = 0; ch < numChannels; ++ch)
        synthesise (ch);
}
//==============================================================================
void TameAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numCh = juce::jmin (2, buffer.getNumChannels());

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    const float mix     = apvts.getRawParameterValue ("mix")->load() * 0.01f;
    const float outGain = juce::Decibels::decibelsToGain (apvts.getRawParameterValue ("output")->load());
    const bool  delta   = apvts.getRawParameterValue ("delta")->load() > 0.5f;
    auto* scBus = getBus (true, 1);
    const bool scConnected = scBus != nullptr && scBus->isEnabled() && scBus->getNumberOfChannels() > 0;

    sidechainActive = scConnected && apvts.getRawParameterValue ("sc")->load() > 0.5f;

    const juce::AudioBuffer<float> scBuffer = sidechainActive ? getBusBuffer (buffer, true, 1)
                                                              : juce::AudioBuffer<float>();

    const bool  monitor = apvts.getRawParameterValue ("ms")->load() > 0.5f;
    const float msAmount = juce::jlimit (0.0f, 1.0f,
                                         std::abs (apvts.getRawParameterValue ("msw")->load()) * 0.01f);

    for (int i = 0; i < numSamples; ++i)
    {
        bool hopDue = false;

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto& c = chans[ch];
            float* d = buffer.getWritePointer (ch);
            const float x = d[i];

            const float dry = c.dryDelay[(size_t) c.dryPos];
            c.dryDelay[(size_t) c.dryPos] = x;
            if (++c.dryPos >= fftSize) c.dryPos = 0;

            c.inRing[(size_t) c.inPos] = x;
            c.inPos = (c.inPos + 1) & (fftSize - 1);

            const float scX = sidechainActive
                                ? scBuffer.getSample (juce::jmin (ch, scBuffer.getNumChannels() - 1), i)
                                : x;

            c.scRing[(size_t) c.scPos] = scX;

            if (++c.scPos >= fftSize)
                c.scPos = 0;

            float wet = c.hopOut[(size_t) c.outIndex];

            if (++c.outIndex >= hopSize)
            {
                c.outIndex = 0;
                hopDue = true;
            }

            if (delta)
                d[i] = (dry - wet) * mix * outGain;
            else
                d[i] = dry + (wet * outGain - dry) * mix;
        }

        // M/S listen: centre is mid, either side of the knob is side
        if (monitor && numCh > 1)
        {
            const float l = buffer.getSample (0, i);
            const float r = buffer.getSample (1, i);
            const float m = 0.5f * (l + r);
            const float s = 0.5f * (l - r);
            const float o = m + (s - m) * msAmount;

            buffer.setSample (0, i, o);
            buffer.setSample (1, i, o);
        }

        if (hopDue)
            renderHop (numCh);
    }
}

//==============================================================================
bool TameAudioProcessor::readScope (Scope& dest) const
{
    const juce::SpinLock::ScopedTryLockType sl (scopeLock);

    if (! sl.isLocked() || ! scope.valid)
        return false;

    dest = scope;
    return true;
}

//==============================================================================
void TameAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void TameAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessorEditor* TameAudioProcessor::createEditor()
{
    return new TameAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TameAudioProcessor();
}
