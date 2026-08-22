// CinemixBridge — shared declarations for the AU component and its Cocoa
// view (the custom property through which the view reaches the C++ core).
#ifndef CINEMIX_MAC_BRIDGE_H
#define CINEMIX_MAC_BRIDGE_H

#include "CoreMidiTransport.h"
#include "cinemix/AutomationEngine.h"
#include "cinemix/Diagnostics.h"

// Custom Audio Unit property: returns a pointer to a BridgeContext
// (size = sizeof(void*), global scope, read-only). The Cocoa view fetches it
// with AudioUnitGetProperty to reach the engine/transport/diagnostics.
enum {
    kCinemixProperty_BridgeContext = 'Cxtx',
};

namespace cinemix_mac {

struct BridgeContext {
    cinemix::AutomationEngine* engine;
    cinemix::Diagnostics* diag;
    CoreMidiTransport* transport;
};

// Component identity (stable; subtype 'DRcm' kept from the legacy bridge).
//   type 'augn' (generator: no audio inputs, silence output) — loads into an
//   instrument slot in Logic, exactly how the legacy VST instrument was used.
const AudioComponentDescription kCinemixComponentDescription = {
    kAudioUnitType_Generator, // 'augn'
    'DRcm',                   // subtype (legacy _Plugin_Unique_ID_)
    'CBRG',                   // manufacturer
    0,
    0,
};

} // namespace cinemix_mac

#endif // CINEMIX_MAC_BRIDGE_H
