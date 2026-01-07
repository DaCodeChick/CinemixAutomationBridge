/*
  ==============================================================================

    CinemixAutomationBridge - ChannelStripComponent.h
    JUCE-based replication of D&R Cinemix VST Automation Bridge
    
    Copyright (c) 2012 Guido Scognamiglio (original VST 2.4 version)
    Copyright (c) 2026 (JUCE 8.0.12 replication)
    
    MIT License - See LICENSE file for details

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
 * ChannelStripComponent
 * 
 * Represents a single channel strip with dual rows (Upper/Lower).
 * Each row contains a vertical fader and a mute button.
 * 
 * Dimensions: 22px wide × 540px tall (approx)
 * 
 * Layout:
 * ┌────────┐
 * │  Chan  │ Upper row label
 * │ [Mute] │ Upper mute button
 * │   ║    │ Upper fader
 * │   ║    │
 * │   ║    │
 * ├────────┤
 * │  Mix   │ Lower row label
 * │ [Mute] │ Lower mute button
 * │   ║    │ Lower fader
 * │   ║    │
 * │   ║    │
 * └────────┘
 */
class ChannelStripComponent : public juce::Component
{
public:
    //==============================================================================
    /**
     * Constructor
     * 
     * @param apvts Reference to the AudioProcessorValueTreeState
     * @param channelIndex Channel index (0-35)
     */
    ChannelStripComponent(juce::AudioProcessorValueTreeState& apvts, int channelIndex);
    ~ChannelStripComponent() override;

    //==============================================================================
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    //==============================================================================
    // Reference to APVTS
    juce::AudioProcessorValueTreeState& apvts;
    
    // Channel info
    int channelIndex;
    juce::String channelName;
    
    //==============================================================================
    // Upper row (Chan)
    juce::Slider upperFader;
    juce::TextButton upperMute;
    juce::Label upperLabel;
    
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> upperFaderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> upperMuteAttachment;
    
    //==============================================================================
    // Lower row (Mix)
    juce::Slider lowerFader;
    juce::TextButton lowerMute;
    juce::Label lowerLabel;
    
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lowerFaderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> lowerMuteAttachment;
    
    //==============================================================================
    // Helper methods
    void setupFader(juce::Slider& fader);
    void setupMuteButton(juce::TextButton& button);
    void setupLabel(juce::Label& label, const juce::String& text);
    
    juce::String getChannelName(int index) const;
    
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChannelStripComponent)
};
