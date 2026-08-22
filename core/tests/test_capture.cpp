// CaptureFile (.cmi) tests — record codec + file round trips.
#include <cstdio>
#include <string>
#include <sstream>
#include <type_traits>
#include <unistd.h>

#include "TestFramework.h"
#include "cinemix/CaptureFile.h"

using namespace cinemix;

namespace {

std::string tempPath() {
    char tmpl[] = "/tmp/cinemix_capture_XXXXXX";
    const int fd = mkstemp(tmpl);
    if (fd >= 0) close(fd);
    return std::string(tmpl) + ".cmi";
}

CaptureEvent makeEvent(std::uint64_t ts, std::uint8_t direction, std::uint8_t port,
                       const MidiMessage& message) {
    CaptureEvent event;
    event.timestampUs = ts;
    event.direction = direction;
    event.port = port;
    event.message = message;
    return event;
}

TEST_CASE("capture codec: record round trip is lossless") {
    const MidiMessage cc = MidiMessage::controlChange(1, 0, 63, 1);
    const CaptureEvent original = makeEvent(123456789, 1, 1, cc);

    const capture_detail::EncodedRecord encoded = capture_detail::encodeRecord(original);
    CHECK_EQ(static_cast<int>(encoded.length), 14); // header 11 + 3 bytes

    const capture_detail::DecodedRecord decoded =
        capture_detail::decodeRecord(encoded.bytes.data(), encoded.length);
    CHECK(decoded.status == capture_detail::DecodeStatus::Ok);
    CHECK_EQ(decoded.consumed, static_cast<size_t>(encoded.length));
    CHECK_EQ(decoded.event.timestampUs, original.timestampUs);
    CHECK_EQ(static_cast<int>(decoded.event.direction), 1);
    CHECK_EQ(static_cast<int>(decoded.event.port), 1);
    CHECK_EQ(static_cast<int>(decoded.event.message.length), 3);
    CHECK_EQ(static_cast<int>(decoded.event.message.data[0]), static_cast<int>(cc.data[0]));
    CHECK_EQ(static_cast<int>(decoded.event.message.data[2]), 63);
}

TEST_CASE("capture codec: little-endian primitives (size in the type)") {
    constexpr std::array<std::uint8_t, 8> encoded64 =
        capture_detail::encodeU64Le(0x0102030405060708ULL);
    CHECK_EQ(capture_detail::decodeU64Le(encoded64), 0x0102030405060708ULL);
    CHECK_EQ(static_cast<int>(encoded64[0]), 0x08); // least significant byte first
    static_assert(encoded64.size() == 8, "u64 encodes to 8 bytes");

    constexpr std::array<std::uint8_t, 4> encoded32 =
        capture_detail::encodeU32Le(0xDEADBEEFu);
    CHECK_EQ(capture_detail::decodeU32Le(encoded32), 0xDEADBEEFu);
    CHECK_EQ(static_cast<int>(encoded32[0]), 0xEF);
    static_assert(encoded32.size() == 4, "u32 encodes to 4 bytes");
}

TEST_CASE("capture codec: truncated and malformed records rejected") {
    const MidiMessage cc = MidiMessage::controlChange(1, 0, 63, 1);
    const capture_detail::EncodedRecord encoded =
        capture_detail::encodeRecord(makeEvent(1, 0, 0, cc));

    // Truncated: one byte short.
    capture_detail::DecodedRecord decoded =
        capture_detail::decodeRecord(encoded.bytes.data(), encoded.length - 1);
    CHECK(decoded.status == capture_detail::DecodeStatus::Truncated);
    CHECK_EQ(decoded.consumed, static_cast<size_t>(0));

    // Malformed: length byte says 4 (impossible).
    std::array<std::uint8_t, capture_detail::kMaxRecordSize> bad{};
    for (size_t i = 0; i < 11; ++i) bad[i] = encoded.bytes[i];
    bad[10] = 4;
    decoded = capture_detail::decodeRecord(bad.data(), 11);
    CHECK(decoded.status == capture_detail::DecodeStatus::Malformed);

    // Truncated at the header boundary.
    decoded = capture_detail::decodeRecord(encoded.bytes.data(), 5);
    CHECK(decoded.status == capture_detail::DecodeStatus::Truncated);
}

TEST_CASE("capture: write and read back events") {
    const std::string path = tempPath();
    {
        CaptureWriter writer(path);
        CHECK(writer.ok());
        writer.writeEvent(makeEvent(1000, 1, 1, MidiMessage::controlChange(1, 0, 63, 1)));
        writer.writeEvent(makeEvent(2000, 1, 0, MidiMessage::controlChange(5, 127, 127, 0)));
        writer.writeEvent(makeEvent(3000, 1, 0, MidiMessage::systemReset(0)));
    }
    {
        CaptureReader reader(path);
        CHECK(reader.ok());
        CaptureEvent event;
        CHECK(reader.next(event));
        CHECK_EQ(event.timestampUs, static_cast<std::uint64_t>(1000));
        CHECK_EQ(static_cast<int>(event.direction), 1);
        CHECK_EQ(static_cast<int>(event.port), 1);
        CHECK_EQ(static_cast<int>(event.message.data[1]), 0);
        CHECK_EQ(static_cast<int>(event.message.data[2]), 63);

        CHECK(reader.next(event));
        CHECK_EQ(event.timestampUs, static_cast<std::uint64_t>(2000));
        CHECK_EQ(static_cast<int>(event.message.data[2]), 127);

        CHECK(reader.next(event));
        CHECK(event.message.isSystemReset());

        CHECK(!reader.next(event)); // clean EOF
        CHECK_EQ(reader.corruptCount(), static_cast<size_t>(0));
    }
    std::remove(path.c_str());
}

TEST_CASE("capture: corrupted magic rejected") {
    const std::string path = tempPath();
    {
        FILE* f = fopen(path.c_str(), "wb");
        const char garbage[16] = "NOTAMIDIFILE!!!";
        fwrite(garbage, 1, 16, f);
        fclose(f);
    }
    CaptureReader reader(path);
    CHECK(!reader.ok());
    std::remove(path.c_str());
}

TEST_CASE("capture: unsupported version rejected") {
    const std::string path = tempPath();
    {
        FILE* f = fopen(path.c_str(), "wb");
        const char magic[8] = {'C', 'M', 'I', 'X', 'C', 'A', 'P', 'I'};
        const std::uint8_t version[4] = {9, 0, 0, 0}; // version 9: unknown
        fwrite(magic, 1, 8, f);
        fwrite(version, 1, 4, f);
        fclose(f);
    }
    CaptureReader reader(path);
    CHECK(!reader.ok()); // never guessed
    std::remove(path.c_str());
}

TEST_CASE("capture: truncated event stream stops cleanly") {
    const std::string path = tempPath();
    {
        CaptureWriter writer(path);
        writer.writeEvent(makeEvent(1000, 1, 1, MidiMessage::controlChange(1, 0, 63, 1)));
    }
    // Append a partial event record.
    {
        FILE* f = fopen(path.c_str(), "ab");
        unsigned char partial[6] = {1, 2, 3, 4, 5, 6}; // too short for ts+hdr+data
        fwrite(partial, 1, 6, f);
        fclose(f);
    }
    CaptureReader reader(path);
    CHECK(reader.ok());
    CaptureEvent event;
    CHECK(reader.next(event));  // the complete event
    CHECK(!reader.next(event)); // truncated record → stop
    CHECK(reader.corruptCount() > 0);
    std::remove(path.c_str());
}

TEST_CASE("capture codec: invalid direction and port fields are Malformed") {
    const MidiMessage cc = MidiMessage::controlChange(1, 0, 63, 1);
    const capture_detail::EncodedRecord encoded =
        capture_detail::encodeRecord(makeEvent(1, 0, 0, cc));

    // direction = 7: invalid.
    std::array<std::uint8_t, capture_detail::kMaxRecordSize> bad{};
    for (size_t i = 0; i < 11; ++i) bad[i] = encoded.bytes[i];
    bad[8] = 7;
    capture_detail::DecodedRecord decoded =
        capture_detail::decodeRecord(bad.data(), 11 + 3);
    CHECK(decoded.status == capture_detail::DecodeStatus::Malformed);

    // port = 9: invalid.
    for (size_t i = 0; i < 11; ++i) bad[i] = encoded.bytes[i];
    bad[9] = 9;
    decoded = capture_detail::decodeRecord(bad.data(), 11 + 3);
    CHECK(decoded.status == capture_detail::DecodeStatus::Malformed);
}

TEST_CASE("capture: failed writes leave the writer failed, not healthy") {
    // /dev/full: every write fails with ENOSPC on Linux (POSIX).
    if (FILE* probe = fopen("/dev/full", "wb")) {
        fclose(probe);
        {
            CaptureWriter writer("/dev/full");
            // Opening succeeds; the header write fails.
            CHECK(!writer.ok());
            // Later writes are predictable no-ops that never revive ok().
            writer.writeEvent(makeEvent(1, 1, 1, MidiMessage::controlChange(1, 0, 63, 1)));
            CHECK(!writer.ok());
        }
    }
}

TEST_CASE("capture: in-memory stream round trip (borrowed-stream API)") {
    std::stringstream buffer(std::ios::in | std::ios::out | std::ios::binary);
    {
        CaptureWriter writer(buffer);
        CHECK(writer.ok());
        writer.writeEvent(makeEvent(1000, 1, 1, MidiMessage::controlChange(1, 0, 63, 1)));
        writer.writeEvent(makeEvent(2000, 0, 0, MidiMessage::systemReset(0)));
    }
    buffer.seekg(0);
    {
        CaptureReader reader(buffer);
        CHECK(reader.ok());
        CaptureEvent event;
        CHECK(reader.next(event));
        CHECK_EQ(static_cast<int>(event.message.data[2]), 63);
        CHECK(reader.next(event));
        CHECK(event.message.isSystemReset());
        CHECK(!reader.next(event)); // clean EOF
        CHECK_EQ(reader.corruptCount(), static_cast<size_t>(0));
    }
}

TEST_CASE("capture codec: zero-length payload is Malformed") {
    const MidiMessage cc = MidiMessage::controlChange(1, 0, 63, 1);
    const capture_detail::EncodedRecord encoded =
        capture_detail::encodeRecord(makeEvent(1, 0, 0, cc));
    std::array<std::uint8_t, capture_detail::kMaxRecordSize> bad{};
    for (size_t i = 0; i < 11; ++i) bad[i] = encoded.bytes[i];
    bad[10] = 0; // length 0: format requires 1..3
    capture_detail::DecodedRecord decoded =
        capture_detail::decodeRecord(bad.data(), capture_detail::kRecordHeaderSize);
    CHECK(decoded.status == capture_detail::DecodeStatus::Malformed);
}

TEST_CASE("capture codec: layout constants are self-consistent") {
    static_assert(capture_detail::kDirectionOffset == 8, "direction offset");
    static_assert(capture_detail::kPortOffset == 9, "port offset");
    static_assert(capture_detail::kLengthOffset == 10, "length offset");
    static_assert(capture_detail::kPayloadOffset == 11, "payload offset");
    static_assert(capture_detail::kRecordHeaderSize == 11, "record header size");
    static_assert(capture_detail::kMaxRecordSize == 14, "max record size");
    CHECK(true);
}

TEST_CASE("capture: structurally invalid payloads are skipped, not written") {
    std::stringstream buffer(std::ios::in | std::ios::out | std::ios::binary);
    {
        CaptureWriter writer(buffer);
        CHECK(writer.ok());
        CaptureEvent bad = makeEvent(1, 1, 1, MidiMessage::controlChange(1, 0, 63, 1));
        bad.message.length = 0; // caller bug: no format representation
        writer.writeEvent(bad);
        writer.writeEvent(makeEvent(2, 1, 1, MidiMessage::controlChange(1, 0, 63, 1)));
        CHECK(writer.ok()); // skipping is not an I/O failure
    }
    buffer.seekg(0);
    CaptureReader reader(buffer);
    CHECK(reader.ok());
    CaptureEvent event;
    CHECK(reader.next(event));
    CHECK_EQ(event.timestampUs, static_cast<std::uint64_t>(2)); // only the valid record
    CHECK(!reader.next(event));
    CHECK_EQ(reader.corruptCount(), static_cast<size_t>(0));
}

TEST_CASE("capture writer: rejects records the reader would reject") {
    std::stringstream buffer(std::ios::in | std::ios::out | std::ios::binary);
    {
        CaptureWriter writer(buffer);
        CHECK(writer.ok());

        CaptureEvent bad = makeEvent(1, 1, 1, MidiMessage::controlChange(1, 0, 63, 1));
        bad.direction = 9; // invalid
        writer.writeEvent(bad);

        bad = makeEvent(2, 1, 1, MidiMessage::controlChange(1, 0, 63, 1));
        bad.port = 9; // invalid
        writer.writeEvent(bad);

        bad = makeEvent(3, 1, 1, MidiMessage::controlChange(1, 0, 63, 1));
        bad.message.length = 0; // invalid
        writer.writeEvent(bad);

        writer.writeEvent(makeEvent(4, 1, 1, MidiMessage::controlChange(1, 0, 63, 1)));
        CHECK(writer.ok()); // rejection is not an I/O failure
    }
    // Only the single valid record survives, and the reader accepts it.
    buffer.seekg(0);
    CaptureReader reader(buffer);
    CHECK(reader.ok());
    CaptureEvent event;
    CHECK(reader.next(event));
    CHECK_EQ(event.timestampUs, static_cast<std::uint64_t>(4));
    CHECK(!reader.next(event));
    CHECK_EQ(reader.corruptCount(), static_cast<size_t>(0));
}

TEST_CASE("capture writer: copy/move contract is explicit") {
    static_assert(!std::is_copy_constructible<CaptureWriter>::value, "writer not copyable");
    static_assert(!std::is_copy_assignable<CaptureWriter>::value, "writer not copy-assignable");
    static_assert(!std::is_move_constructible<CaptureWriter>::value, "writer not movable");
    static_assert(!std::is_move_assignable<CaptureWriter>::value, "writer not move-assignable");
    static_assert(!std::is_copy_constructible<CaptureReader>::value, "reader not copyable");
    static_assert(!std::is_move_constructible<CaptureReader>::value, "reader not movable");
    CHECK(true);
}

} // namespace

int main(int argc, char** argv) {
    return testfw::Registry::instance().runAll(argc > 1 ? argv[1] : nullptr);
}
