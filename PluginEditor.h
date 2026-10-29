#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class TameAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                       private juce::Timer
{
public:
    explicit TameAudioProcessorEditor (TameAudioProcessor&);
    ~TameAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override {}

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;

    void drawSpectrum (juce::Graphics&);
    void drawKnob (juce::Graphics&, int index);
    void drawLabels (juce::Graphics&);
    void drawPresetBar (juce::Graphics&);

    juce::Rectangle<int> scopeBounds() const;
    juce::Point<float>   knobCentre (int index) const;
    float                knobRadius (int index) const;
    float                knobNorm (int index) const;
    void                 setKnobNorm (int index, float norm);
    float                freqToX (float f) const;
    float                xToFreq (float x) const;

    void pointParams (int index, float& f, float& g) const;
    void setPoint (int index, float f, float g);

    juce::File presetFolder() const;
    void installFactoryPresets();
    void refreshPresets();
    void showPresetMenu();
    void savePreset();

    TameAudioProcessor& processorRef;

    TameAudioProcessor::Scope scope;
    bool scopeValid = false;

    int   dragKnob = -1;
    float dragStartNorm = 0.0f;

    int   pointDrag = -1;

    bool  msDrag = false;
    bool  msMoved = false;
    float msStart = 0.0f;

    juce::StringArray presetNames;
    juce::String currentPresetName { "init" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TameAudioProcessorEditor)
};
