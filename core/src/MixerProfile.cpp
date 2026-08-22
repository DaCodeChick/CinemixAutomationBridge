#include "cinemix/MixerProfile.h"

namespace cinemix {

std::string MixerProfile::stripLabel(std::size_t strip) const {
    // Legacy: strips 25..28 (0-based 24..27) are the stereo pairs S1..S4;
    // mono numbering skips them. Mono numbers are zero-padded ("M01") so
    // host parameter lists sort naturally.
    if (isStereoStrip(strip)) {
        std::size_t s = 0;
        for (std::size_t i = 0; i < stereoStrips.size(); ++i)
            if (stereoStrips[i] < strip) ++s;
        char buf[16];
        snprintf(buf, sizeof(buf), "S%zu", s + 1);
        return buf;
    }
    std::size_t mono = 1;
    for (std::size_t i = 0; i < strip; ++i)
        if (!isStereoStrip(i)) ++mono;
    char buf[16];
    snprintf(buf, sizeof(buf), "M%02zu", mono);
    return buf;
}

MixerProfile MixerProfile::legacyDefault() {
    MixerProfile profile;
    profile.loStrips = 24;
    profile.hiStrips = 12;
    profile.stereoStrips.push_back(24);
    profile.stereoStrips.push_back(25);
    profile.stereoStrips.push_back(26);
    profile.stereoStrips.push_back(27);
    profile.name = "Default (LO24/HI12, S1-S4)";
    return profile;
}

MixerProfile MixerProfile::lo24hi8() {
    MixerProfile profile;
    profile.loStrips = 24;
    profile.hiStrips = 8;
    profile.name = "LO24/HI8";
    return profile;
}

} // namespace cinemix
