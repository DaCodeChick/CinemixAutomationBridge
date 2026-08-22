// Cinemix core — foundational types.
// Portable, conservative C++14. No Apple/Windows/plugin dependencies.
//
// Part of the Cinemix Automation Bridge. Protocol knowledge recovered from the
// legacy GSi "CinemixAutomationBridge" (2012–2017) — see docs/PROTOCOL.md.
#ifndef CINEMIX_TYPES_H
#define CINEMIX_TYPES_H

#include <array>
#include <cstddef>
#include <cstdint>

namespace cinemix {

// One 7-bit MIDI data byte.
using MidiByte = std::uint8_t;

// Host/AU parameter id. Stable for the lifetime of a parameter map; the
// default profile reproduces the legacy plugin's 0..160 numbering.
using ParamId = std::uint16_t;
constexpr ParamId kNoParam = 0xFFFFu;

// Physical console side of a channel strip.
enum class ConsoleSide : std::uint8_t { Lo = 0, Hi = 1 };

// Dual-path strip: CHANNEL path ("upper", 60 mm motorfaders in the author's
// console) and MIX path ("lower", 100 mm motorfaders).
enum class StripPath : std::uint8_t { Chan = 0, Mix = 1 };

// Origin of a state change — used for feedback-loop prevention.
enum class Origin : std::uint8_t {
    None = 0,         // no change yet
    Host = 1,         // Logic automation / host parameter write
    Console = 2,      // physical console (fader, mute, joystick)
    UserInterface = 3, // our own UI button (snapshot/reset/all-mutes)
    Internal = 4,     // initialization, test mode
};

// A control on the console, independent of its MIDI encoding.
enum class ControlClass : std::uint8_t {
    Fader = 0,   // strip fader (strip + path)
    Mute = 1,    // strip mute (strip + path)
    Touch = 2,   // strip touch/SEL sensor (strip + path); value disambiguates
    JoyAxis = 3, // joystick X/Y (index = joystick; path carries the axis)
    JoyMute = 4,
    JoySel = 5,
    MasterFader = 6,
    MasterSel = 7,
    AuxMute = 8, // index = aux number (0-based)
};

// Addresses a console control. Unused fields are zero.
struct ControlRef {
    ControlClass cls;
    std::uint8_t strip; // 0-based strip index
    StripPath path;     // Chan/Mix for Fader/Mute/Touch; for JoyAxis: axis (0=X,1=Y)
    std::uint8_t index; // joystick number (0/1), aux number (0..9)

    constexpr ControlRef() noexcept
        : cls(ControlClass::Fader), strip(0), path(StripPath::Chan), index(0) {}
    constexpr ControlRef(ControlClass controlClass, std::uint8_t stripIndex,
                         StripPath stripPath, std::uint8_t controlIndex) noexcept
        : cls(controlClass), strip(stripIndex), path(stripPath), index(controlIndex) {}

    constexpr bool operator==(const ControlRef& other) const noexcept {
        return cls == other.cls && strip == other.strip && path == other.path &&
               index == other.index;
    }
    constexpr bool operator!=(const ControlRef& other) const noexcept {
        return !(*this == other);
    }
};

// One MIDI message, as sent to or received from the console.
// Channel messages are 2 or 3 bytes; system messages (0xFF reset) are 1 byte.
struct MidiMessage {
    std::array<MidiByte, 3> data{};
    std::uint8_t length = 0; // 1..3
    std::uint8_t port = 0;   // outbound routing: 0 = broadcast, 1 = LO, 2 = HI

    constexpr MidiMessage() noexcept : data{}, length(0), port(0) {}

    // Plain functions, not constexpr: mutating std::array elements is not a
    // C++14 constant-expression capability (constexpr std::array::operator[]
    // for mutation arrived with C++17).
    static MidiMessage controlChange(std::uint8_t channel, std::uint8_t cc,
                                     std::uint8_t value,
                                     std::uint8_t port = 0) noexcept {
        MidiMessage message;
        message.data[0] = static_cast<MidiByte>(0xB0u | ((channel - 1u) & 0x0Fu));
        message.data[1] = static_cast<MidiByte>(cc & 0x7Fu);
        message.data[2] = static_cast<MidiByte>(value & 0x7Fu);
        message.length = 3;
        message.port = port;
        return message;
    }

    static MidiMessage systemReset(std::uint8_t port = 0) noexcept {
        MidiMessage message;
        message.data[0] = 0xFF;
        message.length = 1;
        message.port = port;
        return message;
    }

    constexpr bool isSystemReset() const noexcept { return length == 1 && data[0] == 0xFF; }
};

// An event decoded from an incoming MIDI message. Raw MIDI is never exposed to
// the automation engine; it only sees these.
enum class EventKind : std::uint8_t {
    FaderPosition = 0, // normalized position available
    MuteChanged = 1,   // `on` available
    TouchBegin = 2,
    TouchEnd = 3,
    SelPressed = 4,      // strip SEL button
    MasterSelPressed = 5,
    JoyMuteChanged = 6,
    AuxMuteChanged = 7,
    Unknown = 8, // valid MIDI CC, not part of the protocol (diagnostic)
    Ignored = 9, // protocol CC with a value we deliberately ignore
};

struct ConsoleEvent {
    EventKind kind;
    ControlRef control;
    MidiByte value;   // raw 7-bit value as received
    bool on;          // for mute events
    float normalized; // for fader/axis events (value / 127)
    std::uint8_t channel; // raw channel (1..16) — filled for Unknown events
    std::uint8_t cc;      // raw CC number — filled for Unknown events

    ConsoleEvent()
        : kind(EventKind::Unknown), control(), value(0), on(false), normalized(0.f),
          channel(0), cc(0) {}
};

// One outbound command through the scheduler's FIFO lanes.
enum class CommandKind : std::uint8_t {
    SetMode = 0,     // strip/master/joystick SEL value 0..3
    RemoteControl = 1, // CC127 ch5: 127 = enter, 0 = exit remote mode
    SystemReset = 2, // 0xFF byte
};

struct OutboundCommand {
    CommandKind kind;
    MidiMessage message; // fully encoded
    ParamId param;       // kNoParam unless it is a position update

    OutboundCommand() : kind(CommandKind::SetMode), param(kNoParam) {}
};

// ---------------------------------------------------------------------------
// Normalized-value math.
//
// The legacy bridge quantizes with truncation: int(value * 127). That is
// preserved deliberately — host automation must produce byte-identical
// console traffic. NaN and infinities are defined away so the float→int
// conversion never has undefined behavior.

// NaN, values ≤ 0 and negative infinity map to 0; values ≥ 1 and positive
// infinity map to 1. The `!(value > 0)` form is deliberate: it catches NaN.
constexpr float clamp01(float value) noexcept {
    if (!(value > 0.0f)) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

// 7-bit quantization, legacy truncation: static_cast<int>(v * 127).
constexpr MidiByte quantize7(float value) noexcept {
    const int truncated = static_cast<int>(clamp01(value) * 127.0f);
    return static_cast<MidiByte>(truncated < 0 ? 0 : (truncated > 127 ? 127 : truncated));
}

constexpr float normalize7(MidiByte value) noexcept {
    return static_cast<float>(value) / 127.0f;
}

// Echo-suppression hysteresis unit: one 7-bit step.
constexpr float faderHysteresis() noexcept { return 1.0f / 127.0f; }

} // namespace cinemix

#endif // CINEMIX_TYPES_H
