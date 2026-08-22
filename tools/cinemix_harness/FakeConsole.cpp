#include "FakeConsole.h"

namespace cinemix_harness {

void FakeConsole::sendCc(uint8_t channel, uint8_t cc, uint8_t value) {
    uint8_t bytes[3] = {uint8_t(0xB0u | ((channel - 1) & 0x0F)), uint8_t(cc & 0x7F), uint8_t(value & 0x7F)};
    if (transport_.onIncoming) transport_.onIncoming(bytes, 3);
}

void FakeConsole::sendReset() {
    uint8_t b = 0xFF;
    if (transport_.onIncoming) transport_.onIncoming(&b, 1);
}

void FakeConsole::emulateMotorEcho(uint8_t faderChannel, uint8_t faderCc, uint8_t from, uint8_t to) {
    if (from < to) {
        for (int v = from; v <= int(to); v += 8) sendCc(faderChannel, faderCc, uint8_t(v));
        if ((to - from) % 8 != 0) sendCc(faderChannel, faderCc, to);
    } else if (from > to) {
        for (int v = from; v >= int(to); v -= 8) sendCc(faderChannel, faderCc, uint8_t(v));
        if ((from - to) % 8 != 0) sendCc(faderChannel, faderCc, to);
    } else {
        sendCc(faderChannel, faderCc, to);
    }
}

void FakeConsole::pressTouch(uint8_t controlChannel, uint8_t touchCc) {
    sendCc(controlChannel, touchCc, 6);
}
void FakeConsole::releaseTouch(uint8_t controlChannel, uint8_t touchCc) {
    sendCc(controlChannel, touchCc, 5);
}
void FakeConsole::pressSel(uint8_t controlChannel, uint8_t selCc) {
    sendCc(controlChannel, selCc, 1);
}
void FakeConsole::setMute(uint8_t controlChannel, uint8_t muteCc, bool on) {
    sendCc(controlChannel, muteCc, on ? 3 : 2);
}

} // namespace cinemix_harness
