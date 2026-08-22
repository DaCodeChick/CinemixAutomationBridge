// MidiParser — byte-stream MIDI parser for the console input path.
//
// More robust than the legacy bridge (which assumed every RtMidi callback
// delivered exactly one complete 3-byte CC message): handles running status,
// real-time bytes interleaved in any message, sysex skipping, and truncated
// messages. Channel-voice CCs are delivered complete; the system-reset byte
// (0xFF) is delivered as a system message; everything else is counted and
// optionally reported.
#ifndef CINEMIX_MIDI_PARSER_H
#define CINEMIX_MIDI_PARSER_H

#include <cstdint>
#include <cstddef>

#include "cinemix/Types.h"

namespace cinemix {

class MidiParser {
public:
    typedef void (*ControlChangeFn)(void* user, uint8_t channel, uint8_t cc, uint8_t value);
    typedef void (*SystemByteFn)(void* user, uint8_t status);
    typedef void (*MalformedFn)(void* user);

    MidiParser() : user_(nullptr), onCc_(nullptr), onSystem_(nullptr), onMalformed_(nullptr) { reset(); }

    void setHandlers(void* user, ControlChangeFn cc, SystemByteFn sys, MalformedFn malformed) {
        user_ = user; onCc_ = cc; onSystem_ = sys; onMalformed_ = malformed;
    }

    // Feed bytes from the transport (e.g. a CoreMIDI packet or a captured
    // stream). Cheap enough for the transport callback, but the intended
    // place is the bridge worker thread.
    void feed(const uint8_t* data, size_t n);

    void reset();

    size_t malformedCount() const { return malformed_; }
    size_t ignoredCount() const { return ignored_; }

private:
    void emitCc(uint8_t channel, uint8_t cc, uint8_t value);

    void* user_;
    ControlChangeFn onCc_;
    SystemByteFn onSystem_;
    MalformedFn onMalformed_;

    uint8_t runningStatus_; // 0 = none
    uint8_t pendingData_[2];
    uint8_t pendingLen_;
    uint8_t pendingNeed_;
    uint8_t sysCommonSkip_;
    bool inSysex_;
    size_t malformed_;
    size_t ignored_;
};

} // namespace cinemix

#endif // CINEMIX_MIDI_PARSER_H
