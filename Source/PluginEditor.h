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

//==============================================================================
/**
 * CinemixBridgeEditor
 * 
 * Main GUI editor for the Cinemix Automation Bridge plugin.
 * Phase 1: Basic placeholder UI to verify the plugin loads.
 * Full UI will be implemented in Phases 5-6.
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

    // Placeholder label for Phase 1
    juce::Label titleLabel;
    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CinemixBridgeEditor)
};
