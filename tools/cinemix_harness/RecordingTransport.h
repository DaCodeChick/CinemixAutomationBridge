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

    bool send(std::uint8_t port, const cinemix::MidiMessage& message) override {
        cinemix::CaptureEvent event;
        event.timestampUs = nowUs();
        event.direction = 1;
        event.port = port;
        event.message = message;
        writer_.writeEvent(event);
        return inner_.send(port, message);
    }
    bool connected() const override { return inner_.connected(); }
    std::string description() const override { return inner_.description(); }
    void stopInbound() override { inner_.stopInbound(); }

    // Inbound bytes from the console side: record (direction 0) and forward
    // to the transport's consumer.
    void deliverInbound(const std::uint8_t* data, std::size_t size) {
        parser_.feed(data, size);
        if (onIncoming) onIncoming(data, size);
    }

private:
    static void onCc(void* user, std::uint8_t channel, std::uint8_t cc, std::uint8_t value) {
        RecordingTransport* self = static_cast<RecordingTransport*>(user);
        cinemix::CaptureEvent event;
        event.timestampUs = self->nowUs();
        event.direction = 0;
        event.port = 0; // source port is not known at the byte level
        event.message = cinemix::MidiMessage::controlChange(channel, cc, value, 0);
        self->writer_.writeEvent(event);
    }
    static void onSystem(void* user, std::uint8_t status) {
        RecordingTransport* self = static_cast<RecordingTransport*>(user);
        if (status == 0xFF) {
            cinemix::CaptureEvent event;
            event.timestampUs = self->nowUs();
            event.direction = 0;
            event.port = 0;
            event.message = cinemix::MidiMessage::systemReset(0);
            self->writer_.writeEvent(event);
        }
    }
    static void onMalformed(void* user) { static_cast<void>(user); }

    std::uint64_t nowUs() const {
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now - start_).count());
    }

    cinemix::IMidiTransport& inner_;
    cinemix::CaptureWriter& writer_;
    std::chrono::steady_clock::time_point start_;
    cinemix::MidiParser parser_;
};

} // namespace cinemix_harness

#endif // CINEMIX_HARNESS_RECORDING_TRANSPORT_H
