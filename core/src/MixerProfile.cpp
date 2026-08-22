#include "cinemix/MixerProfile.h"

namespace cinemix {

std::string MixerProfile::stripLabel(size_t strip) const {
    // Legacy: strips 25..28 (0-based 24..27) are the stereo pairs S1..S4;
    // mono numbering skips them. Mono numbers are zero-padded ("M01") so
    // host parameter lists sort naturally.
    if (isStereoStrip(strip)) {
        size_t s = 0;
        for (size_t i = 0; i < stereoStrips.size(); ++i)
            if (stereoStrips[i] < strip) ++s;
        char buf[16];
        snprintf(buf, sizeof(buf), "S%zu", s + 1);
        return buf;
    }
    size_t mono = 1;
    for (size_t i = 0; i < strip; ++i)
        if (!isStereoStrip(i)) ++mono;
    char buf[16];
    snprintf(buf, sizeof(buf), "M%02zu", mono);
    return buf;
}

MixerProfile MixerProfile::legacyDefault() {
    MixerProfile p;
    p.loStrips = 24;
    p.hiStrips = 12;
    p.stereoStrips.push_back(24);
    p.stereoStrips.push_back(25);
    p.stereoStrips.push_back(26);
    p.stereoStrips.push_back(27);
    p.name = "Default (LO24/HI12, S1-S4)";
    return p;
}

MixerProfile MixerProfile::lo24hi8() {
    MixerProfile p;
    p.loStrips = 24;
    p.hiStrips = 8;
    p.name = "LO24/HI8";
    return p;
}

} // namespace cinemix
