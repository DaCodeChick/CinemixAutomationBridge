// cinemix_harness — hardware-free testing harness for the Cinemix Automation
// Bridge (docs/TESTING.md).
//
//   selftest [--out FILE.cmi]   scripted console scenario over a loopback
//                               transport; asserts the bridge's behavior and
//                               records the session to a .cmi capture.
//   replay FILE.cmi             feeds a recorded capture's console-side
//                               traffic through a fresh bridge and logs every
//                               decoded event and every outbound byte.
//   demo                        paced interactive-style scenario with a
//                               simulated motor, printing the traffic.
//
// Runs on any POSIX system; on the target Mac the same core is used by the
// Audio Unit (see mac/).
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "FakeConsole.h"
#include "RecordingTransport.h"
#include "cinemix/AutomationEngine.h"
#include "cinemix/CaptureFile.h"
#include "cinemix/Diagnostics.h"
#include "cinemix/testing/FakeTransport.h"

using namespace cinemix;
using namespace cinemix_test;
using namespace cinemix_harness;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

struct PrintListener : public AutomationEngine::Listener {
    void onGesture(ParamId p, bool begin) override {
        std::printf("  host gesture: param %u %s\n", static_cast<unsigned>(p), begin ? "BEGIN" : "END");
    }
    void onParameter(ParamId p, float v, Origin o) override {
        std::printf("  host parameter: %u = %.4f (origin %d)\n", static_cast<unsigned>(p), v, static_cast<int>(o));
    }
    void onConnected(bool a) override {
        std::printf("  console %s\n", a ? "ACTIVATED" : "RELEASED");
    }
};

std::string hexByte(std::uint8_t value) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.push_back(kHex[(value >> 4) & 0x0F]);
    out.push_back(kHex[value & 0x0F]);
    return out;
}

std::string describe(const MidiMessage& m) {
    std::string text = "port" + std::to_string(static_cast<unsigned>(m.port)) + ": ";
    if (m.length == 1) return text + "FF";
    for (std::uint8_t i = 0; i < m.length; ++i) {
        if (i > 0) text.push_back(' ');
        text += hexByte(m.data[i]);
    }
    return text;
}

// ---------------------------------------------------------------------------

int runSelftest(const std::string& capturePath) {
    g_failures = 0;
    std::printf("== Cinemix bridge selftest (simulated console) ==\n");

    MixerProfile profile = MixerProfile::legacyDefault();
    Diagnostics diag;
    diag.setLevel(Diagnostics::Level::Info);
    diag.setSink([](Diagnostics::Level l, const std::string& m) {
        std::printf("  diag[%s] %s\n", Diagnostics::levelName(l), m.c_str());
    });

    FakeTransport transport;
    CaptureWriter capture(capturePath);
    RecordingTransport recorded(transport, capture);
    AutomationEngine engine(profile, diag, recorded);
    PrintListener listener;
    engine.setListener(&listener);

    // The transport's inbound path: the harness forwards console bytes here.
    FakeConsole console(recorded);

    // 1. Activation — verify the legacy sequence reached the console.
    engine.activate();
    engine.drainNow();
    check(engine.isActivated(), "activation: engine reports activated");
    check(!transport.sentToPort1.empty() && !transport.sentToPort2.empty(),
          "activation: traffic on both console ports");
    const MidiMessage& first = transport.sentToPort1[0];
    check(first.length == 3 && (first.data[0] & 0x0F) == 4 &&
              first.data[1] == 127 && first.data[2] == 127,
          "activation: first message is CC127=127 on ch5 (remote mode)");
    {
        // Sweeps are broadcast: port 1 sees ch3 (48 LO touch CCs) and ch4
        // (24 HI touch CCs + 2 joystick SEL CCs 88/90, all mode writes).
        size_t ch3v2 = 0, ch3v3 = 0, ch4v2 = 0, ch4v3 = 0;
        bool masterMax = false;
        for (size_t i = 0; i < transport.sentToPort2.size(); ++i) {
            const MidiMessage& m = transport.sentToPort2[i];
            const uint8_t ch = static_cast<uint8_t>(m.data[0] & 0x0F) + 1;
            if (m.data[1] >= 64 && m.data[1] <= 111) {
                if (ch == 3 && m.data[2] == 2) ++ch3v2;
                if (ch == 3 && m.data[2] == 3) ++ch3v3;
                if (ch == 4 && m.data[2] == 2) ++ch4v2;
                if (ch == 4 && m.data[2] == 3) ++ch4v3;
            }
            if (ch == 5 && m.data[1] == 0 && m.data[2] == 127) masterMax = true;
        }
        check(ch3v2 == 48 && ch4v2 == 24 + 2,
              "activation: WRITE sweep = 48 LO slots + 24 HI slots + 2 joystick SELs");
        check(ch3v3 == 48 && ch4v3 == 24 + 2,
              "activation: AUTO sweep = 48 LO slots + 24 HI slots + 2 joystick SELs");
        check(masterMax, "activation: snapshot commands the master fader to 127");
    }

    // 2. Touch → write-mode reply (high priority).
    transport.clear();
    console.pressTouch(3, 64); // strip 1 Chan path
    engine.drainNow();
    check(transport.sentToPort1.size() == 1 && transport.sentToPort1[0].data[1] == 64 &&
              transport.sentToPort1[0].data[2] == 2,
          "touch: bridge replies WRITE(2) on the touch CC");

    // 3. Hand move while touched → host automation, no bounce.
    transport.clear();
    console.sendCc(1, 0, 100); // fader 1 moved by hand
    engine.drainNow();
    check(transport.sentToPort1.empty(), "hand move: nothing bounced back to the console");
    check(engine.getParameter(0) > 0.7f, "hand move: engine state follows the console");

    // 4. Release.
    transport.clear();
    console.releaseTouch(3, 64);
    engine.drainNow();
    check(!transport.sentToPort1.empty() && transport.sentToPort1.back().data[2] == 3,
          "release: bridge replies AUTO(3)");

    // 5. Host automation → console.
    transport.clear();
    engine.setHostParameter(0, 0.5f);
    engine.drainNow();
    check(transport.sentToPort1.size() == 1 && transport.sentToPort1[0].data[1] == 0 &&
              transport.sentToPort1[0].data[2] == 63,
          "host automation: fader 1 = 0.5 reaches the console as CC0=63");

    // 6. Console echoes the commanded position (motor confirmation).
    transport.clear();
    console.sendCc(1, 0, 63);
    engine.drainNow();
    check(transport.sentToPort1.empty(), "echo: confirmed position is not re-sent");

    // 7. Mute round trip.
    transport.clear();
    console.setMute(3, 0, true); // strip 1 Chan mute ON
    engine.drainNow();
    check(engine.getParameter(72) == 1.f, "mute: console ON reaches the engine");
    engine.setHostParameter(72, 0.f);
    engine.drainNow();
    check(!transport.sentToPort1.empty() && transport.sentToPort1.back().data[2] == 2,
          "mute: host OFF reaches the console");

    // 8. SEL press rotates the strip mode (3 → 0 after activation).
    transport.clear();
    console.pressSel(3, 64);
    engine.drainNow();
    check(!transport.sentToPort1.empty() && transport.sentToPort1.back().data[2] == 0,
          "SEL press: mode rotates AUTO(3) → ISO(0) and replies");

    // 9. Deactivation — console released, 0xFF is the final byte.
    transport.clear();
    engine.deactivate();
    engine.drainNow();
    check(!transport.sentToPort1.empty(), "deactivation: release traffic sent");
    check(transport.sentToPort1.back().isSystemReset() &&
              transport.sentToPort2.back().isSystemReset(),
          "deactivation: 0xFF system reset is the final byte on both ports");
    check(!engine.isActivated(), "deactivation: engine reports released");

    engine.setListener(nullptr);
    std::printf("selftest: %s (%d failure(s)); capture: %s\n",
                g_failures == 0 ? "PASSED" : "FAILED", g_failures,
                capture.ok() ? capturePath.c_str() : "(capture failed)");
    return g_failures == 0 ? 0 : 1;
}

// ---------------------------------------------------------------------------

int runReplay(const std::string& path) {
    std::printf("== Replaying capture: %s ==\n", path.c_str());
    CaptureReader reader(path);
    if (!reader.ok()) {
        std::printf("cannot open capture file\n");
        return 1;
    }
    MixerProfile profile = MixerProfile::legacyDefault();
    Diagnostics diag;
    diag.setLevel(Diagnostics::Level::MidiOut);
    diag.setSink([](Diagnostics::Level l, const std::string& m) {
        std::printf("  diag[%s] %s\n", Diagnostics::levelName(l), m.c_str());
    });
    FakeTransport transport;
    AutomationEngine engine(profile, diag, transport);
    PrintListener listener;
    engine.setListener(&listener);

    // Replayed console traffic applies only to an activated bridge.
    engine.activate();
    engine.drainNow();
    transport.clear();

    CaptureEvent ev;
    while (reader.next(ev)) {
        if (ev.direction == 0) {
            // Console → bridge: feed the bytes.
            std::printf("  [t=%llu] console -> bridge: %s\n",
                        static_cast<unsigned long long>(ev.timestampUs), describe(ev.message).c_str());
            engine.handleIncoming(ev.message.data.data(), ev.message.length);
            engine.drainNow();
        } else {
            std::printf("  [t=%llu] bridge -> console: %s\n",
                        static_cast<unsigned long long>(ev.timestampUs), describe(ev.message).c_str());
        }
    }
    if (reader.corruptCount() > 0)
        std::printf("warning: %zu corrupt record(s) in capture\n", reader.corruptCount());
    engine.setListener(nullptr);
    return 0;
}

// ---------------------------------------------------------------------------

int runDemo() {
    std::printf("== Demo: simulated motor following host automation ==\n");
    MixerProfile profile = MixerProfile::legacyDefault();
    Diagnostics diag;
    diag.setLevel(Diagnostics::Level::MidiOut);
    diag.setSink([](Diagnostics::Level l, const std::string& m) {
        std::printf("  diag[%s] %s\n", Diagnostics::levelName(l), m.c_str());
    });
    FakeTransport transport;
    AutomationEngine engine(profile, diag, transport);
    PrintListener listener;
    engine.setListener(&listener);
    FakeConsole console(transport);

    engine.activate();
    engine.drainNow();
    transport.clear();

    // Host writes a fader ramp; the console echoes motor progress.
    std::printf("  host: fader 1 ramp 0.0 -> 1.0\n");
    for (int i = 0; i <= 10; ++i) {
        engine.setHostParameter(0, static_cast<float>(i) / 10.f);
        engine.drainNow();
        const MidiMessage* m = transport.last(1);
        if (m) {
            console.emulateMotorEcho(1, 0, static_cast<uint8_t>(i == 0 ? 0 : (i - 1) * 12), m->data[2]);
            engine.drainNow();
        }
    }
    std::printf("  final fader state: %.4f (console saw %u)\n",
                engine.getParameter(0), static_cast<unsigned>(transport.sentToPort1.back().data[2]));
    engine.setListener(nullptr);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("usage: %s selftest [--out FILE.cmi] | replay FILE.cmi | demo\n", argv[0]);
        return 2;
    }
    if (std::strcmp(argv[1], "selftest") == 0) {
        std::string out = "selftest.cmi";
        for (int i = 2; i + 1 < argc; i += 2)
            if (std::strcmp(argv[i], "--out") == 0) out = argv[i + 1];
        return runSelftest(out);
    }
    if (std::strcmp(argv[1], "replay") == 0 && argc >= 3) return runReplay(argv[2]);
    if (std::strcmp(argv[1], "demo") == 0) return runDemo();
    std::printf("unknown command\n");
    return 2;
}
