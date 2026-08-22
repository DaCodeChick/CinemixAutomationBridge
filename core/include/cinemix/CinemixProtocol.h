// CinemixProtocol — table-driven translation between Cinemix domain concepts
// and the MIDI byte protocol. This is the ONLY place raw CC/channel numbers
// for the console live (values themselves come from MixerProfile).
//
// Encodes: control addresses + values → MidiMessage.
// Decodes: incoming MIDI CC → ConsoleEvent (including Unknown/Ignored
// classification for diagnostics).
//
// Portable C++14. All facts sourced from the legacy bridge — see
// docs/PROTOCOL.md.
#ifndef CINEMIX_PROTOCOL_H
#define CINEMIX_PROTOCOL_H

#include <cstdint>
#include <vector>

#include "cinemix/MixerProfile.h"
#include "cinemix/Types.h"

namespace cinemix {

struct MidiAddress {
    std::uint8_t port;    // 0 = broadcast, 1 = LO, 2 = HI
    std::uint8_t channel; // 1..16
    std::uint8_t cc;      // 0..127

    constexpr MidiAddress() noexcept : port(0), channel(0), cc(0) {}
    constexpr MidiAddress(std::uint8_t portNumber, std::uint8_t channelNumber,
                          std::uint8_t ccNumber) noexcept
        : port(portNumber), channel(channelNumber), cc(ccNumber) {}
};

class CinemixProtocol {
public:
    explicit CinemixProtocol(const MixerProfile& profile);

    // ---- Address resolution (derived from profile) ------------------------
    MidiAddress stripFaderAddress(std::uint16_t strip, StripPath path) const;  // even CC
    MidiAddress stripFaderFineAddress(std::uint16_t strip, StripPath path) const; // odd CC
    MidiAddress stripMuteAddress(std::uint16_t strip, StripPath path) const;
    MidiAddress stripTouchAddress(std::uint16_t strip, StripPath path) const;  // also SEL
    MidiAddress masterFaderAddress() const;   // CC 0, ch 5, port 2
    MidiAddress masterSelAddress() const;     // CC 64, ch 5, port 2
    MidiAddress joyAxisAddress(std::uint8_t joy /*0|1*/, std::uint8_t axis /*0=X,1=Y*/) const;
    MidiAddress joyMuteAddress(std::uint8_t joy) const;
    MidiAddress joySelAddress(std::uint8_t joy) const;
    MidiAddress auxMuteAddress() const;       // CC 96, ch 5, port 2
    MidiMessage remoteControl(bool enter) const; // CC127 ch5 127/0, broadcast
    MidiMessage systemReset() const;             // 0xFF, broadcast

    // ---- Value encoding ----------------------------------------------------
    static constexpr MidiByte muteByte(bool on) noexcept { return on ? 3 : 2; } // 2=OFF, 3=ON
    static constexpr MidiByte auxMuteByte(std::uint8_t auxIndex, bool on) noexcept {
        // AUX n = 2n / 2n+1.
        return static_cast<MidiByte>(2u * (auxIndex + 1u) + (on ? 1u : 0u));
    }

    // ---- Encoding (domain → MIDI) ------------------------------------------
    // Strip fader position (7-bit scheme sends the even CC; 14-bit scheme
    // sends both, MSB first).
    std::vector<MidiMessage> encodeStripFader(std::uint16_t strip, StripPath path,
                                              float value01) const;
    MidiMessage encodeStripMute(std::uint16_t strip, StripPath path, bool on) const;
    MidiMessage encodeMasterFader(float value01) const;
    MidiMessage encodeJoyAxis(std::uint8_t joy, std::uint8_t axis, float value01) const;
    MidiMessage encodeJoyMute(std::uint8_t joy, bool on) const;
    MidiMessage encodeAuxMute(std::uint8_t auxIndex, bool on) const;
    MidiMessage encodeSetMode(const MidiAddress& address, std::uint8_t mode) const;

    // ---- Decoding (MIDI → domain) ------------------------------------------
    // Returns an event; Unknown CCs produce kind Unknown (with raw bytes kept
    // for diagnostics), protocol CCs with meaningless values produce Ignored.
    // Not thread-safe by design: decoding is expected to happen on the bridge
    // worker thread only (the 14-bit mode keeps a small MSB cache).
    ConsoleEvent decode(std::uint8_t statusByte, std::uint8_t cc, std::uint8_t value);

    const MixerProfile& profile() const { return profile_; }

private:
    struct TableEntry {
        bool used;
        ControlClass cls;
        std::uint16_t strip;
        StripPath path;
        std::uint8_t index;
    };

    MixerProfile profile_;
    std::vector<TableEntry> table_; // [channel-1][cc], 16*128 entries
    // 14-bit fader MSB cache (unverified mode): [strip*2 + path]
    std::vector<std::uint8_t> faderMsb_;
    std::size_t indexOf(std::uint8_t channel, std::uint8_t cc) const noexcept {
        return static_cast<std::size_t>(channel - 1u) * 128u + static_cast<std::size_t>(cc);
    }
    void setEntry(std::uint8_t channel, std::uint8_t cc, const TableEntry& entry);
};

} // namespace cinemix

#endif // CINEMIX_PROTOCOL_H
