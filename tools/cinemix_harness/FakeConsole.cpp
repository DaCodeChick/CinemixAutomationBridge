#include "FakeConsole.h"

#include <array>

namespace cinemix_harness {

void FakeConsole::sendCc(std::uint8_t channel, std::uint8_t cc, std::uint8_t value) {
    std::array<std::uint8_t, 3> bytes = {
        static_cast<std::uint8_t>(0xB0u | ((channel - 1u) & 0x0Fu)),
        static_cast<std::uint8_t>(cc & 0x7Fu),
        static_cast<std::uint8_t>(value & 0x7Fu)};
    if (transport_.onIncoming) transport_.onIncoming(bytes.data(), bytes.size());
}

void FakeConsole::sendReset() {
    std::uint8_t byte = 0xFF;
    if (transport_.onIncoming) transport_.onIncoming(&byte, 1);
}

void FakeConsole::emulateMotorEcho(std::uint8_t faderChannel, std::uint8_t faderCc,
                                   std::uint8_t from, std::uint8_t to) {
    if (from < to) {
        for (int value = from; value <= static_cast<int>(to); value += 8)
            sendCc(faderChannel, faderCc, static_cast<std::uint8_t>(value));
        if ((to - from) % 8 != 0) sendCc(faderChannel, faderCc, to);
    } else if (from > to) {
        for (int value = from; value >= static_cast<int>(to); value -= 8)
            sendCc(faderChannel, faderCc, static_cast<std::uint8_t>(value));
        if ((from - to) % 8 != 0) sendCc(faderChannel, faderCc, to);
    } else {
        sendCc(faderChannel, faderCc, to);
    }
}

void FakeConsole::pressTouch(std::uint8_t controlChannel, std::uint8_t touchCc) {
    sendCc(controlChannel, touchCc, 6);
}
void FakeConsole::releaseTouch(std::uint8_t controlChannel, std::uint8_t touchCc) {
    sendCc(controlChannel, touchCc, 5);
}
void FakeConsole::pressSel(std::uint8_t controlChannel, std::uint8_t selCc) {
    sendCc(controlChannel, selCc, 1);
}
void FakeConsole::setMute(std::uint8_t controlChannel, std::uint8_t muteCc, bool on) {
    sendCc(controlChannel, muteCc, on ? 3 : 2);
}

} // namespace cinemix_harness
