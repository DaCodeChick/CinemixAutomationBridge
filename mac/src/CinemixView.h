// CinemixView — the AppKit configuration/status panel.
// Programmatic UI (no xib), macOS 10.13-safe AppKit only.
// Implemented but NOT compiled on the development host (Linux).
//
// The panel is deliberately functional, not flashy (brief §16): port
// selection, activation, snapshot/reset/all-mutes/test-mode commands,
// connection status, and a live diagnostics log. Logic's generic AU
// parameter view remains fully usable for all 161 parameters.
#import <Cocoa/Cocoa.h>
#import <CoreAudioKit/CoreAudioKit.h>
#import <AudioToolbox/AudioUnit.h>

// Exposed to Logic via kAudioUnitProperty_CocoaUI (see CinemixAU.mm).
@interface CinemixCocoaViewFactory : NSObject <AUCocoaUIBase>
@end

@interface CinemixCocoaView : NSView
- (instancetype)initWithFrame:(NSRect)frame audioUnit:(AudioUnit)au;
@end
