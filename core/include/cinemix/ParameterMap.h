// ParameterMap — the AU parameter surface: ids, control references, names.
//
// Parameter IDs follow the legacy plugin's layout exactly for the default
// profile (faders, mutes, AUX, joysticks, master = 0..160). For other
// profiles the same derivation rule is applied, so strip-count changes shift
// the AUX/master ids the same way the legacy scheme would have.
//
// IDs are stable for a given profile and must remain stable across versions.
#ifndef CINEMIX_PARAMETER_MAP_H
#define CINEMIX_PARAMETER_MAP_H

#include <string>
#include <vector>

#include "cinemix/MixerProfile.h"
#include "cinemix/Types.h"

namespace cinemix {

struct ParameterInfo {
    ParamId id;
    ControlRef control;   // what the parameter is
    std::string name;     // long name: "M01 Chan Fader"
    std::string shortName; // short name for hosts (≤ 8 chars preferred)
    bool isMuteLike;      // switch parameters display On/Off
    float defaultValue;   // master fader = 1.0, everything else 0.0

    ParameterInfo() : id(0), isMuteLike(false), defaultValue(0.f) {}
};

class ParameterMap {
public:
    explicit ParameterMap(const MixerProfile& profile);

    size_t size() const { return params_.size(); }
    const ParameterInfo& info(ParamId id) const { return params_[id]; }
    const std::vector<ParameterInfo>& all() const { return params_; }

    // Reverse lookup: control → parameter id.
    bool find(const ControlRef& control, ParamId& out) const;

    // Param id of a strip fader / mute (convenience).
    ParamId stripFaderId(uint16_t strip, StripPath path) const;
    ParamId stripMuteId(uint16_t strip, StripPath path) const;

private:
    std::vector<ParameterInfo> params_;
};

} // namespace cinemix

#endif // CINEMIX_PARAMETER_MAP_H
