#include "cinemix/TransmissionScheduler.h"

#include <algorithm>

namespace cinemix {

namespace {

// Direct string construction for TX diagnostics: no fixed buffer, no
// truncation (brief §25).
std::string hexByte(std::uint8_t value) {
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.push_back(kHex[(value >> 4) & 0x0F]);
    out.push_back(kHex[value & 0x0F]);
    return out;
}

std::string describeOutbound(const MidiMessage& message) {
    std::string text = "TX port " + std::to_string(static_cast<unsigned>(message.port)) + ": ";
    if (message.length == 1) return text + "FF";
    for (std::uint8_t i = 0; i < message.length; ++i) {
        if (i > 0) text.push_back(' ');
        text += hexByte(message.data[i]);
    }
    return text;
}

// Small burst allowance: lets the budget absorb a brief spike (e.g. a touch
// reply immediately after a burst) without overshooting the steady rate.
constexpr double kMaxBurst = 8.0;
// Rate-limit the "transport rejected" warning.
constexpr std::size_t kTransportFailureWarningLimit = 8;
// Rate-limit the "queue full" warning.
constexpr std::size_t kQueueFullWarningLimit = 4;
} // namespace

TransmissionScheduler::TransmissionScheduler(const MixerProfile& profile,
                                             Diagnostics& diag,
                                             IMidiTransport& transport)
    : profile_(profile), diag_(diag), transport_(transport),
      credit_(0.0),
      budgetPerTick_(static_cast<double>(profile.budgetMessagesPerSecond) *
                     static_cast<double>(profile.schedulerTickMs) / 1000.0),
      sent_(0), dropped_(0), coalesced_(0) {
}

void TransmissionScheduler::enqueueCommand(const OutboundCommand& command) {
    if (main_.size() >= kMaxQueuedCommands) {
        if (command.kind == CommandKind::SystemReset) {
            // The release byte is safety-critical: it must always be sent,
            // even if that means evicting an older queued command.
            main_.pop_front();
        } else {
            ++dropped_;
            if (dropped_ <= kQueueFullWarningLimit)
                diag_.warning("outbound queue full — command dropped");
            return;
        }
    }
    Entry entry;
    entry.cmd = command;
    main_.push_back(entry);
}

void TransmissionScheduler::enqueueHigh(const OutboundCommand& command) {
    if (high_.size() >= kMaxQueuedCommands) {
        // Touch mode replies are absolute state: dropping the newest one is
        // safe (the next reply supersedes it). Never evict older replies —
        // ordering within the high lane is part of the touch state machine.
        ++dropped_;
        if (dropped_ <= kQueueFullWarningLimit)
            diag_.warning("high-priority queue full — reply dropped");
        return;
    }
    Entry entry;
    entry.cmd = command;
    high_.push_back(entry);
}

void TransmissionScheduler::enqueuePosition(ParamId param, const MidiMessage& message) {
    // Latest-wins coalescing: remove any earlier pending update for the same
    // parameter (scan from the tail; the first hit is the newest).
    for (std::deque<Entry>::reverse_iterator it = main_.rbegin(); it != main_.rend(); ++it) {
        if (it->cmd.param == param) {
            main_.erase(std::next(it).base());
            ++coalesced_;
            break;
        }
    }
    OutboundCommand command;
    command.kind = CommandKind::PositionUpdate;
    command.message = message;
    command.param = param;
    Entry entry;
    entry.cmd = command;
    main_.push_back(entry);
}

void TransmissionScheduler::cancelPosition(ParamId param) {
    for (std::deque<Entry>::iterator it = main_.begin(); it != main_.end();) {
        if (it->cmd.param == param) {
            it = main_.erase(it);
            ++coalesced_;
        } else {
            ++it;
        }
    }
}

void TransmissionScheduler::cancelAllPositions() {
    for (std::deque<Entry>::iterator it = main_.begin(); it != main_.end();) {
        if (it->cmd.param != kNoParam) {
            it = main_.erase(it);
            ++coalesced_;
        } else {
            ++it;
        }
    }
}

bool TransmissionScheduler::hasPending(ParamId param) const {
    for (std::deque<Entry>::const_iterator it = main_.begin(); it != main_.end(); ++it)
        if (it->cmd.param == param) return true;
    return false;
}

void TransmissionScheduler::sendOne(const OutboundCommand& command) {
    const MidiMessage& message = command.message;
    if (message.length == 0 || message.length > 3) {
        ++dropped_;
        return; // malformed: drop, keep going
    }

    if (!transport_.send(message.port, message)) {
        ++dropped_;
        if (dropped_ <= kTransportFailureWarningLimit)
            diag_.warning("transport rejected outbound message (not connected?)");
        return;
    }
    ++sent_;

    if (static_cast<std::uint8_t>(diag_.level()) >=
        static_cast<std::uint8_t>(Diagnostics::Level::MidiOut)) {
        diag_.midiOut(describeOutbound(message));
    }
}

bool TransmissionScheduler::tick() {
    credit_ = std::min(credit_ + budgetPerTick_, kMaxBurst);
    if (credit_ < 1.0) return pending() > 0;

    // High-priority lane first.
    while (credit_ >= 1.0 && !high_.empty()) {
        Entry entry = high_.front();
        high_.pop_front();
        sendOne(entry.cmd);
        credit_ -= 1.0;
    }
    // Then the main lane.
    while (credit_ >= 1.0 && !main_.empty()) {
        Entry entry = main_.front();
        main_.pop_front();
        sendOne(entry.cmd);
        credit_ -= 1.0;
    }
    return pending() > 0;
}

std::size_t TransmissionScheduler::drainToEmpty() {
    std::size_t sent = 0;
    while (!high_.empty() || !main_.empty()) {
        Entry entry;
        if (!high_.empty()) {
            entry = high_.front();
            high_.pop_front();
        } else {
            entry = main_.front();
            main_.pop_front();
        }
        sendOne(entry.cmd);
        ++sent;
    }
    return sent;
}

} // namespace cinemix
