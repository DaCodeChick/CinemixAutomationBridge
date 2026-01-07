/*
  ==============================================================================

    CinemixAutomationBridge - MidiRouter.cpp
    JUCE-based replication of D&R Cinemix VST Automation Bridge
    
    Copyright (c) 2012 Guido Scognamiglio (original VST 2.4 version)
    Copyright (c) 2026 (JUCE 8.0.12 replication)
    
    MIT License - See LICENSE file for details

  ==============================================================================
*/

#include "MidiRouter.h"

//==============================================================================
MidiRouter::MidiRouter()
{
    // Initialize previous values to -1 (force send on first update)
    previousMidiValues.fill(-1);
    
    // Build the complete parameter-to-MIDI mapping table
    initializeMappingTable();
}

MidiRouter::~MidiRouter()
{
    closeMidiPorts();
}

//==============================================================================
// Device Management

juce::StringArray MidiRouter::getAvailableMidiInputs() const
{
    juce::StringArray names;
    auto devices = juce::MidiInput::getAvailableDevices();
    for (const auto& device : devices)
        names.add(device.name);
    return names;
}

juce::StringArray MidiRouter::getAvailableMidiOutputs() const
{
    juce::StringArray names;
    auto devices = juce::MidiOutput::getAvailableDevices();
    for (const auto& device : devices)
        names.add(device.name);
    return names;
}

bool MidiRouter::openMidiPorts(int inPort1Index, int inPort2Index, 
                                int outPort1Index, int outPort2Index)
{
    closeMidiPorts();  // Close any existing connections
    
    auto inputDevices = juce::MidiInput::getAvailableDevices();
    auto outputDevices = juce::MidiOutput::getAvailableDevices();
    
    bool success = true;
    
    // Open Input Port 1 (LOW section)
    if (inPort1Index >= 0 && inPort1Index < inputDevices.size())
    {
        midiIn1 = juce::MidiInput::openDevice(inputDevices[inPort1Index].identifier, this);
        if (midiIn1)
        {
            midiIn1->start();
            midiIn1Name = inputDevices[inPort1Index].name;
        }
        else
            success = false;
    }
    
    // Open Input Port 2 (HIGH section)
    if (inPort2Index >= 0 && inPort2Index < inputDevices.size())
    {
        midiIn2 = juce::MidiInput::openDevice(inputDevices[inPort2Index].identifier, this);
        if (midiIn2)
        {
            midiIn2->start();
            midiIn2Name = inputDevices[inPort2Index].name;
        }
        else
            success = false;
    }
    
    // Open Output Port 1 (LOW section)
    if (outPort1Index >= 0 && outPort1Index < outputDevices.size())
    {
        midiOut1 = juce::MidiOutput::openDevice(outputDevices[outPort1Index].identifier);
        if (midiOut1)
            midiOut1Name = outputDevices[outPort1Index].name;
        else
            success = false;
    }
    
    // Open Output Port 2 (HIGH section)
    if (outPort2Index >= 0 && outPort2Index < outputDevices.size())
    {
        midiOut2 = juce::MidiOutput::openDevice(outputDevices[outPort2Index].identifier);
        if (midiOut2)
            midiOut2Name = outputDevices[outPort2Index].name;
        else
            success = false;
    }
    
    return success;
}

void MidiRouter::closeMidiPorts()
{
    if (midiIn1) midiIn1->stop();
    if (midiIn2) midiIn2->stop();
    
    midiIn1.reset();
    midiIn2.reset();
    midiOut1.reset();
    midiOut2.reset();
    
    midiIn1Name.clear();
    midiIn2Name.clear();
    midiOut1Name.clear();
    midiOut2Name.clear();
}

bool MidiRouter::arePortsOpen() const
{
    // At minimum, we need both output ports to send commands
    return (midiOut1 != nullptr && midiOut2 != nullptr);
}

juce::String MidiRouter::getPortStatusString() const
{
    juce::String status;
    status << "IN1: " << (midiIn1 ? midiIn1Name : "None") << "\n";
    status << "IN2: " << (midiIn2 ? midiIn2Name : "None") << "\n";
    status << "OUT1: " << (midiOut1 ? midiOut1Name : "None") << "\n";
    status << "OUT2: " << (midiOut2 ? midiOut2Name : "None");
    return status;
}

//==============================================================================
// MIDI Output

void MidiRouter::sendMidiCC(int midiChannel, int ccNumber, int value, int portIndex)
{
    // Safety check: ensure ports are open
    if (portIndex == 1 && !midiOut1)
        return;
    if (portIndex == 2 && !midiOut2)
        return;
    
    juce::MidiMessage msg = juce::MidiMessage::controllerEvent(midiChannel, ccNumber, value);
    
    if (portIndex == 1 && midiOut1)
        midiOut1->sendMessageNow(msg);
    else if (portIndex == 2 && midiOut2)
        midiOut2->sendMessageNow(msg);
}

void MidiRouter::sendFaderValue(int midiChannel, int ccPair, float normalizedValue, int portIndex)
{
    // Convert 0.0-1.0 to 14-bit MIDI (0-16383)
    int value14bit = floatToMidi14Bit(normalizedValue);
    
    // Split into MSB (CC) and LSB (CC+1)
    int msb = (value14bit >> 7) & 0x7F;  // Upper 7 bits
    int lsb = value14bit & 0x7F;          // Lower 7 bits
    
    // Send both CCs
    sendMidiCC(midiChannel, ccPair, msb, portIndex);
    sendMidiCC(midiChannel, ccPair + 1, lsb, portIndex);
}

void MidiRouter::sendParameterUpdate(int paramIndex, float value)
{
    if (paramIndex < 0 || paramIndex >= 161)
        return;
    
    const MidiMapping& mapping = parameterMappings[paramIndex];
    
    if (mapping.isFader)
    {
        // Fader: 14-bit dual CC
        int value14bit = floatToMidi14Bit(value);
        
        // Check if value changed (prevent duplicates)
        if (previousMidiValues[paramIndex] == value14bit)
            return;
        
        previousMidiValues[paramIndex] = value14bit;
        sendFaderValue(mapping.midiChannel, mapping.ccNumber, value, mapping.portIndex);
    }
    else
    {
        // Button/Switch: single CC with specific encoding
        int midiValue;
        
        // Handle special cases
        if (paramIndex >= 72 && paramIndex <= 143)
        {
            // Mute buttons: 2=OFF, 3=ON
            midiValue = (value > 0.5f) ? 3 : 2;
        }
        else if (paramIndex >= 144 && paramIndex <= 153)
        {
            // AUX Mutes: special encoding on CC#96
            int auxIndex = paramIndex - 144;
            midiValue = (value > 0.5f) ? (auxIndex * 2 + 3) : (auxIndex * 2 + 2);
            
            // AUX mutes all use CC#96 on Channel 5, Port 2
            sendMidiCC(5, 96, midiValue, 2);
            previousMidiValues[paramIndex] = midiValue;
            return;
        }
        else if (paramIndex == 156 || paramIndex == 159)
        {
            // Joystick mutes: 2=OFF, 3=ON
            midiValue = (value > 0.5f) ? 3 : 2;
        }
        else
        {
            // Joystick X/Y and Master Fader: 0-127
            midiValue = floatToMidi7Bit(value);
        }
        
        // Check if value changed
        if (previousMidiValues[paramIndex] == midiValue)
            return;
        
        previousMidiValues[paramIndex] = midiValue;
        sendMidiCC(mapping.midiChannel, mapping.ccNumber, midiValue, mapping.portIndex);
    }
}

void MidiRouter::sendActivateCommand()
{
    // Send CC#127, Value 127, Channel 5 to both ports
    sendMidiCC(5, 127, 127, 1);
    sendMidiCC(5, 127, 127, 2);
}

void MidiRouter::sendDeactivateCommand()
{
    // Send System Reset (0xFF) to both ports
    juce::MidiMessage resetMsg(0xFF);
    
    if (midiOut1)
        midiOut1->sendMessageNow(resetMsg);
    if (midiOut2)
        midiOut2->sendMessageNow(resetMsg);
}

void MidiRouter::sendInitializationSequence()
{
    // Console initialization sequence per original protocol:
    // 1. CC# 127, Val: 127, Ch. 5 (both ports)
    sendMidiCC(5, 127, 127, 1);
    sendMidiCC(5, 127, 127, 2);
    
    // 2. CC# 65, Val: 1, Ch. 5 (both ports)
    sendMidiCC(5, 65, 1, 1);
    sendMidiCC(5, 65, 1, 2);
    
    // 3. Reset all SEL and Touch Faders
    // SEL switches: CC# 64-111, Ch. 3 (Port 1, Channels 1-24)
    for (int cc = 64; cc <= 111; ++cc)
        sendMidiCC(3, cc, 0, 1);  // 0 = ISO (off)
    
    // SEL switches: CC# 64-111, Ch. 4 (Port 2, Channels 25-36)
    for (int cc = 64; cc <= 111; ++cc)
        sendMidiCC(4, cc, 0, 2);  // 0 = ISO (off)
    
    // 4. CC# 127, Val: 127, Ch. 5 (both ports) - second activation
    sendMidiCC(5, 127, 127, 1);
    sendMidiCC(5, 127, 127, 2);
    
    // 5. CC# 65, Val: 15, Ch. 5 (both ports)
    sendMidiCC(5, 65, 15, 1);
    sendMidiCC(5, 65, 15, 2);
}

void MidiRouter::sendFullSnapshot(const std::array<float, 161>& parameterValues)
{
    // Clear cache to force all values to send
    clearMidiCache();
    
    // Send all parameter updates
    for (int i = 0; i < 161; ++i)
    {
        sendParameterUpdate(i, parameterValues[i]);
    }
}

//==============================================================================
// MIDI Input

void MidiRouter::handleIncomingMidiMessage(juce::MidiInput* source, 
                                            const juce::MidiMessage& message)
{
    if (!message.isController())
        return;
    
    // Determine which port this came from
    int portIndex = (source == midiIn1.get()) ? 1 : 2;
    
    int channel = message.getChannel();
    int ccNumber = message.getControllerNumber();
    int value = message.getControllerValue();
    
    // Find parameter index from MIDI message
    int paramIndex = findParameterIndex(channel, ccNumber, portIndex);
    
    if (paramIndex >= 0 && onParameterChangeFromConsole)
    {
        const MidiMapping& mapping = parameterMappings[paramIndex];
        
        if (mapping.isFader)
        {
            // For faders, we need to combine MSB and LSB
            // This is a simplified version - full implementation would need
            // to cache MSB values and wait for LSB
            // For now, treat as 7-bit
            float normalizedValue = midi7BitToFloat(value);
            onParameterChangeFromConsole(paramIndex, normalizedValue);
        }
        else
        {
            // Handle button/switch values
            float normalizedValue = 0.0f;
            
            // Special handling for mutes (values 2/3)
            if (paramIndex >= 72 && paramIndex <= 143)
            {
                // Mute buttons: 2=OFF, 3=ON
                normalizedValue = (value == 3) ? 1.0f : 0.0f;
            }
            else if (paramIndex >= 144 && paramIndex <= 153)
            {
                // AUX mutes: special encoding
                int auxIndex = paramIndex - 144;
                int expectedOn = auxIndex * 2 + 3;
                normalizedValue = (value == expectedOn) ? 1.0f : 0.0f;
            }
            else if (paramIndex == 156 || paramIndex == 159)
            {
                // Joystick mutes: 2=OFF, 3=ON
                normalizedValue = (value == 3) ? 1.0f : 0.0f;
            }
            else
            {
                // Joystick X/Y: standard 0-127
                normalizedValue = midi7BitToFloat(value);
            }
            
            onParameterChangeFromConsole(paramIndex, normalizedValue);
        }
    }
}

//==============================================================================
// Duplicate Prevention

void MidiRouter::clearMidiCache()
{
    previousMidiValues.fill(-1);
}

//==============================================================================
// Mapping Table Initialization

void MidiRouter::initializeMappingTable()
{
    mapFaders_Channels1_24();
    mapFaders_Channels25_36();
    mapFaders_LowerRow();
    mapMutes_Channels1_24();
    mapMutes_Channels25_36();
    mapMutes_LowerRow();
    mapAuxMutes();
    mapMasterSection();
}

void MidiRouter::mapFaders_Channels1_24()
{
    // Parameters 0-23: Upper faders for Channels 1-24 (Port 1, LOW section)
    // MIDI: CC pairs 0-47, Channel 1
    
    for (int i = 0; i < 24; ++i)
    {
        parameterMappings[i] = MidiMapping(
            1,              // MIDI Channel 1
            i * 2,          // CC pair starts at i*2 (0, 2, 4, ... 46)
            i * 2 + 1,      // CC pair LSB
            1,              // Port 1
            true            // Is fader (14-bit)
        );
    }
}

void MidiRouter::mapFaders_Channels25_36()
{
    // Parameters 24-35: Upper faders for Channels 25-36 (Port 2, HIGH section)
    // MIDI: CC pairs 0-23, Channel 2
    
    for (int i = 0; i < 12; ++i)
    {
        int paramIndex = 24 + i;
        parameterMappings[paramIndex] = MidiMapping(
            2,              // MIDI Channel 2
            i * 2,          // CC pair starts at i*2 (0, 2, 4, ... 22)
            i * 2 + 1,      // CC pair LSB
            2,              // Port 2
            true            // Is fader
        );
    }
}

void MidiRouter::mapFaders_LowerRow()
{
    // Parameters 36-71: Lower (Mix) faders for all 36 channels
    // Channels 1-24: Port 1, Channel 1, CC pairs 48-95 (offset +48 from upper)
    // Channels 25-36: Port 2, Channel 2, CC pairs 24-47 (offset +24 from upper)
    
    // Lower faders for Channels 1-24 (Param 36-59)
    for (int i = 0; i < 24; ++i)
    {
        int paramIndex = 36 + i;
        parameterMappings[paramIndex] = MidiMapping(
            1,                  // MIDI Channel 1
            48 + i * 2,         // CC pair: 48, 50, 52, ... 94
            48 + i * 2 + 1,     // LSB
            1,                  // Port 1
            true                // Is fader
        );
    }
    
    // Lower faders for Channels 25-36 (Param 60-71)
    for (int i = 0; i < 12; ++i)
    {
        int paramIndex = 60 + i;
        parameterMappings[paramIndex] = MidiMapping(
            2,                  // MIDI Channel 2
            24 + i * 2,         // CC pair: 24, 26, 28, ... 46
            24 + i * 2 + 1,     // LSB
            2,                  // Port 2
            true                // Is fader
        );
    }
}

void MidiRouter::mapMutes_Channels1_24()
{
    // Parameters 72-95: Upper mutes for Channels 1-24
    // MIDI: CC 0-23, Channel 3, Port 1, Values 2=OFF, 3=ON
    
    for (int i = 0; i < 24; ++i)
    {
        int paramIndex = 72 + i;
        parameterMappings[paramIndex] = MidiMapping(
            3,              // MIDI Channel 3
            i,              // CC: 0, 1, 2, ... 23
            -1,             // No second CC
            1,              // Port 1
            false           // Not a fader
        );
    }
}

void MidiRouter::mapMutes_Channels25_36()
{
    // Parameters 96-107: Upper mutes for Channels 25-36
    // MIDI: CC 0-11, Channel 4, Port 2, Values 2=OFF, 3=ON
    
    for (int i = 0; i < 12; ++i)
    {
        int paramIndex = 96 + i;
        parameterMappings[paramIndex] = MidiMapping(
            4,              // MIDI Channel 4
            i,              // CC: 0, 1, 2, ... 11
            -1,             // No second CC
            2,              // Port 2
            false           // Not a fader
        );
    }
}

void MidiRouter::mapMutes_LowerRow()
{
    // Parameters 108-143: Lower (Mix) mutes for all 36 channels
    // Channels 1-24: Port 1, Channel 3, CC 24-47
    // Channels 25-36: Port 2, Channel 4, CC 12-23
    
    // Lower mutes for Channels 1-24 (Param 108-131)
    for (int i = 0; i < 24; ++i)
    {
        int paramIndex = 108 + i;
        parameterMappings[paramIndex] = MidiMapping(
            3,              // MIDI Channel 3
            24 + i,         // CC: 24, 25, 26, ... 47
            -1,
            1,              // Port 1
            false
        );
    }
    
    // Lower mutes for Channels 25-36 (Param 132-143)
    for (int i = 0; i < 12; ++i)
    {
        int paramIndex = 132 + i;
        parameterMappings[paramIndex] = MidiMapping(
            4,              // MIDI Channel 4
            12 + i,         // CC: 12, 13, 14, ... 23
            -1,
            2,              // Port 2
            false
        );
    }
}

void MidiRouter::mapAuxMutes()
{
    // Parameters 144-153: AUX 1-10 mutes
    // Special encoding: all use CC#96, Channel 5, Port 2
    // Values: AUX n → Val (n-1)*2+2 for OFF, (n-1)*2+3 for ON
    
    for (int i = 0; i < 10; ++i)
    {
        int paramIndex = 144 + i;
        parameterMappings[paramIndex] = MidiMapping(
            5,              // MIDI Channel 5
            96,             // CC 96 (special shared CC)
            -1,
            2,              // Port 2
            false
        );
    }
}

void MidiRouter::mapMasterSection()
{
    // Parameter 154: Joystick 1 X - CC#48, Ch 2, Port 2
    parameterMappings[154] = MidiMapping(2, 48, -1, 2, false);
    
    // Parameter 155: Joystick 1 Y - CC#50, Ch 2, Port 2
    parameterMappings[155] = MidiMapping(2, 50, -1, 2, false);
    
    // Parameter 156: Joystick 1 Mute - CC#24, Ch 4, Port 2, Val 2/3
    parameterMappings[156] = MidiMapping(4, 24, -1, 2, false);
    
    // Parameter 157: Joystick 2 X - CC#52, Ch 2, Port 2
    parameterMappings[157] = MidiMapping(2, 52, -1, 2, false);
    
    // Parameter 158: Joystick 2 Y - CC#54, Ch 2, Port 2
    parameterMappings[158] = MidiMapping(2, 54, -1, 2, false);
    
    // Parameter 159: Joystick 2 Mute - CC#26, Ch 4, Port 2, Val 2/3
    parameterMappings[159] = MidiMapping(4, 26, -1, 2, false);
    
    // Parameter 160: Master Fader - CC#0+1, Ch 5, Port 2 (14-bit)
    parameterMappings[160] = MidiMapping(5, 0, 1, 2, true);
}

//==============================================================================
// Helper Methods

int MidiRouter::floatToMidi14Bit(float normalizedValue) const
{
    // Convert 0.0-1.0 to 0-16383
    return static_cast<int>(juce::jlimit(0.0f, 1.0f, normalizedValue) * 16383.0f);
}

float MidiRouter::midi14BitToFloat(int midiValue14Bit) const
{
    // Convert 0-16383 to 0.0-1.0
    return juce::jlimit(0, 16383, midiValue14Bit) / 16383.0f;
}

int MidiRouter::floatToMidi7Bit(float normalizedValue) const
{
    // Convert 0.0-1.0 to 0-127
    return static_cast<int>(juce::jlimit(0.0f, 1.0f, normalizedValue) * 127.0f);
}

float MidiRouter::midi7BitToFloat(int midiValue7Bit) const
{
    // Convert 0-127 to 0.0-1.0
    return juce::jlimit(0, 127, midiValue7Bit) / 127.0f;
}

int MidiRouter::findParameterIndex(int midiChannel, int ccNumber, int portIndex) const
{
    // Search through all parameter mappings for matching MIDI message
    for (int i = 0; i < 161; ++i)
    {
        const MidiMapping& mapping = parameterMappings[i];
        
        // Check if MIDI channel, CC, and port match
        if (mapping.midiChannel == midiChannel && 
            mapping.portIndex == portIndex)
        {
            // For faders, check primary CC (MSB)
            if (mapping.isFader && mapping.ccNumber == ccNumber)
                return i;
            
            // For non-faders, check CC number
            if (!mapping.isFader && mapping.ccNumber == ccNumber)
                return i;
        }
    }
    
    return -1;  // No matching parameter found
}

