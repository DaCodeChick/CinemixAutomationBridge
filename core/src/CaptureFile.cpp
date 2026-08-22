#include "cinemix/CaptureFile.h"

#include <cstring>

namespace cinemix {

namespace {

constexpr std::array<char, 8> kMagic = {'C', 'M', 'I', 'X', 'C', 'A', 'P', 'I'};
constexpr std::uint32_t kVersion = 1;

} // namespace

namespace capture_detail {

EncodedRecord encodeRecord(const CaptureEvent& event) {
    EncodedRecord record;
    const std::array<std::uint8_t, 8> timestamp = encodeU64Le(event.timestampUs);
    for (std::size_t i = 0; i < timestamp.size(); ++i) record.bytes[i] = timestamp[i];
    record.bytes[8] = event.direction;
    record.bytes[9] = event.port;
    const std::uint8_t length = event.message.length > 3 ? 3 : event.message.length;
    record.bytes[10] = length;
    for (std::uint8_t i = 0; i < length; ++i)
        record.bytes[11 + i] = event.message.data[i];
    record.length = static_cast<std::uint8_t>(kRecordHeaderSize + length);
    return record;
}

DecodedRecord decodeRecord(const std::uint8_t* data, std::size_t size) noexcept {
    DecodedRecord result;
    result.status = DecodeStatus::Truncated;
    result.consumed = 0;
    if (size < kRecordHeaderSize) return result;

    // Complete field validation — Malformed means what it says.
    const std::uint8_t direction = data[8];
    const std::uint8_t port = data[9];
    const std::uint8_t length = data[10];
    const bool directionValid =
        (direction == kDirectionInbound || direction == kDirectionOutbound);
    const bool portValid = (port == kPortBroadcast || port == kPortLo || port == kPortHi);
    if (!directionValid || !portValid || length > 3) {
        result.status = DecodeStatus::Malformed;
        return result;
    }
    if (size < kRecordHeaderSize + length) return result; // truncated payload

    std::array<std::uint8_t, 8> timestampBytes{};
    for (std::size_t i = 0; i < timestampBytes.size(); ++i) timestampBytes[i] = data[i];
    result.event.timestampUs = decodeU64Le(timestampBytes);
    result.event.direction = direction;
    result.event.port = port;
    result.event.message.length = length;
    result.event.message.port = port;
    result.event.message.data = {};
    for (std::uint8_t i = 0; i < length; ++i)
        result.event.message.data[i] = data[11 + i];
    result.consumed = kRecordHeaderSize + length;
    result.status = DecodeStatus::Ok;
    return result;
}

} // namespace capture_detail

// ---------------------------------------------------------------------------
// Writer

CaptureWriter::CaptureWriter(const std::string& path)
    : owned_(new std::ofstream(path, std::ios::binary)), out_(*owned_), failed_(false) {
    if (owned_->is_open()) writeHeader();
    else failed_ = true; // cannot open: writer is failed from the start
}

CaptureWriter::CaptureWriter(std::ostream& out)
    : owned_(), out_(out), failed_(false) {
    writeHeader();
}

CaptureWriter::~CaptureWriter() = default;

void CaptureWriter::writeHeader() {
    // Magic + version (little-endian).
    std::array<std::uint8_t, capture_detail::kHeaderSize> header{};
    std::memcpy(header.data(), kMagic.data(), kMagic.size());
    const std::array<std::uint8_t, 4> version = capture_detail::encodeU32Le(kVersion);
    for (std::size_t i = 0; i < version.size(); ++i) header[8 + i] = version[i];
    out_.write(reinterpret_cast<const char*>(header.data()),
               static_cast<std::streamsize>(header.size()));
    // flush() is REQUIRED for correct failure semantics: a buffered stream
    // hides short writes and I/O errors until the buffer is flushed, so an
    // unwritten flush would let ok() keep reporting success.
    out_.flush();
    if (!out_) failed_ = true;
}

void CaptureWriter::writeEvent(const CaptureEvent& event) {
    // After a failed write the writer stays failed: no misleading "valid"
    // truncated captures, and later writes are predictable no-ops.
    if (!ok()) {
        failed_ = true;
        return;
    }
    const capture_detail::EncodedRecord record = capture_detail::encodeRecord(event);
    out_.write(reinterpret_cast<const char*>(record.bytes.data()),
               static_cast<std::streamsize>(record.length));
    out_.flush(); // see writeHeader(): buffered streams hide write failures
    if (!out_) failed_ = true;
}

// ---------------------------------------------------------------------------
// Reader

CaptureReader::CaptureReader(const std::string& path)
    : owned_(new std::ifstream(path, std::ios::binary)), in_(*owned_), corrupt_(0) {
    if (!in_.good()) return;

    std::array<char, 8> magic{};
    in_.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in_ || std::memcmp(magic.data(), kMagic.data(), kMagic.size()) != 0) {
        in_.setstate(std::ios::failbit); // not a .cmi capture
        return;
    }
    std::array<std::uint8_t, 4> versionBytes{};
    in_.read(reinterpret_cast<char*>(versionBytes.data()),
             static_cast<std::streamsize>(versionBytes.size()));
    if (!in_) return; // truncated header

    const std::uint32_t version = capture_detail::decodeU32Le(versionBytes);
    if (version != kVersion) {
        // Unsupported versions are rejected, never guessed.
        in_.setstate(std::ios::failbit);
        return;
    }
}

CaptureReader::CaptureReader(std::istream& in) : in_(in), corrupt_(0) {
    if (!in_.good()) return;

    std::array<char, 8> magic{};
    in_.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in_ || std::memcmp(magic.data(), kMagic.data(), kMagic.size()) != 0) {
        in_.setstate(std::ios::failbit);
        return;
    }
    std::array<std::uint8_t, 4> versionBytes{};
    in_.read(reinterpret_cast<char*>(versionBytes.data()),
             static_cast<std::streamsize>(versionBytes.size()));
    if (!in_) return;

    const std::uint32_t version = capture_detail::decodeU32Le(versionBytes);
    if (version != kVersion) {
        in_.setstate(std::ios::failbit);
        return;
    }
}

CaptureReader::~CaptureReader() = default;

bool CaptureReader::next(CaptureEvent& out) {
    if (!in_.good()) return false;

    // Assemble one record; read() distinguishes clean EOF (0 bytes) from a
    // truncated record (1..N-1 bytes) via gcount.
    std::array<std::uint8_t, capture_detail::kMaxRecordSize> buffer{};
    std::size_t got = 0;

    // Timestamp (8).
    in_.read(reinterpret_cast<char*>(buffer.data()), 8);
    const std::streamsize tsGot = in_.gcount();
    if (tsGot == 0 && in_.eof()) return false; // clean EOF
    if (tsGot != 8) { ++corrupt_; return false; }
    got += 8;

    // Record header (3).
    in_.read(reinterpret_cast<char*>(buffer.data() + got), 3);
    if (in_.gcount() != 3) { ++corrupt_; return false; }
    got += 3;

    // Payload.
    const std::uint8_t length = buffer[10];
    if (length > 3) { ++corrupt_; return false; }
    if (length > 0) {
        in_.read(reinterpret_cast<char*>(buffer.data() + got), length);
        if (in_.gcount() != length) { ++corrupt_; return false; }
    }
    got += length;

    // Full structural validation in the pure codec.
    const capture_detail::DecodedRecord record =
        capture_detail::decodeRecord(buffer.data(), got);
    if (record.status != capture_detail::DecodeStatus::Ok) {
        ++corrupt_;
        return false;
    }
    out = record.event;
    return true;
}

} // namespace cinemix
