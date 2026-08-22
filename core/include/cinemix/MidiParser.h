// MidiParser — byte-stream MIDI parser for the console input path.
//
// More robust than the legacy bridge (which assumed every RtMidi callback
// delivered exactly one complete 3-byte CC message): handles running status,
// real-time bytes interleaved in any message, sysex skipping, and truncated
// messages. Channel-voice CCs are delivered complete; the system-reset byte
// (0xFF) is delivered as a system message; everything else is counted.
#ifndef CINEMIX_MIDI_PARSER_H
#define CINEMIX_MIDI_PARSER_H

#include <array>
#include <cstddef>
#include <cstdint>

#include "cinemix/Types.h"

namespace cinemix {

class MidiParser {
public:
    using ControlChangeFn = void (*)(void* user, std::uint8_t channel,
                                     std::uint8_t cc, std::uint8_t value);
    using SystemByteFn = void (*)(void* user, std::uint8_t status);
    using MalformedFn = void (*)(void* user);

    MidiParser() noexcept
        : user_(nullptr), onCc_(nullptr), onSystem_(nullptr), onMalformed_(nullptr),
          runningStatus_(0), pendingLen_(0), pendingNeed_(0), sysCommonSkip_(0),
          inSysex_(false), malformed_(0), ignored_(0) {}

    void setHandlers(void* user, ControlChangeFn cc, SystemByteFn sys, MalformedFn malformed) {
        user_ = user; onCc_ = cc; onSystem_ = sys; onMalformed_ = malformed;
    }

    // Feed bytes from the transport (e.g. a CoreMIDI packet or a captured
    // stream). Cheap enough for the transport callback, but the intended
    // place is the bridge worker thread.
    void feed(const std::uint8_t* data, std::size_t size);

    void reset();

    std::size_t malformedCount() const noexcept { return malformed_; }
    std::size_t ignoredCount() const noexcept { return ignored_; }

private:
    void emitCc(std::uint8_t channel, std::uint8_t cc, std::uint8_t value);

    void* user_;
    ControlChangeFn onCc_;
    SystemByteFn onSystem_;
    MalformedFn onMalformed_;

    std::uint8_t runningStatus_; // 0 = none
    std::array<std::uint8_t, 2> pendingData_{};
    std::uint8_t pendingLen_;
    std::uint8_t pendingNeed_;
    std::uint8_t sysCommonSkip_;
    bool inSysex_;
    std::size_t malformed_;
    std::size_t ignored_;
};

} // namespace cinemix

#endif // CINEMIX_MIDI_PARSER_H
