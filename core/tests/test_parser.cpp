// MidiParser robustness tests — running status, realtime interleaving, sysex,
// system common, malformed streams.
#include <cstdint>
#include <vector>

#include "TestFramework.h"
#include "cinemix/MidiParser.h"

using namespace cinemix;

namespace {

struct Captured {
    std::vector<uint8_t> channels;
    std::vector<uint8_t> ccs;
    std::vector<uint8_t> values;
    std::vector<uint8_t> systemBytes;
};

void onCc(void* user, uint8_t channel, uint8_t cc, uint8_t value) {
    Captured* c = static_cast<Captured*>(user);
    c->channels.push_back(channel);
    c->ccs.push_back(cc);
    c->values.push_back(value);
}
void onSystem(void* user, uint8_t status) {
    Captured* c = static_cast<Captured*>(user);
    c->systemBytes.push_back(status);
}
void onMalformed(void* user) { (void)user; }

Captured parse(const std::vector<uint8_t>& bytes) {
    MidiParser p;
    Captured c;
    p.setHandlers(&c, &onCc, &onSystem, &onMalformed);
    p.feed(bytes.data(), bytes.size());
    return c;
}

TEST_CASE("parser: single CC message") {
    Captured c = parse({0xB0, 10, 20});
    CHECK_EQ(c.channels.size(), size_t(1));
    CHECK_EQ(int(c.channels[0]), 1);
    CHECK_EQ(int(c.ccs[0]), 10);
    CHECK_EQ(int(c.values[0]), 20);
}

TEST_CASE("parser: running status") {
    Captured c = parse({0xB0, 10, 20, 30, 40});
    CHECK_EQ(c.channels.size(), size_t(2));
    CHECK_EQ(int(c.ccs[0]), 10);
    CHECK_EQ(int(c.values[0]), 20);
    CHECK_EQ(int(c.ccs[1]), 30);
    CHECK_EQ(int(c.values[1]), 40);
}

TEST_CASE("parser: running status switches on new status byte") {
    Captured c = parse({0xB0, 10, 20, 0xB1, 5, 6});
    CHECK_EQ(c.channels.size(), size_t(2));
    CHECK_EQ(int(c.channels[1]), 2);
    CHECK_EQ(int(c.ccs[1]), 5);
}

TEST_CASE("parser: realtime bytes interleaved inside a message") {
    Captured c = parse({0xB0, 10, 0xF8, 20, 0xFE, 30});
    CHECK_EQ(c.channels.size(), size_t(1));
    CHECK_EQ(int(c.ccs[0]), 10);
    CHECK_EQ(int(c.values[0]), 20);
    // 0xFE (active sense) is not 0xFF: only 0xFF is reported as a system byte.
    CHECK_EQ(c.systemBytes.size(), size_t(0));
}

TEST_CASE("parser: system reset byte reported") {
    Captured c = parse({0xFF});
    CHECK_EQ(c.systemBytes.size(), size_t(1));
    CHECK_EQ(int(c.systemBytes[0]), 0xFF);
}

TEST_CASE("parser: sysex skipped") {
    Captured c = parse({0xF0, 1, 2, 3, 4, 0xF7, 0xB0, 10, 20});
    CHECK_EQ(c.channels.size(), size_t(1));
    CHECK_EQ(int(c.ccs[0]), 10);
}

TEST_CASE("parser: sysex ends implicitly at next status byte") {
    Captured c = parse({0xF0, 1, 2, 0xB0, 10, 20});
    CHECK_EQ(c.channels.size(), size_t(1));
    CHECK_EQ(int(c.ccs[0]), 10);
}

TEST_CASE("parser: system common consumed") {
    // F2 (song position) has 2 data bytes; the CC after it must still parse.
    Captured c = parse({0xF2, 0x10, 0x20, 0xB0, 7, 8});
    CHECK_EQ(c.channels.size(), size_t(1));
    CHECK_EQ(int(c.ccs[0]), 7);
}

TEST_CASE("parser: non-CC channel voice ignored") {
    Captured c = parse({0xC0, 5}); // program change
    CHECK_EQ(c.channels.size(), size_t(0));
}

TEST_CASE("parser: stray data bytes count as malformed") {
    MidiParser p;
    Captured c;
    p.setHandlers(&c, &onCc, &onSystem, &onMalformed);
    p.feed((const uint8_t*)"\x40\x41\x42", 3);
    CHECK_EQ(c.channels.size(), size_t(0));
    CHECK(p.malformedCount() >= 3);
}

TEST_CASE("parser: truncated trailing data byte is not emitted") {
    Captured c = parse({0xB0, 10}); // one data byte only
    CHECK_EQ(c.channels.size(), size_t(0));
}

TEST_CASE("parser: byte-at-a-time feed equals bulk feed") {
    const std::vector<uint8_t> bytes = {0xB0, 10, 20, 0xB0, 11, 21, 0xFF};
    Captured bulk = parse(bytes);

    MidiParser p;
    Captured slow;
    p.setHandlers(&slow, &onCc, &onSystem, &onMalformed);
    for (size_t i = 0; i < bytes.size(); ++i) p.feed(&bytes[i], 1);

    CHECK_EQ(bulk.channels.size(), slow.channels.size());
    CHECK_EQ(bulk.ccs.size(), slow.ccs.size());
    CHECK_EQ(bulk.systemBytes.size(), slow.systemBytes.size());
    for (size_t i = 0; i < bulk.ccs.size(); ++i) {
        CHECK_EQ(int(bulk.channels[i]), int(slow.channels[i]));
        CHECK_EQ(int(bulk.ccs[i]), int(slow.ccs[i]));
        CHECK_EQ(int(bulk.values[i]), int(slow.values[i]));
    }
}

} // namespace

int main(int argc, char** argv) {
    return testfw::Registry::instance().runAll(argc > 1 ? argv[1] : nullptr);
}
