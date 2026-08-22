// FakeTransport — in-memory transport for tests and the hardware-free
// harness. `send()` records messages per port (port 0 = broadcast to both
// port streams); `injectIncoming()` simulates console bytes arriving at the
// bridge.
#ifndef CINEMIX_TEST_FAKE_TRANSPORT_H
#define CINEMIX_TEST_FAKE_TRANSPORT_H

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

    FakeTransport() : connectedFlag(true) {}

    bool send(uint8_t port, const cinemix::MidiMessage& message) override {
        if (!connectedFlag) return false;
        if (port == 0 || port == 1) sentToPort1.push_back(message);
        if (port == 0 || port == 2) sentToPort2.push_back(message);
        return true;
    }
    bool connected() const override { return connectedFlag; }
    std::string description() const override { return "fake transport"; }

    void clear() {
        sentToPort1.clear();
        sentToPort2.clear();
    }

    void injectIncoming(const std::vector<uint8_t>& bytes) {
        if (onIncoming && !bytes.empty()) onIncoming(bytes.data(), bytes.size());
    }

    void injectCc(uint8_t channel, uint8_t cc, uint8_t value) {
        std::vector<uint8_t> bytes;
        bytes.push_back(uint8_t(0xB0u | ((channel - 1) & 0x0F)));
        bytes.push_back(cc & 0x7F);
        bytes.push_back(value & 0x7F);
        injectIncoming(bytes);
    }

    // Utility for tests: last message sent to a given port stream.
    const cinemix::MidiMessage* last(size_t port) const {
        const std::vector<cinemix::MidiMessage>* v =
            (port == 1) ? &sentToPort1 : &sentToPort2;
        return v->empty() ? nullptr : &v->back();
    }
};

} // namespace cinemix_test

#endif // CINEMIX_TEST_FAKE_TRANSPORT_H
