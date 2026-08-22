#include "cinemix/MidiParser.h"

namespace cinemix {

namespace {
constexpr bool isStatus(std::uint8_t byte) noexcept { return (byte & 0x80u) != 0; }
constexpr bool isRealtime(std::uint8_t byte) noexcept { return (byte & 0xF8u) == 0xF8u; }
constexpr bool isSystemCommon(std::uint8_t byte) noexcept { return (byte & 0xF0u) == 0xF0u; }
constexpr int dataLengthForStatus(std::uint8_t status) noexcept {
    // Channel voice messages only; system messages are handled separately.
    switch (status & 0xF0u) {
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

void MidiParser::emitCc(std::uint8_t channel, std::uint8_t cc, std::uint8_t value) {
    if (onCc_) onCc_(user_, channel, cc, value);
}

void MidiParser::feed(const std::uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        const std::uint8_t byte = data[i];

        // Realtime bytes may appear anywhere (including inside another
        // message) and never disturb parser state.
        if (isRealtime(byte)) {
            if (byte == 0xFF && onSystem_) onSystem_(user_, byte);
            continue;
        }

        // Inside sysex: everything is data until 0xF7, or until a new status
        // byte appears (defensive).
        if (inSysex_) {
            if (byte == 0xF7) { inSysex_ = false; continue; }
            if (!isStatus(byte)) continue;
            inSysex_ = false;
        }

        if (isStatus(byte)) {
            if (byte == 0xF0) { inSysex_ = true; runningStatus_ = 0; pendingLen_ = 0; continue; }
            if (isSystemCommon(byte)) {
                // System common (0xF1..0xF6): not part of the Cinemix protocol.
                // Consume their fixed data bytes, then carry on.
                ++ignored_;
                runningStatus_ = 0; // system common cancels running status
                pendingLen_ = 0;
                if (byte == 0xF1 || byte == 0xF3) sysCommonSkip_ = 1;
                else if (byte == 0xF2) sysCommonSkip_ = 2;
                else sysCommonSkip_ = 0;
                continue;
            }
            // Channel voice status: becomes the running status.
            runningStatus_ = byte;
            pendingLen_ = 0;
            pendingNeed_ = static_cast<std::uint8_t>(dataLengthForStatus(byte));
            continue;
        }

        // Data byte.
        if (sysCommonSkip_ > 0) { --sysCommonSkip_; continue; }
        if (runningStatus_ == 0) {
            // Stray data byte with no running status: nothing to attach it to.
            ++malformed_;
            continue;
        }
        if (pendingLen_ < 2) pendingData_[pendingLen_++] = byte;
        if (pendingLen_ >= pendingNeed_) {
            const std::uint8_t status = runningStatus_;
            const std::uint8_t data0 = pendingData_[0];
            const std::uint8_t data1 = (pendingNeed_ == 2) ? pendingData_[1] : 0;
            pendingLen_ = 0;
            if ((status & 0xF0u) == 0xB0u) {
                emitCc(static_cast<std::uint8_t>((status & 0x0F) + 1), data0 & 0x7Fu,
                       data1 & 0x7Fu);
            } else {
                // Non-CC channel voice: not part of the protocol.
                ++ignored_;
            }
        }
    }
}

} // namespace cinemix
