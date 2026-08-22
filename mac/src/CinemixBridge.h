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

// Component identity (declared in Resources/Info.plist and registered by the
// AUSDK_COMPONENT_ENTRY macro in CinemixAU.h; documented here as the
// contract): type 'augn' (generator: no audio inputs, silence output) loads
// into an instrument slot in Logic, exactly how the legacy VST instrument was
// used. Subtype 'DRcm' is kept from the legacy bridge (_Plugin_Unique_ID_);
// manufacturer 'CBRG' is new, so the old GSi AU cannot conflict.

} // namespace cinemix_mac

#endif // CINEMIX_MAC_BRIDGE_H
