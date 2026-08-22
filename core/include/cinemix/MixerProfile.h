// MixerProfile — explicit description of a physical Cinemix configuration.
// Portable C++11. No Apple/Windows dependencies.
//
// The protocol layer and parameter map are built from this profile; console
// assumptions must live here, not scattered through the code.
//
// Defaults reproduce the legacy bridge's proven configuration:
//   LO=24 HI=12 MSTR=hi, 32 mono + 4 stereo inputs (strips 25..28 = S1..S4),
//   2 joysticks, 10 AUX mutes, master fader on the HI side.
#ifndef CINEMIX_MIXER_PROFILE_H
#define CINEMIX_MIXER_PROFILE_H

#include <cstdint>
#include <string>
#include <vector>

#include "cinemix/Types.h"

namespace cinemix {

// Interpretation of the two consecutive CC numbers each fader occupies.
// SevenBit  = legacy-proven: send the even CC only; either CC read as a 7-bit
//             position. FourteenBit = unverified: even=MSB, odd=LSB.
enum class FaderResolution : uint8_t { SevenBit = 0, FourteenBit = 1 };

struct MixerProfile {
    // ---- Geometry ---------------------------------------------------------
    uint16_t loStrips = 24; // first N strips on port 1 ("LO")
    uint16_t hiStrips = 12; // remaining strips + master section on port 2 ("HI")
    std::vector<uint16_t> stereoStrips; // 0-based strip indices of stereo pairs
                                        // (default {24,25,26,27} => strips 25..28)

    bool hasMasterFader = true;
    bool hasJoystick1 = true;
    bool hasJoystick2 = true;
    uint16_t auxMuteCount = 10; // 0..10

    // ---- Protocol scheme (hardware-fixed; documented, not assumed) --------
    uint8_t loFaderChannel = 1;   // LO fader positions
    uint8_t hiFaderChannel = 2;   // HI fader positions
    uint8_t loControlChannel = 3; // LO mutes / touch / SEL
    uint8_t hiControlChannel = 4; // HI mutes / touch / SEL
    uint8_t masterChannel = 5;    // master fader/SEL, AUX mutes, remote mode
    // Joystick axes ride the HI fader channel (2); mutes/SEL the HI control
    // channel (4) — same as the legacy mapping.

    FaderResolution faderResolution = FaderResolution::SevenBit;

    // Echo suppression: an untouched fader/axis report within this many 7-bit
    // steps of the last value we commanded is treated as a motor echo and is
    // not forwarded to the host (feedback-loop prevention). 0 disables.
    uint8_t echoHysteresisSteps = 2;

    // ---- Outbound pacing --------------------------------------------------
    uint32_t budgetMessagesPerSecond = 500; // scheduler cap (DIN 3-byte capacity ≈ 1040/s)
    uint32_t schedulerTickMs = 1;           // worker tick granularity

    // ---- Names ------------------------------------------------------------
    std::string name = "Default (LO24/HI12, S1-S4)";

    // ---- Derived ----------------------------------------------------------
    size_t stripCount() const { return size_t(loStrips) + size_t(hiStrips); }
    size_t faderCount() const { return stripCount() * 2; }
    size_t muteCount() const { return stripCount() * 2; }

    // Parameter count for the default scheme: faders + mutes + AUX + joystick
    // axes + joystick mutes + master fader.
    size_t paramCount() const {
        size_t joys = (hasJoystick1 ? 1 : 0) + (hasJoystick2 ? 1 : 0);
        return faderCount() + muteCount() + auxMuteCount + 2 * joys + joys +
               (hasMasterFader ? 1 : 0);
    }

    ConsoleSide sideOfStrip(size_t strip /*0-based*/) const {
        return strip < loStrips ? ConsoleSide::Lo : ConsoleSide::Hi;
    }
    // 0-based index of the strip within its side.
    size_t withinSide(size_t strip) const {
        return strip < loStrips ? strip : strip - loStrips;
    }

    bool isStereoStrip(size_t strip) const {
        for (size_t i = 0; i < stereoStrips.size(); ++i)
            if (stereoStrips[i] == strip) return true;
        return false;
    }

    // Human label for a strip: "M1".. or "S1".. — mono numbering skips the
    // stereo strips exactly like the legacy plugin did.
    std::string stripLabel(size_t strip) const;

    // The legacy author's console. Reproduces legacy parameter IDs 0..160.
    static MixerProfile legacyDefault();

    // A profile that describes nothing unusual but validates addressing for a
    // smaller console (used by tests): LO=24, HI=8, no stereo strips.
    static MixerProfile lo24hi8();
};

} // namespace cinemix

#endif // CINEMIX_MIXER_PROFILE_H
