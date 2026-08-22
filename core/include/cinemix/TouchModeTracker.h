// TouchModeTracker — the SEL/touch state machine recovered from the legacy
// bridge (docs/PROTOCOL.md §5):
//
//   * SEL press on a strip rotates that strip's mode 0→1→2→3→0 and replies
//     the new mode (console asks with value 1; plugin answers).
//   * Touch (6) while the strip is in AUTO (3) → answer WRITE (2).
//   * Release (5) while in AUTO → answer AUTO (3)  [R+W LEDs].
//   * Master SEL press rotates the master mode and applies it to ALL strips
//     (the master fader has no touch sensor). The master's own SEL address is
//     not rewritten — legacy behavior, preserved.
//
// Single-threaded: bridge worker thread only.
#ifndef CINEMIX_TOUCH_MODE_TRACKER_H
#define CINEMIX_TOUCH_MODE_TRACKER_H

#include <cstdint>
#include <vector>

#include "cinemix/CinemixProtocol.h"
#include "cinemix/MixerProfile.h"
#include "cinemix/TransmissionScheduler.h"
#include "cinemix/Types.h"

namespace cinemix {

class TouchModeTracker {
public:
    TouchModeTracker(const MixerProfile& profile, const CinemixProtocol& protocol,
                     TransmissionScheduler& scheduler);

    // Inbound console events:
    void onSelPressed(uint16_t strip, StripPath path);
    void onMasterSelPressed();
    void onTouchChanged(uint16_t strip, StripPath path, bool touching);

    // Programmatic mode changes (activation/reset/test mode):
    void setStripMode(uint16_t strip, StripPath path, uint8_t mode);
    void setMasterMode(uint8_t mode);
    void setJoystickModes(uint8_t mode);
    // Sweep every strip on both sides, in the legacy byte order (CC number
    // ascending, ch3/ch4 interleaved), scoped to the strips in the profile.
    void setAllStripModes(uint8_t mode);

    uint8_t stripMode(uint16_t strip, StripPath path) const;
    uint8_t masterMode() const { return masterMode_; }

private:
    size_t slot(uint16_t strip, StripPath path) const {
        return size_t(strip) * 2u + (path == StripPath::Chan ? 0u : 1u);
    }
    void enqueueMode(const MidiAddress& addr, uint8_t mode, bool highPriority);

    const MixerProfile& profile_;
    const CinemixProtocol& protocol_;
    TransmissionScheduler& scheduler_;
    std::vector<uint8_t> modes_; // [strip*2 + path]
    uint8_t masterMode_;
};

} // namespace cinemix

#endif // CINEMIX_TOUCH_MODE_TRACKER_H
