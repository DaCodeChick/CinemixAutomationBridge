// TransmissionScheduler tests — ordering, coalescing, dedupe, budget,
// cancellation, priority lane.
#include <vector>

#include "TestFramework.h"
#include "cinemix/Diagnostics.h"
#include "cinemix/TransmissionScheduler.h"
#include "cinemix/testing/FakeTransport.h"

using namespace cinemix;
using namespace cinemix_test;

namespace {

struct Fixture {
    MixerProfile profile;
    Diagnostics diag;
    FakeTransport transport;
    TransmissionScheduler sched;

    Fixture() : profile(MixerProfile::legacyDefault()), sched(profile, diag, transport) {
        diag.setLevel(Diagnostics::Level::Error); // keep tests quiet
    }
};

OutboundCommand cmd(uint8_t channel, uint8_t cc, uint8_t value, uint8_t port = 0) {
    OutboundCommand c;
    c.kind = CommandKind::SetMode;
    c.message = MidiMessage::controlChange(channel, cc, value, port);
    c.param = kNoParam;
    return c;
}

TEST_CASE("scheduler: FIFO order for commands preserved") {
    Fixture f;
    f.sched.enqueueCommand(cmd(3, 64, 1));
    f.sched.enqueueCommand(cmd(3, 65, 2));
    f.sched.enqueueCommand(cmd(3, 66, 3));
    f.sched.drainToEmpty();

    CHECK_EQ(f.transport.sentToPort1.size(), static_cast<size_t>(3));
    CHECK_EQ(static_cast<int>(f.transport.sentToPort1[0].data[1]), 64);
    CHECK_EQ(static_cast<int>(f.transport.sentToPort1[1].data[1]), 65);
    CHECK_EQ(static_cast<int>(f.transport.sentToPort1[2].data[1]), 66);
    CHECK_EQ(f.sched.pending(), static_cast<size_t>(0));
}

TEST_CASE("scheduler: broadcast goes to both port streams") {
    Fixture f;
    f.sched.enqueueCommand(cmd(5, 127, 127, 0));
    f.sched.drainToEmpty();
    CHECK_EQ(f.transport.sentToPort1.size(), static_cast<size_t>(1));
    CHECK_EQ(f.transport.sentToPort2.size(), static_cast<size_t>(1));
    CHECK_EQ(static_cast<int>(f.transport.sentToPort1[0].data[2]), 127);
    CHECK_EQ(static_cast<int>(f.transport.sentToPort2[0].data[2]), 127);
}

TEST_CASE("scheduler: position coalescing keeps only latest value") {
    Fixture f;
    f.sched.enqueuePosition(0, MidiMessage::controlChange(1, 0, 10, 1));
    f.sched.enqueuePosition(0, MidiMessage::controlChange(1, 0, 20, 1));
    f.sched.enqueuePosition(0, MidiMessage::controlChange(1, 0, 30, 1));
    f.sched.drainToEmpty();

    // Exactly one CC0 message sent, with the latest value.
    size_t cc0 = 0;
    for (size_t i = 0; i < f.transport.sentToPort1.size(); ++i)
        if (f.transport.sentToPort1[i].data[1] == 0) ++cc0;
    CHECK_EQ(cc0, static_cast<size_t>(1));
    CHECK_EQ(static_cast<int>(f.transport.sentToPort1[0].data[2]), 30);
    CHECK_EQ(f.sched.coalescedCount(), static_cast<size_t>(2));
}

TEST_CASE("scheduler: identical position updates are both sent (dedupe is engine-level)") {
    Fixture f;
    f.sched.enqueuePosition(1, MidiMessage::controlChange(1, 2, 64, 1));
    f.sched.drainToEmpty();
    CHECK_EQ(f.transport.sentToPort1.size(), static_cast<size_t>(1));

    // The scheduler does not dedupe: legacy prev_CC_Val semantics live in the
    // engine (keyed on the parameter value, not the wire byte).
    f.sched.enqueuePosition(1, MidiMessage::controlChange(1, 2, 64, 1));
    f.sched.drainToEmpty();
    CHECK_EQ(f.transport.sentToPort1.size(), static_cast<size_t>(2));

    f.sched.enqueuePosition(1, MidiMessage::controlChange(1, 2, 65, 1));
    f.sched.drainToEmpty();
    CHECK_EQ(f.transport.sentToPort1.size(), static_cast<size_t>(3));
}

TEST_CASE("scheduler: cancelPosition drops pending update") {
    Fixture f;
    f.sched.enqueuePosition(0, MidiMessage::controlChange(1, 0, 10, 1));
    f.sched.enqueuePosition(1, MidiMessage::controlChange(1, 2, 20, 1));
    f.sched.cancelPosition(0);
    f.sched.drainToEmpty();
    CHECK_EQ(f.transport.sentToPort1.size(), static_cast<size_t>(1));
    CHECK_EQ(static_cast<int>(f.transport.sentToPort1[0].data[1]), 2);
}

TEST_CASE("scheduler: cancelAllPositions keeps commands") {
    Fixture f;
    f.sched.enqueueCommand(cmd(3, 64, 1));
    f.sched.enqueuePosition(0, MidiMessage::controlChange(1, 0, 10, 1));
    f.sched.enqueueCommand(cmd(3, 65, 1));
    f.sched.cancelAllPositions();
    f.sched.drainToEmpty();
    CHECK_EQ(f.transport.sentToPort1.size(), static_cast<size_t>(2));
    CHECK_EQ(static_cast<int>(f.transport.sentToPort1[0].data[1]), 64);
    CHECK_EQ(static_cast<int>(f.transport.sentToPort1[1].data[1]), 65);
}

TEST_CASE("scheduler: high lane drains before main lane") {
    Fixture f;
    f.sched.enqueueCommand(cmd(3, 64, 1));
    f.sched.enqueueHigh(cmd(3, 70, 2)); // touch reply
    f.sched.enqueueCommand(cmd(3, 65, 1));
    f.sched.drainToEmpty();
    CHECK_EQ(static_cast<int>(f.transport.sentToPort1[0].data[1]), 70); // high first
    CHECK_EQ(static_cast<int>(f.transport.sentToPort1[1].data[1]), 64);
    CHECK_EQ(static_cast<int>(f.transport.sentToPort1[2].data[1]), 65);
}

TEST_CASE("scheduler: budget pacing caps the send rate") {
    Fixture f;
    f.profile.budgetMessagesPerSecond = 2000;
    f.profile.schedulerTickMs = 1;
    TransmissionScheduler paced(f.profile, f.diag, f.transport);
    for (int i = 0; i < 10; ++i)
        paced.enqueueCommand(cmd(3, static_cast<uint8_t>(64 + i), 1));

    // 2 messages/tick budget, no burst accumulation yet: tick 1 sends 2.
    CHECK_EQ(paced.tick() ? 1 : 0, 1);
    CHECK_EQ(f.transport.sentToPort1.size(), static_cast<size_t>(2));
    CHECK_EQ(paced.tick() ? 1 : 0, 1);
    CHECK_EQ(f.transport.sentToPort1.size(), static_cast<size_t>(4));
    // 6 remain.
    CHECK_EQ(paced.pending(), static_cast<size_t>(6));
    // Drain the rest unpaced for cleanup.
    paced.drainToEmpty();
    CHECK_EQ(paced.pending(), static_cast<size_t>(0));
}

TEST_CASE("scheduler: transport failure counts as dropped") {
    Fixture f;
    f.transport.connectedFlag = false;
    f.sched.enqueueCommand(cmd(3, 64, 1));
    f.sched.drainToEmpty();
    CHECK_EQ(f.transport.sentToPort1.size(), static_cast<size_t>(0));
    CHECK_EQ(f.sched.droppedTotal(), static_cast<size_t>(1));
}

TEST_CASE("scheduler: queue-cap policy — drops count, SystemReset always admitted") {
    Fixture f;
    // Fill the main lane past its cap with ordinary commands: the overflow
    // drops the NEWEST command and counts it.
    for (int i = 0; i < 1100; ++i) f.sched.enqueueCommand(cmd(3, 64, 1));
    const size_t beforeReset = f.sched.pending();
    CHECK(beforeReset <= 1024);
    CHECK(f.sched.droppedTotal() > 0);

    // The release byte is safety-critical: it is always admitted even when
    // the lane is full (evicting the oldest entry).
    OutboundCommand reset;
    reset.kind = CommandKind::SystemReset;
    reset.message = MidiMessage::systemReset(0);
    f.sched.enqueueCommand(reset);
    CHECK(f.sched.pending() <= 1024);

    f.sched.drainToEmpty();
    CHECK(f.transport.sentToPort1.back().isSystemReset());
}

TEST_CASE("scheduler: high-lane saturation drops newest reply, keeps order") {
    Fixture f;
    // Fill the high lane past its cap: the overflow drops the NEWEST reply
    // and counts it; earlier replies keep their relative order.
    for (int i = 0; i < 1100; ++i)
        f.sched.enqueueHigh(cmd(3, uint8_t(static_cast<uint8_t>(64 + (i % 48))), 2));
    CHECK(f.sched.pending() <= 1024);
    CHECK(f.sched.droppedTotal() > 0);
    f.sched.drainToEmpty();
    CHECK(f.transport.sentToPort1.size() > 0);
}

TEST_CASE("scheduler: 14-bit position — coarse/fine order and cancellation") {
    Fixture f;
    // A 14-bit fader position = coarse (MSB) + fine (LSB) continuation.
    const MidiMessage coarse = MidiMessage::controlChange(1, 0, 63, 1);
    const MidiMessage fine = MidiMessage::controlChange(1, 1, 127, 1);
    f.sched.enqueuePosition(5, coarse);
    f.sched.enqueuePositionContinuation(5, fine);
    f.sched.drainToEmpty();
    // MSB then LSB, both sent, in order.
    CHECK_EQ(f.transport.sentToPort1.size(), static_cast<size_t>(2));
    CHECK_EQ(static_cast<int>(f.transport.sentToPort1[0].data[1]), 0);
    CHECK_EQ(static_cast<int>(f.transport.sentToPort1[1].data[1]), 1);
    CHECK_EQ(static_cast<int>(f.transport.sentToPort1[1].data[2]), 127);
}

TEST_CASE("scheduler: cancelPosition removes both 14-bit components") {
    Fixture f;
    f.sched.enqueuePosition(5, MidiMessage::controlChange(1, 0, 63, 1));
    f.sched.enqueuePositionContinuation(5, MidiMessage::controlChange(1, 1, 127, 1));
    f.sched.enqueuePosition(6, MidiMessage::controlChange(1, 2, 10, 1));
    f.sched.cancelPosition(5);
    f.sched.drainToEmpty();
    // Only parameter 6's position survives; neither 5's coarse nor fine.
    CHECK_EQ(f.transport.sentToPort1.size(), static_cast<size_t>(1));
    CHECK_EQ(static_cast<int>(f.transport.sentToPort1[0].data[1]), 2);
}

TEST_CASE("scheduler: cancelAllPositions removes 14-bit components too") {
    Fixture f;
    f.sched.enqueueCommand(cmd(3, 64, 1)); // a real command survives
    f.sched.enqueuePosition(5, MidiMessage::controlChange(1, 0, 63, 1));
    f.sched.enqueuePositionContinuation(5, MidiMessage::controlChange(1, 1, 127, 1));
    f.sched.cancelAllPositions();
    f.sched.drainToEmpty();
    CHECK_EQ(f.transport.sentToPort1.size(), static_cast<size_t>(1));
    CHECK_EQ(static_cast<int>(f.transport.sentToPort1[0].data[1]), 64);
}

TEST_CASE("scheduler: queue saturation with 14-bit position traffic") {
    Fixture f;
    // Repeated 14-bit updates for many parameters: coalescing keeps the lane
    // bounded (≤ 2 entries per actively-updated param), so the cap is only
    // reachable by stacking commands on top. Drops are counted, positions
    // stay cancellable, and the safety-critical reset is always admitted.
    for (int round = 0; round < 10; ++round) {
        for (int param = 0; param < 161; ++param) {
            const uint8_t coarseCc = static_cast<uint8_t>(2 * param);
            const uint8_t fineCc = static_cast<uint8_t>(2 * param + 1);
            f.sched.enqueuePosition(static_cast<ParamId>(param),
                                    MidiMessage::controlChange(1, coarseCc, 63, 1));
            f.sched.enqueuePositionContinuation(static_cast<ParamId>(param),
                                                MidiMessage::controlChange(1, fineCc, 127, 1));
        }
    }
    // Stack commands to reach the cap.
    for (int i = 0; i < 900; ++i) f.sched.enqueueCommand(cmd(3, 64, 1));
    CHECK(f.sched.pending() <= 1024);
    CHECK(f.sched.droppedTotal() > 0);

    // The release byte is admitted even with a saturated lane.
    OutboundCommand reset;
    reset.kind = CommandKind::SystemReset;
    reset.message = MidiMessage::systemReset(0);
    f.sched.enqueueCommand(reset);

    // Global cancellation removes EVERY pending position component (coarse
    // AND fine); only commands plus the reset may transmit.
    f.sched.cancelAllPositions();
    f.sched.drainToEmpty();

    bool leakedFineComponent = false;
    for (size_t i = 0; i < f.transport.sentToPort1.size(); ++i) {
        const MidiMessage& m = f.transport.sentToPort1[i];
        const bool isOddPositionCc =
            (m.data[0] == 0xB0) && ((m.data[1] & 0x01u) != 0) && (m.data[1] != 64);
        if (isOddPositionCc) leakedFineComponent = true;
    }
    CHECK(!leakedFineComponent);
    CHECK(f.transport.sentToPort1.back().isSystemReset());
}

TEST_CASE("scheduler: system reset passes through as a 1-byte message") {
    Fixture f;
    OutboundCommand c;
    c.kind = CommandKind::SystemReset;
    c.message = MidiMessage::systemReset(0);
    f.sched.enqueueCommand(c);
    f.sched.drainToEmpty();
    CHECK_EQ(f.transport.sentToPort1.size(), static_cast<size_t>(1));
    CHECK(f.transport.sentToPort1[0].isSystemReset());
}

} // namespace

int main(int argc, char** argv) {
    return testfw::Registry::instance().runAll(argc > 1 ? argv[1] : nullptr);
}
