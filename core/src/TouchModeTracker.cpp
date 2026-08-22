#include "cinemix/TouchModeTracker.h"

namespace cinemix {

TouchModeTracker::TouchModeTracker(const MixerProfile& profile,
                                   const CinemixProtocol& protocol,
                                   TransmissionScheduler& scheduler)
    : profile_(profile), protocol_(protocol), scheduler_(scheduler),
      modes_(profile.faderCount(), 0), masterMode_(0) {
}

void TouchModeTracker::enqueueMode(const MidiAddress& address, std::uint8_t mode,
                                   bool highPriority) {
    OutboundCommand command;
    command.kind = CommandKind::SetMode;
    command.message = protocol_.encodeSetMode(address, mode);
    command.param = kNoParam;
    if (highPriority) scheduler_.enqueueHigh(command);
    else scheduler_.enqueueCommand(command);
}

void TouchModeTracker::onSelPressed(std::uint16_t strip, StripPath path) {
    const std::size_t index = slot(strip, path);
    std::uint8_t mode = modes_[index];
    if (++mode > 3) mode = 0; // rotate: 0=ISO, 1=READ, 2=WRITE, 3=AUTO
    modes_[index] = mode;
    enqueueMode(protocol_.stripTouchAddress(strip, path), mode, false);
}

void TouchModeTracker::onMasterSelPressed() {
    if (++masterMode_ > 3) masterMode_ = 0;
    setAllStripModes(masterMode_);
}

void TouchModeTracker::onTouchChanged(std::uint16_t strip, StripPath path, bool touching) {
    // Only in AUTO mode does a touch change the console's write state
    // (legacy gate: touch is ignored unless the local mode is 3).
    if (modes_[slot(strip, path)] != 3) return;
    const MidiAddress address = protocol_.stripTouchAddress(strip, path);
    // Touch → WRITE(2); release → AUTO(3) = "RW". High priority: the console
    // needs this promptly for correct automation write behavior.
    enqueueMode(address, touching ? 2 : 3, true);
}

void TouchModeTracker::setStripMode(std::uint16_t strip, StripPath path, std::uint8_t mode) {
    modes_[slot(strip, path)] = mode & 0x03u;
    enqueueMode(protocol_.stripTouchAddress(strip, path), modes_[slot(strip, path)], false);
}

void TouchModeTracker::setMasterMode(std::uint8_t mode) {
    masterMode_ = mode & 0x03u;
    enqueueMode(protocol_.masterSelAddress(), masterMode_, false);
}

void TouchModeTracker::setJoystickModes(std::uint8_t mode) {
    if (profile_.hasJoystick1) enqueueMode(protocol_.joySelAddress(0), mode, false);
    if (profile_.hasJoystick2) enqueueMode(protocol_.joySelAddress(1), mode, false);
}

void TouchModeTracker::setAllStripModes(std::uint8_t mode) {
    // Legacy order: controller number ascending, channel 3 and 4 interleaved
    // (both broadcast). Scoped to the strips that exist in the profile — the
    // legacy bridge always swept 48 slots per side and thus also wrote the
    // joystick SEL addresses (CC 88/90) and unknown addresses on the HI side.
    const std::uint8_t maskedMode = mode & 0x03u;
    const std::size_t loSlots = 2u * profile_.loStrips;
    const std::size_t hiSlots = 2u * profile_.hiStrips;
    const std::size_t sweep = loSlots > hiSlots ? loSlots : hiSlots;
    for (std::size_t within = 0; within < sweep; ++within) {
        if (within < loSlots) {
            MidiAddress address(0, profile_.loControlChannel,
                                static_cast<std::uint8_t>(64u + within));
            const std::uint16_t strip = static_cast<std::uint16_t>(within / 2);
            const StripPath path =
                (within % 2 == 0) ? StripPath::Chan : StripPath::Mix;
            modes_[slot(strip, path)] = maskedMode;
            enqueueMode(address, maskedMode, false);
        }
        if (within < hiSlots) {
            MidiAddress address(0, profile_.hiControlChannel,
                                static_cast<std::uint8_t>(64u + within));
            const std::uint16_t strip =
                static_cast<std::uint16_t>(profile_.loStrips + within / 2);
            const StripPath path =
                (within % 2 == 0) ? StripPath::Chan : StripPath::Mix;
            modes_[slot(strip, path)] = maskedMode;
            enqueueMode(address, maskedMode, false);
        }
    }
}

std::vector<std::uint8_t> TouchModeTracker::allStripModes() const {
    return modes_;
}

void TouchModeTracker::restoreAllStripModes(const std::vector<std::uint8_t>& modes) {
    // Used by Test Mode: put every strip back to its saved mode. The order
    // matches setAllStripModes so the console state is fully rewritten.
    const std::size_t count = modes_.size() < modes.size() ? modes_.size() : modes.size();
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint16_t strip = static_cast<std::uint16_t>(i / 2);
        const StripPath path = (i % 2 == 0) ? StripPath::Chan : StripPath::Mix;
        setStripMode(strip, path, modes[i]);
    }
}

std::uint8_t TouchModeTracker::stripMode(std::uint16_t strip, StripPath path) const {
    return modes_[slot(strip, path)];
}

} // namespace cinemix
