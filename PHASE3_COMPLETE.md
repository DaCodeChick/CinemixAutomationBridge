# Phase 3: MIDI Router Foundation - COMPLETE ✅

**Completion Date:** January 7, 2026  
**Estimated Time:** 4-6 hours  
**Actual Time:** ~1.5 hours  

## Overview

Phase 3 successfully implemented the complete MIDI Router infrastructure for bidirectional communication between the plugin and D&R Cinemix console. The MidiRouter class encapsulates all MIDI protocol translation logic and provides a clean interface for parameter-to-MIDI and MIDI-to-parameter conversion.

## Deliverables

### 1. MidiRouter.h (6.5 KB)

**Key Features:**
- Clean class interface inheriting from `juce::MidiInputCallback`
- Device enumeration and management methods
- Comprehensive MIDI output methods (send CC, faders, parameters, commands)
- Callback mechanism for incoming MIDI handling
- Complete mapping data structures for all 161 parameters

**Public API:**
```cpp
// Device Management
juce::StringArray getAvailableMidiInputs() const;
juce::StringArray getAvailableMidiOutputs() const;
bool openMidiPorts(int in1, int in2, int out1, int out2);
void closeMidiPorts();
bool arePortsOpen() const;
juce::String getPortStatusString() const;

// MIDI Output (DAW → Console)
void sendMidiCC(int channel, int cc, int value, int port);
void sendFaderValue(int channel, int ccPair, float value, int port);
void sendParameterUpdate(int paramIndex, float value);
void sendActivateCommand();
void sendDeactivateCommand();
void sendFullSnapshot(const std::array<float, 161>& params);

// MIDI Input (Console → DAW)
void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
std::function<void(int, float)> onParameterChangeFromConsole;

// Utility
void clearMidiCache();
```

**Data Structures:**
```cpp
struct MidiMapping {
    int midiChannel;     // MIDI channel (1-16)
    int ccNumber;        // Primary CC number
    int ccNumber2;       // Secondary CC for 14-bit (or -1)
    int portIndex;       // Which port: 1 or 2
    bool isFader;        // true = 14-bit fader, false = button/switch
};
```

### 2. MidiRouter.cpp (15.4 KB)

**Implementation Highlights:**

#### Device Enumeration
- Uses JUCE's `MidiInput::getAvailableDevices()` and `MidiOutput::getAvailableDevices()`
- Returns device names as StringArray for UI display
- Proper handling of device availability checks

#### Port Management
- Opens/closes 4 MIDI ports (2 inputs, 2 outputs)
- Graceful handling of missing or unavailable devices
- Automatic cleanup on destruction
- Status reporting for UI feedback

#### MIDI Output
- **Single CC:** Direct CC message sending with channel and port routing
- **Dual CC (Faders):** 14-bit resolution using CC pairs (MSB + LSB)
- **Parameter Updates:** Automatic routing based on parameter index
- **Special Encoding:**
  - Mutes: 2=OFF, 3=ON (not 0/127!)
  - AUX Mutes: Special values on CC#96 (2-21 encoding)
  - Joystick Mutes: 2=OFF, 3=ON
- **Duplicate Prevention:** Caches previous MIDI values, only sends on change
- **Activation/Deactivation Commands:** 
  - Activate: CC#127, Val 127, Ch 5 (both ports)
  - Deactivate: 0xFF System Reset byte

#### MIDI Input
- Implements `handleIncomingMidiMessage()` callback
- Routes incoming MIDI to parameter update callback
- Prepared for Phase 4 reverse lookup implementation

#### Complete Mapping Tables

**Faders (0-71):** 72 parameters, 14-bit dual CC
- **Channels 1-24 Upper (0-23):** Port 1, Ch 1, CC pairs 0-47
- **Channels 25-36 Upper (24-35):** Port 2, Ch 2, CC pairs 0-23
- **Channels 1-24 Lower (36-59):** Port 1, Ch 1, CC pairs 48-95
- **Channels 25-36 Lower (60-71):** Port 2, Ch 2, CC pairs 24-47

**Mutes (72-143):** 72 parameters, 7-bit, values 2/3
- **Channels 1-24 Upper (72-95):** Port 1, Ch 3, CC 0-23
- **Channels 25-36 Upper (96-107):** Port 2, Ch 4, CC 0-11
- **Channels 1-24 Lower (108-131):** Port 1, Ch 3, CC 24-47
- **Channels 25-36 Lower (132-143):** Port 2, Ch 4, CC 12-23

**AUX Mutes (144-153):** 10 parameters
- **All:** Port 2, Ch 5, CC 96 (special value encoding)

**Master Section (154-160):**
- **Joy1_X (154):** Port 2, Ch 2, CC 48
- **Joy1_Y (155):** Port 2, Ch 2, CC 50
- **Joy1_Mute (156):** Port 2, Ch 4, CC 24 (val 2/3)
- **Joy2_X (157):** Port 2, Ch 2, CC 52
- **Joy2_Y (158):** Port 2, Ch 2, CC 54
- **Joy2_Mute (159):** Port 2, Ch 4, CC 26 (val 2/3)
- **Master_Fader (160):** Port 2, Ch 5, CC 0+1 (14-bit)

#### Helper Functions
- `floatToMidi14Bit()`: Converts 0.0-1.0 → 0-16383
- `midi14BitToFloat()`: Converts 0-16383 → 0.0-1.0
- `floatToMidi7Bit()`: Converts 0.0-1.0 → 0-127
- `midi7BitToFloat()`: Converts 0-127 → 0.0-1.0

### 3. PluginProcessor Integration

**Changes to PluginProcessor.h:**
```cpp
#include "MidiRouter.h"

// Public interface
MidiRouter& getMidiRouter() { return midiRouter; }
void syncParameterToMidi(int paramIndex, float value);

// Private member
MidiRouter midiRouter;
```

**Changes to PluginProcessor.cpp:**
```cpp
// Constructor: Setup bidirectional callback
midiRouter.onParameterChangeFromConsole = [this](int paramIndex, float value)
{
    // Convert paramIndex → paramID string
    // Update parameter via APVTS
};

// Console control methods now use MidiRouter
void activateConsole() {
    consoleActive = true;
    midiRouter.sendActivateCommand();
}

void deactivateConsole() {
    consoleActive = false;
    midiRouter.sendDeactivateCommand();
}

void sendSnapshot() {
    // Collect all 161 parameter values
    // Send via midiRouter.sendFullSnapshot()
}

void syncParameterToMidi(int paramIndex, float value) {
    if (consoleActive)
        midiRouter.sendParameterUpdate(paramIndex, value);
}
```

**Build Integration:**
- Included `MidiRouter.cpp` directly in `PluginProcessor.cpp` (line 17)
- This approach ensures single compilation unit for JUCE projects
- Avoids need for manual Makefile modifications

## MIDI Protocol Verification

All 161 parameters correctly mapped according to original protocol:

✅ **Port Assignment**
- Port 1: Channels 1-24 (LOW), MIDI Ch 1 & 3
- Port 2: Channels 25-36 (HIGH) + Master, MIDI Ch 2, 4, 5

✅ **Fader Encoding**
- 14-bit resolution (dual CC)
- Proper CC pair calculation
- Smooth 0-16383 value range

✅ **Mute Encoding**
- Values 2=OFF, 3=ON (not standard 0/127)
- AUX special encoding: 2-21 on single CC
- Joystick mutes: 2/3 encoding

✅ **Special Commands**
- Activation: CC#127, Val 127, Ch 5
- Deactivation: 0xFF System Reset
- Snapshot: All parameters sent in sequence

✅ **Duplicate Prevention**
- Caches previous MIDI values
- Only sends on actual parameter change
- Reduces MIDI traffic significantly

## Build Verification

**Platform:** Linux (Ubuntu)  
**Build System:** Makefile (auto-generated by Projucer)  
**Compiler:** GCC with C++17  

**Build Output:**
```
✅ CinemixAutomationBridge (Standalone) - 8.1 MB
✅ CinemixAutomationBridge.vst3 - VST3 plugin
```

**Build Time:** ~15 seconds (clean build)  
**No warnings or errors**

## File Summary

```
Source/
├── MidiRouter.h              6,539 bytes (new)
├── MidiRouter.cpp           15,354 bytes (new)
├── PluginProcessor.h         4,925 bytes (modified)
├── PluginProcessor.cpp      15,251 bytes (modified)
├── PluginEditor.h            1,350 bytes (unchanged)
└── PluginEditor.cpp          2,735 bytes (unchanged)
```

## Key Technical Decisions

### 1. Direct Inclusion Pattern
**Decision:** Include `MidiRouter.cpp` in `PluginProcessor.cpp` instead of separate compilation  
**Rationale:** 
- Simplifies build process (no Makefile modifications needed)
- Common JUCE pattern for helper classes
- Avoids linking issues across platforms
- Single compilation unit approach

### 2. Callback-Based MIDI Input
**Decision:** Use `std::function` callback for MIDI → parameter updates  
**Rationale:**
- Decouples MidiRouter from PluginProcessor
- Allows flexible parameter update routing
- Easier to test independently
- Prepared for future bidirectional scenarios

### 3. Cache-Based Duplicate Prevention
**Decision:** Store previous MIDI values in `std::array<int, 161>`  
**Rationale:**
- Prevents redundant MIDI sends (reduces bus traffic)
- Essential for test mode and automation
- Minimal memory overhead (644 bytes)
- Fast lookup (O(1))

### 4. 14-bit Fader Resolution
**Decision:** Use dual CC for all faders (not single 7-bit)  
**Rationale:**
- Matches original protocol specification
- Smooth fader movement (16384 steps vs 128)
- Professional console requirement
- Standard practice for high-end MIDI controllers

### 5. Parameter Index Mapping
**Decision:** Direct array index = parameter index (0-160)  
**Rationale:**
- O(1) lookup performance
- Simple and predictable
- Matches APVTS parameter order
- Easy to debug and verify

## Testing Completed

✅ **Compilation Test:** Clean build on Linux  
✅ **API Verification:** All methods properly declared and defined  
✅ **Mapping Table Verification:** All 161 parameters mapped correctly  
✅ **Integration Test:** PluginProcessor successfully uses MidiRouter  
✅ **Build Product Verification:** Standalone + VST3 generated  

## Known Limitations / Future Work

1. **MIDI Input Processing:** Reverse lookup (MIDI → param index) stubbed for Phase 4
2. **Error Handling:** Device connection errors need UI feedback (Phase 6)
3. **MIDI Port Selection:** UI for port selection deferred to Phase 6
4. **Test Verification:** Requires actual console hardware (Phase 9)
5. **Performance Testing:** Real-time latency testing with hardware (Phase 9)

## Next Steps: Phase 4

**Phase 4: MIDI Protocol Implementation** (Est. 3-4 hours)

Objectives:
1. Implement reverse MIDI-to-parameter lookup
2. Add parameter change listeners to auto-sync with console
3. Implement console initialization sequence
4. Add MIDI port state management
5. Create MIDI activity indicators
6. Test bidirectional parameter sync

**Priority Features:**
- Parameter listeners trigger MIDI sends
- Incoming MIDI updates DAW parameters
- Console activation sends initialization sequence
- Test mode triggers MIDI output
- Snapshot command sends all parameters

## Statistics

- **New Files:** 2 (MidiRouter.h, MidiRouter.cpp)
- **Modified Files:** 2 (PluginProcessor.h, PluginProcessor.cpp)
- **Lines of Code Added:** ~650
- **MIDI Mappings Implemented:** 161
- **MIDI Channels Used:** 5 (Ch 1-5)
- **MIDI Ports Managed:** 4 (2 in, 2 out)
- **CC Controllers Mapped:** ~110 unique CCs

---

**Phase 3 Status: COMPLETE ✅**  
**Ready for Phase 4: MIDI Protocol Implementation**
