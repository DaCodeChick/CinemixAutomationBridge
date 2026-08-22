#include "cinemix/CaptureFile.h"

#include <cassert>
#include <cstring>

namespace cinemix {

namespace {

constexpr std::array<char, 8> kMagic = {'C', 'M', 'I', 'X', 'C', 'A', 'P', 'I'};
constexpr std::uint32_t kVersion = 1;

} // namespace

namespace capture_detail {

EncodedRecord encodeRecord(const CaptureEvent& event) {
    // Precondition: the message payload is 1..3 bytes (the format cannot
    // represent other lengths — the decoder rejects them). Oversized input is
    // defensively clamped to 3; undersized input is a caller bug.
    assert(event.message.length >= 1);
    const std::uint8_t length =
        event.message.length > kMaxMessageLength ? kMaxMessageLength
                                                 : event.message.length;

    EncodedRecord record;
    const std::array<std::uint8_t, kTimestampSize> timestamp = encodeU64Le(event.timestampUs);
    for (std::size_t i = 0; i < timestamp.size(); ++i) record.bytes[i] = timestamp[i];
    record.bytes[kDirectionOffset] = event.direction;
    record.bytes[kPortOffset] = event.port;
    record.bytes[kLengthOffset] = length;
    for (std::uint8_t i = 0; i < length; ++i)
        record.bytes[kPayloadOffset + i] = event.message.data[i];
    record.length = static_cast<std::uint8_t>(kRecordHeaderSize + length);
    return record;
}

DecodedRecord decodeRecord(const std::uint8_t* data, std::size_t size) noexcept {
    DecodedRecord result;
    result.status = DecodeStatus::Truncated;
    result.consumed = 0;
    if (size < kRecordHeaderSize) return result;

    // Complete field validation — Malformed means what it says. Every field
    // the format constrains is enforced: direction ∈ {0,1}, port ∈ {0,1,2},
    // payload length ∈ 1..3.
    const std::uint8_t direction = data[kDirectionOffset];
    const std::uint8_t port = data[kPortOffset];
    const std::uint8_t length = data[kLengthOffset];
    const bool directionValid =
        (direction == kDirectionInbound || direction == kDirectionOutbound);
    const bool portValid = (port == kPortBroadcast || port == kPortLo || port == kPortHi);
    const bool lengthValid = (length >= 1 && length <= kMaxMessageLength);
    if (!directionValid || !portValid || !lengthValid) {
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
        result.event.message.data[i] = data[kPayloadOffset + i];
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

void CaptureWriter::writeHeader() {
    // Header = magic bytes, then the little-endian version. The two parts are
    // written directly — no synthetic assembly buffer, no raw offsets.
    out_.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    const std::array<std::uint8_t, 4> version = capture_detail::encodeU32Le(kVersion);
    out_.write(reinterpret_cast<const char*>(version.data()),
               static_cast<std::streamsize>(version.size()));
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
    // Structurally invalid payloads (0 or >3 bytes) are caller bugs and have
    // no format representation; skip them so the writer never emits a record
    // the decoder would reject. This is not an I/O failure: ok() stays true.
    if (event.message.length < 1 ||
        event.message.length > capture_detail::kMaxMessageLength)
        return;
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

bool CaptureReader::next(CaptureEvent& out) {
    if (!in_.good()) return false;

    // Assemble one record; read() distinguishes clean EOF (0 bytes) from a
    // truncated record (1..N-1 bytes) via gcount. Offsets come from the
    // capture_detail layout constants, never raw numbers.
    std::array<std::uint8_t, capture_detail::kMaxRecordSize> buffer{};
    std::size_t got = 0;

    // Timestamp.
    in_.read(reinterpret_cast<char*>(buffer.data()),
             static_cast<std::streamsize>(capture_detail::kTimestampSize));
    const std::streamsize tsGot = in_.gcount();
    if (tsGot == 0 && in_.eof()) return false; // clean EOF
    if (tsGot != static_cast<std::streamsize>(capture_detail::kTimestampSize)) {
        ++corrupt_;
        return false;
    }
    got += capture_detail::kTimestampSize;

    // Direction + port + length.
    const std::size_t fixedHeader = capture_detail::kRecordHeaderSize -
                                    capture_detail::kTimestampSize; // 3
    in_.read(reinterpret_cast<char*>(buffer.data() + got),
             static_cast<std::streamsize>(fixedHeader));
    if (in_.gcount() != static_cast<std::streamsize>(fixedHeader)) {
        ++corrupt_;
        return false;
    }
    got += fixedHeader;

    // Payload.
    const std::uint8_t length = buffer[capture_detail::kLengthOffset];
    if (length < 1 || length > capture_detail::kMaxMessageLength) {
        ++corrupt_;
        return false;
    }
    in_.read(reinterpret_cast<char*>(buffer.data() + got), length);
    if (in_.gcount() != length) { ++corrupt_; return false; }
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
