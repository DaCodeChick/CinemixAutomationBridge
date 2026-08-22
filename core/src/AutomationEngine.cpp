#include "cinemix/AutomationEngine.h"

#include <cmath>
#include <cstdio>
#include <system_error>

namespace cinemix {

// Out-of-class definitions for the static constexpr data members (pre-C++17
// ODR rule: required when a member is odr-used, e.g. kOscillatorStepPeriod is
// bound to a const& by the duration comparison in stepTestMode).
constexpr std::size_t AutomationEngine::kInboundQueueBytes;
constexpr std::size_t AutomationEngine::kInboundDrainBatch;
constexpr std::chrono::milliseconds AutomationEngine::kOscillatorStepPeriod;

// ---------------------------------------------------------------------------
// Construction / destruction

AutomationEngine::AutomationEngine(const MixerProfile& profile, Diagnostics& diag,
                                   IMidiTransport& transport)
    : profile_(profile), diag_(diag), transport_(transport),
      protocol_(profile), paramMap_(profile),
      scheduler_(profile, diag, transport),
      touchModes_(profile, protocol_, scheduler_),
      oscillator_(profile.faderCount()),
      listener_(nullptr),
      values_(new std::atomic<float>[profile.paramCount()]),
      hostDirty_(profile.paramCount()),
      lastProcessed_(profile.paramCount(), -1.f),
      lastCommanded_(profile.paramCount(), -1.f),
      touched_(profile.paramCount(), 0),
      activated_(false), testMode_(false), allMutes_(false),
      inbound_(kInboundQueueBytes),
      stopRequested_(false), workerRunning_(false) {
    for (std::size_t i = 0; i < profile_.paramCount(); ++i) {
        values_[i].store(paramMap_.info(static_cast<ParamId>(i)).defaultValue,
                         std::memory_order_relaxed);
        hostDirty_[i].store(0, std::memory_order_relaxed);
    }

    transport_.onIncoming = [this](const std::uint8_t* data, std::size_t size) {
        handleIncoming(data, size);
    };

    parser_.setHandlers(this, &AutomationEngine::parserCcCallback,
                        &AutomationEngine::parserSystemCallback,
                        &AutomationEngine::parserMalformedCallback);
}

AutomationEngine::~AutomationEngine() {
    // The listener may already be destroyed (members of an owner class are
    // torn down in reverse declaration order); never call it from here.
    listener_ = nullptr;

    // Safety net (legacy destructor behavior): always release the console.
    // If the worker is running, ask it to deactivate before stopping; if not,
    // run the release sequence inline. After this destructor returns, nothing
    // runs and nothing sends.
    if (workerRunning_) {
        if (activated_.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock(cmdMu_);
            cmdQ_.push_back([this]() { deactivateInternal(); });
            cmdCv_.notify_all();
        }
        stop();
    } else if (activated_.load(std::memory_order_acquire)) {
        deactivateInternal();
        scheduler_.drainToEmpty();
    }
    transport_.onIncoming = nullptr;
}

// ---------------------------------------------------------------------------
// Real-time safe entry points

void AutomationEngine::setHostParameter(ParamId param, float value) {
    if (param >= paramMap_.size()) return;
    // Multi-producer safe by construction: two atomic stores, no queue.
    // The release store of the dirty flag publishes the relaxed value store;
    // the worker's acquire exchange observes it. Concurrent writers to the
    // same parameter race benignly (latest value wins — the documented
    // coalescing semantic). Wait-free, allocation-free, non-blocking.
    values_[param].store(clamp01(value), std::memory_order_relaxed);
    hostDirty_[param].store(1, std::memory_order_release);
}

float AutomationEngine::getParameter(ParamId param) const {
    if (param >= paramMap_.size()) return 0.0f;
    return values_[param].load(std::memory_order_relaxed);
}

void AutomationEngine::handleIncoming(const std::uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        if (!inbound_.push(data[i])) {
            if ((inbound_.overflowCount() & 0x3FFu) == 1)
                diag_.warning("MIDI input queue overflow — console data lost");
        }
    }
}

// ---------------------------------------------------------------------------
// UI commands (marshaled to the worker)

void AutomationEngine::activate() {
    std::lock_guard<std::mutex> lock(cmdMu_);
    cmdQ_.push_back([this]() { activateInternal(); });
    cmdCv_.notify_all();
}

void AutomationEngine::deactivate() {
    std::lock_guard<std::mutex> lock(cmdMu_);
    cmdQ_.push_back([this]() { deactivateInternal(); });
    cmdCv_.notify_all();
}

void AutomationEngine::sendSnapshot() {
    std::lock_guard<std::mutex> lock(cmdMu_);
    cmdQ_.push_back([this]() { snapshotInternal(Origin::UserInterface); });
    cmdCv_.notify_all();
}

void AutomationEngine::resetAll() {
    std::lock_guard<std::mutex> lock(cmdMu_);
    cmdQ_.push_back([this]() { resetAllInternal(); });
    cmdCv_.notify_all();
}

void AutomationEngine::toggleAllMutes() {
    std::lock_guard<std::mutex> lock(cmdMu_);
    cmdQ_.push_back([this]() {
        allMutes_ = !allMutes_;
        for (ParamId param = 0; param < paramMap_.size(); ++param) {
            if (paramMap_.info(param).isMuteLike)
                setParamInternal(param, allMutes_ ? 1.0f : 0.0f, Origin::UserInterface,
                                 true, true);
        }
        diag_.info(allMutes_ ? "all mutes ON" : "all mutes OFF");
    });
    cmdCv_.notify_all();
}

void AutomationEngine::setTestMode(bool on) {
    std::lock_guard<std::mutex> lock(cmdMu_);
    cmdQ_.push_back([this, on]() { testModeInternal(on); });
    cmdCv_.notify_all();
}

// ---------------------------------------------------------------------------
// Thread control

void AutomationEngine::start() {
    if (workerRunning_) return;
    stopRequested_ = false;
    workerRunning_ = true;
    try {
        worker_ = std::thread(&AutomationEngine::workerLoop, this);
    } catch (const std::system_error&) {
        // Thread creation failed (resource exhaustion): leave a coherent
        // stopped state, never a half-running engine.
        workerRunning_ = false;
        diag_.error("cannot create bridge worker thread");
        throw;
    }
}

void AutomationEngine::stop() {
    if (!workerRunning_) return;
    {
        std::lock_guard<std::mutex> lock(cmdMu_);
        stopRequested_ = true;
    }
    cmdCv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void AutomationEngine::drainNow() {
    processInbound();
    processHostEvents();
    {
        std::unique_lock<std::mutex> lock(cmdMu_);
        processCommands(lock);
    }
    if (testMode_.load(std::memory_order_acquire)) stepTestMode();
    scheduler_.drainToEmpty();
}

bool AutomationEngine::processOnce() {
    processInbound();
    processHostEvents();
    {
        std::unique_lock<std::mutex> lock(cmdMu_);
        processCommands(lock);
    }
    if (testMode_.load(std::memory_order_acquire)) stepTestMode();
    return scheduler_.tick();
}

// ---------------------------------------------------------------------------
// Parser callbacks (called on the worker thread from processInbound)

void AutomationEngine::parserCcCallback(void* user, std::uint8_t channel, std::uint8_t cc,
                                        std::uint8_t value) {
    AutomationEngine* self = static_cast<AutomationEngine*>(user);
    const std::uint8_t status = static_cast<std::uint8_t>(0xB0u | ((channel - 1) & 0x0F));
    if (static_cast<std::uint8_t>(self->diag_.level()) >=
        static_cast<std::uint8_t>(Diagnostics::Level::MidiIn)) {
        self->diag_.midiIn("RX ch=" + std::to_string(static_cast<unsigned>(channel)) +
                            " cc=" + std::to_string(static_cast<unsigned>(cc)) +
                            " val=" + std::to_string(static_cast<unsigned>(value)));
    }
    const ConsoleEvent event = self->protocol_.decode(status, cc, value);
    self->handleConsoleEvent(event);
}

void AutomationEngine::parserSystemCallback(void* user, std::uint8_t status) {
    AutomationEngine* self = static_cast<AutomationEngine*>(user);
    // A console never sends 0xFF (it is an input to the console), but if we
    // ever see one it means something downstream released remote mode.
    if (status == 0xFF)
        self->diag_.info("received system reset (0xFF) from console side");
    else
        self->diag_.verbose("received system realtime byte");
}

void AutomationEngine::parserMalformedCallback(void* user) {
    AutomationEngine* self = static_cast<AutomationEngine*>(user);
    self->diag_.verbose("malformed MIDI input ignored");
}

// ---------------------------------------------------------------------------
// Inbound console events

void AutomationEngine::handleConsoleEvent(const ConsoleEvent& event) {
    if (event.kind == EventKind::Unknown) {
        if (static_cast<std::uint8_t>(diag_.level()) >=
            static_cast<std::uint8_t>(Diagnostics::Level::Verbose)) {
            diag_.verbose("unknown console CC: ch=" +
                          std::to_string(static_cast<unsigned>(event.channel)) +
                          " cc=" + std::to_string(static_cast<unsigned>(event.cc)) +
                          " val=" + std::to_string(static_cast<unsigned>(event.value)));
        }
        return;
    }
    if (event.kind == EventKind::Ignored) {
        diag_.verbose("ignored console CC (protocol value not meaningful)");
        return;
    }

    // The legacy bridge received nothing before activation (its RtMidi ports
    // were only opened by ACTIVATE). We keep reading for diagnostics, but
    // apply nothing until activated — same observable behavior.
    if (!activated_.load(std::memory_order_acquire)) {
        diag_.verbose("console event ignored (bridge not activated)");
        return;
    }

    // During Test Mode, console reports (motor echoes of the oscillator) must
    // not leak into the host as user automation.
    const bool testModeActive = testMode_.load(std::memory_order_acquire);

    ParamId param = kNoParam;
    switch (event.kind) {
    case EventKind::FaderPosition:
    case EventKind::MuteChanged: {
        if (!paramMap_.find(event.control, param)) {
            diag_.verbose("event for unmapped control");
            return;
        }
        const float value = (event.kind == EventKind::FaderPosition)
                                ? event.normalized
                                : (event.on ? 1.0f : 0.0f);
        const bool notifyHost = !testModeActive;
        if (touched_[param]) {
            // A hand on the fader: user-originated. The hand beats the motor —
            // cancel whatever the host commanded and report to the host.
            cancelPending(param);
            setParamInternal(param, value, Origin::Console, notifyHost, false);
        } else if (scheduler_.hasPending(param)) {
            // We commanded a value that has not reached the wire yet; this
            // report reflects the *previous* state (motor still traveling).
            // Keep the pending target, update the visible state silently.
            setParamInternal(param, value, Origin::Console, false, false);
        } else if (isEcho(param, value)) {
            // Console reporting near the position we commanded (motor echo /
            // interpolation jitter): update state silently, never bounce it
            // back to the host.
            setParamInternal(param, value, Origin::Console, false, false);
        } else {
            // Console moved without touch registering (hand move, master
            // fader, mutes): user-originated.
            cancelPending(param);
            setParamInternal(param, value, Origin::Console, notifyHost, false);
        }
        break;
    }
    case EventKind::TouchBegin: {
        // Touch sensors are not parameters; they gate the strip's fader.
        const ParamId faderParam =
            paramMap_.stripFaderId(event.control.strip, event.control.path);
        if (faderParam == kNoParam) return;
        touched_[faderParam] = 1;
        if (listener_ && !testModeActive) listener_->onGesture(faderParam, true);
        touchModes_.onTouchChanged(event.control.strip, event.control.path, true);
        break;
    }
    case EventKind::TouchEnd: {
        const ParamId faderParam =
            paramMap_.stripFaderId(event.control.strip, event.control.path);
        if (faderParam == kNoParam) return;
        touched_[faderParam] = 0;
        if (listener_ && !testModeActive) listener_->onGesture(faderParam, false);
        touchModes_.onTouchChanged(event.control.strip, event.control.path, false);
        break;
    }
    case EventKind::SelPressed:
        touchModes_.onSelPressed(event.control.strip, event.control.path);
        break;
    case EventKind::MasterSelPressed:
        touchModes_.onMasterSelPressed();
        break;
    case EventKind::AuxMuteChanged: {
        ControlRef reference = event.control;
        if (!paramMap_.find(reference, param)) {
            diag_.verbose("AUX event for unmapped control");
            return;
        }
        const float value = event.on ? 1.0f : 0.0f;
        if (scheduler_.hasPending(param))
            setParamInternal(param, value, Origin::Console, false, false);
        else if (isEcho(param, value))
            setParamInternal(param, value, Origin::Console, false, false);
        else {
            cancelPending(param);
            setParamInternal(param, value, Origin::Console, !testModeActive, false);
        }
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// State + outbound

void AutomationEngine::setParamInternal(ParamId param, float value, Origin origin,
                                        bool notify, bool sendOutbound) {
    if (param >= paramMap_.size()) return;
    const float clamped = clamp01(value);
    const float previousProcessed = lastProcessed_[param];
    values_[param].store(clamped, std::memory_order_relaxed);
    lastProcessed_[param] = clamped; // legacy prev_CC_Val: updated on every processed value
    if (sendOutbound) {
        if (activated_.load(std::memory_order_acquire)) {
            // Legacy dedupe semantics: skip when the value did not change
            // since the last value processed for this parameter — compared on
            // the parameter domain, not the wire byte (mute 2/3, AUX 2n/2n+1
            // encodings make wire-byte dedupe wrong).
            const bool sameValue =
                (previousProcessed >= 0.0f) &&
                (paramMap_.info(param).isMuteLike
                     ? ((clamped != 0.0f) == (previousProcessed != 0.0f))
                     : (quantize7(clamped) == quantize7(previousProcessed)));
            if (!sameValue) {
                enqueueOutbound(param, clamped);
                lastCommanded_[param] = clamped;
            }
        }
    }
    if (notify && listener_) listener_->onParameter(param, clamped, origin);
}

void AutomationEngine::enqueueOutbound(ParamId param, float value) {
    const ParameterInfo& info = paramMap_.info(param);
    const ControlRef& control = info.control;
    switch (control.cls) {
    case ControlClass::Fader: {
        const std::vector<MidiMessage> messages =
            protocol_.encodeStripFader(control.strip, control.path, value);
        if (!messages.empty()) {
            scheduler_.enqueuePosition(param, messages[0]);
            // 14-bit mode sends a fine CC as well; append it without
            // coalescing (it must not be dropped when a newer MSB arrives).
            for (std::size_t i = 1; i < messages.size(); ++i) {
                OutboundCommand command;
                command.kind = CommandKind::SetMode;
                command.message = messages[i];
                command.param = kNoParam;
                scheduler_.enqueueCommand(command);
            }
        }
        break;
    }
    case ControlClass::Mute:
        scheduler_.enqueuePosition(param,
            protocol_.encodeStripMute(control.strip, control.path, value != 0.0f));
        break;
    case ControlClass::JoyAxis:
        scheduler_.enqueuePosition(param,
            protocol_.encodeJoyAxis(control.index,
                                    control.path == StripPath::Chan ? 0 : 1, value));
        break;
    case ControlClass::JoyMute:
        scheduler_.enqueuePosition(param, protocol_.encodeJoyMute(control.index, value != 0.0f));
        break;
    case ControlClass::AuxMute:
        scheduler_.enqueuePosition(param, protocol_.encodeAuxMute(control.index, value != 0.0f));
        break;
    case ControlClass::MasterFader:
        scheduler_.enqueuePosition(param, protocol_.encodeMasterFader(value));
        break;
    default:
        // Touch/SEL are not parameters.
        break;
    }
}

bool AutomationEngine::isEcho(ParamId param, float incoming) const {
    if (param >= lastCommanded_.size()) return false;
    const float baseline = lastCommanded_[param];
    if (baseline < 0.0f) return false; // we never commanded this control
    if (paramMap_.info(param).isMuteLike) return incoming == baseline;
    const float window =
        static_cast<float>(profile_.echoHysteresisSteps) * faderHysteresis();
    return std::fabs(incoming - baseline) <= window + 1e-6f;
}

void AutomationEngine::cancelPending(ParamId param) {
    scheduler_.cancelPosition(param);
}

// ---------------------------------------------------------------------------
// Command implementations (worker thread)

void AutomationEngine::snapshotInternal(Origin origin) {
    for (ParamId param = 0; param < paramMap_.size(); ++param) {
        const float value = values_[param].load(std::memory_order_relaxed);
        // Legacy: SendSnapshot resets the dedupe cache and re-sends every
        // parameter; the sent values become the new dedupe/echo references.
        lastProcessed_[param] = value;
        lastCommanded_[param] = value;
        if (activated_.load(std::memory_order_acquire)) enqueueOutbound(param, value);
        if (listener_) listener_->onParameter(param, value, origin);
    }
    diag_.info("snapshot sent");
}

void AutomationEngine::resetAllInternal() {
    touchModes_.setAllStripModes(3);
    touchModes_.setMasterMode(3);
    touchModes_.setJoystickModes(3);
    for (ParamId param = 0; param < paramMap_.size(); ++param) {
        const bool isMaster = paramMap_.info(param).control.cls == ControlClass::MasterFader;
        values_[param].store(isMaster ? 1.0f : 0.0f, std::memory_order_relaxed);
    }
    allMutes_ = false;
    snapshotInternal(Origin::UserInterface);
    diag_.info("reset all");
}

void AutomationEngine::activateInternal() {
    if (activated_.load(std::memory_order_acquire)) return;
    if (!transport_.connected()) {
        diag_.error("cannot activate: MIDI outputs are not connected");
        return;
    }
    diag_.info("activating console (remote control mode)");

    // Legacy activation sequence, byte-for-byte (docs/COMPATIBILITY.md §1):
    // remote mode → everything to WRITE → snapshot → everything to AUTO.
    OutboundCommand command;
    command.kind = CommandKind::RemoteControl;
    command.message = protocol_.remoteControl(true);
    command.param = kNoParam;
    scheduler_.enqueueCommand(command);

    touchModes_.setAllStripModes(2);
    touchModes_.setMasterMode(2);
    touchModes_.setJoystickModes(2);

    activated_.store(true, std::memory_order_release);
    snapshotInternal(Origin::Internal);

    touchModes_.setAllStripModes(3);
    touchModes_.setMasterMode(3);
    touchModes_.setJoystickModes(3);

    if (listener_) listener_->onConnected(true);
    diag_.info("console activated");
}

void AutomationEngine::deactivateInternal() {
    diag_.info("deactivating console (releasing remote control mode)");

    // Test Mode must not outlive the console link: stop the oscillator before
    // the mode sweep overwrites everything with ISO(0).
    if (testMode_.load(std::memory_order_acquire)) {
        testMode_.store(false, std::memory_order_release);
        savedStripModes_.clear();
        diag_.info("test mode stopped (console released)");
    }

    // Legacy deactivation sequence, byte-for-byte: CC127=0 → everything to
    // ISO(0) → master/joystick SEL 0 → system reset 0xFF. Position data
    // pending in the queue is canceled so nothing is sent after the reset.
    OutboundCommand command;
    command.kind = CommandKind::RemoteControl;
    command.message = protocol_.remoteControl(false);
    command.param = kNoParam;
    scheduler_.enqueueCommand(command);

    touchModes_.setAllStripModes(0);
    touchModes_.setMasterMode(0);
    touchModes_.setJoystickModes(0);

    command.kind = CommandKind::SystemReset;
    command.message = protocol_.systemReset();
    scheduler_.enqueueCommand(command);
    scheduler_.cancelAllPositions();

    for (std::size_t param = 0; param < touched_.size(); ++param) {
        if (touched_[param]) {
            touched_[param] = 0;
            if (listener_) listener_->onGesture(static_cast<ParamId>(param), false);
        }
    }
    activated_.store(false, std::memory_order_release);
    if (listener_) listener_->onConnected(false);
    diag_.info("console deactivated");
}

void AutomationEngine::testModeInternal(bool on) {
    if (on) {
        if (!activated_.load(std::memory_order_acquire)) {
            diag_.warning("test mode requires an activated console");
            return;
        }
        if (testMode_.load(std::memory_order_acquire)) return;

        // Move strip modes to READ(1) for the duration: the console follows
        // incoming positions but its touch→WRITE gating is disabled (the
        // legacy bridge used READ for its fader test for the same reason).
        // Modes are restored on exit. Nothing else is touched: no reset, no
        // snapshot, no mutes.
        savedStripModes_ = touchModes_.allStripModes();
        touchModes_.setAllStripModes(1);
        testStart_ = std::chrono::steady_clock::now();
        lastOscillatorStep_ = testStart_;
        testMode_.store(true, std::memory_order_release);
        diag_.info("test mode ON (fader oscillator)");
    } else {
        if (!testMode_.load(std::memory_order_acquire)) return;
        testMode_.store(false, std::memory_order_release);
        touchModes_.restoreAllStripModes(savedStripModes_);
        savedStripModes_.clear();
        // Immediate stop: drop everything still queued for the console; the
        // oscillator never writes again and host automation re-queues its
        // values on its own cadence.
        scheduler_.cancelAllPositions();
        diag_.info("test mode OFF (modes restored)");
    }
}

// ---------------------------------------------------------------------------
// Worker

void AutomationEngine::workerLoop() {
    std::unique_lock<std::mutex> lock(cmdMu_);
    while (true) {
        lock.unlock();
        processInbound();
        processHostEvents();
        if (testMode_.load(std::memory_order_acquire)) stepTestMode();
        scheduler_.tick();
        lock.lock();

        if (stopRequested_) {
            // Execute whatever was queued (e.g. a deactivate safety command),
            // then flush the console output before exiting.
            while (!cmdQ_.empty()) {
                std::function<void()> fn = std::move(cmdQ_.front());
                cmdQ_.pop_front();
                fn();
            }
            lock.unlock();
            processInbound();
            processHostEvents();
            scheduler_.drainToEmpty();
            workerRunning_ = false;
            return;
        }

        processCommands(lock);
        cmdCv_.wait_for(lock, std::chrono::milliseconds(profile_.schedulerTickMs));
    }
}

void AutomationEngine::processCommands(std::unique_lock<std::mutex>& lock) {
    // `lock` is held by contract (worker loop or drainNow).
    static_cast<void>(lock);
    while (!cmdQ_.empty()) {
        std::function<void()> fn = std::move(cmdQ_.front());
        cmdQ_.pop_front();
        fn(); // commands are quick, worker-only internals; no lock re-entry
    }
}

void AutomationEngine::processInbound() {
    // Stack batch buffer: the drain is a hot path, and a fixed-size batch
    // avoids per-drain allocation (justified raw array, brief §5).
    MidiByte buffer[kInboundDrainBatch];
    std::size_t count = 0;
    while ((count = inbound_.popBulk(buffer, kInboundDrainBatch)) > 0) {
        parser_.feed(buffer, count);
    }
}

void AutomationEngine::processHostEvents() {
    // Scan the dirty flags: any host thread may have written any parameter
    // since the last tick. Acquire pairs with the producer's release; the
    // value is then re-read so a burst of writes coalesces to the latest
    // (matching the legacy prev_CC_Val dedupe philosophy).
    for (ParamId param = 0; param < paramMap_.size(); ++param) {
        if (hostDirty_[param].exchange(0, std::memory_order_acquire) == 0) continue;
        const float value = values_[param].load(std::memory_order_relaxed);
        setParamInternal(param, value, Origin::Host, false, true);
    }
}

void AutomationEngine::stepTestMode() {
    if (!testMode_.load(std::memory_order_acquire)) return;

    // Endpoint loss: never keep driving an oscillator into a dead transport.
    // The oscillator stops, modes are restored and queued positions are
    // canceled (the same immediate-stop path as explicit shutdown).
    if (!transport_.connected()) {
        diag_.warning("test mode stopped: MIDI transport disconnected");
        testMode_.store(false, std::memory_order_release);
        touchModes_.restoreAllStripModes(savedStripModes_);
        savedStripModes_.clear();
        scheduler_.cancelAllPositions();
        return;
    }

    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (now - lastOscillatorStep_ < kOscillatorStepPeriod) return;
    lastOscillatorStep_ = now;

    const double elapsed =
        std::chrono::duration_cast<std::chrono::duration<double> >(now - testStart_).count();
    // Faders only — Test Mode never touches mutes or other controls.
    for (ParamId param = 0; param < profile_.faderCount(); ++param) {
        const float value = oscillator_.valueAt(elapsed, param);
        // notify=false: test motion is never user automation. The normal
        // dedupe + scheduler path applies (bandwidth respected).
        setParamInternal(param, value, Origin::Internal, false, true);
    }
}

} // namespace cinemix
