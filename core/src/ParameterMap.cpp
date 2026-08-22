#include "cinemix/ParameterMap.h"

#include <cstdio>

namespace cinemix {

namespace {

// Strip prefix used in parameter names: "M01", "S1" — same labeling rule as
// the legacy plugin (mono numbering skips the stereo strips).
std::string stripPrefix(const MixerProfile& profile, std::uint16_t strip) {
    return profile.stripLabel(strip);
}

} // namespace

ParameterMap::ParameterMap(const MixerProfile& profile) {
    const std::size_t strips = profile.stripCount();

    // --- Faders: 0 .. 2*strips-1  (legacy order: per strip Chan then Mix)
    for (std::uint16_t strip = 0; strip < strips; ++strip) {
        const std::string prefix = stripPrefix(profile, strip);
        for (int pathIndex = 0; pathIndex < 2; ++pathIndex) {
            const StripPath path =
                (pathIndex == 0) ? StripPath::Chan : StripPath::Mix;
            ParameterInfo info;
            info.id = static_cast<ParamId>(params_.size());
            info.control = ControlRef(ControlClass::Fader, static_cast<std::uint8_t>(strip),
                                      path, 0);
            const std::string pathName = (path == StripPath::Chan) ? "Chan" : "Mix";
            info.name = prefix + " " + pathName + " Fader";
            info.shortName = prefix + pathName;
            if (info.shortName.size() > 8) info.shortName = info.shortName.substr(0, 8);
            info.isMuteLike = false;
            info.defaultValue = 0.0f;
            params_.push_back(info);
        }
    }

    // --- Mutes: 2*strips .. 4*strips-1
    for (std::uint16_t strip = 0; strip < strips; ++strip) {
        const std::string prefix = stripPrefix(profile, strip);
        for (int pathIndex = 0; pathIndex < 2; ++pathIndex) {
            const StripPath path =
                (pathIndex == 0) ? StripPath::Chan : StripPath::Mix;
            ParameterInfo info;
            info.id = static_cast<ParamId>(params_.size());
            info.control = ControlRef(ControlClass::Mute, static_cast<std::uint8_t>(strip),
                                      path, 0);
            const std::string pathName = (path == StripPath::Chan) ? "Chan" : "Mix";
            info.name = prefix + " " + pathName + " Mute";
            info.shortName = prefix + pathName;
            if (info.shortName.size() > 8) info.shortName = info.shortName.substr(0, 8);
            info.isMuteLike = true;
            info.defaultValue = 0.0f;
            params_.push_back(info);
        }
    }

    // --- AUX mutes
    for (std::uint16_t aux = 0; aux < profile.auxMuteCount; ++aux) {
        ParameterInfo info;
        info.id = static_cast<ParamId>(params_.size());
        info.control = ControlRef(ControlClass::AuxMute, 0, StripPath::Chan,
                                  static_cast<std::uint8_t>(aux));
        char buf[16];
        snprintf(buf, sizeof(buf), "AUX %u Mute", static_cast<unsigned>(aux + 1));
        info.name = buf;
        snprintf(buf, sizeof(buf), "Aux%uMut", static_cast<unsigned>(aux + 1));
        info.shortName = buf;
        info.isMuteLike = true;
        info.defaultValue = 0.0f;
        params_.push_back(info);
    }

    // --- Joysticks: legacy order is X, Y, Mute per joystick
    for (std::uint8_t joy = 0; joy < 2; ++joy) {
        const bool present = (joy == 0) ? profile.hasJoystick1 : profile.hasJoystick2;
        if (!present) continue;
        const char* joyName = (joy == 0) ? "Joy 1" : "Joy 2";
        for (std::uint8_t axis = 0; axis < 2; ++axis) {
            ParameterInfo info;
            info.id = static_cast<ParamId>(params_.size());
            info.control = ControlRef(ControlClass::JoyAxis, 0,
                                      (axis == 0) ? StripPath::Chan : StripPath::Mix, joy);
            info.name = std::string(joyName) + (axis == 0 ? " X" : " Y");
            info.shortName = info.name;
            info.isMuteLike = false;
            info.defaultValue = 0.0f;
            params_.push_back(info);
        }
        ParameterInfo info;
        info.id = static_cast<ParamId>(params_.size());
        info.control = ControlRef(ControlClass::JoyMute, 0, StripPath::Chan, joy);
        info.name = std::string(joyName) + " Mute";
        info.shortName = info.name;
        info.isMuteLike = true;
        info.defaultValue = 0.0f;
        params_.push_back(info);
    }

    // --- Master fader
    if (profile.hasMasterFader) {
        ParameterInfo info;
        info.id = static_cast<ParamId>(params_.size());
        info.control = ControlRef(ControlClass::MasterFader, 0, StripPath::Chan, 0);
        info.name = "Master Fader";
        info.shortName = "Master";
        info.isMuteLike = false;
        info.defaultValue = 1.0f; // legacy default: master at maximum
        params_.push_back(info);
    }
}

bool ParameterMap::find(const ControlRef& control, ParamId& out) const {
    for (std::size_t i = 0; i < params_.size(); ++i) {
        if (params_[i].control == control) {
            out = static_cast<ParamId>(i);
            return true;
        }
    }
    return false;
}

ParamId ParameterMap::stripFaderId(std::uint16_t strip, StripPath path) const {
    ParamId id = kNoParam;
    find(ControlRef(ControlClass::Fader, static_cast<std::uint8_t>(strip), path, 0), id);
    return id;
}

ParamId ParameterMap::stripMuteId(std::uint16_t strip, StripPath path) const {
    ParamId id = kNoParam;
    find(ControlRef(ControlClass::Mute, static_cast<std::uint8_t>(strip), path, 0), id);
    return id;
}

} // namespace cinemix
