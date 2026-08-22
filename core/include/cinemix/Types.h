// Cinemix core — foundational types.
// Portable C++11. No Apple/Windows/plugin dependencies.
//
// Part of the Cinemix Automation Bridge. Protocol knowledge recovered from the
// legacy GSi "CinemixAutomationBridge" (2012–2017) — see docs/PROTOCOL.md.
#ifndef CINEMIX_TYPES_H
#define CINEMIX_TYPES_H

#include <cstdint>
#include <cstddef>
#include <string>

namespace cinemix {

using MidiByte = uint8_t;

// AU/host parameter id. Stable for the lifetime of a parameter map; the
// default profile reproduces the legacy plugin's 0..160 numbering.
typedef uint16_t ParamId;
static const ParamId kNoParam = 0xFFFFu;

// Physical console side of a channel strip.
enum class ConsoleSide : uint8_t { Lo = 0, Hi = 1 };

// Dual-path strip: CHANNEL path ("upper", 60 mm motorfaders in the author's
// console) and MIX path ("lower", 100 mm motorfaders).
enum class StripPath : uint8_t { Chan = 0, Mix = 1 };

// Origin of a state change — used for feedback-loop prevention.
enum class Origin : uint8_t {
    None = 0,        // no change yet
    Host = 1,        // Logic automation / host parameter write
    Console = 2,     // physical console (fader, mute, joystick)
    UserInterface = 3, // our own UI button (snapshot/reset/all-mutes)
    Internal = 4,    // initialization, test mode
};

// A control on the console, independent of its MIDI encoding.
enum class ControlClass : uint8_t {
    Fader = 0,   // strip fader (strip + path)
    Mute = 1,    // strip mute (strip + path)
    Touch = 2,   // strip touch/SEL sensor (strip + path); value disambiguates
    JoyAxis = 3, // joystick X/Y (strip unused; index = joystick, axis in path? no — see below)
    JoyMute = 4,
    JoySel = 5,
    MasterFader = 6,
    MasterSel = 7,
    AuxMute = 8, // index = aux number (0-based)
};

// Addresses a console control. Unused fields are zero.
struct ControlRef {
    ControlClass cls;
    uint8_t strip;  // 0-based strip index
    StripPath path; // Chan/Mix for Fader/Mute/Touch; for JoyAxis: axis (0=X,1=Y)
    uint8_t index;  // joystick number (0/1), aux number (0..9)

    ControlRef() : cls(ControlClass::Fader), strip(0), path(StripPath::Chan), index(0) {}
    ControlRef(ControlClass c, uint8_t s, StripPath p, uint8_t i)
        : cls(c), strip(s), path(p), index(i) {}

    bool operator==(const ControlRef& o) const {
        return cls == o.cls && strip == o.strip && path == o.path && index == o.index;
    }
    bool operator!=(const ControlRef& o) const { return !(*this == o); }
};

// One MIDI message, as sent to or received from the console.
// Channel messages are 2 or 3 bytes; system messages (0xFF reset) are 1 byte.
struct MidiMessage {
    MidiByte data[3];
    uint8_t length; // 1..3
    uint8_t port;   // outbound routing: 0 = broadcast (both ports), 1, 2

    MidiMessage() : length(0), port(0) { data[0] = data[1] = data[2] = 0; }

    static MidiMessage controlChange(uint8_t channel, uint8_t cc, uint8_t value, uint8_t port = 0) {
        MidiMessage m;
        m.data[0] = MidiByte(0xB0u | ((channel - 1) & 0x0F));
        m.data[1] = cc & 0x7F;
        m.data[2] = value & 0x7F;
        m.length = 3;
        m.port = port;
        return m;
    }

    static MidiMessage systemReset(uint8_t port = 0) {
        MidiMessage m;
        m.data[0] = 0xFF;
        m.length = 1;
        m.port = port;
        return m;
    }

    bool isSystemReset() const { return length == 1 && data[0] == 0xFF; }
};

// An event decoded from an incoming MIDI message. Raw MIDI is never exposed to
// the automation engine; it only sees these.
enum class EventKind : uint8_t {
    FaderPosition = 0, // normalized position available
    MuteChanged = 1,   // `on` available
    TouchBegin = 2,
    TouchEnd = 3,
    SelPressed = 4,      // strip SEL button
    MasterSelPressed = 5,
    JoyMuteChanged = 6,
    AuxMuteChanged = 7,
    Unknown = 8,   // valid MIDI CC, not part of the protocol (diagnostic)
    Ignored = 9,   // protocol CC with a value we deliberately ignore
};

struct ConsoleEvent {
    EventKind kind;
    ControlRef control;
    MidiByte value;       // raw 7-bit value as received
    bool on;              // for mute events
    float normalized;     // for fader/axis events (value / 127)
    uint8_t channel;      // raw channel (1..16) — filled for Unknown events
    uint8_t cc;           // raw CC number — filled for Unknown events

    ConsoleEvent()
        : kind(EventKind::Unknown), control(), value(0), on(false),
          normalized(0.f), channel(0), cc(0) {}
};

// One outbound command through the scheduler's FIFO lane.
enum class CommandKind : uint8_t {
    SetMode = 0,      // strip/master/joystick SEL value 0..3
    RemoteControl = 1, // CC127 ch5: 127 = enter, 0 = exit remote mode
    SystemReset = 2,  // 0xFF byte
};

struct OutboundCommand {
    CommandKind kind;
    MidiMessage message; // fully encoded
    ParamId param;       // kNoParam unless it is a position update

    OutboundCommand() : kind(CommandKind::SetMode), param(kNoParam) {}
};

// Normalized parameter value: 0.0..1.0.
inline float clamp01(float v) {
    return v < 0.f ? 0.f : (v > 1.f ? 1.f : v);
}

// 7-bit quantization exactly as the legacy bridge does it: truncation of
// v * 127 (legacy: `int val = int(value * 127);`). Preserved deliberately so
// that host automation produces byte-identical console traffic.
inline MidiByte quantize7(float v) {
    int i = int(clamp01(v) * 127.f);
    return MidiByte(i < 0 ? 0 : (i > 127 ? 127 : i));
}

inline float normalize7(MidiByte v) { return float(v) / 127.f; }

// Echo-suppression hysteresis: one 7-bit step.
inline float faderHysteresis() { return 1.f / 127.f; }

} // namespace cinemix

#endif // CINEMIX_TYPES_H
