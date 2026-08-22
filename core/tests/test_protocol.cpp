// Protocol encode/decode tests — gold data from the legacy bridge
// (docs/PROTOCOL.md). These are the regression tests that pin the recovered
// hardware behavior.
#include <cmath>
#include <limits>

#include "TestFramework.h"
#include "cinemix/CinemixProtocol.h"
#include "cinemix/MixerProfile.h"

using namespace cinemix;

namespace {

MixerProfile defaultProfile() { return MixerProfile::legacyDefault(); }

// ---------------------------------------------------------------------------

TEST_CASE("protocol: strip fader addressing (legacy table)") {
    CinemixProtocol p(defaultProfile());

    MidiAddress a = p.stripFaderAddress(0, StripPath::Chan);
    CHECK_EQ(static_cast<int>(a.port), 1); CHECK_EQ(static_cast<int>(a.channel), 1); CHECK_EQ(static_cast<int>(a.cc), 0);
    a = p.stripFaderAddress(0, StripPath::Mix);
    CHECK_EQ(static_cast<int>(a.port), 1); CHECK_EQ(static_cast<int>(a.channel), 1); CHECK_EQ(static_cast<int>(a.cc), 2);
    a = p.stripFaderAddress(23, StripPath::Mix);
    CHECK_EQ(static_cast<int>(a.channel), 1); CHECK_EQ(static_cast<int>(a.cc), 94);
    // First HI strip (25): numbering restarts at CC 0 on channel 2, port 2.
    a = p.stripFaderAddress(24, StripPath::Chan);
    CHECK_EQ(static_cast<int>(a.port), 2); CHECK_EQ(static_cast<int>(a.channel), 2); CHECK_EQ(static_cast<int>(a.cc), 0);
    a = p.stripFaderAddress(35, StripPath::Mix);
    CHECK_EQ(static_cast<int>(a.port), 2); CHECK_EQ(static_cast<int>(a.channel), 2); CHECK_EQ(static_cast<int>(a.cc), 46);
    a = p.stripFaderFineAddress(35, StripPath::Mix);
    CHECK_EQ(static_cast<int>(a.cc), 47);
}

TEST_CASE("protocol: strip mute/touch addressing (legacy table)") {
    CinemixProtocol p(defaultProfile());

    MidiAddress a = p.stripMuteAddress(0, StripPath::Chan);
    CHECK_EQ(static_cast<int>(a.port), 1); CHECK_EQ(static_cast<int>(a.channel), 3); CHECK_EQ(static_cast<int>(a.cc), 0);
    a = p.stripMuteAddress(23, StripPath::Mix);
    CHECK_EQ(static_cast<int>(a.channel), 3); CHECK_EQ(static_cast<int>(a.cc), 47);
    a = p.stripMuteAddress(24, StripPath::Chan);
    CHECK_EQ(static_cast<int>(a.port), 2); CHECK_EQ(static_cast<int>(a.channel), 4); CHECK_EQ(static_cast<int>(a.cc), 0);
    a = p.stripMuteAddress(35, StripPath::Mix);
    CHECK_EQ(static_cast<int>(a.channel), 4); CHECK_EQ(static_cast<int>(a.cc), 23);

    a = p.stripTouchAddress(0, StripPath::Chan);
    CHECK_EQ(static_cast<int>(a.port), 0); CHECK_EQ(static_cast<int>(a.channel), 3); CHECK_EQ(static_cast<int>(a.cc), 64);
    a = p.stripTouchAddress(23, StripPath::Mix);
    CHECK_EQ(static_cast<int>(a.channel), 3); CHECK_EQ(static_cast<int>(a.cc), 111);
    a = p.stripTouchAddress(24, StripPath::Chan);
    CHECK_EQ(static_cast<int>(a.channel), 4); CHECK_EQ(static_cast<int>(a.cc), 64);
    a = p.stripTouchAddress(35, StripPath::Mix);
    CHECK_EQ(static_cast<int>(a.channel), 4); CHECK_EQ(static_cast<int>(a.cc), 87);
}

TEST_CASE("protocol: master section addressing") {
    CinemixProtocol p(defaultProfile());

    MidiAddress a = p.masterFaderAddress();
    CHECK_EQ(static_cast<int>(a.port), 2); CHECK_EQ(static_cast<int>(a.channel), 5); CHECK_EQ(static_cast<int>(a.cc), 0);
    a = p.masterSelAddress();
    CHECK_EQ(static_cast<int>(a.port), 0); CHECK_EQ(static_cast<int>(a.channel), 5); CHECK_EQ(static_cast<int>(a.cc), 64);
    a = p.joyAxisAddress(0, 0);
    CHECK_EQ(static_cast<int>(a.port), 2); CHECK_EQ(static_cast<int>(a.channel), 2); CHECK_EQ(static_cast<int>(a.cc), 48);
    a = p.joyAxisAddress(0, 1);
    CHECK_EQ(static_cast<int>(a.cc), 50);
    a = p.joyAxisAddress(1, 0);
    CHECK_EQ(static_cast<int>(a.cc), 52);
    a = p.joyAxisAddress(1, 1);
    CHECK_EQ(static_cast<int>(a.cc), 54);
    a = p.joyMuteAddress(0);
    CHECK_EQ(static_cast<int>(a.channel), 4); CHECK_EQ(static_cast<int>(a.cc), 24);
    a = p.joyMuteAddress(1);
    CHECK_EQ(static_cast<int>(a.cc), 26);
    a = p.joySelAddress(0);
    CHECK_EQ(static_cast<int>(a.port), 0); CHECK_EQ(static_cast<int>(a.channel), 4); CHECK_EQ(static_cast<int>(a.cc), 88);
    a = p.joySelAddress(1);
    CHECK_EQ(static_cast<int>(a.cc), 90);
    a = p.auxMuteAddress();
    CHECK_EQ(static_cast<int>(a.port), 2); CHECK_EQ(static_cast<int>(a.channel), 5); CHECK_EQ(static_cast<int>(a.cc), 96);
}

TEST_CASE("protocol: remote control and value encoding") {
    CinemixProtocol p(defaultProfile());

    MidiMessage m = p.remoteControl(true);
    CHECK_EQ(static_cast<int>(m.length), 3);
    CHECK_EQ(static_cast<int>(m.data[0] & 0x0F), 4); // channel 5
    CHECK_EQ(static_cast<int>(m.data[1]), 127);
    CHECK_EQ(static_cast<int>(m.data[2]), 127);
    CHECK_EQ(static_cast<int>(m.port), 0);
    m = p.remoteControl(false);
    CHECK_EQ(static_cast<int>(m.data[2]), 0);
    m = p.systemReset();
    CHECK(m.isSystemReset());
    CHECK_EQ(static_cast<int>(m.port), 0);

    CHECK_EQ(static_cast<int>(CinemixProtocol::muteByte(false)), 2);
    CHECK_EQ(static_cast<int>(CinemixProtocol::muteByte(true)), 3);
    for (uint8_t n = 0; n < 10; ++n) {
        CHECK_EQ(static_cast<int>(CinemixProtocol::auxMuteByte(n, false)), 2 * (n + 1));
        CHECK_EQ(static_cast<int>(CinemixProtocol::auxMuteByte(n, true)), 2 * (n + 1) + 1);
    }
}

TEST_CASE("protocol: fader quantization matches legacy truncation") {
    CinemixProtocol p(defaultProfile());
    std::vector<MidiMessage> msgs;

    msgs = p.encodeStripFader(0, StripPath::Chan, 1.0f);
    CHECK_EQ(msgs.size(), static_cast<size_t>(1));
    CHECK_EQ(static_cast<int>(msgs[0].data[2]), 127);

    msgs = p.encodeStripFader(0, StripPath::Chan, 0.0f);
    CHECK_EQ(static_cast<int>(msgs[0].data[2]), 0);

    // Legacy: static_cast<int>(value * 127) — truncation, so 0.5 → 63 (not 64).
    msgs = p.encodeStripFader(0, StripPath::Chan, 0.5f);
    CHECK_EQ(static_cast<int>(msgs[0].data[2]), 63);

    CHECK_EQ(static_cast<int>(quantize7(1.5f)), 127); // clamped
    CHECK_EQ(static_cast<int>(quantize7(-1.f)), 0);

    // Mute encoding: strip 5 Mix → ch3 CC 11, ON=3.
    MidiMessage m = p.encodeStripMute(5, StripPath::Mix, true);
    CHECK_EQ(static_cast<int>(m.data[0] & 0x0F), 2); // channel 3
    CHECK_EQ(static_cast<int>(m.data[1]), 11);
    CHECK_EQ(static_cast<int>(m.data[2]), 3);
    CHECK_EQ(static_cast<int>(m.port), 1);

    // Master fader → ch5 CC0 port2.
    m = p.encodeMasterFader(0.25f);
    CHECK_EQ(static_cast<int>(m.data[0] & 0x0F), 4);
    CHECK_EQ(static_cast<int>(m.data[1]), 0);
    CHECK_EQ(static_cast<int>(m.data[2]), 31); // 0.25*127 = 31.75 → 31
    CHECK_EQ(static_cast<int>(m.port), 2);
}

TEST_CASE("protocol: decode — faders (either CC of pair, legacy 7-bit)") {
    CinemixProtocol p(defaultProfile());
    const uint8_t statusCh1 = 0xB0;
    const uint8_t statusCh2 = 0xB1;

    ConsoleEvent ev = p.decode(statusCh1, 0, 100);
    CHECK(ev.kind == EventKind::FaderPosition);
    CHECK_EQ(static_cast<int>(ev.control.strip), 0);
    CHECK(ev.control.path == StripPath::Chan);
    CHECK_NEAR(ev.normalized, 100.f / 127.f, 1e-6);

    // Odd CC of the pair → same control (legacy behavior).
    ev = p.decode(statusCh1, 1, 100);
    CHECK(ev.kind == EventKind::FaderPosition);
    CHECK(ev.control.path == StripPath::Chan);

    ev = p.decode(statusCh1, 3, 42); // strip 0 Mix path (CC 2+3 pair)
    CHECK(ev.kind == EventKind::FaderPosition);
    CHECK_EQ(static_cast<int>(ev.control.strip), 0);
    CHECK(ev.control.path == StripPath::Mix);

    ev = p.decode(statusCh1, 5, 42); // strip 1 Chan path (CC 4+5 pair)
    CHECK(ev.kind == EventKind::FaderPosition);
    CHECK_EQ(static_cast<int>(ev.control.strip), 1);
    CHECK(ev.control.path == StripPath::Chan);

    // HI side: strip 35 Mix = CC46/47 ch2.
    ev = p.decode(statusCh2, 46, 77);
    CHECK(ev.kind == EventKind::FaderPosition);
    CHECK_EQ(static_cast<int>(ev.control.strip), 35);
    CHECK(ev.control.path == StripPath::Mix);

    // Master fader CC0 and fine CC1 on ch5.
    ev = p.decode(0xB4, 0, 90);
    CHECK(ev.kind == EventKind::FaderPosition);
    CHECK(ev.control.cls == ControlClass::MasterFader);
    ev = p.decode(0xB4, 1, 90);
    CHECK(ev.kind == EventKind::FaderPosition);
    CHECK(ev.control.cls == ControlClass::MasterFader);
}

TEST_CASE("protocol: decode — mutes, touch, SEL, joysticks, AUX") {
    CinemixProtocol p(defaultProfile());

    ConsoleEvent ev = p.decode(0xB2, 0, 3); // ch3 CC0 = strip1 Chan mute ON
    CHECK(ev.kind == EventKind::MuteChanged);
    CHECK(ev.on);
    ev = p.decode(0xB2, 0, 2);
    CHECK(!ev.on);
    ev = p.decode(0xB2, 1, 7); // any non-3 → OFF (legacy)
    CHECK(!ev.on);

    ev = p.decode(0xB2, 64, 6); // touch
    CHECK(ev.kind == EventKind::TouchBegin);
    ev = p.decode(0xB2, 64, 5);
    CHECK(ev.kind == EventKind::TouchEnd);
    ev = p.decode(0xB2, 64, 1);
    CHECK(ev.kind == EventKind::SelPressed);
    ev = p.decode(0xB2, 64, 0);
    CHECK(ev.kind == EventKind::Ignored);

    ev = p.decode(0xB4, 64, 1); // master SEL
    CHECK(ev.kind == EventKind::MasterSelPressed);
    ev = p.decode(0xB4, 64, 3);
    CHECK(ev.kind == EventKind::Ignored);

    ev = p.decode(0xB1, 48, 55); // joy 1 X
    CHECK(ev.kind == EventKind::FaderPosition);
    CHECK(ev.control.cls == ControlClass::JoyAxis);
    CHECK_EQ(static_cast<int>(ev.control.index), 0);
    CHECK(ev.control.path == StripPath::Chan);
    ev = p.decode(0xB1, 54, 55); // joy 2 Y
    CHECK(ev.control.cls == ControlClass::JoyAxis);
    CHECK_EQ(static_cast<int>(ev.control.index), 1);
    CHECK(ev.control.path == StripPath::Mix);

    ev = p.decode(0xB3, 24, 3); // joy 1 mute on
    CHECK(ev.kind == EventKind::MuteChanged);
    CHECK(ev.control.cls == ControlClass::JoyMute);
    CHECK(ev.on);

    // AUX mutes on CC96 ch5: value 2n = off, 2n+1 = on.
    ev = p.decode(0xB4, 96, 7);
    CHECK(ev.kind == EventKind::AuxMuteChanged);
    CHECK_EQ(static_cast<int>(ev.control.index), 2);
    CHECK(ev.on);
    ev = p.decode(0xB4, 96, 12);
    CHECK(!ev.on);
    CHECK_EQ(static_cast<int>(ev.control.index), 5);
    ev = p.decode(0xB4, 96, 22); // out of range
    CHECK(ev.kind == EventKind::Ignored);

    // Unknowns.
    ev = p.decode(0xB4, 127, 127); // remote-mode CC — inbound: unknown
    CHECK(ev.kind == EventKind::Unknown);
    CHECK_EQ(static_cast<int>(ev.channel), 5);
    CHECK_EQ(static_cast<int>(ev.cc), 127);
    ev = p.decode(0xB8, 1, 0); // ch9
    CHECK(ev.kind == EventKind::Unknown);
    ev = p.decode(0xB2, 48, 0); // ch3 CC48: gap between mutes and touch
    CHECK(ev.kind == EventKind::Unknown);
}

TEST_CASE("protocol: fourteenBit profile encodes pair and combines on decode") {
    MixerProfile prof = defaultProfile();
    prof.faderResolution = FaderResolution::FourteenBit;
    CinemixProtocol p(prof);

    // value 0.5 → 8191 of 16383 → msb 63, lsb 127.
    std::vector<MidiMessage> msgs = p.encodeStripFader(0, StripPath::Chan, 0.5f);
    CHECK_EQ(msgs.size(), static_cast<size_t>(2));
    CHECK_EQ(static_cast<int>(msgs[0].data[1]), 0);
    CHECK_EQ(static_cast<int>(msgs[0].data[2]), 63);
    CHECK_EQ(static_cast<int>(msgs[1].data[1]), 1);
    CHECK_EQ(static_cast<int>(msgs[1].data[2]), 127);

    ConsoleEvent ev = p.decode(0xB0, 0, 63);
    CHECK_NEAR(ev.normalized, 63.f / 127.f, 1e-6);
    ev = p.decode(0xB0, 1, 127);
    CHECK_NEAR(ev.normalized, (63.f * 128.f + 127.f) / 16383.f, 1e-6);
}

TEST_CASE("protocol: alternate profile (LO24/HI8) addressing") {
    MixerProfile prof = MixerProfile::lo24hi8();
    CinemixProtocol p(prof);

    // Strip 25 (first HI) still restarts at ch2 CC0.
    MidiAddress a = p.stripFaderAddress(24, StripPath::Chan);
    CHECK_EQ(static_cast<int>(a.channel), 2); CHECK_EQ(static_cast<int>(a.cc), 0);
    // Strip 32 (last) Mix = within 7 → CC 30/31.
    a = p.stripFaderAddress(31, StripPath::Mix);
    CHECK_EQ(static_cast<int>(a.cc), 30);
    // HI touch range ends at CC 64+15.
    a = p.stripTouchAddress(31, StripPath::Mix);
    CHECK_EQ(static_cast<int>(a.channel), 4); CHECK_EQ(static_cast<int>(a.cc), 79);
    // CC 80..111 on ch4 must be unknown for this console.
    ConsoleEvent ev = p.decode(0xB3, 80, 6);
    CHECK(ev.kind == EventKind::Unknown);
}

TEST_CASE("protocol: quantization handles NaN and infinities deterministically") {
    // The legacy truncation behavior is preserved for finite values; NaN and
    // infinities are defined away so float→int conversion is never UB.
    CHECK_EQ(static_cast<int>(quantize7(0.5f)), 63);       // legacy truncation
    CHECK_EQ(static_cast<int>(quantize7(0.0f)), 0);
    CHECK_EQ(static_cast<int>(quantize7(1.0f)), 127);
    CHECK_EQ(static_cast<int>(quantize7(-0.25f)), 0);      // clamped low
    CHECK_EQ(static_cast<int>(quantize7(1.5f)), 127);      // clamped high
    CHECK_EQ(static_cast<int>(quantize7(std::numeric_limits<float>::quiet_NaN())), 0);
    CHECK_EQ(static_cast<int>(quantize7(std::numeric_limits<float>::infinity())), 127);
    CHECK_EQ(static_cast<int>(quantize7(-std::numeric_limits<float>::infinity())), 0);

    CHECK_EQ(clamp01(std::numeric_limits<float>::quiet_NaN()), 0.0f);
    CHECK_EQ(clamp01(std::numeric_limits<float>::infinity()), 1.0f);
    CHECK_EQ(clamp01(-std::numeric_limits<float>::infinity()), 0.0f);
    CHECK_EQ(clamp01(0.5f), 0.5f);

    // Round trip: normalize7(quantize7(x)) is within one 7-bit step of x.
    const float x = 0.4321f;
    CHECK_NEAR(normalize7(quantize7(x)), x, 1.0f / 127.0f);
}

TEST_CASE("protocol: channel-only decoding disambiguates both console ports") {
    // Port-identity audit (docs/ARCHITECTURE.md): the Cinemix protocol does
    // NOT require knowing which physical MIDI port a message arrived on —
    // the two console halves use disjoint MIDI channels (LO: 1/3, HI: 2/4,
    // master: 5), and the legacy bridge's RtMidi callbacks discarded source
    // identity too. Decoding is channel-based by design; this test pins the
    // overlap case: the same CC number on different channels must map to
    // different console controls.
    CinemixProtocol p(defaultProfile());

    // CC0: LO strip 1 Chan fader (ch1) vs HI strip 25 Chan fader (ch2).
    ConsoleEvent lo = p.decode(0xB0, 0, 100);
    ConsoleEvent hi = p.decode(0xB1, 0, 100);
    CHECK(lo.kind == EventKind::FaderPosition);
    CHECK(hi.kind == EventKind::FaderPosition);
    CHECK_EQ(static_cast<int>(lo.control.strip), 0);
    CHECK_EQ(static_cast<int>(hi.control.strip), 24);

    // CC0 on ch3 (LO strip 1 mute) vs ch4 (HI strip 25 mute).
    lo = p.decode(0xB2, 0, 3);
    hi = p.decode(0xB3, 0, 3);
    CHECK(lo.kind == EventKind::MuteChanged && lo.on);
    CHECK(hi.kind == EventKind::MuteChanged && hi.on);
    CHECK_EQ(static_cast<int>(lo.control.strip), 0);
    CHECK_EQ(static_cast<int>(hi.control.strip), 24);

    // Touch CC64 on ch3 (LO strip 1) vs ch4 (HI strip 25).
    lo = p.decode(0xB2, 64, 6);
    hi = p.decode(0xB3, 64, 6);
    CHECK(lo.kind == EventKind::TouchBegin);
    CHECK(hi.kind == EventKind::TouchBegin);
    CHECK_EQ(static_cast<int>(lo.control.strip), 0);
    CHECK_EQ(static_cast<int>(hi.control.strip), 24);
}

} // namespace

int main(int argc, char** argv) {
    return testfw::Registry::instance().runAll(argc > 1 ? argv[1] : nullptr);
}
