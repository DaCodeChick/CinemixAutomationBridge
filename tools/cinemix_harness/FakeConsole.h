// FakeConsole — a scripted D&R Cinemix for hardware-free testing.
//
// Sits on the transport boundary: the harness *reads* whatever the bridge
// sends to the two console ports (via FakeTransport's recorded streams), and
// this class *sends* console traffic back through the transport's inbound
// path. It models the protocol-facing behavior of the real console
// (echoing commanded positions, reporting touches, SEL presses, mute
// changes) without pretending to be the hardware.
#ifndef CINEMIX_HARNESS_FAKE_CONSOLE_H
#define CINEMIX_HARNESS_FAKE_CONSOLE_H

#include <cstdint>

#include "cinemix/MidiTransport.h"

namespace cinemix_harness {

class FakeConsole {
public:
    explicit FakeConsole(cinemix::IMidiTransport& bridgeTransport)
        : transport_(bridgeTransport) {}

    // Console → bridge. 3-byte CC or the 1-byte 0xFF.
    void sendCc(uint8_t channel, uint8_t cc, uint8_t value);
    void sendReset();

    // --- Scripted hardware behaviors ----------------------------------------
    // The motor responds to a commanded position with an interpolated echo
    // stream (step size 8; the real console's step cadence is unknown).
    void emulateMotorEcho(uint8_t faderChannel, uint8_t faderCc, uint8_t from, uint8_t to);
    // Touch sensor: value 6 = touch, 5 = release (on the touch CC).
    void pressTouch(uint8_t controlChannel, uint8_t touchCc);
    void releaseTouch(uint8_t controlChannel, uint8_t touchCc);
    // SEL press: value 1.
    void pressSel(uint8_t controlChannel, uint8_t selCc);
    // Mute press: 3 = ON, 2 = OFF.
    void setMute(uint8_t controlChannel, uint8_t muteCc, bool on);

private:
    cinemix::IMidiTransport& transport_;
};

} // namespace cinemix_harness

#endif // CINEMIX_HARNESS_FAKE_CONSOLE_H
