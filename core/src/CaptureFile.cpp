#include "cinemix/CaptureFile.h"

#include <cstring>

namespace cinemix {

namespace {
const char kMagic[8] = {'C', 'M', 'I', 'X', 'C', 'A', 'P', 'I'};

// Reads 8 bytes: 1 = full, 0 = clean EOF, -1 = partial (corrupt).
inline int readU64Status(FILE* f, uint64_t& v) {
    uint8_t b[8];
    const size_t got = fread(b, 1, 8, f);
    if (got == 8) {
        v = 0;
        for (int i = 0; i < 8; ++i) v |= uint64_t(b[i]) << (8 * i);
        return 1;
    }
    return got == 0 ? 0 : -1;
}
inline void writeU32(FILE* f, uint32_t v) {
    uint8_t b[4] = {uint8_t(v & 0xFF), uint8_t((v >> 8) & 0xFF),
                    uint8_t((v >> 16) & 0xFF), uint8_t((v >> 24) & 0xFF)};
    fwrite(b, 1, 4, f);
}
inline void writeU64(FILE* f, uint64_t v) {
    uint8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = uint8_t((v >> (8 * i)) & 0xFF);
    fwrite(b, 1, 8, f);
}
inline bool readU32(FILE* f, uint32_t& v) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return false;
    v = uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
    return true;
}
} // namespace

CaptureWriter::CaptureWriter(const std::string& path) : file_(nullptr) {
    file_ = fopen(path.c_str(), "wb");
    if (!file_) return;
    fwrite(kMagic, 1, 8, file_);
    writeU32(file_, 1u);
}

CaptureWriter::~CaptureWriter() {
    if (file_) fclose(file_);
}

void CaptureWriter::writeEvent(uint64_t timestampUs, uint8_t direction, uint8_t port,
                               const MidiMessage& message) {
    if (!file_) return;
    writeU64(file_, timestampUs);
    uint8_t hdr[3] = {direction, port, message.length};
    fwrite(hdr, 1, 3, file_);
    fwrite(message.data, 1, message.length, file_);
}

CaptureReader::CaptureReader(const std::string& path) : file_(nullptr), corrupt_(0) {
    file_ = fopen(path.c_str(), "rb");
    if (!file_) return;
    char magic[8];
    if (fread(magic, 1, 8, file_) != 8 || std::memcmp(magic, kMagic, 8) != 0) {
        fclose(file_);
        file_ = nullptr;
        return;
    }
    uint32_t version = 0;
    if (!readU32(file_, version) || version != 1) {
        fclose(file_);
        file_ = nullptr;
        return;
    }
}

CaptureReader::~CaptureReader() {
    if (file_) fclose(file_);
}

bool CaptureReader::next(CaptureEvent& out) {
    if (!file_) return false;
    // A clean stream always ends on a record boundary: 0 bytes read = EOF,
    // 1..7 bytes read = truncated record = corruption.
    const int ts = readU64Status(file_, out.timestampUs);
    if (ts == 0) return false;
    if (ts < 0) { ++corrupt_; return false; }
    uint8_t hdr[3];
    if (fread(hdr, 1, 3, file_) != 3) { ++corrupt_; return false; }
    out.direction = hdr[0];
    out.port = hdr[1];
    const uint8_t len = hdr[2];
    if (len > 3) { ++corrupt_; return false; }
    out.message.length = len;
    out.message.port = out.port;
    out.message.data[0] = out.message.data[1] = out.message.data[2] = 0;
    if (len > 0 && fread(out.message.data, 1, len, file_) != len) { ++corrupt_; return false; }
    return true;
}

} // namespace cinemix
