// AutomationEngine — bidirectional automation state machine with
// origin tracking and feedback-loop prevention (docs/ARCHITECTURE.md §4).
//
// Threading contract:
//   * `setHostParameter` / `getParameter` / `handleIncoming` are real-time
//     safe (lock-free pushes / atomic loads) and may be called from any
//     thread (Logic's audio thread, CoreMIDI's read proc).
//   * Everything else runs on the bridge worker thread (`start()`), which
//     owns the scheduler, touch modes, test mode and all console I/O.
//   * UI-facing commands (`activate`, …) marshal onto the worker through a
//     mutex/condvar queue.
//
// Lifecycle contract (authoritative):
//   * `deactivate()` releases the console with the full legacy sequence
//     (CC127=0 → modes to ISO → master/joystick SEL 0 → 0xFF, positions
//     canceled). Call it explicitly for orderly teardown — the UI's
//     Deactivate button, the AU's disposal path and the destructor all use it.
//   * The DESTRUCTOR is the safety net: if the console is still in remote
//     mode it runs the same release sequence (on the worker if it is
//     running, inline otherwise) before anything else is torn down. Explicit
//     deactivation is therefore recommended but never required for safety.
//   * `stop()` drains queued work (including a pending deactivation) and
//     joins the worker; after destruction nothing runs, nothing sends, and
//     the transport's inbound handler is detached before return.
#ifndef CINEMIX_AUTOMATION_ENGINE_H
#define CINEMIX_AUTOMATION_ENGINE_H

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "cinemix/CinemixProtocol.h"
#include "cinemix/Diagnostics.h"
#include "cinemix/FaderOscillator.h"
#include "cinemix/MidiParser.h"
#include "cinemix/MidiTransport.h"
#include "cinemix/MixerProfile.h"
#include "cinemix/ParameterMap.h"
#include "cinemix/RingBuffer.h"
#include "cinemix/TouchModeTracker.h"
#include "cinemix/TransmissionScheduler.h"
#include "cinemix/Types.h"

namespace cinemix {

class AutomationEngine {
public:
    // Host-side observer (the AU implements this to drive Logic automation).
    struct Listener {
        virtual ~Listener() {}
        // Touch-based parameter gesture (console touch sensor → host write
        // arming). Called on the worker thread.
        virtual void onGesture(ParamId param, bool begin) = 0;
        // A parameter changed with a non-host origin (console move, UI
        // action, snapshot/reset). Called on the worker thread.
        virtual void onParameter(ParamId param, float value, Origin origin) = 0;
        // Console remote mode entered/left.
        virtual void onConnected(bool activated) = 0;
    };

    AutomationEngine(const MixerProfile& profile, Diagnostics& diag, IMidiTransport& transport);
    ~AutomationEngine(); // safety net: releases the console, then stops everything

    AutomationEngine(const AutomationEngine&) = delete;
    AutomationEngine& operator=(const AutomationEngine&) = delete;

    void setListener(Listener* listener) { listener_ = listener; }

    // ---- Host-facing, real-time safe --------------------------------------
    void setHostParameter(ParamId param, float value);
    float getParameter(ParamId param) const;
    std::size_t parameterCount() const { return paramMap_.size(); }

    // ---- Transport-facing, real-time safe ---------------------------------
    void handleIncoming(const std::uint8_t* data, std::size_t size);

    // ---- UI-facing commands (marshaled to the worker) ---------------------
    void activate();
    void deactivate();
    void sendSnapshot();
    void resetAll();
    void toggleAllMutes();
    void setTestMode(bool on);

    bool isActivated() const { return activated_.load(std::memory_order_acquire); }
    bool testMode() const { return testMode_.load(std::memory_order_acquire); }

    // ---- Thread control ----------------------------------------------------
    void start(); // idempotent; spawns the worker
    void stop();  // drains queued work (incl. pending MIDI) then joins

    // ---- Accessors (immutable after construction) --------------------------
    const MixerProfile& profile() const { return profile_; }
    const CinemixProtocol& protocol() const { return protocol_; }
    const ParameterMap& parameterMap() const { return paramMap_; }
    const TouchModeTracker& touchModes() const { return touchModes_; }
    TransmissionScheduler& scheduler() { return scheduler_; }

    // ---- Synchronous helpers for tests/harness (no worker thread) ----------
    // Processes everything pending in one pass, unpaced. Call after injecting
    // events/commands when the worker is NOT running.
    void drainNow();

    // A single paced worker cycle (no wait): inbound → host events → commands
    // → one budget-limited scheduler tick. Returns true if outbound work
    // remains. Test/harness use only (the worker thread runs this loop).
    bool processOnce();

private:
    struct HostEvent {
        ParamId param;
        float value;
    };

    // Queue geometry (documented bounds; overflow is counted, never fatal).
    static constexpr std::size_t kInboundQueueBytes = 65536;
    static constexpr std::size_t kHostEventQueueCapacity = 1024;
    static constexpr std::size_t kInboundDrainBatch = 512;
    // Test-mode oscillator recomputation throttle: 20 Hz is plenty for a
    // 12-second wave; the scheduler coalesces anything beyond the budget.
    static constexpr std::chrono::milliseconds kOscillatorStepPeriod{50};

    static void parserCcCallback(void* user, std::uint8_t channel, std::uint8_t cc,
                                 std::uint8_t value);
    static void parserSystemCallback(void* user, std::uint8_t status);
    static void parserMalformedCallback(void* user);

    void handleConsoleEvent(const ConsoleEvent& event);
    void setParamInternal(ParamId param, float value, Origin origin, bool notify,
                          bool sendOutbound);
    void enqueueOutbound(ParamId param, float value);
    void snapshotInternal(Origin origin);
    void resetAllInternal();
    void activateInternal();
    void deactivateInternal();
    void testModeInternal(bool on);
    bool isEcho(ParamId param, float incoming) const;
    void cancelPending(ParamId param);

    void workerLoop();
    void processInbound();
    void processHostEvents();
    void processCommands(std::unique_lock<std::mutex>& lock);
    void stepTestMode();

    const MixerProfile profile_;
    Diagnostics& diag_;
    IMidiTransport& transport_;
    CinemixProtocol protocol_;
    ParameterMap paramMap_;
    TransmissionScheduler scheduler_;
    TouchModeTracker touchModes_;
    FaderOscillator oscillator_;
    MidiParser parser_;
    Listener* listener_;

    // Parameter state.
    std::unique_ptr<std::atomic<float>[]> values_;
    // Worker-only. lastProcessed_ = last value through the engine for this
    // parameter (any origin) — the legacy prev_CC_Val dedupe reference.
    // lastCommanded_ = last value we commanded the console (outbound) — the
    // echo-suppression baseline; -1 = never commanded.
    std::vector<float> lastProcessed_;
    std::vector<float> lastCommanded_;
    std::vector<std::uint8_t> touched_; // worker-only: console touch state per param

    std::atomic<bool> activated_;
    std::atomic<bool> testMode_;
    bool allMutes_; // worker-only

    // Test mode state (worker-only).
    std::vector<std::uint8_t> savedStripModes_;
    std::chrono::steady_clock::time_point testStart_;
    std::chrono::steady_clock::time_point lastOscillatorStep_;

    // Inbound (transport → worker).
    SpScRingBuffer<MidiByte> inbound_;

    // Host parameter writes (audio thread → worker).
    SpScRingBuffer<HostEvent> hostEvents_;

    // Command queue (UI thread → worker).
    std::mutex cmdMu_;
    std::condition_variable cmdCv_;
    std::deque<std::function<void()> > cmdQ_;
    bool stopRequested_;
    bool workerRunning_;
    std::thread worker_;
};

} // namespace cinemix

#endif // CINEMIX_AUTOMATION_ENGINE_H
