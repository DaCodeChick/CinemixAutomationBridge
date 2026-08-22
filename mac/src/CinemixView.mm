// CinemixView implementation — macOS only, not compiled on Linux.
#import "CinemixView.h"

#include <cstdio>
#include <cstring>
#include <deque>
#include <string>

#include "CinemixBridge.h"
#include "Config.h"

using cinemix_mac::BridgeContext;

static const CGFloat kPanelWidth = 560;
static const CGFloat kPanelHeight = 420;

@interface CinemixCocoaView () {
    AudioUnit _au;
    BridgeContext _ctx; // fetched from the AU at open; engine is thread-safe to command

    NSPopUpButton* _in1;
    NSPopUpButton* _in2;
    NSPopUpButton* _out1;
    NSPopUpButton* _out2;
    NSPopUpButton* _level;
    NSTextField* _status;
    NSTextView* _log;
    NSTimer* _timer;

    // C++ value member (RAII): no manual new/delete; the ObjC runtime calls
    // the C++ destructor during -dealloc under ARC.
    std::deque<std::string> _pendingLog;
    NSLock* _logLock;
    NSButton* _testFadersButton;

    BOOL _sinkInstalled;
}
@end

@implementation CinemixCocoaViewFactory

- (NSView*)uiViewForAudioUnit:(AudioUnit)inAudioUnit withSize:(NSSize)inPreferredSize {
    (void)inPreferredSize;
    return [[CinemixCocoaView alloc] initWithFrame:NSMakeRect(0, 0, kPanelWidth, kPanelHeight)
                                         audioUnit:inAudioUnit];
}

@end

@implementation CinemixCocoaView

// ---------------------------------------------------------------------------

- (instancetype)initWithFrame:(NSRect)frame audioUnit:(AudioUnit)au {
    if ((self = [super initWithFrame:frame]) == nil) return nil;
    _au = au;
    _logLock = [[NSLock alloc] init];
    _sinkInstalled = NO;
    memset(&_ctx, 0, sizeof(_ctx));

    // Reach the C++ core through the custom bridge property.
    BridgeContext* ctxPtr = nullptr;
    UInt32 size = sizeof(ctxPtr);
    if (AudioUnitGetProperty(au, kCinemixProperty_BridgeContext, kAudioUnitScope_Global, 0,
                             &ctxPtr, &size) == noErr && ctxPtr) {
        _ctx = *ctxPtr;
    }
    [self buildUI];

    // Subscribe to the engine's diagnostics for the log pane (worker thread →
    // main thread queue). Capture an explicit local pointer rather than the
    // implicit `self`; the sink is swapped back to the default in -dealloc
    // (Diagnostics::setSink waits for any in-flight log call).
    if (_ctx.diag) {
        cinemix::Diagnostics* diag = _ctx.diag;
        CinemixCocoaView* view = self;
        diag->setSink([view](cinemix::Diagnostics::Level level, const std::string& message) {
            // Direct construction: no fixed buffer, no silent truncation —
            // diagnostics for vintage hardware must not lose text.
            const std::string line = std::string("[") +
                                     cinemix::Diagnostics::levelName(level) + "] " + message;
            [view appendLogLine:line.c_str()];
        });
        _sinkInstalled = YES;
    }

    // Block-based timer with a weak capture: the timer does not retain the
    // view, so -dealloc can run and invalidate it. 0.5 s endpoint
    // re-enumeration is deliberate: the UI's topology refresh is
    // polling-based (docs/ARCHITECTURE.md §7) — there is no CoreMIDI
    // notification callback API to hook, and re-enumerating a handful of
    // endpoints twice a second is trivial on the target hardware.
    __weak CinemixCocoaView* weakSelf = self;
    _timer = [NSTimer scheduledTimerWithTimeInterval:0.5
                                             repeats:YES
                                               block:^(NSTimer* timer) {
        [weakSelf refresh:timer];
    }];
    [self refresh:nil];
    return self;
}

- (void)viewWillMoveToWindow:(NSWindow*)newWindow {
    // AppKit lifetime hook: when the panel leaves its window the refresh
    // timer must stop even if -dealloc is delayed.
    if (newWindow == nil) [_timer invalidate];
    [super viewWillMoveToWindow:newWindow];
}

- (void)appendLogLine:(const char*)line {
    [_logLock lock];
    if (_pendingLog.size() > 500) _pendingLog.pop_front(); // bounded tail
    _pendingLog.push_back(std::string(line));
    [_logLock unlock];
}

- (void)buildUI {
    // --- Port selection row ---
    _in1 = [self makePortPopup];
    _in2 = [self makePortPopup];
    _out1 = [self makePortPopup];
    _out2 = [self makePortPopup];

    [self addSubview:[self label:@"MIDI In 1" atX:10]];
    [self placePopup:_in1 atX:10 y:0];
    [self addSubview:[self label:@"MIDI In 2" atX:150]];
    [self placePopup:_in2 atX:150 y:0];
    [self addSubview:[self label:@"MIDI Out 1" atX:290]];
    [self placePopup:_out1 atX:290 y:0];
    [self addSubview:[self label:@"MIDI Out 2" atX:430]];
    [self placePopup:_out2 atX:430 y:0];

    // --- Command buttons ---
    NSButton* activate = [NSButton buttonWithTitle:@"Activate" target:self action:@selector(onActivate:)];
    [self placeButton:activate atX:10 y:1 width:90];
    NSButton* deactivate = [NSButton buttonWithTitle:@"Deactivate" target:self action:@selector(onDeactivate:)];
    [self placeButton:deactivate atX:105 y:1 width:90];
    NSButton* snapshot = [NSButton buttonWithTitle:@"Send Snapshot" target:self action:@selector(onSnapshot:)];
    [self placeButton:snapshot atX:200 y:1 width:110];
    NSButton* reset = [NSButton buttonWithTitle:@"Reset All" target:self action:@selector(onResetAll:)];
    [self placeButton:reset atX:315 y:1 width:90];
    NSButton* mutes = [NSButton buttonWithTitle:@"All Mutes" target:self action:@selector(onAllMutes:)];
    [self placeButton:mutes atX:410 y:1 width:90];
    _testFadersButton = [[NSButton alloc] initWithFrame:NSMakeRect(10, 26, 130, 28)];
    [_testFadersButton setButtonType:NSPushOnPushOffButton];
    [_testFadersButton setTitle:@"Test Faders"]; // operator-facing: this moves real motors
    [_testFadersButton setTarget:self];
    [_testFadersButton setAction:@selector(onTestMode:)];
    [self addSubview:_testFadersButton];

    // --- Diagnostics level ---
    [self addSubview:[self label:@"Log:" atX:130]];
    _level = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(165, 22, 120, 28) pullsDown:NO];
    [_level addItemsWithTitles:@[ @"Errors", @"Warnings", @"Info", @"Verbose", @"MIDI in/out" ]];
    [_level selectItemAtIndex:static_cast<NSInteger>(cinemix_mac::config::diagnosticsLevel())];
    [_level setTarget:self];
    [_level setAction:@selector(onLevelChanged:)];
    [self addSubview:_level];

    // --- Status line ---
    _status = [NSTextField labelWithString:@"…"];
    [_status setFrame:NSMakeRect(10, 52, kPanelWidth - 20, 16)];
    [self addSubview:_status];

    // --- Log pane ---
    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:
        NSMakeRect(10, 80, kPanelWidth - 20, kPanelHeight - 90)];
    [scroll setHasVerticalScroller:YES];
    [scroll setAutohidesScrollers:YES];
    NSSize contentSize = [scroll contentSize];
    _log = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, contentSize.width, contentSize.height)];
    [_log setMinSize:NSMakeSize(0, contentSize.height)];
    [_log setMaxSize:NSMakeSize(FLT_MAX, FLT_MAX)];
    [_log setVerticallyResizable:YES];
    [_log setHorizontallyResizable:NO];
    [_log setAutoresizingMask:NSViewWidthSizable];
    [[_log textContainer] setContainerSize:NSMakeSize(contentSize.width, FLT_MAX)];
    [[_log textContainer] setWidthTracksTextView:YES];
    [_log setEditable:NO];
    [_log setFont:[NSFont fontWithName:@"Menlo" size:10]];
    [scroll setDocumentView:_log];
    [self addSubview:scroll];
}

- (NSTextField*)label:(NSString*)text atX:(CGFloat)x {
    NSTextField* l = [NSTextField labelWithString:text];
    [l setFrame:NSMakeRect(x, 30, 80, 14)];
    return l;
}

- (NSPopUpButton*)makePortPopup {
    NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(0, 0, 120, 28)
                                                       pullsDown:NO];
    [popup setTarget:self];
    [popup setAction:@selector(onPortChanged:)];
    [popup addItemWithTitle:@"(none)"];
    [popup setTag:0];
    return popup;
}

- (void)placePopup:(NSPopUpButton*)popup atX:(CGFloat)x y:(CGFloat)y {
    (void)y;
    [popup setFrame:NSMakeRect(x, 4, 120, 28)];
    [self addSubview:popup];
}

- (void)placeButton:(NSButton*)button atX:(CGFloat)x y:(CGFloat)y width:(CGFloat)w {
    (void)y;
    [button setFrame:NSMakeRect(x, 26, w, 28)];
    [button setBezelStyle:NSRoundedBezelStyle];
    [self addSubview:button];
}

// ---------------------------------------------------------------------------
// Actions

- (void)onActivate:(id)sender {
    (void)sender;
    if (_ctx.engine) _ctx.engine->activate();
}

- (void)onDeactivate:(id)sender {
    (void)sender;
    if (_ctx.engine) _ctx.engine->deactivate();
}

- (void)onSnapshot:(id)sender {
    (void)sender;
    if (_ctx.engine) _ctx.engine->sendSnapshot();
}

- (void)onResetAll:(id)sender {
    (void)sender;
    if (_ctx.engine) _ctx.engine->resetAll();
}

- (void)onAllMutes:(id)sender {
    (void)sender;
    if (_ctx.engine) _ctx.engine->toggleAllMutes();
}

- (void)onTestMode:(id)sender {
    NSButton* b = (NSButton*)sender;
    if (_ctx.engine) _ctx.engine->setTestMode([b state] == NSOnState);
}

- (void)onLevelChanged:(id)sender {
    NSPopUpButton* popup = (NSPopUpButton*)sender;
    const NSInteger level = [popup indexOfSelectedItem];
    cinemix_mac::config::setDiagnosticsLevel(static_cast<int>(level));
    if (_ctx.diag)
        _ctx.diag->setLevel(static_cast<cinemix::Diagnostics::Level>(level));
}

- (void)onPortChanged:(id)sender {
    if (!_ctx.transport) return;
    NSPopUpButton* popup = (NSPopUpButton*)sender;
    NSString* title = [popup titleOfSelectedItem];
    const std::string portName =
        (title && ![title isEqualToString:@"(none)"]) ? [title UTF8String] : "";

    if (popup == _in1) {
        cinemix_mac::config::setInput1Name(portName);
        _ctx.transport->selectInputs(portName, cinemix_mac::config::input2Name());
    } else if (popup == _in2) {
        cinemix_mac::config::setInput2Name(portName);
        _ctx.transport->selectInputs(cinemix_mac::config::input1Name(), portName);
    } else if (popup == _out1) {
        cinemix_mac::config::setOutput1Name(portName);
        _ctx.transport->selectOutputs(portName, cinemix_mac::config::output2Name());
    } else if (popup == _out2) {
        cinemix_mac::config::setOutput2Name(portName);
        _ctx.transport->selectOutputs(cinemix_mac::config::output1Name(), portName);
    }
    [self refresh:nil];
}

// ---------------------------------------------------------------------------
// Periodic refresh

- (void)refresh:(NSTimer*)timer {
    (void)timer;
    if (_ctx.transport) {
        // Keep the port popups in sync with CoreMIDI topology (device
        // plug/unplug) without disturbing the current selection.
        const std::vector<std::string> ins = _ctx.transport->inputNames();
        const std::vector<std::string> outs = _ctx.transport->outputNames();
        [self repopulate:_in1 withNames:ins selected:cinemix_mac::config::input1Name()];
        [self repopulate:_in2 withNames:ins selected:cinemix_mac::config::input2Name()];
        [self repopulate:_out1 withNames:outs selected:cinemix_mac::config::output1Name()];
        [self repopulate:_out2 withNames:outs selected:cinemix_mac::config::output2Name()];
    }
    if (_ctx.engine) {
        // Terminology (deliberate): ACTIVE = remote-control mode has been
        // COMMANDED to the console; it does not prove the physical mixer
        // responded — the Log pane shows actual console traffic. "Outputs
        // selected" = CoreMIDI destinations configured; not a claim that the
        // console answered.
        const bool faderTestActive = _ctx.engine->testMode();
        NSString* state = _ctx.engine->isActivated()
            ? (faderTestActive ? @"ACTIVE — fader test running" : @"ACTIVE")
            : @"standby (console released)";
        NSString* conn = (_ctx.transport && _ctx.transport->connected())
            ? @"MIDI outputs selected" : @"no MIDI outputs selected";
        [_status setStringValue:[NSString stringWithFormat:@"%@ — %@", state, conn]];
        // Explicit operator-facing state: the toggle reads "Stop Fader Test"
        // while the oscillator drives physical motors.
        [_testFadersButton setTitle:faderTestActive ? @"Stop Fader Test" : @"Test Faders"];
        if ([_testFadersButton state] != (faderTestActive ? NSOnState : NSOffState))
            [_testFadersButton setState:faderTestActive ? NSOnState : NSOffState];
    }
    [self drainLog];
}

- (void)repopulate:(NSPopUpButton*)popup withNames:(const std::vector<std::string>&)names
          selected:(const std::string&)selectedName {
    NSMutableArray* titles = [NSMutableArray arrayWithObject:@"(none)"];
    NSInteger selection = 0;
    for (size_t i = 0; i < names.size(); ++i) {
        [titles addObject:[NSString stringWithUTF8String:names[i].c_str()]];
        if (names[i] == selectedName) selection = static_cast<NSInteger>(i + 1);
    }
    if (![[popup itemTitles] isEqualToArray:titles]) {
        [popup removeAllItems];
        [popup addItemsWithTitles:titles];
    }
    [popup selectItemAtIndex:selection];
}

- (void)drainLog {
    [_logLock lock];
    while (!_pendingLog.empty()) {
        std::string line = _pendingLog.front();
        _pendingLog.pop_front();
        [_logLock unlock];
        NSDictionary* attrs = @{ NSFontAttributeName : [NSFont fontWithName:@"Menlo" size:10] };
        NSAttributedString* s = [[NSAttributedString alloc]
            initWithString:[NSString stringWithUTF8String:line.c_str()] attributes:attrs];
        [[_log textStorage] appendAttributedString:s];
        [[_log textStorage] appendAttributedString:[[NSAttributedString alloc] initWithString:@"\n"]];
        [_log scrollRangeToVisible:NSMakeRange([[_log string] length], 0)];
        [_logLock lock];
    }
    [_logLock unlock];
}

// ---------------------------------------------------------------------------

- (void)dealloc {
    // Timer uses a weak capture, so dealloc can run; invalidate anyway.
    [_timer invalidate];
    if (_sinkInstalled && _ctx.diag) {
        // Restore the AU's default os_log sink so diagnostics keep flowing
        // after the panel closes. Diagnostics::setSink is mutex-guarded and
        // waits for any in-flight log call, so no sink invocation can touch
        // this view after this point.
        cinemix_mac::config::installDefaultLogSink(*_ctx.diag);
    }
}

@end
