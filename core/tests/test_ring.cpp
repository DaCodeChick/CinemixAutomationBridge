// SPSC ring buffer tests (the host-write and MIDI-input queues).
#include <cstdint>

#include "TestFramework.h"
#include "cinemix/RingBuffer.h"

using namespace cinemix;

namespace {

TEST_CASE("ring: push/pop fifo order") {
    SpScRingBuffer<int> r(4); // capacity rounds up to 8
    for (int i = 0; i < 5; ++i) CHECK(r.push(i));
    for (int i = 0; i < 5; ++i) {
        int v = -1;
        CHECK(r.pop(v));
        CHECK_EQ(v, i);
    }
    int v = -1;
    CHECK(!r.pop(v));
}

TEST_CASE("ring: bulk pop drains in order") {
    SpScRingBuffer<uint8_t> r(8);
    const uint8_t data[] = {1, 2, 3, 4, 5};
    for (size_t i = 0; i < 5; ++i) CHECK(r.push(data[i]));
    uint8_t out[8] = {0};
    CHECK_EQ(r.popBulk(out, 8), static_cast<size_t>(5));
    for (size_t i = 0; i < 5; ++i) CHECK_EQ(static_cast<int>(out[i]), static_cast<int>(data[i]));
}

TEST_CASE("ring: overflow drops newest and is counted") {
    SpScRingBuffer<int> r(4); // capacity 8 → holds 7
    for (int i = 0; i < 20; ++i) r.push(i);
    CHECK(r.overflowCount() > 0);
    // The ring kept the oldest 7 elements: 0..6.
    int v = -1;
    CHECK(r.pop(v));
    CHECK_EQ(v, 0);
    for (int i = 1; i < 7; ++i) {
        CHECK(r.pop(v));
        CHECK_EQ(v, i);
    }
    CHECK(!r.pop(v));
}

TEST_CASE("ring: wrap-around integrity") {
    SpScRingBuffer<uint32_t> r(16);
    for (uint32_t round = 0; round < 100; ++round) {
        for (uint32_t i = 0; i < 10; ++i) CHECK(r.push(round * 10 + i));
        for (uint32_t i = 0; i < 10; ++i) {
            uint32_t v = 0xFFFFFFFFu;
            CHECK(r.pop(v));
            CHECK_EQ(v, round * 10 + i);
        }
    }
    CHECK_EQ(r.available(), static_cast<size_t>(0));
}

} // namespace

int main(int argc, char** argv) {
    return testfw::Registry::instance().runAll(argc > 1 ? argv[1] : nullptr);
}
