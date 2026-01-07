/*
  ==============================================================================

    CinemixAutomationBridge - PluginEditor.h
    JUCE-based replication of D&R Cinemix VST Automation Bridge
    
    Copyright (c) 2012 Guido Scognamiglio (original VST 2.4 version)
    Copyright (c) 2026 (JUCE 8.0.12 replication)
    
    MIT License - See LICENSE file for details

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "ChannelStripComponent.h"

//==============================================================================
/**
 * CinemixBridgeEditor
 * 
 * Main GUI editor for the Cinemix Automation Bridge plugin.
 * Phase 5: Single channel strip UI for testing.
 * Full 36-channel UI will be implemented in Phase 6.
 */
class CinemixBridgeEditor  : public juce::AudioProcessorEditor
{
public:
    CinemixBridgeEditor (CinemixBridgeProcessor&);
    ~CinemixBridgeEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    CinemixBridgeProcessor& audioProcessor;

    // Phase 5: Single channel strip for testing
    std::unique_ptr<ChannelStripComponent> channelStrip;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CinemixBridgeEditor)
};
