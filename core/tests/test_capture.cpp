// CaptureFile (.cmi) round-trip tests.
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

TEST_CASE("capture: write and read back events") {
    const std::string path = tempPath();
    {
        CaptureWriter w(path);
        CHECK(w.ok());
        MidiMessage m = MidiMessage::controlChange(1, 0, 63, 1);
        w.writeEvent(1000, 1, 1, m);
        m = MidiMessage::controlChange(5, 127, 127, 0);
        w.writeEvent(2000, 1, 0, m);
        m = MidiMessage::systemReset(0);
        w.writeEvent(3000, 1, 0, m);
    }
    {
        CaptureReader r(path);
        CHECK(r.ok());
        CaptureEvent ev;
        CHECK(r.next(ev));
        CHECK_EQ(ev.timestampUs, uint64_t(1000));
        CHECK_EQ(int(ev.direction), 1);
        CHECK_EQ(int(ev.port), 1);
        CHECK_EQ(int(ev.message.data[1]), 0);
        CHECK_EQ(int(ev.message.data[2]), 63);

        CHECK(r.next(ev));
        CHECK_EQ(ev.timestampUs, uint64_t(2000));
        CHECK_EQ(int(ev.message.data[2]), 127);

        CHECK(r.next(ev));
        CHECK(ev.message.isSystemReset());

        CHECK(!r.next(ev)); // EOF
        CHECK_EQ(r.corruptCount(), size_t(0));
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
    CaptureReader r(path);
    CHECK(!r.ok());
    std::remove(path.c_str());
}

TEST_CASE("capture: truncated event stream stops cleanly") {
    const std::string path = tempPath();
    {
        CaptureWriter w(path);
        MidiMessage m = MidiMessage::controlChange(1, 0, 63, 1);
        w.writeEvent(1000, 1, 1, m);
    }
    // Append a partial event record.
    {
        FILE* f = fopen(path.c_str(), "ab");
        unsigned char partial[6] = {1, 2, 3, 4, 5, 6}; // too short for ts+hdr+data
        fwrite(partial, 1, 6, f);
        fclose(f);
    }
    CaptureReader r(path);
    CHECK(r.ok());
    CaptureEvent ev;
    CHECK(r.next(ev));            // the complete event
    CHECK(!r.next(ev));           // truncated record → stop
    CHECK(r.corruptCount() > 0);
    std::remove(path.c_str());
}

} // namespace

int main(int argc, char** argv) {
    return testfw::Registry::instance().runAll(argc > 1 ? argv[1] : nullptr);
}
