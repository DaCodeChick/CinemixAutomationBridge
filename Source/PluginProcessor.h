/*
  ==============================================================================

    CinemixAutomationBridge - PluginProcessor.h
    JUCE-based replication of D&R Cinemix VST Automation Bridge
    
    Copyright (c) 2012 Guido Scognamiglio (original VST 2.4 version)
    Copyright (c) 2026 (JUCE 8.0.12 replication)
    
    MIT License - See LICENSE file for details

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "MidiRouter.h"

//==============================================================================
/**
 * CinemixBridgeProcessor
 * 
 * Main audio processor for the Cinemix Automation Bridge plugin.
 * Manages 161 parameters and handles bidirectional MIDI communication
 * with the D&R Cinemix console.
 */
class CinemixBridgeProcessor : public juce::AudioProcessor,
                                private juce::AudioProcessorValueTreeState::Listener
{
public:
    //==============================================================================
    CinemixBridgeProcessor();
    ~CinemixBridgeProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==============================================================================
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==============================================================================
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Console control methods
    void activateConsole();
    void deactivateConsole();
    void resetAll();
    void toggleAllMutes();
    void sendSnapshot();
    void setTestMode(bool enable);
    
    //==============================================================================
    // Parameter access
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    
    // MIDI Router access
    MidiRouter& getMidiRouter() { return midiRouter; }
    
    // Helper to send parameter changes to console
    void syncParameterToMidi(int paramIndex, float value);
    
    //==============================================================================
    // Parameter indices (for reference)
    enum ParameterIndices
    {
        // Faders: 0-71 (36 channels × 2 rows)
        FaderStart = 0,
        FaderEnd = 71,
        
        // Mutes: 72-143 (36 channels × 2 rows)
        MuteStart = 72,
        MuteEnd = 143,
        
        // AUX Mutes: 144-153
        AuxMuteStart = 144,
        AuxMuteEnd = 153,
        
        // Master Section: 154-160
        Joy1_X = 154,
        Joy1_Y = 155,
        Joy1_Mute = 156,
        Joy2_X = 157,
        Joy2_Y = 158,
        Joy2_Mute = 159,
        MasterFader = 160,
        
        TotalParameters = 161
    };

private:
    //==============================================================================
    // Parameter management
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    // MIDI Router
    MidiRouter midiRouter;
    
    // Helper methods for parameter creation
    juce::String getFaderParameterName(int index);
    juce::String getMuteParameterName(int index);
    
    // Helper to convert parameter ID to index
    int getParameterIndex(const juce::String& parameterID) const;
    
    // AudioProcessorValueTreeState::Listener implementation
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    
    //==============================================================================
    // Console state
    bool consoleActive = false;
    bool testModeEnabled = false;
    bool allMutesState = false;
    
    // Test mode animation
    int sampleCounter = 0;
    int faderAnimRate = 0;
    int muteAnimRate = 0;
    std::array<float, 72> animPhase;
    
    void animateFaders();
    void animateMutes();
    
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CinemixBridgeProcessor)
};
