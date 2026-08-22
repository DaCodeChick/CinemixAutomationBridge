// TransmissionScheduler — paced, coalescing outbound MIDI.
//
// Design (see docs/ARCHITECTURE.md §6):
//   * TWO FIFO lanes: `high_` (touch/mode replies — never delayed, never
//     coalesced) and `main_` (everything else, preserving the legacy
//     byte-order of multi-step sequences like activation).
//   * Position updates inside `main_` are coalesced per parameter
//     (latest-wins): safe because all position data is absolute.
//   * A global message budget protects the DIN MIDI link; the high lane
//     drains first within a tick, then the main lane (both draw from the
//     same credit).
//
// The scheduler is single-threaded by design: only the bridge worker thread
// calls it (the engine funnels host/UI writes through the worker first).
// `drainToEmpty()` exists for tests/harness and ignores pacing.
//
// Queue-full policy (explicit):
//   * position updates coalesce per parameter, so the main lane can never
//     hold more than paramCount position entries;
//   * both lanes additionally cap their TOTAL size at kMaxQueuedCommands;
//     a command that exceeds the cap is dropped and counted (rate-limited
//     warning) — except SystemReset, which is safety-critical and instead
//     evicts the oldest queued entry;
//   * the high lane (touch mode replies) drops the NEWEST reply on overflow
//     (mode values are absolute state; the next reply supersedes), never
//     evicting older replies (their order matters to the touch state
//     machine).
#ifndef CINEMIX_TRANSMISSION_SCHEDULER_H
#define CINEMIX_TRANSMISSION_SCHEDULER_H

#include <cstddef>
#include <cstdint>
#include <deque>

#include "cinemix/Diagnostics.h"
#include "cinemix/MidiTransport.h"
#include "cinemix/MixerProfile.h"
#include "cinemix/Types.h"

namespace cinemix {

class TransmissionScheduler {
public:
    TransmissionScheduler(const MixerProfile& profile, Diagnostics& diag,
                          IMidiTransport& transport);

    // Enqueue a management command (never coalesced, FIFO order preserved).
    void enqueueCommand(const OutboundCommand& command);

    // Enqueue a high-priority management command (touch replies).
    void enqueueHigh(const OutboundCommand& command);

    // Enqueue a position update: coalesces with any pending update for the
    // same parameter (latest wins).
    void enqueuePosition(ParamId param, const MidiMessage& message);

    // Enqueue the non-coalescible fine (LSB) continuation of a 14-bit fader
    // position. It keeps its ParamId so cancelPosition/cancelAllPositions
    // remove it together with the coarse component, but it is NOT scanned by
    // coalescing (the coarse component already coalesces the whole logical
    // position). MUST be enqueued immediately after its coarse component to
    // preserve MSB→LSB transmission order.
    void enqueuePositionContinuation(ParamId param, const MidiMessage& message);

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

    // Unpaced drain: sends everything (ordering and coalescing still apply).
    // Returns messages sent. Test/harness use only.
    std::size_t drainToEmpty();

    std::size_t pending() const noexcept { return high_.size() + main_.size(); }
    std::size_t sentTotal() const noexcept { return sent_; }
    std::size_t droppedTotal() const noexcept { return dropped_; }
    std::size_t coalescedCount() const noexcept { return coalesced_; }

private:
    struct Entry {
        OutboundCommand cmd;
    };

    void sendOne(const OutboundCommand& command);

    const MixerProfile& profile_;
    Diagnostics& diag_;
    IMidiTransport& transport_;

    std::deque<Entry> high_;
    std::deque<Entry> main_;

    // Hard bound on each lane's TOTAL size (positions + commands). Chosen far
    // above the largest legitimate burst (activation ≈ 360 entries) so it
    // only engages on a runaway producer.
    static constexpr std::size_t kMaxQueuedCommands = 1024;

    double credit_;
    double budgetPerTick_;
    std::size_t sent_;
    std::size_t dropped_;
    std::size_t coalesced_;
};

} // namespace cinemix

#endif // CINEMIX_TRANSMISSION_SCHEDULER_H
