// IMidiTransport — transport abstraction for the portable core.
//
// The core never touches CoreMIDI/ALSA directly; transports implement this.
//   CoreMidiTransport   (mac/, Objective-C++ → CoreMIDI)
//   LoopbackTransport   (tools/, simulated console wiring)
//   CaptureTransport    (tools/, .cmi capture/replay — hardware-free testing)
#ifndef CINEMIX_MIDI_TRANSPORT_H
#define CINEMIX_MIDI_TRANSPORT_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "cinemix/Types.h"

namespace cinemix {

class IMidiTransport {
public:
    virtual ~IMidiTransport() {}

    // Send one message. port 0 = broadcast (both console pairs), 1 = LO pair,
    // 2 = HI pair. Returns false if the transport cannot deliver (disconnected
    // output, unselected destination). Implementations must be non-blocking
    // in practice and safe to call from the bridge worker thread only.
    virtual bool send(uint8_t port, const MidiMessage& message) = 0;

    // True when outputs are configured/open and inputs are being read.
    virtual bool connected() const = 0;

    // Short human-readable description for the UI/diagnostics.
    virtual std::string description() const = 0;

    // Inbound bytes from the console (any producer thread). The handler must
    // be fast (copy into a queue). Set once before use.
    std::function<void(const uint8_t* data, size_t n)> onIncoming;
};

} // namespace cinemix

#endif // CINEMIX_MIDI_TRANSPORT_H
