// CaptureFile — the .cmi capture/replay format used for hardware-free
// testing and diagnostics (docs/TESTING.md).
//
// Format (unchanged since v1; little-endian):
//   magic   "CMIXCAPI" (8 bytes)
//   u32     version = 1
//   events, repeated:
//     u64  timestampUs  (microseconds since capture start)
//     u8   direction    (0 = inbound/console→bridge, 1 = outbound/bridge→console)
//     u8   port         (1/2; 0 = broadcast)
//     u8   length       (1..3)
//     u8[] bytes
//
// Layers:
//   * capture_detail — pure record codec over caller-owned buffers
//     (value-returning, no unchecked pointers, full field validation,
//     little-endian via explicit shifts — no host endianness assumptions).
//   * CaptureWriter/CaptureReader — the stream layer. Standard library
//     streams were chosen over FILE* deliberately (second audit, brief §7):
//     capture I/O is not a real-time path, std::ofstream/std::ifstream give
//     RAII ownership, explicit stream state for failure propagation, and the
//     same classes accept in-memory stringstreams for testing — the simpler
//     robust design. A writer that has suffered a failed write leaves ok()
//     false and silently ignores further writes; it never produces a
//     supposedly valid truncated capture.
#ifndef CINEMIX_CAPTURE_FILE_H
#define CINEMIX_CAPTURE_FILE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iosfwd>
#include <memory>
#include <string>

#include "cinemix/Types.h"

namespace cinemix {

struct CaptureEvent {
    std::uint64_t timestampUs;
    std::uint8_t direction; // 0 = console→bridge, 1 = bridge→console
    std::uint8_t port;      // 1/2; 0 = broadcast
    MidiMessage message;
};

namespace capture_detail {

constexpr std::size_t kHeaderSize = 12;        // magic(8) + version(4)
constexpr std::size_t kRecordHeaderSize = 11;  // timestamp(8) + direction(1) + port(1) + length(1)
constexpr std::size_t kMaxRecordSize = kRecordHeaderSize + 3; // max message length

constexpr std::uint8_t kDirectionInbound = 0;
constexpr std::uint8_t kDirectionOutbound = 1;
constexpr std::uint8_t kPortBroadcast = 0;
constexpr std::uint8_t kPortLo = 1;
constexpr std::uint8_t kPortHi = 2;

// A fully encoded record, header included.
struct EncodedRecord {
    std::array<std::uint8_t, kMaxRecordSize> bytes{};
    std::uint8_t length = 0; // 11..14
};

enum class DecodeStatus {
    Ok,
    Truncated, // ran out of bytes mid-record
    Malformed, // structurally invalid (bad length, direction, port, …)
};

struct DecodedRecord {
    DecodeStatus status;
    CaptureEvent event;
    std::size_t consumed; // bytes consumed from the buffer (0 unless Ok)
};

// Pure little-endian codecs — the encoded size lives in the type, the
// functions are constexpr, and there is no unchecked output pointer.
constexpr std::array<std::uint8_t, 4> encodeU32Le(std::uint32_t value) noexcept {
    return {static_cast<std::uint8_t>(value & 0xFFu),
            static_cast<std::uint8_t>((value >> 8) & 0xFFu),
            static_cast<std::uint8_t>((value >> 16) & 0xFFu),
            static_cast<std::uint8_t>((value >> 24) & 0xFFu)};
}
constexpr std::array<std::uint8_t, 8> encodeU64Le(std::uint64_t value) noexcept {
    return {static_cast<std::uint8_t>(value & 0xFFu),
            static_cast<std::uint8_t>((value >> 8) & 0xFFu),
            static_cast<std::uint8_t>((value >> 16) & 0xFFu),
            static_cast<std::uint8_t>((value >> 24) & 0xFFu),
            static_cast<std::uint8_t>((value >> 32) & 0xFFu),
            static_cast<std::uint8_t>((value >> 40) & 0xFFu),
            static_cast<std::uint8_t>((value >> 48) & 0xFFu),
            static_cast<std::uint8_t>((value >> 56) & 0xFFu)};
}
constexpr std::uint32_t decodeU32Le(const std::array<std::uint8_t, 4>& bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
}
constexpr std::uint64_t decodeU64Le(const std::array<std::uint8_t, 8>& bytes) noexcept {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
        value |= static_cast<std::uint64_t>(bytes[static_cast<std::size_t>(i)])
                 << (8 * i);
    return value;
}

EncodedRecord encodeRecord(const CaptureEvent& event);
DecodedRecord decodeRecord(const std::uint8_t* data, std::size_t size) noexcept;

} // namespace capture_detail

// ---------------------------------------------------------------------------
// Stream layer — RAII-owned std::ofstream/std::ifstream (or caller-owned
// streams for in-memory testing).

class CaptureWriter {
public:
    // Owning variant: the writer creates and owns its file stream.
    explicit CaptureWriter(const std::string& path);
    // Borrowed variant (tests): the stream is owned by the caller and must
    // outlive the writer.
    explicit CaptureWriter(std::ostream& out);
    ~CaptureWriter();
    CaptureWriter(const CaptureWriter&) = delete;
    CaptureWriter& operator=(const CaptureWriter&) = delete;

    // True while the header has been written and no write has failed.
    // Once a write fails, ok() stays false and writeEvent() becomes a no-op:
    // the writer never pretends to have produced a valid capture.
    bool ok() const { return !failed_ && out_.good(); }

    void writeEvent(const CaptureEvent& event);

private:
    void writeHeader();

    // Declaration order matters: owned_ first, so the reference out_ can bind
    // to *owned_ in the initializer list of the owning constructor.
    std::unique_ptr<std::ofstream> owned_;
    std::ostream& out_;
    bool failed_;
};

class CaptureReader {
public:
    explicit CaptureReader(const std::string& path);
    explicit CaptureReader(std::istream& in);
    ~CaptureReader();
    CaptureReader(const CaptureReader&) = delete;
    CaptureReader& operator=(const CaptureReader&) = delete;

    // False when the stream could not be opened or is not a supported .cmi
    // (wrong magic or unsupported version — versions are never guessed).
    bool ok() const { return in_.good(); }

    // Reads the next event. False at clean EOF; truncated/corrupt records
    // also return false and increment corruptCount().
    bool next(CaptureEvent& out);

    std::size_t corruptCount() const { return corrupt_; }

private:
    // Declaration order matters (see CaptureWriter).
    std::unique_ptr<std::ifstream> owned_;
    std::istream& in_;
    std::size_t corrupt_;
};

} // namespace cinemix

#endif // CINEMIX_CAPTURE_FILE_H
