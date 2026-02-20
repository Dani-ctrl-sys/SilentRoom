/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
class SilentRoomAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                        public juce::Timer
{
public:
    SilentRoomAudioProcessorEditor (SilentRoomAudioProcessor&);
    ~SilentRoomAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    // --- Timer ---
    void timerCallback() override;

private:
    SilentRoomAudioProcessor& audioProcessor;

    // --- Sliders ---
    juce::Slider thresholdSlider;
    juce::Slider ratioSlider;
    juce::Slider attackSlider;
    juce::Slider releaseSlider;

    // --- Labels ---
    juce::Label thresholdLabel;
    juce::Label ratioLabel;
    juce::Label attackLabel;
    juce::Label releaseLabel;

    // --- Attachments (APVTS -> Sliders) ---
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> thresholdAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> ratioAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;

    // --- Gain Reduction Meter ---
    float currentGR = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SilentRoomAudioProcessorEditor)
};
