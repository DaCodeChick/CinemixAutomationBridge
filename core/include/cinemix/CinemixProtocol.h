// CinemixProtocol — table-driven translation between Cinemix domain concepts
// and the MIDI byte protocol. This is the ONLY place raw CC/channel numbers
// for the console live (values themselves come from MixerProfile).
//
// Encodes: control addresses + values → MidiMessage.
// Decodes: incoming MIDI CC → ConsoleEvent (including Unknown/Ignored
// classification for diagnostics).
//
// Portable C++11. All facts sourced from the legacy bridge — see
// docs/PROTOCOL.md.
#ifndef CINEMIX_PROTOCOL_H
#define CINEMIX_PROTOCOL_H

#include <cstdint>
#include <vector>

#include "cinemix/MixerProfile.h"
#include "cinemix/Types.h"

namespace cinemix {

struct MidiAddress {
    uint8_t port;    // 0 = broadcast, 1 = LO, 2 = HI
    uint8_t channel; // 1..16
    uint8_t cc;      // 0..127

    MidiAddress() : port(0), channel(0), cc(0) {}
    MidiAddress(uint8_t p, uint8_t c, uint8_t ccNum) : port(p), channel(c), cc(ccNum) {}
};

class CinemixProtocol {
public:
    explicit CinemixProtocol(const MixerProfile& profile);

    // ---- Address resolution (derived from profile) ------------------------
    MidiAddress stripFaderAddress(uint16_t strip, StripPath path) const;  // even CC
    MidiAddress stripFaderFineAddress(uint16_t strip, StripPath path) const; // odd CC
    MidiAddress stripMuteAddress(uint16_t strip, StripPath path) const;
    MidiAddress stripTouchAddress(uint16_t strip, StripPath path) const;  // also SEL
    MidiAddress masterFaderAddress() const;   // CC 0, ch 5, port 2
    MidiAddress masterSelAddress() const;     // CC 64, ch 5, port 2
    MidiAddress joyAxisAddress(uint8_t joy /*0|1*/, uint8_t axis /*0=X,1=Y*/) const;
    MidiAddress joyMuteAddress(uint8_t joy) const;
    MidiAddress joySelAddress(uint8_t joy) const;
    MidiAddress auxMuteAddress() const;       // CC 96, ch 5, port 2
    MidiMessage remoteControl(bool enter) const; // CC127 ch5 127/0, broadcast
    MidiMessage systemReset() const;             // 0xFF, broadcast

    // ---- Value encoding ----------------------------------------------------
    static MidiByte muteByte(bool on) { return on ? 3 : 2; }     // 2=OFF, 3=ON
    static MidiByte auxMuteByte(uint8_t auxIndex, bool on) {     // 2n/2n+1
        return MidiByte(2u * (auxIndex + 1u) + (on ? 1u : 0u));
    }

    // ---- Encoding (domain → MIDI) ------------------------------------------
    // Strip fader position (7-bit scheme sends the even CC; 14-bit scheme
    // sends both, MSB first).
    std::vector<MidiMessage> encodeStripFader(uint16_t strip, StripPath path, float v01) const;
    MidiMessage encodeStripMute(uint16_t strip, StripPath path, bool on) const;
    MidiMessage encodeMasterFader(float v01) const;
    MidiMessage encodeJoyAxis(uint8_t joy, uint8_t axis, float v01) const;
    MidiMessage encodeJoyMute(uint8_t joy, bool on) const;
    MidiMessage encodeAuxMute(uint8_t auxIndex, bool on) const;
    MidiMessage encodeSetMode(const MidiAddress& addr, uint8_t mode) const;

    // ---- Decoding (MIDI → domain) ------------------------------------------
    // Returns an event; Unknown CCs produce kind Unknown (with raw bytes kept
    // for diagnostics), protocol CCs with meaningless values produce Ignored.
    // Not thread-safe by design: decoding is expected to happen on the bridge
    // worker thread only (the 14-bit mode keeps a small MSB cache).
    ConsoleEvent decode(uint8_t statusByte, uint8_t cc, uint8_t value);

    const MixerProfile& profile() const { return profile_; }

private:
    struct TableEntry {
        bool used;
        ControlClass cls;
        uint16_t strip;
        StripPath path;
        uint8_t index;
    };

    MixerProfile profile_;
    std::vector<TableEntry> table_; // [channel-1][cc], 16*128 entries
    // 14-bit fader MSB cache (unverified mode): [strip*2 + path]
    mutable std::vector<uint8_t> faderMsb_;
    size_t indexOf(uint8_t channel, uint8_t cc) const {
        return size_t(channel - 1) * 128u + size_t(cc);
    }
    void setEntry(uint8_t channel, uint8_t cc, const TableEntry& e);
};

} // namespace cinemix

#endif // CINEMIX_PROTOCOL_H
