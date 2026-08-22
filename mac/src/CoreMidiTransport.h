// CoreMidiTransport — CoreMIDI transport for the bridge.
// Implemented but NOT compiled on the development host (Linux); requires
// macOS 10.13+ CoreMIDI. See docs/BUILDING.md for the target-Mac build.
//
// Design (docs/ARCHITECTURE.md §2, §7):
//   * one input port, connected to the two selected console sources. Source-
//     port identity is NOT needed: the two console halves use disjoint MIDI
//     channels (LO 1/3, HI 2/4, master 5) and the legacy bridge decoded by
//     channel only, so channel-based decoding through one port is correct.
//   * two output ports (LO pair, HI pair). `send(port == 0)` broadcasts.
//   * the read proc only copies bytes into the engine's lock-free queue —
//     no allocation, no parsing, no logging on the CoreMIDI thread. CoreMIDI
//     invokes a port's read proc serially on its internal thread, which is
//     the single-producer guarantee the engine's SPSC byte ring relies on.
//   * endpoint disappearance is observed by the UI's polling timer (the view
//     re-enumerates endpoints on its refresh tick); there is deliberately NO
//     notification callback API here — one coherent mechanism, not two.
//   * the bridge never auto-reactivates a console (manual re-activation is
//     the safe behavior for vintage hardware).
//
// Startup contract: `start()` either fully succeeds (client + input port +
// both output ports created) or fails with diagnostics and cleans up every
// resource it created — the destructor is additionally safe at ANY stage of
// initialization (idempotent shutdown of zero-or-more resources).
#ifndef CINEMIX_MAC_COREMIDI_TRANSPORT_H
#define CINEMIX_MAC_COREMIDI_TRANSPORT_H

#include <atomic>
#include <string>
#include <vector>

#include <CoreMIDI/CoreMIDI.h>

#include "cinemix/Diagnostics.h"
#include "cinemix/MidiTransport.h"

namespace cinemix_mac {

class CoreMidiTransport : public cinemix::IMidiTransport {
public:
    explicit CoreMidiTransport(cinemix::Diagnostics& diag);
    ~CoreMidiTransport() override;

    CoreMidiTransport(const CoreMidiTransport&) = delete;
    CoreMidiTransport& operator=(const CoreMidiTransport&) = delete;

    // Creates the MIDI client, input port and both output ports. Returns
    // false if ANY required step fails; on failure everything created so far
    // is disposed and the object is left in the stopped state. Call before
    // selecting endpoints.
    bool start();

    // ---- Endpoint enumeration (called from the UI thread) ------------------
    std::vector<std::string> inputNames() const;
    std::vector<std::string> outputNames() const;
    // Endpoint by name; returns nullptr if not found.
    static MIDIEndpointRef findSource(const std::string& name);
    static MIDIEndpointRef findDestination(const std::string& name);

    // ---- Endpoint selection (UI thread) -------------------------------------
    // Select by endpoint name ("" = none). Persisted by the UI layer.
    // Connection failures are diagnosed; a failed connect leaves the role
    // unconnected rather than half-claimed.
    bool selectInputs(const std::string& loSource, const std::string& hiSource);
    bool selectOutputs(const std::string& loDest, const std::string& hiDest);
    std::string input1Name() const { return input1Name_; }
    std::string input2Name() const { return input2Name_; }
    std::string output1Name() const { return output1Name_; }
    std::string output2Name() const { return output2Name_; }

    // ---- IMidiTransport ------------------------------------------------------
    bool send(std::uint8_t port, const cinemix::MidiMessage& message) override;
    bool connected() const override;
    std::string description() const override;

private:
    static void readProc(const MIDIPacketList* pktlist, void* refCon, void* connRefCon);

    bool sendTo(MIDIEndpointRef dest, MIDIPortRef port, const cinemix::MidiMessage& message);
    // Idempotent disposal of every owned CoreMIDI resource (safe at any
    // stage of initialization).
    void shutdown() noexcept;

    cinemix::Diagnostics& diag_;

    MIDIClientRef client_;
    MIDIPortRef inPort_;
    MIDIEndpointRef src1_;
    MIDIEndpointRef src2_;
    MIDIPortRef outPort1_;
    MIDIPortRef outPort2_;
    // Endpoint refs are written by the UI thread and read by the bridge
    // worker (connected()/send) — atomic to keep that race-free.
    std::atomic<MIDIEndpointRef> dst1_;
    std::atomic<MIDIEndpointRef> dst2_;

    std::string input1Name_;
    std::string input2Name_;
    std::string output1Name_;
    std::string output2Name_;
};

} // namespace cinemix_mac

#endif // CINEMIX_MAC_COREMIDI_TRANSPORT_H
