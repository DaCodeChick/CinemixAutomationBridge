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
// Split into two layers:
//   * capture_detail — pure record codec (byte arrays in/out, no file I/O):
//     unit-testable in memory, little-endian by explicit shifts (no host
//     endianness assumptions).
//   * CaptureWriter/CaptureReader — the file layer: RAII FILE ownership
//     (unique_ptr with an fclose deleter), explicit header/version handling,
//     clean-EOF vs truncated-record distinction.
#ifndef CINEMIX_CAPTURE_FILE_H
#define CINEMIX_CAPTURE_FILE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
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

// A fully encoded record, header included.
struct EncodedRecord {
    std::array<std::uint8_t, kMaxRecordSize> bytes{};
    std::uint8_t length = 0; // 11..14
};

enum class DecodeStatus {
    Ok,
    Truncated, // ran out of bytes mid-record
    Malformed, // structurally invalid (bad length, direction, …)
};

struct DecodedRecord {
    DecodeStatus status;
    CaptureEvent event;
    std::size_t consumed; // bytes consumed from the buffer (0 unless Ok)
};

// Pure codecs — little-endian via explicit shifts.
void writeU32Le(std::uint8_t* out, std::uint32_t value) noexcept;
void writeU64Le(std::uint8_t* out, std::uint64_t value) noexcept;
std::uint32_t readU32Le(const std::uint8_t* in) noexcept;
std::uint64_t readU64Le(const std::uint8_t* in) noexcept;

EncodedRecord encodeRecord(const CaptureEvent& event);
DecodedRecord decodeRecord(const std::uint8_t* data, std::size_t size) noexcept;

} // namespace capture_detail

// ---------------------------------------------------------------------------
// File layer — explicit, RAII-managed FILE ownership.

class CaptureWriter {
public:
    explicit CaptureWriter(const std::string& path);
    ~CaptureWriter();
    CaptureWriter(const CaptureWriter&) = delete;
    CaptureWriter& operator=(const CaptureWriter&) = delete;

    bool ok() const { return file_ != nullptr; }
    // Appends one record. No-op when the writer failed to open.
    void writeEvent(const CaptureEvent& event);

private:
    struct FileCloser {
        void operator()(FILE* file) const noexcept {
            if (file) fclose(file);
        }
    };
    std::unique_ptr<FILE, FileCloser> file_;
};

class CaptureReader {
public:
    explicit CaptureReader(const std::string& path);
    ~CaptureReader();
    CaptureReader(const CaptureReader&) = delete;
    CaptureReader& operator=(const CaptureReader&) = delete;

    // False when the file could not be opened or is not a supported .cmi
    // (wrong magic or unsupported version — versions are NOT guessed).
    bool ok() const { return file_ != nullptr; }

    // Reads the next event. False at clean EOF; truncated/corrupt records
    // also return false and increment corruptCount().
    bool next(CaptureEvent& out);

    std::size_t corruptCount() const { return corrupt_; }

private:
    struct FileCloser {
        void operator()(FILE* file) const noexcept {
            if (file) fclose(file);
        }
    };
    // Reads exactly `count` bytes: Ok, Eof (0 bytes) or Partial (truncation).
    template <std::size_t N>
    struct RawRead {
        enum class Status { Ok, Eof, Partial };
        Status status;
        std::array<std::uint8_t, N> bytes{};
    };
    template <std::size_t N>
    RawRead<N> readBytes();

    std::unique_ptr<FILE, FileCloser> file_;
    std::size_t corrupt_;
};

} // namespace cinemix

#endif // CINEMIX_CAPTURE_FILE_H
