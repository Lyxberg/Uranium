#pragma once

#include <JuceHeader.h>

//==============================================================================
class TameAudioProcessor final : public juce::AudioProcessor
{
public:
    TameAudioProcessor();
    ~TameAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "URANIUM"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    static constexpr int kScopeBins = 1024;

    struct Scope
    {
        std::vector<float> inDb, smoothDb, redDb;   // spectrum, local average, gain reduction
        bool valid = false;
    };

    bool readScope (Scope& dest) const;

    // test hook: the gain the last frame applied on this bin
    float testGainDb (int channel, int bin) const
    {
        if (channel < 0 || channel > 1 || bin < 0)
            return 0.0f;

        const auto& g = chans[channel].gainS;
        return bin < (int) g.size() ? g[(size_t) bin] : 0.0f;
    }

    juce::AudioProcessorValueTreeState apvts;

private:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;
    static constexpr int hopSize  = fftSize / 16;
    static constexpr int latency  = fftSize;

    struct Channel
    {
        std::vector<float> inRing, scRing, scBuf, ola, hopOut, dryDelay, fftBuf;
        std::vector<float> gainDb, gainS, mags, det;
        int inPos = 0, scPos = 0, outIndex = 0, dryPos = 0;
    };

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void analyseSpectrum (int channel);
    void computeGains (int channel, const float* detSpectrum);
    void synthesise (int channel);
    void renderHop (int numChannels);

    juce::dsp::FFT fft { fftOrder };
    std::vector<float> window;
    std::vector<float> prefix, gPrefix, comb;
    Channel chans[2];

    double currentSampleRate = 48000.0;
    bool sidechainActive = false;

    mutable juce::SpinLock scopeLock;
    Scope scope;
};

