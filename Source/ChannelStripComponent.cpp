/*
  ==============================================================================

    CinemixAutomationBridge - ChannelStripComponent.cpp
    JUCE-based replication of D&R Cinemix VST Automation Bridge
    
    Copyright (c) 2012 Guido Scognamiglio (original VST 2.4 version)
    Copyright (c) 2026 (JUCE 8.0.12 replication)
    
    MIT License - See LICENSE file for details

  ==============================================================================
*/

#include "ChannelStripComponent.h"

//==============================================================================
ChannelStripComponent::ChannelStripComponent(juce::AudioProcessorValueTreeState& apvts, 
                                             int channelIndex)
    : apvts(apvts), channelIndex(channelIndex)
{
    // Get channel name for display
    channelName = getChannelName(channelIndex);
    
    // Calculate parameter indices
    // Upper row = channel * 2
    // Lower row = channel * 2 + 1
    int upperFaderIndex = channelIndex * 2;
    int lowerFaderIndex = channelIndex * 2 + 1;
    int upperMuteIndex = channelIndex * 2;
    int lowerMuteIndex = channelIndex * 2 + 1;
    
    //==============================================================================
    // Setup Upper Row (Chan)
    
    setupFader(upperFader);
    addAndMakeVisible(upperFader);
    upperFaderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "fader_" + juce::String(upperFaderIndex), upperFader);
    
    setupMuteButton(upperMute);
    addAndMakeVisible(upperMute);
    upperMuteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, "mute_" + juce::String(upperMuteIndex), upperMute);
    
    setupLabel(upperLabel, "Chan");
    addAndMakeVisible(upperLabel);
    
    //==============================================================================
    // Setup Lower Row (Mix)
    
    setupFader(lowerFader);
    addAndMakeVisible(lowerFader);
    lowerFaderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, "fader_" + juce::String(lowerFaderIndex), lowerFader);
    
    setupMuteButton(lowerMute);
    addAndMakeVisible(lowerMute);
    lowerMuteAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, "mute_" + juce::String(lowerMuteIndex), lowerMute);
    
    setupLabel(lowerLabel, "Mix");
    addAndMakeVisible(lowerLabel);
}

ChannelStripComponent::~ChannelStripComponent()
{
}

//==============================================================================
void ChannelStripComponent::paint(juce::Graphics& g)
{
    // Draw background
    g.fillAll(juce::Colours::darkgrey.darker());
    
    // Draw border
    g.setColour(juce::Colours::black);
    g.drawRect(getLocalBounds(), 1);
    
    // Draw separator line between upper and lower rows
    int midY = getHeight() / 2;
    g.setColour(juce::Colours::grey);
    g.drawLine(0.0f, (float)midY, (float)getWidth(), (float)midY, 1.0f);
    
    // Draw channel name at the top
    g.setColour(juce::Colours::white);
    g.setFont(10.0f);
    g.drawText(channelName, 0, 2, getWidth(), 12, juce::Justification::centred);
}

void ChannelStripComponent::resized()
{
    auto area = getLocalBounds();
    
    // Reserve space for channel name at top
    area.removeFromTop(14);
    
    // Split into upper and lower halves
    int halfHeight = area.getHeight() / 2;
    auto upperArea = area.removeFromTop(halfHeight);
    auto lowerArea = area;
    
    //==============================================================================
    // Layout Upper Row
    {
        auto upper = upperArea.reduced(2);
        
        // Label at top
        upperLabel.setBounds(upper.removeFromTop(14));
        
        // Mute button below label
        upperMute.setBounds(upper.removeFromTop(18).reduced(1));
        
        // Fader takes remaining space
        upper.removeFromTop(2);  // Small gap
        upperFader.setBounds(upper);
    }
    
    //==============================================================================
    // Layout Lower Row
    {
        auto lower = lowerArea.reduced(2);
        
        // Label at top
        lowerLabel.setBounds(lower.removeFromTop(14));
        
        // Mute button below label
        lowerMute.setBounds(lower.removeFromTop(18).reduced(1));
        
        // Fader takes remaining space
        lower.removeFromTop(2);  // Small gap
        lowerFader.setBounds(lower);
    }
}

//==============================================================================
// Helper Methods

void ChannelStripComponent::setupFader(juce::Slider& fader)
{
    fader.setSliderStyle(juce::Slider::LinearVertical);
    fader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    fader.setRange(0.0, 1.0, 0.001);
    fader.setDoubleClickReturnValue(true, 0.754);  // Unity gain position
}

void ChannelStripComponent::setupMuteButton(juce::TextButton& button)
{
    button.setButtonText("M");
    button.setClickingTogglesState(true);
    button.setColour(juce::TextButton::buttonOnColourId, juce::Colours::red.darker());
}

void ChannelStripComponent::setupLabel(juce::Label& label, const juce::String& text)
{
    label.setText(text, juce::dontSendNotification);
    label.setJustificationType(juce::Justification::centred);
    label.setFont(juce::Font(9.0f));
    label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
}

juce::String ChannelStripComponent::getChannelName(int index) const
{
    // Channels 0-23: M1-M24
    // Channels 24-27: S1-S4
    // Channels 28-35: M25-M33
    
    if (index >= 0 && index < 24)
    {
        return "M" + juce::String(index + 1);
    }
    else if (index >= 24 && index < 28)
    {
        return "S" + juce::String(index - 23);
    }
    else if (index >= 28 && index < 36)
    {
        return "M" + juce::String(index - 3);  // M25-M33
    }
    
    return "??";
}
