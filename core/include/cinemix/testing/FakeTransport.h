// FakeTransport — in-memory transport for tests and the hardware-free
// harness. `send()` records messages per port (port 0 = broadcast to both
// port streams); `injectIncoming()` simulates console bytes arriving at the
// bridge.
#ifndef CINEMIX_TEST_FAKE_TRANSPORT_H
#define CINEMIX_TEST_FAKE_TRANSPORT_H

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "cinemix/MidiTransport.h"
#include "cinemix/Types.h"

namespace cinemix_test {

class FakeTransport : public cinemix::IMidiTransport {
public:
    std::vector<cinemix::MidiMessage> sentToPort1;
    std::vector<cinemix::MidiMessage> sentToPort2;
    bool connectedFlag;
    // Set by stopInbound(): simulates the CoreMIDI guarantee that no further
    // inbound callbacks fire after the input port is quiesced. injectIncoming
    // becomes a no-op once set (the engine's teardown relies on this).
    bool inboundStopped;

    FakeTransport() : connectedFlag(true), inboundStopped(false) {}

    bool send(std::uint8_t port, const cinemix::MidiMessage& message) override {
        if (!connectedFlag) return false;
        if (port == 0 || port == 1) sentToPort1.push_back(message);
        if (port == 0 || port == 2) sentToPort2.push_back(message);
        return true;
    }
    bool connected() const override { return connectedFlag; }
    std::string description() const override { return "fake transport"; }

    void stopInbound() override {
        inboundStopped = true;
        onIncoming = nullptr;
    }

    void clear() {
        sentToPort1.clear();
        sentToPort2.clear();
    }

    void injectIncoming(const std::vector<std::uint8_t>& bytes) {
        // After stopInbound(), no further inbound delivery may occur (the
        // same contract CoreMIDI provides for a disposed port).
        if (inboundStopped) return;
        if (onIncoming && !bytes.empty()) onIncoming(bytes.data(), bytes.size());
    }

    void injectCc(std::uint8_t channel, std::uint8_t cc, std::uint8_t value) {
        std::array<std::uint8_t, 3> bytes = {
            static_cast<std::uint8_t>(0xB0u | ((channel - 1u) & 0x0Fu)),
            static_cast<std::uint8_t>(cc & 0x7Fu),
            static_cast<std::uint8_t>(value & 0x7Fu)};
        injectIncoming(std::vector<std::uint8_t>(bytes.begin(), bytes.end()));
    }

    // Utility for tests: last message sent to a given port stream.
    const cinemix::MidiMessage* last(std::size_t port) const {
        const std::vector<cinemix::MidiMessage>* stream =
            (port == 1) ? &sentToPort1 : &sentToPort2;
        return stream->empty() ? nullptr : &stream->back();
    }
};

} // namespace cinemix_test

#endif // CINEMIX_TEST_FAKE_TRANSPORT_H
