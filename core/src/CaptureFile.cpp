#include "cinemix/CaptureFile.h"

#include <cstring>

namespace cinemix {

namespace {

constexpr std::array<char, 8> kMagic = {'C', 'M', 'I', 'X', 'C', 'A', 'P', 'I'};
constexpr std::uint32_t kVersion = 1;

} // namespace

namespace capture_detail {

void writeU32Le(std::uint8_t* out, std::uint32_t value) noexcept {
    for (int i = 0; i < 4; ++i)
        out[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
}

void writeU64Le(std::uint8_t* out, std::uint64_t value) noexcept {
    for (int i = 0; i < 8; ++i)
        out[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
}

std::uint32_t readU32Le(const std::uint8_t* in) noexcept {
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(in[i]) << (8 * i);
    return value;
}

std::uint64_t readU64Le(const std::uint8_t* in) noexcept {
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
        value |= static_cast<std::uint64_t>(in[i]) << (8 * i);
    return value;
}

EncodedRecord encodeRecord(const CaptureEvent& event) {
    EncodedRecord record;
    writeU64Le(record.bytes.data(), event.timestampUs);
    record.bytes[8] = event.direction;
    record.bytes[9] = event.port;
    const std::uint8_t length = event.message.length > 3 ? 3 : event.message.length;
    record.bytes[10] = length;
    for (std::uint8_t i = 0; i < length; ++i) record.bytes[11 + i] = event.message.data[i];
    record.length = static_cast<std::uint8_t>(kRecordHeaderSize + length);
    return record;
}

DecodedRecord decodeRecord(const std::uint8_t* data, std::size_t size) noexcept {
    DecodedRecord result;
    result.status = DecodeStatus::Truncated;
    result.consumed = 0;
    if (size < kRecordHeaderSize) return result;

    const std::uint8_t length = data[10];
    if (length > 3) {
        result.status = DecodeStatus::Malformed;
        return result;
    }
    if (size < kRecordHeaderSize + length) return result; // truncated

    result.event.timestampUs = readU64Le(data);
    result.event.direction = data[8];
    result.event.port = data[9];
    result.event.message.length = length;
    result.event.message.port = data[9];
    result.event.message.data = {};
    for (std::uint8_t i = 0; i < length; ++i) result.event.message.data[i] = data[11 + i];
    result.consumed = kRecordHeaderSize + length;
    result.status = DecodeStatus::Ok;
    return result;
}

} // namespace capture_detail

// ---------------------------------------------------------------------------
// Writer

CaptureWriter::CaptureWriter(const std::string& path) {
    FILE* file = fopen(path.c_str(), "wb");
    if (!file) return;
    file_.reset(file);

    // Header: magic + version (little-endian).
    std::uint8_t header[capture_detail::kHeaderSize];
    std::memcpy(header, kMagic.data(), kMagic.size());
    capture_detail::writeU32Le(header + 8, kVersion);
    if (fwrite(header, 1, sizeof(header), file_.get()) != sizeof(header)) {
        file_.reset(); // failed to write the header: unusable capture
    }
}

CaptureWriter::~CaptureWriter() = default;

void CaptureWriter::writeEvent(const CaptureEvent& event) {
    if (!file_) return;
    const capture_detail::EncodedRecord record = capture_detail::encodeRecord(event);
    fwrite(record.bytes.data(), 1, record.length, file_.get());
}

// ---------------------------------------------------------------------------
// Reader

CaptureReader::CaptureReader(const std::string& path) : corrupt_(0) {
    FILE* file = fopen(path.c_str(), "rb");
    if (!file) return;
    file_.reset(file);

    std::array<char, 8> magic{};
    if (fread(magic.data(), 1, magic.size(), file_.get()) != magic.size() ||
        std::memcmp(magic.data(), kMagic.data(), kMagic.size()) != 0) {
        file_.reset(); // not a .cmi capture
        return;
    }
    std::array<std::uint8_t, 4> versionBytes{};
    if (fread(versionBytes.data(), 1, versionBytes.size(), file_.get()) !=
        versionBytes.size()) {
        file_.reset(); // truncated header
        return;
    }
    const std::uint32_t version = capture_detail::readU32Le(versionBytes.data());
    if (version != kVersion) {
        // Unsupported versions are rejected, never guessed.
        file_.reset();
        return;
    }
}

CaptureReader::~CaptureReader() = default;

template <std::size_t N>
CaptureReader::RawRead<N> CaptureReader::readBytes() {
    RawRead<N> read;
    read.status = RawRead<N>::Status::Eof;
    const std::size_t got = fread(read.bytes.data(), 1, N, file_.get());
    if (got == N) read.status = RawRead<N>::Status::Ok;
    else if (got > 0) read.status = RawRead<N>::Status::Partial;
    return read;
}

bool CaptureReader::next(CaptureEvent& out) {
    if (!file_) return false;

    // A clean stream always ends on a record boundary: 0 bytes read = EOF,
    // 1..N-1 bytes read = truncated record = corruption.
    const RawRead<8> timestamp = readBytes<8>();
    if (timestamp.status == RawRead<8>::Status::Eof) return false;
    if (timestamp.status == RawRead<8>::Status::Partial) { ++corrupt_; return false; }

    const RawRead<3> header = readBytes<3>();
    if (header.status != RawRead<3>::Status::Ok) { ++corrupt_; return false; }

    const std::uint8_t length = header.bytes[2];
    if (length > 3) { ++corrupt_; return false; }
    std::array<std::uint8_t, 3> payload{};
    if (length > 0) {
        const std::size_t got = fread(payload.data(), 1, length, file_.get());
        if (got != length) { ++corrupt_; return false; }
    }

    // Assemble the full record and run it through the pure codec (bounds and
    // structure validation live there).
    std::array<std::uint8_t, capture_detail::kMaxRecordSize> buffer{};
    for (std::size_t i = 0; i < 8; ++i) buffer[i] = timestamp.bytes[i];
    for (std::size_t i = 0; i < 3; ++i) buffer[8 + i] = header.bytes[i];
    for (std::uint8_t i = 0; i < length; ++i) buffer[11 + i] = payload[i];

    const capture_detail::DecodedRecord record =
        capture_detail::decodeRecord(buffer.data(), capture_detail::kRecordHeaderSize + length);
    if (record.status != capture_detail::DecodeStatus::Ok) {
        ++corrupt_;
        return false;
    }
    out = record.event;
    return true;
}

} // namespace cinemix
