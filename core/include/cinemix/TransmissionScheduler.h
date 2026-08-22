// TransmissionScheduler — paced, coalescing outbound MIDI.
//
// Design (see docs/ARCHITECTURE.md §6):
//   * TWO FIFO lanes: `high_` (touch/mode replies — never delayed, never
//     coalesced) and `main_` (everything else, preserving the legacy
//     byte-order of multi-step sequences like activation).
//   * Position updates inside `main_` are coalesced per parameter
//     (latest-wins): safe because all position data is absolute.
//   * A global message budget protects the DIN MIDI link; commands drain
//     first within a tick, then positions (both lanes draw from the same
//     credit).
//   * Send-time dedupe reproduces the legacy `prev_CC_Val` behavior.
//
// The scheduler is single-threaded by design: only the bridge worker thread
// calls it (the engine funnels host/UI writes through the worker first).
// `drainToEmpty()` exists for tests/harness and ignores pacing.
#ifndef CINEMIX_TRANSMISSION_SCHEDULER_H
#define CINEMIX_TRANSMISSION_SCHEDULER_H

#include <cstdint>
#include <deque>
#include <vector>

#include "cinemix/Diagnostics.h"
#include "cinemix/MidiTransport.h"
#include "cinemix/MixerProfile.h"
#include "cinemix/Types.h"

namespace cinemix {

class TransmissionScheduler {
public:
    TransmissionScheduler(const MixerProfile& profile, Diagnostics& diag, IMidiTransport& transport);

    // Enqueue a management command (never coalesced, FIFO order preserved).
    void enqueueCommand(const OutboundCommand& cmd);

    // Enqueue a high-priority management command (touch replies).
    void enqueueHigh(const OutboundCommand& cmd);

    // Enqueue a position update: coalesces with any pending update for the
    // same parameter (latest wins).
    void enqueuePosition(ParamId param, const MidiMessage& message);

    // Drop a pending position update for one parameter (console beat us to it).
    void cancelPosition(ParamId param);
    // Drop all pending position updates (used right after enqueueing the
    // system-reset byte so no position data can be sent after release).
    void cancelAllPositions();

    // True if a position update for this parameter is still queued (not yet
    // on the wire). Used to protect a freshly commanded target from being
    // canceled by the console's response to the *previous* command.
    bool hasPending(ParamId param) const;

    // Paced drain: sends as many messages as the budget allows. Returns true
    // if work remains. Call from the worker thread at schedulerTickMs cadence.
    bool tick();

    // Unpaced drain: sends everything (ordering, coalescing and dedupe still
    // apply). Returns messages sent. Test/harness use only.
    size_t drainToEmpty();

    size_t pending() const { return high_.size() + main_.size(); }
    size_t sentTotal() const { return sent_; }
    size_t droppedTotal() const { return dropped_; }

    // Diagnostics snapshot: messages coalesced away since start.
    size_t coalescedCount() const { return coalesced_; }

private:
    struct Entry {
        OutboundCommand cmd;
    };

    bool sendOne(const OutboundCommand& cmd);

    const MixerProfile& profile_;
    Diagnostics& diag_;
    IMidiTransport& transport_;

    std::deque<Entry> high_;
    std::deque<Entry> main_;

    double credit_;
    double budgetPerTick_;
    double maxBurst_;
    size_t sent_;
    size_t dropped_;
    size_t coalesced_;
};

} // namespace cinemix

#endif // CINEMIX_TRANSMISSION_SCHEDULER_H
