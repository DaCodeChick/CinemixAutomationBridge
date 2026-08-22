#include "cinemix/CinemixProtocol.h"

namespace cinemix {

namespace {

// The two fader CCs per strip/path are consecutive; even = main (MSB in the
// 14-bit reading), odd = fine.
constexpr std::uint8_t faderCcBase(std::size_t withinSide, StripPath path) noexcept {
    return static_cast<std::uint8_t>(
        4u * withinSide + (path == StripPath::Chan ? 0u : 2u));
}
constexpr std::uint8_t muteCc(std::size_t withinSide, StripPath path) noexcept {
    return static_cast<std::uint8_t>(
        2u * withinSide + (path == StripPath::Chan ? 0u : 1u));
}
constexpr std::uint8_t touchCc(std::size_t withinSide, StripPath path) noexcept {
    return static_cast<std::uint8_t>(
        64u + 2u * withinSide + (path == StripPath::Chan ? 0u : 1u));
}

} // namespace

CinemixProtocol::CinemixProtocol(const MixerProfile& profile)
    : profile_(profile), table_(16u * 128u,
                                TableEntry{false, ControlClass::Fader, 0, StripPath::Chan, 0}) {
    faderMsb_.assign(profile_.faderCount(), 0);

    // ---- Channel strips ----------------------------------------------------
    for (std::uint16_t strip = 0; strip < profile_.stripCount(); ++strip) {
        const bool lo = (strip < profile_.loStrips);
        const std::size_t within = profile_.withinSide(strip);
        const std::uint8_t faderChannel =
            lo ? profile_.loFaderChannel : profile_.hiFaderChannel;
        const std::uint8_t controlChannel =
            lo ? profile_.loControlChannel : profile_.hiControlChannel;

        for (int pathIndex = 0; pathIndex < 2; ++pathIndex) {
            const StripPath path =
                (pathIndex == 0) ? StripPath::Chan : StripPath::Mix;
            const std::uint8_t ccBase = faderCcBase(within, path);

            TableEntry fader;
            fader.used = true; fader.cls = ControlClass::Fader;
            fader.strip = strip; fader.path = path; fader.index = 0;
            setEntry(faderChannel, ccBase, fader);
            setEntry(faderChannel, static_cast<std::uint8_t>(ccBase + 1), fader); // fine CC

            TableEntry mute;
            mute.used = true; mute.cls = ControlClass::Mute;
            mute.strip = strip; mute.path = path; mute.index = 0;
            setEntry(controlChannel, muteCc(within, path), mute);

            TableEntry touch;
            touch.used = true; touch.cls = ControlClass::Touch;
            touch.strip = strip; touch.path = path; touch.index = 0;
            setEntry(controlChannel, touchCc(within, path), touch);
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
        for (std::uint8_t joy = 0; joy < 2; ++joy) {
            const bool present =
                (joy == 0) ? profile_.hasJoystick1 : profile_.hasJoystick2;
            if (!present) continue;
            TableEntry entry;
            entry.used = true; entry.strip = 0; entry.index = joy;
            for (std::uint8_t axis = 0; axis < 2; ++axis) {
                entry.cls = ControlClass::JoyAxis;
                entry.path = (axis == 0) ? StripPath::Chan : StripPath::Mix; // Chan=X, Mix=Y
                setEntry(profile_.hiFaderChannel,
                         static_cast<std::uint8_t>(48u + 4u * joy + 2u * axis), entry);
            }
            entry.cls = ControlClass::JoyMute;
            entry.path = StripPath::Chan;
            setEntry(profile_.hiControlChannel, static_cast<std::uint8_t>(24u + 2u * joy), entry);
        }
    }

    if (profile_.auxMuteCount > 0) {
        TableEntry aux;
        aux.used = true; aux.cls = ControlClass::AuxMute;
        aux.strip = 0; aux.path = StripPath::Chan; aux.index = 0;
        setEntry(profile_.masterChannel, 96, aux);
    }
}

void CinemixProtocol::setEntry(std::uint8_t channel, std::uint8_t cc, const TableEntry& entry) {
    table_[indexOf(channel, cc)] = entry;
}

// ---- Address resolution -----------------------------------------------------

MidiAddress CinemixProtocol::stripFaderAddress(std::uint16_t strip, StripPath path) const {
    const bool lo = (strip < profile_.loStrips);
    return MidiAddress(lo ? 1 : 2,
                       lo ? profile_.loFaderChannel : profile_.hiFaderChannel,
                       faderCcBase(profile_.withinSide(strip), path));
}
MidiAddress CinemixProtocol::stripFaderFineAddress(std::uint16_t strip, StripPath path) const {
    MidiAddress address = stripFaderAddress(strip, path);
    address.cc = static_cast<std::uint8_t>(address.cc + 1);
    return address;
}
MidiAddress CinemixProtocol::stripMuteAddress(std::uint16_t strip, StripPath path) const {
    const bool lo = (strip < profile_.loStrips);
    return MidiAddress(lo ? 1 : 2,
                       lo ? profile_.loControlChannel : profile_.hiControlChannel,
                       muteCc(profile_.withinSide(strip), path));
}
MidiAddress CinemixProtocol::stripTouchAddress(std::uint16_t strip, StripPath path) const {
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
MidiAddress CinemixProtocol::joyAxisAddress(std::uint8_t joy, std::uint8_t axis) const {
    return MidiAddress(2, profile_.hiFaderChannel,
                       static_cast<std::uint8_t>(48u + 4u * joy + 2u * axis));
}
MidiAddress CinemixProtocol::joyMuteAddress(std::uint8_t joy) const {
    return MidiAddress(2, profile_.hiControlChannel, static_cast<std::uint8_t>(24u + 2u * joy));
}
MidiAddress CinemixProtocol::joySelAddress(std::uint8_t joy) const {
    return MidiAddress(0, profile_.hiControlChannel, static_cast<std::uint8_t>(88u + 2u * joy));
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

std::vector<MidiMessage> CinemixProtocol::encodeStripFader(std::uint16_t strip,
                                                           StripPath path,
                                                           float value01) const {
    std::vector<MidiMessage> messages;
    const MidiAddress address = stripFaderAddress(strip, path);
    if (profile_.faderResolution == FaderResolution::SevenBit) {
        messages.push_back(
            MidiMessage::controlChange(address.channel, address.cc, quantize7(value01), address.port));
    } else {
        // FourteenBit (unverified): even CC = MSB, odd CC = LSB.
        // Truncation of v*16383 mirrors the 7-bit legacy quantization.
        const int combined = static_cast<int>(clamp01(value01) * 16383.0f);
        messages.push_back(MidiMessage::controlChange(
            address.channel, address.cc,
            static_cast<MidiByte>((combined >> 7) & 0x7F), address.port));
        messages.push_back(MidiMessage::controlChange(
            address.channel, static_cast<std::uint8_t>(address.cc + 1),
            static_cast<MidiByte>(combined & 0x7F), address.port));
    }
    return messages;
}

MidiMessage CinemixProtocol::encodeStripMute(std::uint16_t strip, StripPath path, bool on) const {
    const MidiAddress address = stripMuteAddress(strip, path);
    return MidiMessage::controlChange(address.channel, address.cc, muteByte(on), address.port);
}

MidiMessage CinemixProtocol::encodeMasterFader(float value01) const {
    const MidiAddress address = masterFaderAddress();
    return MidiMessage::controlChange(address.channel, address.cc, quantize7(value01), address.port);
}

MidiMessage CinemixProtocol::encodeJoyAxis(std::uint8_t joy, std::uint8_t axis,
                                           float value01) const {
    const MidiAddress address = joyAxisAddress(joy, axis);
    return MidiMessage::controlChange(address.channel, address.cc, quantize7(value01), address.port);
}

MidiMessage CinemixProtocol::encodeJoyMute(std::uint8_t joy, bool on) const {
    const MidiAddress address = joyMuteAddress(joy);
    return MidiMessage::controlChange(address.channel, address.cc, muteByte(on), address.port);
}

MidiMessage CinemixProtocol::encodeAuxMute(std::uint8_t auxIndex, bool on) const {
    const MidiAddress address = auxMuteAddress();
    return MidiMessage::controlChange(address.channel, address.cc, auxMuteByte(auxIndex, on),
                                      address.port);
}

MidiMessage CinemixProtocol::encodeSetMode(const MidiAddress& address, std::uint8_t mode) const {
    return MidiMessage::controlChange(address.channel, address.cc, mode & 0x03u, address.port);
}

// ---- Decoding -----------------------------------------------------------------

ConsoleEvent CinemixProtocol::decode(std::uint8_t statusByte, std::uint8_t cc,
                                     std::uint8_t value) {
    const std::uint8_t channel = static_cast<std::uint8_t>((statusByte & 0x0F) + 1);
    ConsoleEvent event;
    event.value = value;
    event.channel = channel;
    event.cc = cc;
    if (channel == 0 || channel > 16) { event.kind = EventKind::Unknown; return event; }

    const TableEntry& entry = table_[indexOf(channel, cc)];
    if (!entry.used) { event.kind = EventKind::Unknown; return event; }

    event.control = ControlRef(entry.cls, static_cast<std::uint8_t>(entry.strip), entry.path,
                               entry.index);

    switch (entry.cls) {
    case ControlClass::Fader: {
        event.kind = EventKind::FaderPosition;
        const bool isFineCc =
            (profile_.faderResolution == FaderResolution::FourteenBit) &&
            (stripFaderAddress(entry.strip, entry.path).cc != cc);
        if (isFineCc) {
            // LSB: combine with the cached MSB (14-bit reading, unverified).
            const std::size_t slot = static_cast<std::size_t>(entry.strip) * 2u +
                                     (entry.path == StripPath::Chan ? 0u : 1u);
            const int combined =
                static_cast<int>(faderMsb_[slot]) * 128 + static_cast<int>(value);
            event.normalized = static_cast<float>(combined) / 16383.0f;
        } else {
            // MSB (or 7-bit mode): cache and emit coarse position.
            const std::size_t slot = static_cast<std::size_t>(entry.strip) * 2u +
                                     (entry.path == StripPath::Chan ? 0u : 1u);
            faderMsb_[slot] = value;
            event.normalized = normalize7(value);
        }
        break;
    }
    case ControlClass::Mute:
        event.kind = EventKind::MuteChanged;
        event.on = (value == 3);
        break;
    case ControlClass::Touch:
        // Same CC for touch sensing and SEL: value disambiguates.
        if (value == 6) event.kind = EventKind::TouchBegin;
        else if (value == 5) event.kind = EventKind::TouchEnd;
        else if (value == 1) event.kind = EventKind::SelPressed;
        else event.kind = EventKind::Ignored;
        break;
    case ControlClass::JoyAxis:
        event.kind = EventKind::FaderPosition;
        event.normalized = normalize7(value);
        break;
    case ControlClass::JoyMute:
        event.kind = EventKind::MuteChanged;
        event.on = (value == 3);
        break;
    case ControlClass::MasterFader:
        event.kind = EventKind::FaderPosition;
        event.normalized = normalize7(value);
        break;
    case ControlClass::MasterSel:
        event.kind = (value == 1) ? EventKind::MasterSelPressed : EventKind::Ignored;
        break;
    case ControlClass::AuxMute:
        // Values 2..21: aux n = (v-2)/2, odd = ON. Anything else is ignored.
        if (value >= 2 && value <= 21) {
            event.kind = EventKind::AuxMuteChanged;
            event.control.index = static_cast<std::uint8_t>((value - 2) / 2);
            event.on = ((value & 1) != 0);
        } else {
            event.kind = EventKind::Ignored;
        }
        break;
    default:
        event.kind = EventKind::Ignored;
        break;
    }
    return event;
}

} // namespace cinemix
