#include "cinemix/CinemixProtocol.h"

namespace cinemix {

namespace {

// The two fader CCs per strip/path are consecutive; even = main (MSB in the
// 14-bit reading), odd = fine.
inline uint8_t faderCcBase(size_t withinSide, StripPath path) {
    return uint8_t(4u * withinSide + (path == StripPath::Chan ? 0u : 2u));
}
inline uint8_t muteCc(size_t withinSide, StripPath path) {
    return uint8_t(2u * withinSide + (path == StripPath::Chan ? 0u : 1u));
}
inline uint8_t touchCc(size_t withinSide, StripPath path) {
    return uint8_t(64u + 2u * withinSide + (path == StripPath::Chan ? 0u : 1u));
}

} // namespace

CinemixProtocol::CinemixProtocol(const MixerProfile& profile)
    : profile_(profile), table_(16u * 128u, TableEntry{false, ControlClass::Fader, 0, StripPath::Chan, 0}) {
    faderMsb_.assign(profile_.faderCount(), 0);
    // ---- Channel strips ----------------------------------------------------
    for (uint16_t s = 0; s < profile_.stripCount(); ++s) {
        const bool lo = (s < profile_.loStrips);
        const size_t w = profile_.withinSide(s);
        const uint8_t faderCh = lo ? profile_.loFaderChannel : profile_.hiFaderChannel;
        const uint8_t ctrlCh = lo ? profile_.loControlChannel : profile_.hiControlChannel;

        for (int p = 0; p < 2; ++p) {
            const StripPath path = (p == 0) ? StripPath::Chan : StripPath::Mix;
            const uint8_t ccBase = faderCcBase(w, path);

            TableEntry fader;
            fader.used = true; fader.cls = ControlClass::Fader;
            fader.strip = s; fader.path = path; fader.index = 0;
            setEntry(faderCh, ccBase, fader);
            setEntry(faderCh, uint8_t(ccBase + 1), fader); // fine CC — same control

            TableEntry mute;
            mute.used = true; mute.cls = ControlClass::Mute;
            mute.strip = s; mute.path = path; mute.index = 0;
            setEntry(ctrlCh, muteCc(w, path), mute);

            TableEntry touch;
            touch.used = true; touch.cls = ControlClass::Touch;
            touch.strip = s; touch.path = path; touch.index = 0;
            setEntry(ctrlCh, touchCc(w, path), touch);
        }
    }

    // ---- Master section ----------------------------------------------------
    if (profile_.hasMasterFader) {
        TableEntry master;
        master.used = true; master.cls = ControlClass::MasterFader;
        master.strip = 0; master.path = StripPath::Chan; master.index = 0;
        setEntry(profile_.masterChannel, 0, master);
        setEntry(profile_.masterChannel, 1, master); // fine CC — same control

        TableEntry masterSel;
        masterSel.used = true; masterSel.cls = ControlClass::MasterSel;
        masterSel.strip = 0; masterSel.path = StripPath::Chan; masterSel.index = 0;
        setEntry(profile_.masterChannel, 64, masterSel);
    }

    if (profile_.hasJoystick1 || profile_.hasJoystick2) {
        for (uint8_t j = 0; j < 2; ++j) {
            const bool present = (j == 0) ? profile_.hasJoystick1 : profile_.hasJoystick2;
            if (!present) continue;
            TableEntry joy;
            joy.used = true; joy.strip = 0; joy.index = j;
            for (uint8_t axis = 0; axis < 2; ++axis) {
                joy.cls = ControlClass::JoyAxis;
                joy.path = (axis == 0) ? StripPath::Chan : StripPath::Mix; // Chan=X, Mix=Y
                setEntry(profile_.hiFaderChannel, uint8_t(48u + 4u * j + 2u * axis), joy);
            }
            joy.cls = ControlClass::JoyMute;
            joy.path = StripPath::Chan;
            setEntry(profile_.hiControlChannel, uint8_t(24u + 2u * j), joy);
        }
    }

    if (profile_.auxMuteCount > 0) {
        TableEntry aux;
        aux.used = true; aux.cls = ControlClass::AuxMute;
        aux.strip = 0; aux.path = StripPath::Chan; aux.index = 0;
        setEntry(profile_.masterChannel, 96, aux);
    }
}

void CinemixProtocol::setEntry(uint8_t channel, uint8_t cc, const TableEntry& e) {
    table_[indexOf(channel, cc)] = e;
}

// ---- Address resolution -----------------------------------------------------

MidiAddress CinemixProtocol::stripFaderAddress(uint16_t strip, StripPath path) const {
    const bool lo = (strip < profile_.loStrips);
    return MidiAddress(lo ? 1 : 2,
                       lo ? profile_.loFaderChannel : profile_.hiFaderChannel,
                       faderCcBase(profile_.withinSide(strip), path));
}
MidiAddress CinemixProtocol::stripFaderFineAddress(uint16_t strip, StripPath path) const {
    MidiAddress a = stripFaderAddress(strip, path);
    a.cc = uint8_t(a.cc + 1);
    return a;
}
MidiAddress CinemixProtocol::stripMuteAddress(uint16_t strip, StripPath path) const {
    const bool lo = (strip < profile_.loStrips);
    return MidiAddress(lo ? 1 : 2,
                       lo ? profile_.loControlChannel : profile_.hiControlChannel,
                       muteCc(profile_.withinSide(strip), path));
}
MidiAddress CinemixProtocol::stripTouchAddress(uint16_t strip, StripPath path) const {
    const bool lo = (strip < profile_.loStrips);
    // Touch/SEL traffic is broadcast to both ports in the legacy bridge.
    return MidiAddress(0,
                       lo ? profile_.loControlChannel : profile_.hiControlChannel,
                       touchCc(profile_.withinSide(strip), path));
}
MidiAddress CinemixProtocol::masterFaderAddress() const {
    return MidiAddress(2, profile_.masterChannel, 0);
}
MidiAddress CinemixProtocol::masterSelAddress() const {
    return MidiAddress(0, profile_.masterChannel, 64);
}
MidiAddress CinemixProtocol::joyAxisAddress(uint8_t joy, uint8_t axis) const {
    return MidiAddress(2, profile_.hiFaderChannel, uint8_t(48u + 4u * joy + 2u * axis));
}
MidiAddress CinemixProtocol::joyMuteAddress(uint8_t joy) const {
    return MidiAddress(2, profile_.hiControlChannel, uint8_t(24u + 2u * joy));
}
MidiAddress CinemixProtocol::joySelAddress(uint8_t joy) const {
    return MidiAddress(0, profile_.hiControlChannel, uint8_t(88u + 2u * joy));
}
MidiAddress CinemixProtocol::auxMuteAddress() const {
    return MidiAddress(2, profile_.masterChannel, 96);
}
MidiMessage CinemixProtocol::remoteControl(bool enter) const {
    return MidiMessage::controlChange(profile_.masterChannel, 127, enter ? 127 : 0, 0);
}
MidiMessage CinemixProtocol::systemReset() const {
    return MidiMessage::systemReset(0);
}

// ---- Encoding ----------------------------------------------------------------

std::vector<MidiMessage> CinemixProtocol::encodeStripFader(uint16_t strip, StripPath path, float v01) const {
    std::vector<MidiMessage> out;
    const MidiAddress a = stripFaderAddress(strip, path);
    if (profile_.faderResolution == FaderResolution::SevenBit) {
        out.push_back(MidiMessage::controlChange(a.channel, a.cc, quantize7(v01), a.port));
    } else {
        // FourteenBit (unverified): even CC = MSB, odd CC = LSB.
        const int v = int(clamp01(v01) * 16383.f);
        out.push_back(MidiMessage::controlChange(a.channel, a.cc, MidiByte((v >> 7) & 0x7F), a.port));
        out.push_back(MidiMessage::controlChange(a.channel, uint8_t(a.cc + 1), MidiByte(v & 0x7F), a.port));
    }
    return out;
}

MidiMessage CinemixProtocol::encodeStripMute(uint16_t strip, StripPath path, bool on) const {
    const MidiAddress a = stripMuteAddress(strip, path);
    return MidiMessage::controlChange(a.channel, a.cc, muteByte(on), a.port);
}

MidiMessage CinemixProtocol::encodeMasterFader(float v01) const {
    const MidiAddress a = masterFaderAddress();
    return MidiMessage::controlChange(a.channel, a.cc, quantize7(v01), a.port);
}

MidiMessage CinemixProtocol::encodeJoyAxis(uint8_t joy, uint8_t axis, float v01) const {
    const MidiAddress a = joyAxisAddress(joy, axis);
    return MidiMessage::controlChange(a.channel, a.cc, quantize7(v01), a.port);
}

MidiMessage CinemixProtocol::encodeJoyMute(uint8_t joy, bool on) const {
    const MidiAddress a = joyMuteAddress(joy);
    return MidiMessage::controlChange(a.channel, a.cc, muteByte(on), a.port);
}

MidiMessage CinemixProtocol::encodeAuxMute(uint8_t auxIndex, bool on) const {
    const MidiAddress a = auxMuteAddress();
    return MidiMessage::controlChange(a.channel, a.cc, auxMuteByte(auxIndex, on), a.port);
}

MidiMessage CinemixProtocol::encodeSetMode(const MidiAddress& addr, uint8_t mode) const {
    return MidiMessage::controlChange(addr.channel, addr.cc, mode & 0x03, addr.port);
}

// ---- Decoding -----------------------------------------------------------------

ConsoleEvent CinemixProtocol::decode(uint8_t statusByte, uint8_t cc, uint8_t value) {
    const uint8_t channel = uint8_t((statusByte & 0x0F) + 1);
    ConsoleEvent ev;
    ev.value = value;
    ev.channel = channel;
    ev.cc = cc;
    if (channel == 0 || channel > 16) { ev.kind = EventKind::Unknown; return ev; }

    const TableEntry& e = table_[indexOf(channel, cc)];
    if (!e.used) { ev.kind = EventKind::Unknown; return ev; }

    ev.control = ControlRef(e.cls, uint16_t(e.strip), e.path, e.index);

    switch (e.cls) {
    case ControlClass::Fader: {
        ev.kind = EventKind::FaderPosition;
        const bool oddCc = (profile_.faderResolution == FaderResolution::FourteenBit) &&
                           (stripFaderAddress(e.strip, e.path).cc != cc);
        if (oddCc) {
            // LSB: combine with the cached MSB (14-bit reading, unverified).
            const size_t slot = size_t(e.strip) * 2u + (e.path == StripPath::Chan ? 0u : 1u);
            const int combined = int(faderMsb_[slot]) * 128 + int(value);
            ev.normalized = float(combined) / 16383.f;
        } else {
            // MSB (or 7-bit mode): cache and emit coarse position.
            const size_t slot = size_t(e.strip) * 2u + (e.path == StripPath::Chan ? 0u : 1u);
            faderMsb_[slot] = value;
            ev.normalized = normalize7(value);
        }
        break;
    }
    case ControlClass::Mute:
        ev.kind = EventKind::MuteChanged;
        ev.on = (value == 3);
        break;
    case ControlClass::Touch:
        // Same CC for touch sensing and SEL: value disambiguates.
        if (value == 6)      ev.kind = EventKind::TouchBegin;
        else if (value == 5) ev.kind = EventKind::TouchEnd;
        else if (value == 1) ev.kind = EventKind::SelPressed;
        else                 ev.kind = EventKind::Ignored;
        break;
    case ControlClass::JoyAxis:
        ev.kind = EventKind::FaderPosition;
        ev.normalized = normalize7(value);
        break;
    case ControlClass::JoyMute:
        ev.kind = EventKind::MuteChanged;
        ev.on = (value == 3);
        break;
    case ControlClass::MasterFader:
        ev.kind = EventKind::FaderPosition;
        ev.normalized = normalize7(value);
        break;
    case ControlClass::MasterSel:
        ev.kind = (value == 1) ? EventKind::MasterSelPressed : EventKind::Ignored;
        break;
    case ControlClass::AuxMute:
        // Values 2..21: aux n = (v-2)/2, odd = ON. Anything else is ignored.
        if (value >= 2 && value <= 21) {
            ev.kind = EventKind::AuxMuteChanged;
            ev.control.index = uint8_t((value - 2) / 2);
            ev.on = ((value & 1) != 0);
        } else {
            ev.kind = EventKind::Ignored;
        }
        break;
    default:
        ev.kind = EventKind::Ignored;
        break;
    }
    return ev;
}

} // namespace cinemix
