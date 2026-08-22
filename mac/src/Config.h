// Config — bridge configuration: NSUserDefaults (MIDI endpoint names by
// role, diagnostics level) and the mixer profile plist.
//
// The portable core never touches these; the profile plist is mapped onto
// cinemix::MixerProfile here. Defaults reproduce the legacy-proven console
// (LO=24, HI=12, strips 25-28 = S1..S4) — docs/PROTOCOL.md, docs/USER_GUIDE.md.
#ifndef CINEMIX_MAC_CONFIG_H
#define CINEMIX_MAC_CONFIG_H

#include <string>

#include "cinemix/Diagnostics.h"
#include "cinemix/MixerProfile.h"

namespace cinemix_mac {
namespace config {

// Installs the default diagnostics sink (os_log → Console.app). The Cocoa
// view replaces the sink with its log pane while open and reinstalls this
// when it closes.
void installDefaultLogSink(cinemix::Diagnostics& diag);

// ---- MIDI endpoint roles (NSUserDefaults) ----------------------------------
std::string input1Name();
std::string input2Name();
std::string output1Name();
std::string output2Name();
void setInput1Name(const std::string& name);
void setInput2Name(const std::string& name);
void setOutput1Name(const std::string& name);
void setOutput2Name(const std::string& name);

// ---- Diagnostics level (NSUserDefaults, 0=Error .. 5=MidiOut) ---------------
int diagnosticsLevel();
void setDiagnosticsLevel(int level);

// ---- Mixer profile ----------------------------------------------------------
// Reads ~/Library/Application Support/CinemixAutomationBridge/profile.plist
// when present (see docs/USER_GUIDE.md for the key format); otherwise the
// legacy default profile.
cinemix::MixerProfile loadProfile();

} // namespace config
} // namespace cinemix_mac

#endif // CINEMIX_MAC_CONFIG_H
