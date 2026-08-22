// Parameter map tests — legacy parameter ID layout (0..160) and naming.
#include "TestFramework.h"
#include "cinemix/MixerProfile.h"
#include "cinemix/ParameterMap.h"

using namespace cinemix;

namespace {

TEST_CASE("param map: default profile reproduces legacy 0..160 layout") {
    MixerProfile prof = MixerProfile::legacyDefault();
    ParameterMap map(prof);

    CHECK_EQ(map.size(), static_cast<size_t>(161));

    // Faders 0..71.
    CHECK_EQ(static_cast<int>(map.stripFaderId(0, StripPath::Chan)), 0);
    CHECK_EQ(static_cast<int>(map.stripFaderId(0, StripPath::Mix)), 1);
    CHECK_EQ(static_cast<int>(map.stripFaderId(23, StripPath::Mix)), 47);
    CHECK_EQ(static_cast<int>(map.stripFaderId(24, StripPath::Chan)), 48);
    CHECK_EQ(static_cast<int>(map.stripFaderId(35, StripPath::Mix)), 71);

    // Mutes 72..143.
    CHECK_EQ(static_cast<int>(map.stripMuteId(0, StripPath::Chan)), 72);
    CHECK_EQ(static_cast<int>(map.stripMuteId(0, StripPath::Mix)), 73);
    CHECK_EQ(static_cast<int>(map.stripMuteId(23, StripPath::Mix)), 119);
    CHECK_EQ(static_cast<int>(map.stripMuteId(24, StripPath::Chan)), 120);
    CHECK_EQ(static_cast<int>(map.stripMuteId(35, StripPath::Mix)), 143);

    // AUX 144..153.
    CHECK(map.info(144).control.cls == ControlClass::AuxMute);
    CHECK_EQ(static_cast<int>(map.info(144).control.index), 0);
    CHECK_EQ(static_cast<int>(map.info(153).control.index), 9);

    // Joysticks 154..159 (legacy order: X, Y, Mute per joystick).
    CHECK(map.info(154).control.cls == ControlClass::JoyAxis);
    CHECK_EQ(static_cast<int>(map.info(154).control.index), 0);
    CHECK(map.info(154).control.path == StripPath::Chan); // X
    CHECK(map.info(155).control.path == StripPath::Mix);  // Y
    CHECK(map.info(156).control.cls == ControlClass::JoyMute);
    CHECK_EQ(static_cast<int>(map.info(156).control.index), 0);
    CHECK(map.info(157).control.cls == ControlClass::JoyAxis);
    CHECK_EQ(static_cast<int>(map.info(157).control.index), 1);
    CHECK(map.info(158).control.path == StripPath::Mix);
    CHECK(map.info(159).control.cls == ControlClass::JoyMute);
    CHECK_EQ(static_cast<int>(map.info(159).control.index), 1);

    // Master fader 160, default 1.0 (legacy default: master at maximum).
    CHECK(map.info(160).control.cls == ControlClass::MasterFader);
    CHECK_NEAR(map.info(160).defaultValue, 1.f, 1e-6);
    CHECK_NEAR(map.info(0).defaultValue, 0.f, 1e-6);
}

TEST_CASE("param map: strip labels — mono numbering skips stereo strips") {
    MixerProfile prof = MixerProfile::legacyDefault();
    ParameterMap map(prof);

    // Strips 1..24 mono → M01..M24; strips 25..28 → S1..S4; 29..36 → M25..M32.
    CHECK_EQ(map.info(0).name, std::string("M01 Chan Fader"));
    CHECK_EQ(map.info(1).name, std::string("M01 Mix Fader"));
    CHECK_EQ(map.info(72).name, std::string("M01 Chan Mute"));
    CHECK_EQ(map.info(48).name, std::string("S1 Chan Fader"));  // strip 25
    CHECK_EQ(map.info(56).name, std::string("M25 Chan Fader")); // strip 29
    CHECK_EQ(map.info(70).name, std::string("M32 Chan Fader")); // strip 36

    // Mute-like flags.
    CHECK(!map.info(0).isMuteLike);
    CHECK(map.info(72).isMuteLike);
    CHECK(map.info(144).isMuteLike);
    CHECK(map.info(156).isMuteLike);
    CHECK(!map.info(160).isMuteLike);

    // Reverse lookup.
    ParamId id = kNoParam;
    CHECK(map.find(ControlRef(ControlClass::MasterFader, 0, StripPath::Chan, 0), id));
    CHECK_EQ(static_cast<int>(id), 160);
    CHECK(!map.find(ControlRef(ControlClass::Touch, 0, StripPath::Chan, 0), id));
}

TEST_CASE("param map: alternate profile (LO24/HI8) shifts master section") {
    MixerProfile prof = MixerProfile::lo24hi8();
    ParameterMap map(prof);

    // 32 strips: 64 faders + 64 mutes + 10 aux + 6 joystick + 1 master = 145.
    CHECK_EQ(map.size(), static_cast<size_t>(145));
    CHECK_EQ(static_cast<int>(map.info(144).id), 144);
    CHECK_EQ(static_cast<int>(map.stripMuteId(31, StripPath::Mix)), 127);
    CHECK(map.info(128).control.cls == ControlClass::AuxMute);
    CHECK(map.info(138).control.cls == ControlClass::JoyAxis); // Joy 1 X
    CHECK(map.info(144).control.cls == ControlClass::MasterFader);
}

TEST_CASE("param map: profile without joysticks/aux/master") {
    MixerProfile prof = MixerProfile::lo24hi8();
    prof.hasJoystick1 = prof.hasJoystick2 = false;
    prof.auxMuteCount = 0;
    prof.hasMasterFader = false;
    ParameterMap map(prof);
    CHECK_EQ(map.size(), static_cast<size_t>(64 + 64));
    CHECK_EQ(static_cast<int>(map.stripMuteId(31, StripPath::Mix)), 127);
}

} // namespace

int main(int argc, char** argv) {
    return testfw::Registry::instance().runAll(argc > 1 ? argv[1] : nullptr);
}
