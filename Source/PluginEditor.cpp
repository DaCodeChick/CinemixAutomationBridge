/*
  ==============================================================================

    CinemixAutomationBridge - PluginEditor.cpp
    JUCE-based replication of D&R Cinemix VST Automation Bridge
    
    Copyright (c) 2012 Guido Scognamiglio (original VST 2.4 version)
    Copyright (c) 2026 (JUCE 8.0.12 replication)
    
    MIT License - See LICENSE file for details

  ==============================================================================
*/

#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
CinemixBridgeEditor::CinemixBridgeEditor (CinemixBridgeProcessor& p)
    : AudioProcessorEditor (&p), audioProcessor (p)
{
    // Set fixed window size (as per PLAN.md)
    setSize (920, 560);
    
    // Phase 5: Create and display a single channel strip for testing
    // Using channel 0 (M1) as test channel
    channelStrip = std::make_unique<ChannelStripComponent>(audioProcessor.getAPVTS(), 0);
    addAndMakeVisible(*channelStrip);
}

CinemixBridgeEditor::~CinemixBridgeEditor()
{
}

//==============================================================================
void CinemixBridgeEditor::paint (juce::Graphics& g)
{
    // Background gradient
    g.fillAll(juce::Colour(0xff2a2a2a));
    
    juce::ColourGradient gradient(juce::Colour(0xff3a3a3a), 0, 0,
                                   juce::Colour(0xff1a1a1a), 0, (float)getHeight(),
                                   false);
    g.setGradientFill(gradient);
    g.fillRect(getLocalBounds());
    
    // Draw border
    g.setColour(juce::Colours::darkgrey);
    g.drawRect(getLocalBounds(), 2);
}

void CinemixBridgeEditor::resized()
{
    auto bounds = getLocalBounds();
    
    // Position channel strip in top-left corner with some padding
    // Channel strip is 22px wide × 540px tall
    if (channelStrip)
        channelStrip->setBounds(20, 10, 22, 540);
}
