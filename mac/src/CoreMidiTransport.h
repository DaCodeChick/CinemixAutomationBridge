// CoreMidiTransport — CoreMIDI transport for the bridge.
// Implemented but NOT compiled on the development host (Linux); requires
// macOS 10.13+ CoreMIDI. See docs/BUILDING.md for the target-Mac build.
//
// Design (docs/ARCHITECTURE.md §2):
//   * one input port, connected to the two selected console sources
//     (the protocol distinguishes the two console halves by MIDI channel,
//     exactly as the legacy bridge did — source identity is not needed);
//   * two output ports (LO pair, HI pair). `send(port == 0)` broadcasts.
//   * the read proc only copies bytes into the engine's lock-free queue —
//     no allocation, no parsing, no logging on the CoreMIDI thread.
//   * endpoint disappearance is observed via the client notification proc;
//     the bridge never auto-reactivates a console (manual re-activation is
//     the safe behavior for vintage hardware).
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

    // Creates the MIDI client and input port. Safe to call before selecting
    // endpoints. Returns false if CoreMIDI is unavailable.
    bool start();

    // ---- Endpoint enumeration (called from the UI thread) ------------------
    std::vector<std::string> inputNames() const;
    std::vector<std::string> outputNames() const;
    // Endpoint by name; returns nullptr if not found.
    static MIDIEndpointRef findSource(const std::string& name);
    static MIDIEndpointRef findDestination(const std::string& name);

    // ---- Endpoint selection (UI thread) -------------------------------------
    // Select by endpoint name ("" = none). Persisted by the UI layer.
    bool selectInputs(const std::string& loSource, const std::string& hiSource);
    bool selectOutputs(const std::string& loDest, const std::string& hiDest);
    std::string input1Name() const { return input1Name_; }
    std::string input2Name() const { return input2Name_; }
    std::string output1Name() const { return output1Name_; }
    std::string output2Name() const { return output2Name_; }

    // ---- IMidiTransport ------------------------------------------------------
    bool send(uint8_t port, const cinemix::MidiMessage& message) override;
    bool connected() const override;
    std::string description() const override;

    // Fired (on an internal CoreMIDI queue) when device topology changes;
    // the UI subscribes to update its port popups.
    void setTopologyChangedHandler(void (*handler)(void*), void* user) {
        topologyHandler_ = handler;
        topologyUser_ = user;
    }

private:
    static void readProc(const MIDIPacketList* pktlist, void* refCon, void* connRefCon);
    static void notifyProc(const MIDINotification* message, void* refCon);

    bool sendTo(MIDIEndpointRef dest, MIDIPortRef port, const cinemix::MidiMessage& message);

    cinemix::Diagnostics& diag_;

    MIDIClientRef client_;
    MIDIPortRef inPort_;
    MIDIEndpointRef src1_;
    MIDIEndpointRef src2_;
    MIDIPortRef outPort1_;
    MIDIPortRef outPort2_;
    // Endpoint refs are written by the UI thread and read by the bridge
    // worker (connected()) — atomic to keep that race-free.
    std::atomic<MIDIEndpointRef> dst1_;
    std::atomic<MIDIEndpointRef> dst2_;

    std::string input1Name_;
    std::string input2Name_;
    std::string output1Name_;
    std::string output2Name_;

    std::atomic<bool> topologyDirty_;
    void (*topologyHandler_)(void*);
    void* topologyUser_;
};

} // namespace cinemix_mac

#endif // CINEMIX_MAC_COREMIDI_TRANSPORT_H
