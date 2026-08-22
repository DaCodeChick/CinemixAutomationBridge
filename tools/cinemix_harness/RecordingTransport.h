// RecordingTransport — wraps another transport and records both directions
// to a .cmi capture file (docs/TESTING.md). Used by the harness to produce
// deterministic fixtures and by diagnostics tooling on the target Mac.
#ifndef CINEMIX_HARNESS_RECORDING_TRANSPORT_H
#define CINEMIX_HARNESS_RECORDING_TRANSPORT_H

#include <chrono>
#include <cstdint>

#include "cinemix/CaptureFile.h"
#include "cinemix/MidiParser.h"
#include "cinemix/MidiTransport.h"
#include "cinemix/Types.h"

namespace cinemix_harness {

class RecordingTransport : public cinemix::IMidiTransport {
public:
    RecordingTransport(cinemix::IMidiTransport& inner, cinemix::CaptureWriter& writer)
        : inner_(inner), writer_(writer), start_(std::chrono::steady_clock::now()) {
        // Inbound bytes are split into complete messages for recording.
        parser_.setHandlers(this, &RecordingTransport::onCc, &RecordingTransport::onSystem,
                            &RecordingTransport::onMalformed);
    }

    bool send(uint8_t port, const cinemix::MidiMessage& message) override {
        writer_.writeEvent(nowUs(), 1, port, message);
        return inner_.send(port, message);
    }
    bool connected() const override { return inner_.connected(); }
    std::string description() const override { return inner_.description(); }

    // Inbound bytes from the console side: record (direction 0) and forward
    // to the transport's consumer.
    void deliverInbound(const uint8_t* data, size_t n) {
        parser_.feed(data, n);
        if (onIncoming) onIncoming(data, n);
    }

private:
    static void onCc(void* user, uint8_t channel, uint8_t cc, uint8_t value) {
        RecordingTransport* self = static_cast<RecordingTransport*>(user);
        cinemix::MidiMessage m = cinemix::MidiMessage::controlChange(channel, cc, value, 0);
        self->writer_.writeEvent(self->nowUs(), 0, 0, m);
    }
    static void onSystem(void* user, uint8_t status) {
        RecordingTransport* self = static_cast<RecordingTransport*>(user);
        if (status == 0xFF) {
            cinemix::MidiMessage m = cinemix::MidiMessage::systemReset(0);
            self->writer_.writeEvent(self->nowUs(), 0, 0, m);
        }
    }
    static void onMalformed(void* user) { (void)user; }

    uint64_t nowUs() const {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        return uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(now - start_).count());
    }

    cinemix::IMidiTransport& inner_;
    cinemix::CaptureWriter& writer_;
    std::chrono::steady_clock::time_point start_;
    cinemix::MidiParser parser_;
};

} // namespace cinemix_harness

#endif // CINEMIX_HARNESS_RECORDING_TRANSPORT_H
