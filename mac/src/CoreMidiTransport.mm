// CoreMidiTransport implementation. macOS only — not compiled on Linux.
#include "CoreMidiTransport.h"

#include <cstring>

#include <CoreFoundation/CoreFoundation.h>

namespace cinemix_mac {

namespace {

std::string endpointName(MIDIEndpointRef endpoint) {
    if (endpoint == 0) return "";
    CFStringRef name = nullptr;
    if (MIDIObjectGetStringProperty(endpoint, kMIDIPropertyName, &name) != noErr || !name)
        return "";
    char buf[256];
    if (!CFStringGetCString(name, buf, sizeof(buf), kCFStringEncodingUTF8)) {
        CFRelease(name);
        return "";
    }
    CFRelease(name);
    return buf;
}

} // namespace

CoreMidiTransport::CoreMidiTransport(cinemix::Diagnostics& diag)
    : diag_(diag),
      client_(0), inPort_(0), src1_(0), src2_(0),
      outPort1_(0), outPort2_(0),
      dst1_(static_cast<MIDIEndpointRef>(0)), dst2_(static_cast<MIDIEndpointRef>(0)) {
}

CoreMidiTransport::~CoreMidiTransport() {
    shutdown();
}

void CoreMidiTransport::shutdown() noexcept {
    // Idempotent, order-safe disposal of every resource that may exist at
    // any stage of initialization. Input quiesced first (stopInbound), then
    // output ports, then the client. CoreMIDI guarantees no further
    // read-proc invocations after the input port/client dispose, so the
    // `this` captured in the read proc cannot be called during or after
    // shutdown (callback lifetime invariant, brief §26).
    stopInbound();
    if (outPort1_ != 0) { MIDIPortDispose(outPort1_); outPort1_ = 0; }
    if (outPort2_ != 0) { MIDIPortDispose(outPort2_); outPort2_ = 0; }
    dst1_.store(0, std::memory_order_release);
    dst2_.store(0, std::memory_order_release);
    if (client_ != 0) { MIDIClientDispose(client_); client_ = 0; }
}

void CoreMidiTransport::stopInbound() noexcept {
    // Disconnect then dispose: after MIDIPortDispose returns, CoreMIDI will
    // not re-enter the read proc. This is the ordering point the engine
    // relies on before detaching onIncoming / destroying itself.
    if (inPort_ != 0) {
        if (src1_ != 0) MIDIPortDisconnectSource(inPort_, src1_);
        if (src2_ != 0) MIDIPortDisconnectSource(inPort_, src2_);
        src1_ = 0;
        src2_ = 0;
        MIDIPortDispose(inPort_);
        inPort_ = 0;
    }
}

bool CoreMidiTransport::start() {
    // Every required step is checked; on failure everything created so far
    // is disposed and start() reports failure — never a half-initialized
    // transport that claims to be operational.
    const OSStatus clientErr =
        MIDIClientCreate(CFSTR("CinemixAutomationBridge"), nullptr, nullptr, &client_);
    if (clientErr != noErr) {
        diag_.error("CoreMIDI: cannot create MIDI client");
        client_ = 0;
        return false;
    }

    const OSStatus inErr =
        MIDIInputPortCreate(client_, CFSTR("Cinemix In"), &CoreMidiTransport::readProc, this,
                            &inPort_);
    if (inErr != noErr) {
        diag_.error("CoreMIDI: cannot create input port");
        shutdown();
        return false;
    }

    const OSStatus out1Err = MIDIOutputPortCreate(client_, CFSTR("Cinemix Out LO"), &outPort1_);
    if (out1Err != noErr) {
        diag_.error("CoreMIDI: cannot create output port 1 (LO)");
        shutdown();
        return false;
    }
    const OSStatus out2Err = MIDIOutputPortCreate(client_, CFSTR("Cinemix Out HI"), &outPort2_);
    if (out2Err != noErr) {
        diag_.error("CoreMIDI: cannot create output port 2 (HI)");
        shutdown();
        return false;
    }
    return true;
}

std::vector<std::string> CoreMidiTransport::inputNames() const {
    std::vector<std::string> names;
    const ItemCount count = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < count; ++i)
        names.push_back(endpointName(MIDIGetSource(i)));
    return names;
}

std::vector<std::string> CoreMidiTransport::outputNames() const {
    std::vector<std::string> names;
    const ItemCount count = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < count; ++i)
        names.push_back(endpointName(MIDIGetDestination(i)));
    return names;
}

MIDIEndpointRef CoreMidiTransport::findSource(const std::string& name) {
    if (name.empty()) return 0;
    const ItemCount count = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < count; ++i) {
        MIDIEndpointRef endpoint = MIDIGetSource(i);
        if (endpointName(endpoint) == name) return endpoint;
    }
    return 0;
}

MIDIEndpointRef CoreMidiTransport::findDestination(const std::string& name) {
    if (name.empty()) return 0;
    const ItemCount count = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < count; ++i) {
        MIDIEndpointRef endpoint = MIDIGetDestination(i);
        if (endpointName(endpoint) == name) return endpoint;
    }
    return 0;
}

bool CoreMidiTransport::selectInputs(const std::string& loSource, const std::string& hiSource) {
    input1Name_ = loSource;
    input2Name_ = hiSource;
    MIDIEndpointRef lo = findSource(loSource);
    MIDIEndpointRef hi = findSource(hiSource);
    if (inPort_) {
        if (src1_) MIDIPortDisconnectSource(inPort_, src1_);
        if (src2_) MIDIPortDisconnectSource(inPort_, src2_);
        src1_ = 0;
        src2_ = 0;
        if (lo) {
            const OSStatus status = MIDIPortConnectSource(inPort_, lo, nullptr);
            if (status == noErr) src1_ = lo;
            else diag_.warning("CoreMIDI: connect failed for input 1: " + loSource +
                               " (OSStatus " + std::to_string(status) + ")");
        }
        if (hi) {
            const OSStatus status = MIDIPortConnectSource(inPort_, hi, nullptr);
            if (status == noErr) src2_ = hi;
            else diag_.warning("CoreMIDI: connect failed for input 2: " + hiSource +
                               " (OSStatus " + std::to_string(status) + ")");
        }
    }
    if (!lo && !loSource.empty()) diag_.warning("MIDI input 1 not found: " + loSource);
    if (!hi && !hiSource.empty()) diag_.warning("MIDI input 2 not found: " + hiSource);

    // Contract (Finding 6): true iff every requested input is actually
    // connected (found AND MIDIPortConnectSource succeeded), not merely
    // found. An empty (unrequested) role counts as satisfied.
    const bool loOk = loSource.empty() || (lo != 0 && src1_ != 0);
    const bool hiOk = hiSource.empty() || (hi != 0 && src2_ != 0);
    return loOk && hiOk;
}

bool CoreMidiTransport::selectOutputs(const std::string& loDest, const std::string& hiDest) {
    output1Name_ = loDest;
    output2Name_ = hiDest;
    dst1_.store(findDestination(loDest), std::memory_order_release);
    dst2_.store(findDestination(hiDest), std::memory_order_release);
    const MIDIEndpointRef d1 = dst1_.load(std::memory_order_acquire);
    const MIDIEndpointRef d2 = dst2_.load(std::memory_order_acquire);
    if (!d1 && !loDest.empty()) diag_.warning("MIDI output 1 not found: " + loDest);
    if (!d2 && !hiDest.empty()) diag_.warning("MIDI output 2 not found: " + hiDest);
    return (d1 || loDest.empty()) && (d2 || hiDest.empty());
}

bool CoreMidiTransport::connected() const {
    // Activation requires both console outputs; inputs are advisory
    // (position data can be driven one-way).
    return dst1_.load(std::memory_order_acquire) != 0 &&
           dst2_.load(std::memory_order_acquire) != 0;
}

std::string CoreMidiTransport::description() const {
    return "CoreMIDI: in1=" + input1Name_ + " in2=" + input2Name_ +
           " out1=" + output1Name_ + " out2=" + output2Name_;
}

bool CoreMidiTransport::sendTo(MIDIEndpointRef dest, MIDIPortRef port,
                               const cinemix::MidiMessage& message) {
    if (!dest || !port) return false;
    MIDIPacketList pktlist;
    MIDIPacket* pkt = MIDIPacketListInit(&pktlist);
    pkt = MIDIPacketListAdd(&pktlist, sizeof(pktlist), pkt, 0,
                            message.length, message.data.data());
    if (!pkt) return false;
    return MIDISend(port, dest, &pktlist) == noErr;
}

bool CoreMidiTransport::send(std::uint8_t port, const cinemix::MidiMessage& message) {
    const MIDIEndpointRef d1 = dst1_.load(std::memory_order_acquire);
    const MIDIEndpointRef d2 = dst2_.load(std::memory_order_acquire);
    bool ok = true;
    if (port == 0 || port == 1) ok = sendTo(d1, outPort1_, message) && ok;
    if (port == 0 || port == 2) ok = sendTo(d2, outPort2_, message) && ok;
    return ok;
}

void CoreMidiTransport::readProc(const MIDIPacketList* pktlist, void* refCon, void* /*connRefCon*/) {
    // Real-time safe: forward raw bytes only. CoreMIDI serializes read-proc
    // invocations per input port, which is the engine's single-producer
    // contract for the inbound byte ring.
    CoreMidiTransport* self = static_cast<CoreMidiTransport*>(refCon);
    if (!self->onIncoming) return;
    const MIDIPacket* pkt = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; ++i) {
        self->onIncoming(pkt->data, pkt->length);
        pkt = MIDIPacketNext(pkt);
    }
}

} // namespace cinemix_mac
