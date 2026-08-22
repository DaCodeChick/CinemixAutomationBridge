#include "cinemix/AutomationEngine.h"

#include <cmath>
#include <cstdio>

namespace cinemix {

// ---------------------------------------------------------------------------
// Construction / destruction

AutomationEngine::AutomationEngine(const MixerProfile& profile, Diagnostics& diag,
                                   IMidiTransport& transport)
    : profile_(profile), diag_(diag), transport_(transport),
      protocol_(profile), paramMap_(profile),
      scheduler_(profile, diag, transport),
      touchModes_(profile, protocol_, scheduler_),
      animator_(profile.faderCount(), profile.muteCount()),
      listener_(nullptr),
      values_(new std::atomic<float>[profile.paramCount()]),
      lastProcessed_(profile.paramCount(), -1.f),
      lastCommanded_(profile.paramCount(), -1.f),
      touched_(profile.paramCount(), 0),
      activated_(false), testMode_(false), allMutes_(false),
      inbound_(65536), inboundOverflowReported_(0),
      hostEvents_(1024),
      stopRequested_(false), workerRunning_(false) {
    for (size_t i = 0; i < profile_.paramCount(); ++i)
        values_[i].store(paramMap_.info(ParamId(i)).defaultValue, std::memory_order_relaxed);

    transport_.onIncoming = [this](const uint8_t* data, size_t n) { handleIncoming(data, n); };

    parser_.setHandlers(this, &AutomationEngine::parserCcCallback,
                        &AutomationEngine::parserSystemCallback,
                        &AutomationEngine::parserMalformedCallback);
}

AutomationEngine::~AutomationEngine() {
    // The listener may already be destroyed (members of an owner class are
    // torn down in reverse declaration order); never call it from here.
    listener_ = nullptr;

    // Safety net (legacy destructor behavior: always release the console).
    // If the worker is running, ask it to deactivate before stopping; if not,
    // run the release sequence inline.
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
    values_[param].store(clamp01(value), std::memory_order_relaxed);
    HostEvent ev;
    ev.param = param;
    ev.value = clamp01(value);
    if (!hostEvents_.push(ev)) {
        // Overflow: the newest value is still stored atomically, and the
        // worker re-reads values on drain, so only the event is lost.
        // Rate-limit the diagnostic.
        if ((hostEvents_.overflowCount() & 0x3F) == 1)
            diag_.warning("host parameter queue overflow — updates may lag");
    }
}

float AutomationEngine::getParameter(ParamId param) const {
    if (param >= paramMap_.size()) return 0.f;
    return values_[param].load(std::memory_order_relaxed);
}

void AutomationEngine::handleIncoming(const uint8_t* data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!inbound_.push(data[i])) {
            if ((inbound_.overflowCount() & 0x3FF) == 1)
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
        for (ParamId p = 0; p < paramMap_.size(); ++p) {
            if (paramMap_.info(p).isMuteLike)
                setParamInternal(p, allMutes_ ? 1.f : 0.f, Origin::UserInterface, true, true);
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
    worker_ = std::thread(&AutomationEngine::workerLoop, this);
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

void AutomationEngine::parserCcCallback(void* user, uint8_t channel, uint8_t cc, uint8_t value) {
    AutomationEngine* self = static_cast<AutomationEngine*>(user);
    const uint8_t status = uint8_t(0xB0u | ((channel - 1) & 0x0F));
    const ConsoleEvent ev = self->protocol_.decode(status, cc, value);
    self->handleConsoleEvent(ev);
}

void AutomationEngine::parserSystemCallback(void* user, uint8_t status) {
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

void AutomationEngine::handleConsoleEvent(const ConsoleEvent& ev) {
    if (ev.kind == EventKind::Unknown) {
        if (static_cast<uint8_t>(diag_.level()) >= static_cast<uint8_t>(Diagnostics::Level::Verbose)) {
            char buf[96];
            snprintf(buf, sizeof(buf), "unknown console CC: ch=%u cc=%u val=%u",
                     unsigned(ev.channel), unsigned(ev.cc), unsigned(ev.value));
            diag_.verbose(buf);
        }
        return;
    }
    if (ev.kind == EventKind::Ignored) {
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

    ParamId param = kNoParam;
    switch (ev.kind) {
    case EventKind::FaderPosition:
    case EventKind::MuteChanged: {
        if (!paramMap_.find(ev.control, param)) { diag_.verbose("event for unmapped control"); return; }
        const float v = (ev.kind == EventKind::FaderPosition) ? ev.normalized : (ev.on ? 1.f : 0.f);
        if (touched_[param]) {
            // A hand on the fader: user-originated. The hand beats the motor —
            // cancel whatever the host commanded and report to the host.
            cancelPending(param);
            setParamInternal(param, v, Origin::Console, true, false);
        } else if (scheduler_.hasPending(param)) {
            // We commanded a value that has not reached the wire yet; this
            // report reflects the *previous* state (motor still traveling).
            // Keep the pending target, update the visible state silently.
            setParamInternal(param, v, Origin::Console, false, false);
        } else if (isEcho(param, v)) {
            // Console reporting near the position we commanded (motor echo /
            // interpolation jitter): update state silently, never bounce it
            // back to the host.
            setParamInternal(param, v, Origin::Console, false, false);
        } else {
            // Console moved without touch registering (hand move, master
            // fader, mutes): user-originated.
            cancelPending(param);
            setParamInternal(param, v, Origin::Console, true, false);
        }
        break;
    }
    case EventKind::TouchBegin: {
        // Touch sensors are not parameters; they gate the strip's fader.
        const ParamId param = paramMap_.stripFaderId(ev.control.strip, ev.control.path);
        if (param == kNoParam) return;
        touched_[param] = 1;
        if (listener_) listener_->onGesture(param, true);
        touchModes_.onTouchChanged(ev.control.strip, ev.control.path, true);
        break;
    }
    case EventKind::TouchEnd: {
        const ParamId param = paramMap_.stripFaderId(ev.control.strip, ev.control.path);
        if (param == kNoParam) return;
        touched_[param] = 0;
        if (listener_) listener_->onGesture(param, false);
        touchModes_.onTouchChanged(ev.control.strip, ev.control.path, false);
        break;
    }
    case EventKind::SelPressed:
        touchModes_.onSelPressed(ev.control.strip, ev.control.path);
        break;
    case EventKind::MasterSelPressed:
        touchModes_.onMasterSelPressed();
        break;
    case EventKind::AuxMuteChanged: {
        ControlRef ref = ev.control;
        if (!paramMap_.find(ref, param)) { diag_.verbose("AUX event for unmapped control"); return; }
        const float v = ev.on ? 1.f : 0.f;
        if (scheduler_.hasPending(param))
            setParamInternal(param, v, Origin::Console, false, false);
        else if (isEcho(param, v))
            setParamInternal(param, v, Origin::Console, false, false);
        else {
            cancelPending(param);
            setParamInternal(param, v, Origin::Console, true, false);
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
    const float v = clamp01(value);
    const float prevProcessed = lastProcessed_[param];
    values_[param].store(v, std::memory_order_relaxed);
    lastProcessed_[param] = v; // legacy prev_CC_Val: updated on every processed value
    if (sendOutbound) {
        if (activated_.load(std::memory_order_acquire)) {
            // Legacy dedupe semantics: skip when the value did not change
            // since the last value processed for this parameter — compared on
            // the parameter domain, not the wire byte (mute 2/3, AUX 2n/2n+1
            // encodings make wire-byte dedupe wrong).
            const bool sameValue =
                (prevProcessed >= 0.f) &&
                (paramMap_.info(param).isMuteLike ? ((v != 0.f) == (prevProcessed != 0.f))
                                                 : (quantize7(v) == quantize7(prevProcessed)));
            if (!sameValue) {
                enqueueOutbound(param, v);
                lastCommanded_[param] = v;
            }
        }
    }
    if (notify && listener_) listener_->onParameter(param, v, origin);
}

void AutomationEngine::enqueueOutbound(ParamId param, float value) {
    const ParameterInfo& info = paramMap_.info(param);
    const ControlRef& c = info.control;
    switch (c.cls) {
    case ControlClass::Fader: {
        const std::vector<MidiMessage> msgs =
            protocol_.encodeStripFader(c.strip, c.path, value);
        if (!msgs.empty()) {
            scheduler_.enqueuePosition(param, msgs[0]);
            // 14-bit mode sends a fine CC as well; append it without
            // coalescing (it must not be dropped when a newer MSB arrives).
            for (size_t i = 1; i < msgs.size(); ++i) {
                OutboundCommand cmd;
                cmd.kind = CommandKind::SetMode;
                cmd.message = msgs[i];
                cmd.param = kNoParam;
                scheduler_.enqueueCommand(cmd);
            }
        }
        break;
    }
    case ControlClass::Mute:
        scheduler_.enqueuePosition(param,
            protocol_.encodeStripMute(c.strip, c.path, value != 0.f));
        break;
    case ControlClass::JoyAxis:
        scheduler_.enqueuePosition(param,
            protocol_.encodeJoyAxis(c.index, c.path == StripPath::Chan ? 0 : 1, value));
        break;
    case ControlClass::JoyMute:
        scheduler_.enqueuePosition(param, protocol_.encodeJoyMute(c.index, value != 0.f));
        break;
    case ControlClass::AuxMute:
        scheduler_.enqueuePosition(param, protocol_.encodeAuxMute(c.index, value != 0.f));
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
    if (baseline < 0.f) return false; // we never commanded this control
    if (paramMap_.info(param).isMuteLike) return incoming == baseline;
    const float window = float(profile_.echoHysteresisSteps) * faderHysteresis();
    return std::fabs(incoming - baseline) <= window + 1e-6f;
}

void AutomationEngine::cancelPending(ParamId param) {
    scheduler_.cancelPosition(param);
}

// ---------------------------------------------------------------------------
// Command implementations (worker thread)

void AutomationEngine::snapshotInternal(Origin origin) {
    for (ParamId p = 0; p < paramMap_.size(); ++p) {
        const float v = values_[p].load(std::memory_order_relaxed);
        // Legacy: SendSnapshot resets the dedupe cache and re-sends every
        // parameter; the sent values become the new dedupe/echo references.
        lastProcessed_[p] = v;
        lastCommanded_[p] = v;
        if (activated_.load(std::memory_order_acquire)) enqueueOutbound(p, v);
        if (listener_) listener_->onParameter(p, v, origin);
    }
    diag_.info("snapshot sent");
}

void AutomationEngine::resetAllInternal() {
    touchModes_.setAllStripModes(3);
    touchModes_.setMasterMode(3);
    touchModes_.setJoystickModes(3);
    for (ParamId p = 0; p < paramMap_.size(); ++p) {
        const bool isMaster = paramMap_.info(p).control.cls == ControlClass::MasterFader;
        values_[p].store(isMaster ? 1.f : 0.f, std::memory_order_relaxed);
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
    OutboundCommand cmd;
    cmd.kind = CommandKind::RemoteControl;
    cmd.message = protocol_.remoteControl(true);
    cmd.param = kNoParam;
    scheduler_.enqueueCommand(cmd);

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

    // Legacy deactivation sequence, byte-for-byte: CC127=0 → everything to
    // ISO(0) → master/joystick SEL 0 → system reset 0xFF. Position data
    // pending in the queue is canceled so nothing is sent after the reset.
    OutboundCommand cmd;
    cmd.kind = CommandKind::RemoteControl;
    cmd.message = protocol_.remoteControl(false);
    cmd.param = kNoParam;
    scheduler_.enqueueCommand(cmd);

    touchModes_.setAllStripModes(0);
    touchModes_.setMasterMode(0);
    touchModes_.setJoystickModes(0);

    cmd.kind = CommandKind::SystemReset;
    cmd.message = protocol_.systemReset();
    scheduler_.enqueueCommand(cmd);
    scheduler_.cancelAllPositions();

    for (size_t p = 0; p < touched_.size(); ++p) {
        if (touched_[p]) {
            touched_[p] = 0;
            if (listener_) listener_->onGesture(ParamId(p), false);
        }
    }
    activated_.store(false, std::memory_order_release);
    if (listener_) listener_->onConnected(false);
    diag_.info("console deactivated");
}

void AutomationEngine::testModeInternal(bool on) {
    resetAllInternal();
    touchModes_.setAllStripModes(on ? 1 : 3);
    testMode_.store(on, std::memory_order_release);
    diag_.info(on ? "test mode ON" : "test mode OFF");
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
    (void)lock;
    while (!cmdQ_.empty()) {
        std::function<void()> fn = std::move(cmdQ_.front());
        cmdQ_.pop_front();
        fn(); // commands are quick, worker-only internals; no lock re-entry
    }
}

void AutomationEngine::processInbound() {
    MidiByte buf[512];
    size_t n = 0;
    while ((n = inbound_.popBulk(buf, 512)) > 0) {
        parser_.feed(buf, n);
    }
}

void AutomationEngine::processHostEvents() {
    HostEvent ev;
    while (hostEvents_.pop(ev)) {
        // Re-read the atomic (a newer write may have replaced the queued
        // value): latest wins, matching the coalescing philosophy.
        const float v = values_[ev.param].load(std::memory_order_relaxed);
        setParamInternal(ev.param, v, Origin::Host, false, true);
    }
}

void AutomationEngine::stepTestMode() {
    std::vector<std::pair<size_t, float> > faderUpdates;
    std::vector<std::pair<size_t, bool> > muteUpdates;
    animator_.step(faderUpdates, muteUpdates);
    for (size_t i = 0; i < faderUpdates.size(); ++i) {
        const size_t strip = faderUpdates[i].first / 2;
        const StripPath path = (faderUpdates[i].first % 2 == 0) ? StripPath::Chan : StripPath::Mix;
        const ParamId p = paramMap_.stripFaderId(uint16_t(strip), path);
        if (p != kNoParam)
            setParamInternal(p, faderUpdates[i].second, Origin::Internal, false, true);
    }
    for (size_t i = 0; i < muteUpdates.size(); ++i) {
        const size_t strip = muteUpdates[i].first / 2;
        const StripPath path = (muteUpdates[i].first % 2 == 0) ? StripPath::Chan : StripPath::Mix;
        const ParamId p = paramMap_.stripMuteId(uint16_t(strip), path);
        if (p != kNoParam)
            setParamInternal(p, muteUpdates[i].second ? 1.f : 0.f, Origin::Internal, false, true);
    }
}

} // namespace cinemix
