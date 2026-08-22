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
      dst1_(static_cast<MIDIEndpointRef>(0)), dst2_(static_cast<MIDIEndpointRef>(0)),
      topologyDirty_(false), topologyHandler_(nullptr), topologyUser_(nullptr) {
}

CoreMidiTransport::~CoreMidiTransport() {
    if (inPort_) {
        MIDIPortDisconnectSource(inPort_, src1_);
        MIDIPortDisconnectSource(inPort_, src2_);
        MIDIPortDispose(inPort_);
    }
    if (outPort1_) MIDIPortDispose(outPort1_);
    if (outPort2_) MIDIPortDispose(outPort2_);
    if (client_) MIDIClientDispose(client_);
}

bool CoreMidiTransport::start() {
    const OSStatus clientErr = MIDIClientCreate(
        CFSTR("CinemixAutomationBridge"), &CoreMidiTransport::notifyProc, this, &client_);
    if (clientErr != noErr) {
        diag_.error("CoreMIDI: cannot create MIDI client");
        return false;
    }
    // One input port; connect both console sources to it. The read proc must
    // stay real-time safe: it only forwards bytes.
    const OSStatus inErr = MIDIInputPortCreate(client_, CFSTR("Cinemix In"), &CoreMidiTransport::readProc, this, &inPort_);
    if (inErr != noErr) {
        diag_.error("CoreMIDI: cannot create input port");
        return false;
    }
    MIDIOutputPortCreate(client_, CFSTR("Cinemix Out LO"), &outPort1_);
    MIDIOutputPortCreate(client_, CFSTR("Cinemix Out HI"), &outPort2_);
    return true;
}

std::vector<std::string> CoreMidiTransport::inputNames() const {
    std::vector<std::string> names;
    const ItemCount n = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < n; ++i)
        names.push_back(endpointName(MIDIGetSource(i)));
    return names;
}

std::vector<std::string> CoreMidiTransport::outputNames() const {
    std::vector<std::string> names;
    const ItemCount n = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < n; ++i)
        names.push_back(endpointName(MIDIGetDestination(i)));
    return names;
}

MIDIEndpointRef CoreMidiTransport::findSource(const std::string& name) {
    if (name.empty()) return 0;
    const ItemCount n = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < n; ++i) {
        MIDIEndpointRef ep = MIDIGetSource(i);
        if (endpointName(ep) == name) return ep;
    }
    return 0;
}

MIDIEndpointRef CoreMidiTransport::findDestination(const std::string& name) {
    if (name.empty()) return 0;
    const ItemCount n = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < n; ++i) {
        MIDIEndpointRef ep = MIDIGetDestination(i);
        if (endpointName(ep) == name) return ep;
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
        if (lo) { MIDIPortConnectSource(inPort_, lo, nullptr); src1_ = lo; }
        if (hi) { MIDIPortConnectSource(inPort_, hi, nullptr); src2_ = hi; }
    }
    if (!lo && !loSource.empty()) diag_.warning("MIDI input 1 not found: " + loSource);
    if (!hi && !hiSource.empty()) diag_.warning("MIDI input 2 not found: " + hiSource);
    return (lo || loSource.empty()) && (hi || hiSource.empty());
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

bool CoreMidiTransport::send(uint8_t port, const cinemix::MidiMessage& message) {
    const MIDIEndpointRef d1 = dst1_.load(std::memory_order_acquire);
    const MIDIEndpointRef d2 = dst2_.load(std::memory_order_acquire);
    bool ok = true;
    if (port == 0 || port == 1) ok = sendTo(d1, outPort1_, message) && ok;
    if (port == 0 || port == 2) ok = sendTo(d2, outPort2_, message) && ok;
    return ok;
}

void CoreMidiTransport::readProc(const MIDIPacketList* pktlist, void* refCon, void* /*connRefCon*/) {
    CoreMidiTransport* self = static_cast<CoreMidiTransport*>(refCon);
    if (!self->onIncoming) return;
    const MIDIPacket* pkt = &pktlist->packet[0];
    for (UInt32 i = 0; i < pktlist->numPackets; ++i) {
        // Forward raw bytes; the engine's lock-free queue absorbs them and
        // the worker thread does all parsing. No allocation here.
        self->onIncoming(pkt->data, pkt->length);
        pkt = MIDIPacketNext(pkt);
    }
}

void CoreMidiTransport::notifyProc(const MIDINotification* message, void* refCon) {
    CoreMidiTransport* self = static_cast<CoreMidiTransport*>(refCon);
    switch (message->messageID) {
    case kMIDIMsgSetupChanged:
    case kMIDIMsgObjectAdded:
    case kMIDIMsgObjectRemoved:
    case kMIDIMsgPropertyChanged:
        self->topologyDirty_.store(true, std::memory_order_release);
        break;
    default:
        break;
    }
}

} // namespace cinemix_mac
