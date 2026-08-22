// CaptureFile — the .cmi capture/replay format used for hardware-free
// testing and diagnostics (docs/TESTING.md).
//
// Little-endian binary:
//   magic   "CMIXCAPI" (8 bytes)
//   u32     version = 1
//   events, repeated:
//     u64  timestampUs  (microseconds since capture start)
//     u8   direction    (0 = inbound/console→bridge, 1 = outbound/bridge→console)
//     u8   port         (1/2; 0 = broadcast)
//     u8   length       (1..3)
//     u8[] bytes
//
// Streams may be truncated mid-event; the reader reports the last valid
// event offset.
#ifndef CINEMIX_CAPTURE_FILE_H
#define CINEMIX_CAPTURE_FILE_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "cinemix/Types.h"

namespace cinemix {

class CaptureWriter {
public:
    explicit CaptureWriter(const std::string& path);
    ~CaptureWriter();
    bool ok() const { return file_ != nullptr; }
    void writeEvent(uint64_t timestampUs, uint8_t direction, uint8_t port,
                    const MidiMessage& message);
private:
    FILE* file_;
};

struct CaptureEvent {
    uint64_t timestampUs;
    uint8_t direction;
    uint8_t port;
    MidiMessage message;
};

class CaptureReader {
public:
    explicit CaptureReader(const std::string& path);
    ~CaptureReader();
    bool ok() const { return file_ != nullptr; }
    // Reads the next event; false at EOF or on a corrupt record (corrupt
    // events stop the stream and are reported via corruptCount()).
    bool next(CaptureEvent& out);
    size_t corruptCount() const { return corrupt_; }
private:
    FILE* file_;
    size_t corrupt_;
};

} // namespace cinemix

#endif // CINEMIX_CAPTURE_FILE_H
