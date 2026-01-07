# CinemixAutomationBridge

Modern JUCE 8.0.12-based replication of the D&R Cinemix VST Automation Bridge.

## Status

✅ **Phase 1 Complete** - Project Setup & Basic Infrastructure  
⏳ Phase 2 - (Merged with Phase 1)  
⏳ Phase 3 - MIDI Router Foundation (Next)  

## Current Features (Phase 1)

- ✅ 161 automatable parameters
  - 72 Faders (36 channels × 2 rows)
  - 72 Mutes (36 channels × 2 rows)
  - 10 AUX Mutes
  - 7 Master Section parameters (2 joysticks + master fader)
- ✅ VST3 and Standalone builds
- ✅ Parameter state save/restore
- ✅ Basic UI (placeholder for full implementation)
- ✅ Console control method stubs
- ✅ Test mode infrastructure

## Building

### Linux
```bash
cd Builds/LinuxMakefile
make CONFIG=Release
```

Built binaries will be in `Builds/LinuxMakefile/build/`:
- `CinemixAutomationBridge.vst3` - VST3 plugin
- `CinemixAutomationBridge` - Standalone application

### macOS
```bash
cd Builds/MacOSX
open CinemixAutomationBridge.xcodeproj
# Build in Xcode
```

### Windows
```bash
cd Builds/VisualStudio2022
# Open CinemixAutomationBridge.sln in Visual Studio 2022
# Build solution
```

## Requirements

- JUCE 8.0.12
- C++17 compiler
- D&R Cinemix console with MIDI automation (for full functionality)
- MIDI interface with 2 input + 2 output ports

## Documentation

- `PLAN.md` - Comprehensive implementation plan
- `PHASE1_COMPLETE.md` - Phase 1 completion summary
- `MIDI_Cinemix.txt` - Original MIDI protocol documentation

## Original Project

This is a modern JUCE replication of:
https://github.com/ZioGuido/CinemixAutomationBridge

Original by Guido Scognamiglio (GSi) - 2012

## License

MIT License - See LICENSE file for details
