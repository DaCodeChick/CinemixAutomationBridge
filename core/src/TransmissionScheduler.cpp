#include "cinemix/TransmissionScheduler.h"

#include <algorithm>
#include <cstdio>

namespace cinemix {

TransmissionScheduler::TransmissionScheduler(const MixerProfile& profile,
                                             Diagnostics& diag,
                                             IMidiTransport& transport)
    : profile_(profile), diag_(diag), transport_(transport),
      credit_(0.0),
      budgetPerTick_(double(profile.budgetMessagesPerSecond) *
                     double(profile.schedulerTickMs) / 1000.0),
      maxBurst_(8.0),
      sent_(0), dropped_(0), coalesced_(0) {
}

void TransmissionScheduler::enqueueCommand(const OutboundCommand& cmd) {
    Entry e;
    e.cmd = cmd;
    main_.push_back(e);
}

void TransmissionScheduler::enqueueHigh(const OutboundCommand& cmd) {
    Entry e;
    e.cmd = cmd;
    high_.push_back(e);
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
    OutboundCommand cmd;
    cmd.kind = CommandKind::SetMode; // irrelevant for positions
    cmd.message = message;
    cmd.param = param;
    Entry e;
    e.cmd = cmd;
    main_.push_back(e);
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

bool TransmissionScheduler::sendOne(const OutboundCommand& cmd) {
    const MidiMessage& m = cmd.message;
    if (m.length == 0 || m.length > 3) {
        ++dropped_;
        return true; // malformed: drop, keep going
    }

    if (!transport_.send(m.port, m)) {
        ++dropped_;
        if (dropped_ <= 8) diag_.warning("transport rejected outbound message (not connected?)");
        return true;
    }
    ++sent_;

    if (static_cast<uint8_t>(diag_.level()) >= static_cast<uint8_t>(Diagnostics::Level::MidiOut)) {
        char buf[80];
        if (m.length == 1)
            snprintf(buf, sizeof(buf), "TX port %u: FF", m.port);
        else if (m.length == 2)
            snprintf(buf, sizeof(buf), "TX port %u: %02X %02X", m.port, m.data[0], m.data[1]);
        else
            snprintf(buf, sizeof(buf), "TX port %u: %02X %02X %02X", m.port, m.data[0], m.data[1], m.data[2]);
        diag_.midiOut(buf);
    }
    return true;
}

bool TransmissionScheduler::tick() {
    credit_ = std::min(credit_ + budgetPerTick_, maxBurst_);
    if (credit_ < 1.0) return pending() > 0;

    // High-priority lane first.
    while (credit_ >= 1.0 && !high_.empty()) {
        Entry e = high_.front();
        high_.pop_front();
        sendOne(e.cmd);
        credit_ -= 1.0;
    }
    // Then the main lane.
    while (credit_ >= 1.0 && !main_.empty()) {
        Entry e = main_.front();
        main_.pop_front();
        sendOne(e.cmd);
        credit_ -= 1.0;
    }
    return pending() > 0;
}

size_t TransmissionScheduler::drainToEmpty() {
    size_t n = 0;
    while (!high_.empty() || !main_.empty()) {
        Entry e;
        bool fromHigh = false;
        if (!high_.empty()) { e = high_.front(); high_.pop_front(); fromHigh = true; }
        else { e = main_.front(); main_.pop_front(); }
        (void)fromHigh;
        sendOne(e.cmd);
        ++n;
    }
    return n;
}

} // namespace cinemix
