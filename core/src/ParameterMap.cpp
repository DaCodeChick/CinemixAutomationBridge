#include "cinemix/ParameterMap.h"

#include <cstdio>

namespace cinemix {

namespace {

// Strip prefix used in parameter names: "M01", "S1" — same labeling rule as
// the legacy plugin (mono numbering skips the stereo strips).
std::string stripPrefix(const MixerProfile& p, uint16_t strip) {
    return p.stripLabel(strip);
}

} // namespace

ParameterMap::ParameterMap(const MixerProfile& profile) {
    const size_t strips = profile.stripCount();

    // --- Faders: 0 .. 2*strips-1  (legacy order: per strip Chan then Mix)
    for (uint16_t s = 0; s < strips; ++s) {
        const std::string prefix = stripPrefix(profile, s);
        for (int p = 0; p < 2; ++p) {
            const StripPath path = (p == 0) ? StripPath::Chan : StripPath::Mix;
            ParameterInfo info;
            info.id = ParamId(params_.size());
            info.control = ControlRef(ControlClass::Fader, s, path, 0);
            const std::string pathName = (path == StripPath::Chan) ? "Chan" : "Mix";
            info.name = prefix + " " + pathName + " Fader";
            info.shortName = prefix + pathName;
            if (info.shortName.size() > 8) info.shortName = info.shortName.substr(0, 8);
            info.isMuteLike = false;
            info.defaultValue = 0.f;
            params_.push_back(info);
        }
    }

    // --- Mutes: 2*strips .. 4*strips-1
    for (uint16_t s = 0; s < strips; ++s) {
        const std::string prefix = stripPrefix(profile, s);
        for (int p = 0; p < 2; ++p) {
            const StripPath path = (p == 0) ? StripPath::Chan : StripPath::Mix;
            ParameterInfo info;
            info.id = ParamId(params_.size());
            info.control = ControlRef(ControlClass::Mute, s, path, 0);
            const std::string pathName = (path == StripPath::Chan) ? "Chan" : "Mix";
            info.name = prefix + " " + pathName + " Mute";
            info.shortName = prefix + pathName;
            if (info.shortName.size() > 8) info.shortName = info.shortName.substr(0, 8);
            info.isMuteLike = true;
            info.defaultValue = 0.f;
            params_.push_back(info);
        }
    }

    // --- AUX mutes
    for (uint16_t a = 0; a < profile.auxMuteCount; ++a) {
        ParameterInfo info;
        info.id = ParamId(params_.size());
        info.control = ControlRef(ControlClass::AuxMute, 0, StripPath::Chan, uint8_t(a));
        char buf[16];
        snprintf(buf, sizeof(buf), "AUX %u Mute", unsigned(a + 1));
        info.name = buf;
        snprintf(buf, sizeof(buf), "Aux%uMut", unsigned(a + 1));
        info.shortName = buf;
        info.isMuteLike = true;
        info.defaultValue = 0.f;
        params_.push_back(info);
    }

    // --- Joysticks: legacy order is X, Y, Mute per joystick
    for (uint8_t j = 0; j < 2; ++j) {
        const bool present = (j == 0) ? profile.hasJoystick1 : profile.hasJoystick2;
        if (!present) continue;
        const char* jn = (j == 0) ? "Joy 1" : "Joy 2";
        for (uint8_t axis = 0; axis < 2; ++axis) {
            ParameterInfo info;
            info.id = ParamId(params_.size());
            info.control = ControlRef(ControlClass::JoyAxis, 0,
                                      (axis == 0) ? StripPath::Chan : StripPath::Mix, j);
            info.name = std::string(jn) + (axis == 0 ? " X" : " Y");
            info.shortName = info.name;
            info.isMuteLike = false;
            info.defaultValue = 0.f;
            params_.push_back(info);
        }
        ParameterInfo info;
        info.id = ParamId(params_.size());
        info.control = ControlRef(ControlClass::JoyMute, 0, StripPath::Chan, j);
        info.name = std::string(jn) + " Mute";
        info.shortName = info.name;
        info.isMuteLike = true;
        info.defaultValue = 0.f;
        params_.push_back(info);
    }

    // --- Master fader
    if (profile.hasMasterFader) {
        ParameterInfo info;
        info.id = ParamId(params_.size());
        info.control = ControlRef(ControlClass::MasterFader, 0, StripPath::Chan, 0);
        info.name = "Master Fader";
        info.shortName = "Master";
        info.isMuteLike = false;
        info.defaultValue = 1.f; // legacy default: master at maximum
        params_.push_back(info);
    }
}

bool ParameterMap::find(const ControlRef& control, ParamId& out) const {
    for (size_t i = 0; i < params_.size(); ++i) {
        if (params_[i].control == control) { out = ParamId(i); return true; }
    }
    return false;
}

ParamId ParameterMap::stripFaderId(uint16_t strip, StripPath path) const {
    ParamId id = kNoParam;
    find(ControlRef(ControlClass::Fader, uint8_t(strip), path, 0), id);
    return id;
}

ParamId ParameterMap::stripMuteId(uint16_t strip, StripPath path) const {
    ParamId id = kNoParam;
    find(ControlRef(ControlClass::Mute, uint8_t(strip), path, 0), id);
    return id;
}

} // namespace cinemix
