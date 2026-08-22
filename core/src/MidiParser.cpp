#include "cinemix/MidiParser.h"

namespace cinemix {

namespace {
inline bool isStatus(uint8_t b) { return (b & 0x80) != 0; }
inline bool isRealtime(uint8_t b) { return (b & 0xF8) == 0xF8; }
inline bool isSystemCommon(uint8_t b) { return (b & 0xF0) == 0xF0; }
inline int dataLengthForStatus(uint8_t status) {
    // Channel voice messages only; system messages are handled separately.
    switch (status & 0xF0) {
    case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
    case 0xC0: case 0xD0: return 1;
    default: return 0;
    }
}
} // namespace

void MidiParser::reset() {
    runningStatus_ = 0;
    pendingLen_ = 0;
    pendingNeed_ = 0;
    sysCommonSkip_ = 0;
    inSysex_ = false;
    malformed_ = 0;
    ignored_ = 0;
}

void MidiParser::emitCc(uint8_t channel, uint8_t cc, uint8_t value) {
    if (onCc_) onCc_(user_, channel, cc, value);
}

void MidiParser::feed(const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        const uint8_t b = data[i];

        // Realtime bytes may appear anywhere (including inside another
        // message) and never disturb parser state.
        if (isRealtime(b)) {
            if (b == 0xFF && onSystem_) onSystem_(user_, b);
            continue;
        }

        // Inside sysex: everything is data until 0xF7, or until a new status
        // byte appears (defensive).
        if (inSysex_) {
            if (b == 0xF7) { inSysex_ = false; continue; }
            if (!isStatus(b)) continue;
            inSysex_ = false;
        }

        if (isStatus(b)) {
            if (b == 0xF0) { inSysex_ = true; runningStatus_ = 0; pendingLen_ = 0; continue; }
            if (isSystemCommon(b)) {
                // System common (0xF1..0xF6): not part of the Cinemix protocol.
                // Consume their fixed data bytes, then carry on.
                ++ignored_;
                runningStatus_ = 0; // system common cancels running status
                pendingLen_ = 0;
                if (b == 0xF1 || b == 0xF3) sysCommonSkip_ = 1;
                else if (b == 0xF2) sysCommonSkip_ = 2;
                else sysCommonSkip_ = 0;
                continue;
            }
            // Channel voice status: becomes the running status.
            runningStatus_ = b;
            pendingLen_ = 0;
            pendingNeed_ = uint8_t(dataLengthForStatus(b));
            continue;
        }

        // Data byte.
        if (sysCommonSkip_ > 0) { --sysCommonSkip_; continue; }
        if (runningStatus_ == 0) {
            // Stray data byte with no running status: nothing to attach it to.
            ++malformed_;
            continue;
        }
        if (pendingLen_ < 2) pendingData_[pendingLen_++] = b;
        if (pendingLen_ >= pendingNeed_) {
            const uint8_t status = runningStatus_;
            const uint8_t d0 = pendingData_[0];
            const uint8_t d1 = (pendingNeed_ == 2) ? pendingData_[1] : 0;
            pendingLen_ = 0;
            if ((status & 0xF0) == 0xB0) {
                emitCc(uint8_t((status & 0x0F) + 1), d0 & 0x7F, d1 & 0x7F);
            } else {
                // Non-CC channel voice: not part of the protocol.
                ++ignored_;
            }
        }
    }
}

} // namespace cinemix
