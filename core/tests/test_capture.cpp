// CaptureFile (.cmi) tests — record codec + file round trips.
#include <cstdio>
#include <string>
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

TEST_CASE("capture codec: little-endian primitives") {
    std::array<std::uint8_t, 8> bytes{};
    capture_detail::writeU64Le(bytes.data(), 0x0102030405060708ULL);
    CHECK_EQ(capture_detail::readU64Le(bytes.data()), 0x0102030405060708ULL);
    CHECK_EQ(static_cast<int>(bytes[0]), 0x08); // least significant byte first

    std::array<std::uint8_t, 4> u32{};
    capture_detail::writeU32Le(u32.data(), 0xDEADBEEFu);
    CHECK_EQ(capture_detail::readU32Le(u32.data()), 0xDEADBEEFu);
    CHECK_EQ(static_cast<int>(u32[0]), 0xEF);
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

} // namespace

int main(int argc, char** argv) {
    return testfw::Registry::instance().runAll(argc > 1 ? argv[1] : nullptr);
}
