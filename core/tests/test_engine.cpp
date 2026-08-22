// AutomationEngine tests — the legacy byte-exact activation/deactivation
// sequences, snapshot/reset/all-mutes, touch modes, echo suppression and
// feedback-loop prevention, origins, profiles, thread mode, and destruction
// safety.
#include <thread>
#include <vector>

#include "TestFramework.h"
#include "cinemix/AutomationEngine.h"
#include "cinemix/testing/FakeTransport.h"

using namespace cinemix;
using namespace cinemix_test;

namespace {

struct MockListener : public AutomationEngine::Listener {
    struct Ev {
        enum Kind { Gesture, Parameter, Connected } kind;
        ParamId param;
        float value;
        Origin origin;
        bool begin;
        bool connected;
    };
    std::vector<Ev> events;

    void onGesture(ParamId p, bool begin) override {
        Ev e = {Ev::Gesture, p, 0.f, Origin::None, begin, false};
        events.push_back(e);
    }
    void onParameter(ParamId p, float v, Origin o) override {
        Ev e = {Ev::Parameter, p, v, o, false, false};
        events.push_back(e);
    }
    void onConnected(bool a) override {
        Ev e = {Ev::Connected, 0, 0.f, Origin::None, false, a};
        events.push_back(e);
    }
    size_t parameterEvents() const {
        size_t n = 0;
        for (size_t i = 0; i < events.size(); ++i)
            if (events[i].kind == Ev::Parameter) ++n;
        return n;
    }
    size_t gestureEvents() const {
        size_t n = 0;
        for (size_t i = 0; i < events.size(); ++i)
            if (events[i].kind == Ev::Gesture) ++n;
        return n;
    }
};

struct Fixture {
    MixerProfile profile;
    Diagnostics diag;
    FakeTransport transport;
    AutomationEngine engine;
    MockListener listener;

    Fixture() : profile(MixerProfile::legacyDefault()), engine(profile, diag, transport) {
        diag.setLevel(Diagnostics::Level::Error);
        engine.setListener(&listener);
    }
    Fixture(const MixerProfile& p) : profile(p), engine(profile, diag, transport) {
        diag.setLevel(Diagnostics::Level::Error);
        engine.setListener(&listener);
    }
};

// ---------------------------------------------------------------------------
// Gold sequence builders — mirror the legacy bridge's message order
// (docs/COMPATIBILITY.md §1). The mode sweeps are profile-scoped (CHANGED vs
// the legacy 48-per-side hardcode — documented).

void expectActivation(size_t loStrips, size_t hiStrips,
                      std::vector<MidiMessage>& out1, std::vector<MidiMessage>& out2) {
    auto bc = [&](uint8_t ch, uint8_t cc, uint8_t v) {
        MidiMessage m = MidiMessage::controlChange(ch, cc, v, 0);
        out1.push_back(m);
        out2.push_back(m);
    };
    auto p1 = [&](uint8_t ch, uint8_t cc, uint8_t v) {
        out1.push_back(MidiMessage::controlChange(ch, cc, v, 1));
    };
    auto p2 = [&](uint8_t ch, uint8_t cc, uint8_t v) {
        out2.push_back(MidiMessage::controlChange(ch, cc, v, 2));
    };

    bc(5, 127, 127);
    // Legacy sweep order: controller number ascending, ch3/ch4 interleaved
    // (Plugin.h SetAllChannelsMode), scoped to existing strips.
    const size_t sweepMax = 2 * (loStrips > hiStrips ? loStrips : hiStrips);
    for (size_t w = 0; w < sweepMax; ++w) {
        if (w < 2 * loStrips) bc(3, uint8_t(64 + w), 2);
        if (w < 2 * hiStrips) bc(4, uint8_t(64 + w), 2);
    }
    bc(5, 64, 2);
    bc(4, 88, 2);
    bc(4, 90, 2);
    // Snapshot: faders (legacy order = parameter order), values 0.
    for (int p = 0; p < 2 * 24; ++p) p1(1, uint8_t(2 * p), 0);          // LO faders
    for (int p = 0; p < 2 * int(hiStrips); ++p) p2(2, uint8_t(2 * p), 0); // HI faders
    // Mutes off (2).
    for (int p = 0; p < 2 * 24; ++p) p1(3, uint8_t(p), 2);
    for (int p = 0; p < 2 * int(hiStrips); ++p) p2(4, uint8_t(p), 2);
    // AUX 1..10 off.
    for (int a = 0; a < 10; ++a) p2(5, 96, uint8_t(2 * (a + 1)));
    // Joysticks + master (master at 1.0 → 127).
    p2(2, 48, 0); p2(2, 50, 0); p2(4, 24, 2);
    p2(2, 52, 0); p2(2, 54, 0); p2(4, 26, 2);
    p2(5, 0, 127);
    // Sweep to AUTO (3).
    for (size_t w = 0; w < sweepMax; ++w) {
        if (w < 2 * loStrips) bc(3, uint8_t(64 + w), 3);
        if (w < 2 * hiStrips) bc(4, uint8_t(64 + w), 3);
    }
    bc(5, 64, 3);
    bc(4, 88, 3);
    bc(4, 90, 3);
}

TEST_CASE("engine: activation sequence matches legacy byte order exactly") {
    Fixture f;
    f.engine.activate();
    f.engine.drainNow();

    std::vector<MidiMessage> exp1, exp2;
    expectActivation(24, 12, exp1, exp2);

    CHECK_EQ(f.transport.sentToPort1.size(), exp1.size());
    CHECK_EQ(f.transport.sentToPort2.size(), exp2.size());
    const size_t n1 = f.transport.sentToPort1.size() < exp1.size()
                          ? f.transport.sentToPort1.size() : exp1.size();
    const size_t n2 = f.transport.sentToPort2.size() < exp2.size()
                          ? f.transport.sentToPort2.size() : exp2.size();
    for (size_t i = 0; i < n1; ++i) {
        const MidiMessage& a = f.transport.sentToPort1[i];
        const MidiMessage& b = exp1[i];
        CHECK_EQ(int(a.data[0]), int(b.data[0]));
        CHECK_EQ(int(a.data[1]), int(b.data[1]));
        CHECK_EQ(int(a.data[2]), int(b.data[2]));
        CHECK_EQ(int(a.port), int(b.port));
        if (a.data[0] != b.data[0] || a.data[1] != b.data[1] || a.data[2] != b.data[2])
            break;
    }
    for (size_t i = 0; i < n2; ++i) {
        const MidiMessage& a = f.transport.sentToPort2[i];
        const MidiMessage& b = exp2[i];
        CHECK_EQ(int(a.data[0]), int(b.data[0]));
        CHECK_EQ(int(a.data[1]), int(b.data[1]));
        CHECK_EQ(int(a.data[2]), int(b.data[2]));
        CHECK_EQ(int(a.port), int(b.port));
        if (a.data[0] != b.data[0] || a.data[1] != b.data[1] || a.data[2] != b.data[2])
            break;
    }
    CHECK(f.engine.isActivated());
}

TEST_CASE("engine: activation without transport connection fails cleanly") {
    Fixture f;
    f.transport.connectedFlag = false;
    f.engine.activate();
    f.engine.drainNow();
    CHECK(!f.engine.isActivated());
    CHECK_EQ(f.transport.sentToPort1.size(), size_t(0));
}

TEST_CASE("engine: deactivation sequence — FF is the final byte") {
    Fixture f;
    f.engine.activate();
    f.engine.drainNow();
    f.transport.clear();

    f.engine.deactivate();
    f.engine.drainNow();

    CHECK(!f.engine.isActivated());
    CHECK_EQ(f.transport.sentToPort1.size(), f.transport.sentToPort2.size());
    // Port 1 stream: remote-off, sweep 0 (48+24), master 0, joy 0s, FF.
    const size_t expected = 1 + 48 + 24 + 1 + 2 + 1;
    CHECK_EQ(f.transport.sentToPort1.size(), expected);
    CHECK(f.transport.sentToPort1.back().isSystemReset());
    CHECK(f.transport.sentToPort2.back().isSystemReset());
    // First message: CC127=0 ch5.
    CHECK_EQ(int(f.transport.sentToPort1[0].data[1]), 127);
    CHECK_EQ(int(f.transport.sentToPort1[0].data[2]), 0);
}

TEST_CASE("engine: host writes stored pre-activation but not transmitted") {
    Fixture f;
    f.engine.setHostParameter(0, 0.4f);
    f.engine.drainNow();
    CHECK_NEAR(f.engine.getParameter(0), 0.4f, 1e-6);
    CHECK_EQ(f.transport.sentToPort1.size(), size_t(0));
    CHECK_EQ(f.listener.parameterEvents(), size_t(0));
}

TEST_CASE("engine: console events ignored before activation") {
    Fixture f;
    f.engine.setHostParameter(0, 0.4f);
    f.engine.drainNow();
    f.transport.injectCc(1, 0, 50); // fader strip 1 moves
    f.engine.drainNow();
    CHECK_NEAR(f.engine.getParameter(0), 0.4f, 1e-6); // unchanged
    CHECK_EQ(f.listener.parameterEvents(), size_t(0));
}

TEST_CASE("engine: host automation flows to console") {
    Fixture f;
    f.engine.activate();
    f.engine.drainNow();
    f.transport.clear();
    f.listener.events.clear(); // activation snapshot legitimately notified 161 params

    f.engine.setHostParameter(0, 0.5f); // M01 Chan fader
    f.engine.drainNow();
    CHECK_EQ(f.transport.sentToPort1.size(), size_t(1));
    CHECK_EQ(int(f.transport.sentToPort1[0].data[0] & 0x0F), 0); // ch1
    CHECK_EQ(int(f.transport.sentToPort1[0].data[1]), 0);        // CC0
    CHECK_EQ(int(f.transport.sentToPort1[0].data[2]), 63);       // trunc(0.5*127)

    f.engine.setHostParameter(72, 1.f); // M01 Chan mute
    f.engine.drainNow();
    const MidiMessage* last = f.transport.last(1);
    CHECK(last != nullptr);
    CHECK_EQ(int(last->data[1]), 0);
    CHECK_EQ(int(last->data[2]), 3); // ON

    // No host listener events for host-originated changes (no echo loop).
    CHECK_EQ(f.listener.parameterEvents(), size_t(0));
}

TEST_CASE("engine: console echoes of commanded values are suppressed") {
    Fixture f;
    f.engine.activate();
    f.engine.drainNow();
    f.transport.clear();
    f.listener.events.clear();

    f.engine.setHostParameter(0, 0.5f);
    f.engine.drainNow();
    CHECK_EQ(f.transport.sentToPort1.size(), size_t(1));

    // The motorized fader reports the commanded position back (echo).
    f.transport.injectCc(1, 0, 63);
    f.engine.drainNow();
    CHECK_NEAR(f.engine.getParameter(0), 63.f / 127.f, 1e-6); // state updated
    CHECK_EQ(f.listener.parameterEvents(), size_t(0));        // but no host write
    CHECK_EQ(f.transport.sentToPort1.size(), size_t(1));      // and no re-send

    // One 7-bit step away is still within hysteresis (motor interpolation).
    f.transport.injectCc(1, 0, 62);
    f.engine.drainNow();
    CHECK_EQ(f.listener.parameterEvents(), size_t(0));
    CHECK_EQ(f.transport.sentToPort1.size(), size_t(1));
}

TEST_CASE("engine: hand move (different value) reports to host and never bounces") {
    Fixture f;
    f.engine.activate();
    f.engine.drainNow();
    f.transport.clear();
    f.listener.events.clear();

    f.engine.setHostParameter(0, 0.5f);
    f.engine.drainNow();
    f.transport.clear();

    // The hand moves the fader somewhere else.
    f.transport.injectCc(1, 0, 100);
    f.engine.drainNow();

    CHECK_EQ(f.listener.parameterEvents(), size_t(1));
    CHECK(f.listener.events[0].kind == MockListener::Ev::Parameter);
    CHECK_EQ(int(f.listener.events[0].param), 0);
    CHECK_NEAR(f.listener.events[0].value, 100.f / 127.f, 1e-6);
    CHECK(f.listener.events[0].origin == Origin::Console);
    CHECK_EQ(f.transport.sentToPort1.size(), size_t(0)); // no bounce-back
}

TEST_CASE("engine: touch → gesture + write mode reply; release → gesture + auto") {
    Fixture f;
    f.engine.activate();
    f.engine.drainNow();
    f.transport.clear();
    f.listener.events.clear();

    // Touch sensor on strip 1 Chan path (ch3 CC64, value 6).
    f.transport.injectCc(3, 64, 6);
    f.engine.drainNow();
    CHECK_EQ(f.listener.gestureEvents(), size_t(1));
    CHECK(f.listener.events[0].begin == true);
    // Reply: mode 2 (WRITE) — high priority.
    CHECK_EQ(f.transport.sentToPort1.size(), size_t(1));
    CHECK_EQ(int(f.transport.sentToPort1[0].data[1]), 64);
    CHECK_EQ(int(f.transport.sentToPort1[0].data[2]), 2);

    // Move while touched: user-originated, no outbound echo.
    f.transport.injectCc(1, 0, 80);
    f.engine.drainNow();
    CHECK_EQ(f.listener.parameterEvents(), size_t(1));
    CHECK(f.listener.events[1].origin == Origin::Console);
    CHECK_EQ(f.transport.sentToPort1.size(), size_t(1));

    // Release: gesture end + mode 3 (AUTO/RW).
    f.transport.injectCc(3, 64, 5);
    f.engine.drainNow();
    CHECK_EQ(f.listener.gestureEvents(), size_t(2));
    CHECK(f.listener.events.back().begin == false);
    CHECK_EQ(int(f.transport.sentToPort1.back().data[2]), 3);
}

TEST_CASE("engine: strip SEL press rotates mode and replies") {
    Fixture f;
    f.engine.activate();
    f.engine.drainNow();
    f.transport.clear();

    // After activation the strip mode is AUTO (3); first SEL press → 0.
    f.transport.injectCc(3, 64, 1);
    f.engine.drainNow();
    CHECK_EQ(int(f.transport.sentToPort1.back().data[2]), 0);
    CHECK_EQ(int(f.engine.touchModes().stripMode(0, StripPath::Chan)), 0);

    f.transport.injectCc(3, 64, 1);
    f.engine.drainNow();
    CHECK_EQ(int(f.transport.sentToPort1.back().data[2]), 1);
    f.transport.injectCc(3, 64, 1);
    f.engine.drainNow();
    CHECK_EQ(int(f.transport.sentToPort1.back().data[2]), 2);
    f.transport.injectCc(3, 64, 1);
    f.engine.drainNow();
    CHECK_EQ(int(f.transport.sentToPort1.back().data[2]), 3);
    // Wrap.
    f.transport.injectCc(3, 64, 1);
    f.engine.drainNow();
    CHECK_EQ(int(f.transport.sentToPort1.back().data[2]), 0);
}

TEST_CASE("engine: touch replies suppressed when mode is not AUTO") {
    Fixture f;
    f.engine.activate();
    f.engine.drainNow();
    f.transport.clear();

    // Rotate strip 1 Chan out of AUTO (3 → 0).
    f.transport.injectCc(3, 64, 1);
    f.engine.drainNow();
    f.transport.clear();

    // Touch while ISO: no mode reply (legacy gate).
    f.transport.injectCc(3, 64, 6);
    f.engine.drainNow();
    CHECK_EQ(f.transport.sentToPort1.size(), size_t(0));
}

TEST_CASE("engine: master SEL press applies mode to all strips") {
    Fixture f;
    f.engine.activate();
    f.engine.drainNow();
    f.transport.clear();

    f.transport.injectCc(5, 64, 1);
    f.engine.drainNow();

    // masterMode was 3 → rotates to 0; sweep sends mode 0 to every strip.
    const MidiMessage& first = f.transport.sentToPort1[0];
    CHECK_EQ(int(first.data[1]), 64);
    CHECK_EQ(int(first.data[2]), 0);
    // 48 LO + 24 HI sweep messages per port stream (broadcast).
    CHECK_EQ(f.transport.sentToPort1.size(), size_t(48 + 24));
    // The master's own SEL address (ch5 CC64) must NOT be rewritten (legacy).
    for (size_t i = 0; i < f.transport.sentToPort2.size(); ++i) {
        const MidiMessage& m = f.transport.sentToPort2[i];
        CHECK(!((m.data[0] & 0x0F) == 4 && m.data[1] == 64));
    }
}

TEST_CASE("engine: snapshot resends everything (dedupe cleared)") {
    Fixture f;
    f.engine.activate();
    f.engine.drainNow();
    f.transport.clear();

    f.engine.sendSnapshot();
    f.engine.drainNow();

    // 161 position messages: 72 faders + 72 mutes + 10 aux + 4 axes
    // + 2 joy mutes + 1 master, routed by side.
    CHECK_EQ(f.transport.sentToPort1.size(), size_t(48 + 48));
    CHECK_EQ(f.transport.sentToPort2.size(), size_t(24 + 24 + 10 + 7));
    CHECK_EQ(int(f.transport.sentToPort2.back().data[0] & 0x0F), 4); // ch5 master
    CHECK_EQ(int(f.transport.sentToPort2.back().data[2]), 127);      // master 1.0
}

TEST_CASE("engine: reset all — modes to AUTO, faders to -inf, master to max") {
    Fixture f;
    f.engine.activate();
    f.engine.drainNow();
    f.engine.setHostParameter(0, 0.9f);
    f.engine.drainNow();
    f.transport.clear();

    f.engine.resetAll();
    f.engine.drainNow();

    // Mode sweep 3 first, then snapshot (mutes off, master 127).
    CHECK_EQ(int(f.transport.sentToPort1[0].data[2]), 3);
    const MidiMessage& master = f.transport.sentToPort2.back();
    CHECK_EQ(int(master.data[1]), 0);
    CHECK_EQ(int(master.data[2]), 127);
    CHECK_NEAR(f.engine.getParameter(160), 1.f, 1e-6);
    CHECK_NEAR(f.engine.getParameter(0), 0.f, 1e-6);
}

TEST_CASE("engine: all mutes toggles every mute-like parameter") {
    Fixture f;
    f.engine.activate();
    f.engine.drainNow();
    f.transport.clear();
    f.listener.events.clear();

    f.engine.toggleAllMutes();
    f.engine.drainNow();

    // 72 strip mutes + 10 aux + 2 joystick mutes = 84 listener events.
    CHECK_EQ(f.listener.parameterEvents(), size_t(72 + 10 + 2));
    CHECK(f.listener.events[0].origin == Origin::UserInterface);

    // Spot checks on the wire: strip mute ON=3, AUX1 ON=3, joy1 mute ON=3.
    bool sawAuxOn = false, sawJoyOn = false;
    for (size_t i = 0; i < f.transport.sentToPort2.size(); ++i) {
        const MidiMessage& m = f.transport.sentToPort2[i];
        if ((m.data[0] & 0x0F) == 4 && m.data[1] == 96 && m.data[2] == 3) sawAuxOn = true;
        if ((m.data[0] & 0x0F) == 3 && m.data[1] == 24 && m.data[2] == 3) sawJoyOn = true;
    }
    CHECK(sawAuxOn);
    CHECK(sawJoyOn);
    CHECK_EQ(int(f.transport.sentToPort1[0].data[2]), 3);

    // Toggle back off.
    f.transport.clear();
    f.engine.toggleAllMutes();
    f.engine.drainNow();
    CHECK_EQ(int(f.transport.sentToPort1[0].data[2]), 2);
}

TEST_CASE("engine: alternate profile activation sweep is profile-scoped") {
    Fixture f(MixerProfile::lo24hi8());
    f.engine.activate();
    f.engine.drainNow();

    // LO: ch3 CC64..111; HI: ch4 CC64..79 only.
    bool sawCh4Cc79 = false, sawCh4Cc80 = false;
    for (size_t i = 0; i < f.transport.sentToPort2.size(); ++i) {
        const MidiMessage& m = f.transport.sentToPort2[i];
        if ((m.data[0] & 0x0F) == 3 && m.data[1] == 79) sawCh4Cc79 = true;
        if ((m.data[0] & 0x0F) == 3 && m.data[1] == 80) sawCh4Cc80 = true;
    }
    CHECK(sawCh4Cc79);
    CHECK(!sawCh4Cc80);
}

TEST_CASE("engine: destruction while activated releases the console") {
    MixerProfile profile = MixerProfile::legacyDefault();
    Diagnostics diag;
    diag.setLevel(Diagnostics::Level::Error);
    FakeTransport* transport = new FakeTransport();
    AutomationEngine* engine = new AutomationEngine(profile, diag, *transport);
    engine->activate();
    engine->drainNow();
    transport->clear();
    delete engine; // destructor must release the console
    CHECK(!transport->sentToPort1.empty());
    CHECK(transport->sentToPort1.back().isSystemReset());
    delete transport;
}

TEST_CASE("engine: worker thread mode — activate/deactivate round trip") {
    MixerProfile profile = MixerProfile::legacyDefault();
    profile.budgetMessagesPerSecond = 5000; // fast flush for the test
    Diagnostics diag;
    diag.setLevel(Diagnostics::Level::Error);
    FakeTransport transport;
    {
        AutomationEngine engine(profile, diag, transport);
        engine.start();
        engine.activate();
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        CHECK(engine.isActivated());
        engine.setHostParameter(5, 0.25f);
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        engine.deactivate();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        engine.stop();
    }
    // All activation + fader + deactivation traffic went out, FF last.
    CHECK(!transport.sentToPort1.empty());
    CHECK(transport.sentToPort1.back().isSystemReset());
    bool sawFader = false;
    for (size_t i = 0; i < transport.sentToPort1.size(); ++i)
        if (transport.sentToPort1[i].data[1] == 10 && transport.sentToPort1[i].data[2] == 31)
            sawFader = true;
    CHECK(sawFader);
}

TEST_CASE("engine: test mode sends MIDI but never host automation") {
    Fixture f;
    f.engine.activate();
    f.engine.drainNow();
    f.transport.clear();
    f.listener.events.clear();

    f.engine.setTestMode(true);
    f.engine.drainNow();
    // Test mode ON: full reset then all strips to READ (1).
    CHECK_EQ(int(f.transport.sentToPort1[0].data[2]), 3); // reset sweep
    bool sawRead = false;
    for (size_t i = 0; i < f.transport.sentToPort1.size(); ++i)
        if (f.transport.sentToPort1[i].data[2] == 1) sawRead = true;
    CHECK(sawRead);

    f.transport.clear();
    f.listener.events.clear();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    f.engine.drainNow();
    // Animation updates went out; the listener saw nothing.
    CHECK(!f.transport.sentToPort1.empty());
    CHECK_EQ(f.listener.parameterEvents(), size_t(0));
    CHECK_EQ(f.listener.gestureEvents(), size_t(0));

    f.engine.setTestMode(false);
    f.engine.drainNow();
    CHECK(!f.engine.testMode());
}

TEST_CASE("engine: AUX mute round trip from console") {
    Fixture f;
    f.engine.activate();
    f.engine.drainNow();
    f.transport.clear();
    f.listener.events.clear();

    // Console: AUX 4 ON (value 9).
    f.transport.injectCc(5, 96, 9);
    f.engine.drainNow();
    CHECK_EQ(f.listener.parameterEvents(), size_t(1));
    CHECK_EQ(int(f.listener.events[0].param), 147);
    CHECK_NEAR(f.listener.events[0].value, 1.f, 1e-6);
    CHECK_EQ(f.transport.sentToPort2.size(), size_t(0)); // no echo

    // Host commands AUX 4 OFF.
    f.engine.setHostParameter(147, 0.f);
    f.engine.drainNow();
    CHECK_EQ(int(f.transport.sentToPort2.back().data[2]), 8);
}

TEST_CASE("engine: pending target survives console reports of the old state") {
    // Use a trickle budget so outbound positions stay queued (pending).
    MixerProfile profile = MixerProfile::legacyDefault();
    profile.budgetMessagesPerSecond = 1; // 0.001 credit/tick: nothing drains
    Diagnostics diag;
    diag.setLevel(Diagnostics::Level::Error);
    FakeTransport transport;
    AutomationEngine engine(profile, diag, transport);
    MockListener listener;
    engine.setListener(&listener);

    engine.activate();
    for (int i = 0; i < 20; ++i) engine.processOnce();
    CHECK(engine.isActivated());
    CHECK(engine.scheduler().pending() > 0); // activation queue backed up
    transport.clear();
    listener.events.clear();

    // Host commands fader 0 → 0.9. The message is pending (not yet sent).
    engine.setHostParameter(0, 0.9f);
    engine.processOnce();
    CHECK(engine.scheduler().hasPending(0));

    // The console reports the position of the *previous* command (motor still
    // traveling to the old target). This must NOT cancel the pending target
    // and must NOT be reported as a user move.
    transport.injectCc(1, 0, 10);
    engine.processOnce();
    CHECK(engine.scheduler().hasPending(0)); // pending survived
    CHECK_EQ(listener.parameterEvents(), size_t(0)); // not a user move
    CHECK_NEAR(engine.getParameter(0), 10.f / 127.f, 1e-6); // visible state updated

    // A second report, still while pending: same treatment.
    transport.injectCc(1, 0, 20);
    engine.processOnce();
    CHECK(engine.scheduler().hasPending(0));
    CHECK_EQ(listener.parameterEvents(), size_t(0));
    engine.setListener(nullptr);
}

} // namespace

int main(int argc, char** argv) {
    return testfw::Registry::instance().runAll(argc > 1 ? argv[1] : nullptr);
}
