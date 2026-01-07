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
    
    // Setup title label
    addAndMakeVisible(titleLabel);
    titleLabel.setText("CinemixAutomationBridge", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(32.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);
    titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    
    // Setup status label
    addAndMakeVisible(statusLabel);
    statusLabel.setText("Phase 1: Basic UI - Plugin Loaded Successfully!\n\n"
                       "161 Parameters Created:\n"
                       "- 72 Faders (36 channels x 2 rows)\n"
                       "- 72 Mutes (36 channels x 2 rows)\n"
                       "- 10 AUX Mutes\n"
                       "- 7 Master Section Parameters\n\n"
                       "Next Phase: MIDI Infrastructure", 
                       juce::dontSendNotification);
    statusLabel.setFont(juce::Font(16.0f));
    statusLabel.setJustificationType(juce::Justification::centred);
    statusLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
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
    
    // Title at top
    titleLabel.setBounds(bounds.removeFromTop(100).reduced(20, 20));
    
    // Status label in center
    statusLabel.setBounds(bounds.reduced(40));
}
