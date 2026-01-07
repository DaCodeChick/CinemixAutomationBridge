/*
  ==============================================================================

    CinemixAutomationBridge - MidiRouter.h
    JUCE-based replication of D&R Cinemix VST Automation Bridge
    
    Copyright (c) 2012 Guido Scognamiglio (original VST 2.4 version)
    Copyright (c) 2026 (JUCE 8.0.12 replication)
    
    MIT License - See LICENSE file for details

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <array>

//==============================================================================
/**
 * MidiRouter
 * 
 * Handles all MIDI communication with the D&R Cinemix console.
 * Manages two input and two output MIDI ports, translates parameters
 * to MIDI CC messages, and processes incoming MIDI from the console.
 * 
 * MIDI Protocol Summary:
 * - Port 1: Channels 1-24 (LOW section), MIDI channels 1 & 3
 * - Port 2: Channels 25-36 (HIGH section) + Master, MIDI channels 2, 4, 5
 * - Faders: 14-bit resolution (dual CC)
 * - Mutes: Values 2=OFF, 3=ON (not 0/127!)
 * - AUX Mutes: Special encoding on CC#96, Ch 5
 */
class MidiRouter : public juce::MidiInputCallback
{
public:
    //==============================================================================
    MidiRouter();
    ~MidiRouter() override;
    
    //==============================================================================
    // Device Management
    
    /** Returns list of available MIDI input device names */
    juce::StringArray getAvailableMidiInputs() const;
    
    /** Returns list of available MIDI output device names */
    juce::StringArray getAvailableMidiOutputs() const;
    
    /** Opens MIDI ports by device index (-1 = don't open) */
    bool openMidiPorts(int inPort1Index, int inPort2Index, 
                       int outPort1Index, int outPort2Index);
    
    /** Closes all MIDI ports */
    void closeMidiPorts();
    
    /** Returns true if all required ports are open */
    bool arePortsOpen() const;
    
    /** Returns status string for display */
    juce::String getPortStatusString() const;
    
    //==============================================================================
    // MIDI Output (DAW → Console)
    
    /** Sends a single MIDI CC message to the console */
    void sendMidiCC(int midiChannel, int ccNumber, int value, int portIndex);
    
    /** Sends dual CC for 14-bit fader value */
    void sendFaderValue(int midiChannel, int ccPair, float normalizedValue, int portIndex);
    
    /** Sends parameter update based on parameter index (0-160) */
    void sendParameterUpdate(int paramIndex, float value);
    
    /** Sends activation command to console (CC#127, Val 127, Ch 5) */
    void sendActivateCommand();
    
    /** Sends deactivation command (0xFF system reset) */
    void sendDeactivateCommand();
    
    /** Sends console initialization sequence */
    void sendInitializationSequence();
    
    /** Sends current snapshot of all parameters to console */
    void sendFullSnapshot(const std::array<float, 161>& parameterValues);
    
    //==============================================================================
    // MIDI Input (Console → DAW)
    
    /** MidiInputCallback override - processes incoming MIDI messages */
    void handleIncomingMidiMessage(juce::MidiInput* source, 
                                    const juce::MidiMessage& message) override;
    
    /** Sets callback for parameter changes from console */
    std::function<void(int paramIndex, float value)> onParameterChangeFromConsole;
    
    //==============================================================================
    // Duplicate Prevention
    
    /** Clears all cached MIDI values (forces re-send on next update) */
    void clearMidiCache();
    
private:
    //==============================================================================
    // MIDI Port Management
    std::unique_ptr<juce::MidiInput> midiIn1;
    std::unique_ptr<juce::MidiInput> midiIn2;
    std::unique_ptr<juce::MidiOutput> midiOut1;
    std::unique_ptr<juce::MidiOutput> midiOut2;
    
    juce::String midiIn1Name;
    juce::String midiIn2Name;
    juce::String midiOut1Name;
    juce::String midiOut2Name;
    
    //==============================================================================
    // MIDI Mapping Data Structure
    struct MidiMapping
    {
        int midiChannel;     // MIDI channel (1-16)
        int ccNumber;        // Primary CC number
        int ccNumber2;       // Secondary CC for 14-bit (or -1 if not used)
        int portIndex;       // Which output port: 1 or 2
        bool isFader;        // true = fader (14-bit), false = button/switch
        
        MidiMapping() 
            : midiChannel(1), ccNumber(0), ccNumber2(-1), 
              portIndex(1), isFader(false) {}
        
        MidiMapping(int ch, int cc, int cc2, int port, bool fader)
            : midiChannel(ch), ccNumber(cc), ccNumber2(cc2), 
              portIndex(port), isFader(fader) {}
    };
    
    // Mapping table: index = parameter index (0-160)
    std::array<MidiMapping, 161> parameterMappings;
    
    // Cache last sent MIDI values to prevent duplicates
    // For faders: stores 14-bit value (0-16383)
    // For buttons/switches: stores 7-bit value (0-127)
    std::array<int, 161> previousMidiValues;
    
    //==============================================================================
    // Initialization
    void initializeMappingTable();
    
    // Mapping table builders for each section
    void mapFaders_Channels1_24();    // Param 0-23: Upper faders Ch 1-24
    void mapFaders_Channels25_36();   // Param 24-47: Upper faders Ch 25-36
    void mapFaders_LowerRow();        // Param 48-71: Lower faders (Mix)
    void mapMutes_Channels1_24();     // Param 72-95: Mutes Ch 1-24
    void mapMutes_Channels25_36();    // Param 96-119: Mutes Ch 25-36
    void mapMutes_LowerRow();         // Param 120-143: Mutes lower row
    void mapAuxMutes();               // Param 144-153: AUX mutes
    void mapMasterSection();          // Param 154-160: Joysticks + Master fader
    
    //==============================================================================
    // Helper methods
    int floatToMidi14Bit(float normalizedValue) const;
    float midi14BitToFloat(int midiValue14Bit) const;
    int floatToMidi7Bit(float normalizedValue) const;
    float midi7BitToFloat(int midiValue7Bit) const;
    
    // Reverse lookup: MIDI message to parameter index
    int findParameterIndex(int midiChannel, int ccNumber, int portIndex) const;
    
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiRouter)
};
