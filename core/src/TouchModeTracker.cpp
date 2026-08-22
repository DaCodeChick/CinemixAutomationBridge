#include "cinemix/TouchModeTracker.h"

namespace cinemix {

TouchModeTracker::TouchModeTracker(const MixerProfile& profile,
                                   const CinemixProtocol& protocol,
                                   TransmissionScheduler& scheduler)
    : profile_(profile), protocol_(protocol), scheduler_(scheduler),
      modes_(profile.faderCount(), 0), masterMode_(0) {
}

void TouchModeTracker::enqueueMode(const MidiAddress& addr, uint8_t mode, bool highPriority) {
    OutboundCommand cmd;
    cmd.kind = CommandKind::SetMode;
    cmd.message = protocol_.encodeSetMode(addr, mode);
    cmd.param = kNoParam;
    if (highPriority) scheduler_.enqueueHigh(cmd);
    else scheduler_.enqueueCommand(cmd);
}

void TouchModeTracker::onSelPressed(uint16_t strip, StripPath path) {
    const size_t s = slot(strip, path);
    uint8_t mode = modes_[s];
    if (++mode > 3) mode = 0; // rotate: 0=ISO, 1=READ, 2=WRITE, 3=AUTO
    modes_[s] = mode;
    enqueueMode(protocol_.stripTouchAddress(strip, path), mode, false);
}

void TouchModeTracker::onMasterSelPressed() {
    if (++masterMode_ > 3) masterMode_ = 0;
    setAllStripModes(masterMode_);
}

void TouchModeTracker::onTouchChanged(uint16_t strip, StripPath path, bool touching) {
    // Only in AUTO mode does a touch change the console's write state
    // (legacy gate: touch is ignored unless the local mode is 3).
    if (modes_[slot(strip, path)] != 3) return;
    const MidiAddress addr = protocol_.stripTouchAddress(strip, path);
    // Touch → WRITE(2); release → AUTO(3) = "RW". High priority: the console
    // needs this promptly for correct automation write behavior.
    enqueueMode(addr, touching ? 2 : 3, true);
}

void TouchModeTracker::setStripMode(uint16_t strip, StripPath path, uint8_t mode) {
    modes_[slot(strip, path)] = mode & 0x03;
    enqueueMode(protocol_.stripTouchAddress(strip, path), modes_[slot(strip, path)], false);
}

void TouchModeTracker::setMasterMode(uint8_t mode) {
    masterMode_ = mode & 0x03;
    enqueueMode(protocol_.masterSelAddress(), masterMode_, false);
}

void TouchModeTracker::setJoystickModes(uint8_t mode) {
    if (profile_.hasJoystick1) enqueueMode(protocol_.joySelAddress(0), mode, false);
    if (profile_.hasJoystick2) enqueueMode(protocol_.joySelAddress(1), mode, false);
}

void TouchModeTracker::setAllStripModes(uint8_t mode) {
    // Legacy order: controller number ascending, channel 3 and 4 interleaved
    // (both broadcast). Scoped to the strips that exist in the profile — the
    // legacy bridge always swept 48 slots per side and thus also wrote the
    // joystick SEL addresses (CC 88/90) and unknown addresses on the HI side.
    const uint8_t m = mode & 0x03;
    const size_t loSlots = 2u * profile_.loStrips;
    const size_t hiSlots = 2u * profile_.hiStrips;
    const size_t sweep = loSlots > hiSlots ? loSlots : hiSlots;
    for (size_t w = 0; w < sweep; ++w) {
        if (w < loSlots) {
            MidiAddress addr(0, profile_.loControlChannel, uint8_t(64u + w));
            // Track local state for real strips only; the address itself
            // falls on a real touch CC because w < 2*loStrips.
            const uint16_t strip = uint16_t(w / 2);
            const StripPath path = (w % 2 == 0) ? StripPath::Chan : StripPath::Mix;
            modes_[slot(strip, path)] = m;
            enqueueMode(addr, m, false);
        }
        if (w < hiSlots) {
            MidiAddress addr(0, profile_.hiControlChannel, uint8_t(64u + w));
            const uint16_t strip = uint16_t(profile_.loStrips + w / 2);
            const StripPath path = (w % 2 == 0) ? StripPath::Chan : StripPath::Mix;
            modes_[slot(strip, path)] = m;
            enqueueMode(addr, m, false);
        }
    }
}

uint8_t TouchModeTracker::stripMode(uint16_t strip, StripPath path) const {
    return modes_[slot(strip, path)];
}

} // namespace cinemix
