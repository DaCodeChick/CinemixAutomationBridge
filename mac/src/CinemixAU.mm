// CinemixAU implementation — macOS only, not compiled on Linux.
#import <CoreAudioKit/CoreAudioKit.h>
#import <CoreFoundation/CoreFoundation.h>
#import <Foundation/Foundation.h>

#include <cstring>

#include "CinemixAU.h"
#include "Config.h"

namespace cinemix_mac {

namespace {

const CFStringRef kBundleIdentifier = CFSTR("org.cinemixbridge.CinemixAutomationBridge");
const CFStringRef kViewFactoryName = CFSTR("CinemixCocoaViewFactory");

std::string defaultPortRole(const char* stored) {
    // No sensible default: the user must pick the ports for their interface.
    return stored ? stored : "";
}

} // namespace

// ---------------------------------------------------------------------------
// Lifecycle

CinemixAU::CinemixAU(AudioComponentInstance ci)
    : Base(ci, 0, 1), // generator: 0 inputs, 1 output bus
      profile_(config::loadProfile()),
      diag_(new cinemix::Diagnostics()),
      transport_(new CoreMidiTransport(*diag_)),
      engine_(new cinemix::AutomationEngine(profile_, *diag_, *transport_)),
      hostBridge_(this) {
    diag_->setLevel(static_cast<cinemix::Diagnostics::Level>(config::diagnosticsLevel()));
    config::installDefaultLogSink(*diag_);

    transport_->start();
    transport_->selectInputs(config::input1Name(), config::input2Name());
    transport_->selectOutputs(config::output1Name(), config::output2Name());

    engine_->setListener(&hostBridge_);
    engine_->start();
}

CinemixAU::~CinemixAU() {
    // engine_ is destroyed first (reverse member order): its destructor runs
    // the full legacy deactivation sequence through the transport if the
    // console was still in remote mode — never leave the console stranded.
    engine_.reset();
    transport_.reset();
    diag_.reset();
}

OSStatus CinemixAU::Initialize() {
    return noErr;
}

void CinemixAU::Cleanup() {
}

// ---------------------------------------------------------------------------
// Audio path: silence only. The bridge does no DSP; the render loop exists
// for host compatibility (the legacy VST did exactly the same).

OSStatus CinemixAU::Render(AudioUnitRenderActionFlags& /*ioActionFlags*/,
                           const AudioTimeStamp& /*inTimeStamp*/,
                           UInt32 /*inNumberFrames*/) {
    // Zero-fill byte-wise: correct silence for any stream format.
    AudioBufferList& bufs = GetOutput(0)->GetBufferList();
    for (UInt32 b = 0; b < bufs.mNumberBuffers; ++b) {
        if (bufs.mBuffers[b].mData)
            std::memset(bufs.mBuffers[b].mData, 0, bufs.mBuffers[b].mDataByteSize);
    }
    return noErr;
}

// ---------------------------------------------------------------------------
// Parameters

OSStatus CinemixAU::GetParameterInfo(AudioUnitScope inScope, AudioUnitParameterID inParameterID,
                                     AudioUnitParameterInfo& outInfo) {
    if (inScope != kAudioUnitScope_Global || inParameterID >= engine_->parameterCount())
        return kAudioUnitErr_InvalidParameter;

    const cinemix::ParameterInfo& info = engine_->parameterMap().info(inParameterID);
    std::memset(&outInfo, 0, sizeof(outInfo));
    outInfo.flags = kAudioUnitParameterFlag_IsWritable | kAudioUnitParameterFlag_IsReadable;
    outInfo.minValue = 0.f;
    outInfo.maxValue = 1.f;
    outInfo.defaultValue = info.defaultValue;
    if (info.isMuteLike) {
        outInfo.unit = kAudioUnitParameterUnit_Boolean;
        outInfo.flags |= kAudioUnitParameterFlag_ValuesHaveStrings;
    } else {
        outInfo.unit = kAudioUnitParameterUnit_Generic;
    }
    // Name (host parameter lists).
    NSString* name = [NSString stringWithUTF8String:info.name.c_str()];
    outInfo.name = (CFStringRef)CFBridgingRetain(name);
    outInfo.flags |= kAudioUnitParameterFlag_HasName;
    return noErr;
}

OSStatus CinemixAU::GetParameter(AudioUnitParameterID inID, AudioUnitScope inScope,
                                 AudioUnitElement inElement,
                                 AudioUnitParameterValue& outValue) {
    if (inScope != kAudioUnitScope_Global || inElement != 0) return kAudioUnitErr_InvalidParameter;
    if (inID >= engine_->parameterCount()) return kAudioUnitErr_InvalidParameter;
    outValue = engine_->getParameter(inID);
    return noErr;
}

OSStatus CinemixAU::SetParameter(AudioUnitParameterID inID, AudioUnitScope inScope,
                                 AudioUnitElement inElement, AudioUnitParameterValue inValue,
                                 UInt32 /*inBufferOffsetInFrames*/) {
    if (inScope != kAudioUnitScope_Global || inElement != 0) return kAudioUnitErr_InvalidParameter;
    if (inID >= engine_->parameterCount()) return kAudioUnitErr_InvalidParameter;
    // Real-time safe: atomic store + lock-free enqueue (never allocates).
    engine_->setHostParameter(inID, static_cast<float>(inValue));
    return noErr;
}

OSStatus CinemixAU::GetParameterValueStrings(AudioUnitScope inScope,
                                             AudioUnitParameterID inParameterID,
                                             CFArrayRef* outStrings) {
    if (inScope != kAudioUnitScope_Global || inParameterID >= engine_->parameterCount())
        return kAudioUnitErr_InvalidParameter;
    if (!engine_->parameterMap().info(inParameterID).isMuteLike)
        return kAudioUnitErr_InvalidPropertyValue;
    CFStringRef values[2] = {CFSTR("Off"), CFSTR("On")};
    *outStrings = CFArrayCreate(kCFAllocatorDefault, (const void**)values, 2,
                                &kCFTypeArrayCallBacks);
    return noErr;
}

// ---------------------------------------------------------------------------
// Properties: CocoaUI, bridge context, parameter listeners; everything else
// defers to AUBase (which implements ClassInfo, StreamFormat, presets, …).

OSStatus CinemixAU::GetPropertyInfo(AudioUnitPropertyID inID, AudioUnitScope inScope,
                                    AudioUnitElement inElement, UInt32& outDataSize,
                                    bool& outWritable) {
    if (inID == kAudioUnitProperty_CocoaUI && inScope == kAudioUnitScope_Global) {
        outDataSize = sizeof(AudioUnitCocoaViewInfo);
        outWritable = false;
        return noErr;
    }
    if (inID == kCinemixProperty_BridgeContext && inScope == kAudioUnitScope_Global) {
        outDataSize = sizeof(BridgeContext*);
        outWritable = false;
        return noErr;
    }
    return Base::GetPropertyInfo(inID, inScope, inElement, outDataSize, outWritable);
}

OSStatus CinemixAU::GetProperty(AudioUnitPropertyID inID, AudioUnitScope inScope,
                                AudioUnitElement inElement, void* outData) {
    if (inID == kAudioUnitProperty_CocoaUI && inScope == kAudioUnitScope_Global) {
        AudioUnitCocoaViewInfo* viewInfo = static_cast<AudioUnitCocoaViewInfo*>(outData);
        CFBundleRef bundle = CFBundleGetBundleWithIdentifier(kBundleIdentifier);
        if (!bundle) return kAudioUnitErr_InvalidPropertyValue;
        viewInfo->mCocoaAUViewBundleLocation = CFBundleCopyBundleURL(bundle);
        viewInfo->mCocoaAUViewClass[0] = CFStringCreateCopy(kCFAllocatorDefault, kViewFactoryName);
        return noErr;
    }
    if (inID == kCinemixProperty_BridgeContext && inScope == kAudioUnitScope_Global) {
        BridgeContext* ctx = static_cast<BridgeContext*>(outData);
        ctx->engine = engine_.get();
        ctx->diag = diag_.get();
        ctx->transport = transport_.get();
        return noErr;
    }
    return Base::GetProperty(inID, inScope, inElement, outData);
}

OSStatus CinemixAU::SetProperty(AudioUnitPropertyID inID, AudioUnitScope inScope,
                                AudioUnitElement inElement, const void* inData,
                                UInt32 inDataSize) {
    if (inID == kAudioUnitProperty_ParameterListener) {
        // AudioUnitAddParameterListener / AudioUnitRemoveParameterListener
        // both arrive here with an AudioUnitParameterListenerBookkeeping.
        // Heuristic: a triple already present is removed, otherwise added —
        // add-then-remove of the same triple is therefore idempotent.
        if (inDataSize != sizeof(AudioUnitParameterListenerBookkeeping))
            return kAudioUnitErr_InvalidPropertyValue;
        const AudioUnitParameterListenerBookkeeping* bk =
            static_cast<const AudioUnitParameterListenerBookkeeping*>(inData);
        std::lock_guard<std::mutex> lock(listenerMu_);
        for (std::vector<ParamListenerEntry>::iterator it = paramListeners_.begin();
             it != paramListeners_.end(); ++it) {
            if (it->param == bk->parameterID && it->proc == bk->proc &&
                it->user == bk->inProcRefCon) {
                paramListeners_.erase(it);
                return noErr;
            }
        }
        ParamListenerEntry e;
        e.param = bk->parameterID;
        e.proc = bk->proc;
        e.user = bk->inProcRefCon;
        paramListeners_.push_back(e);
        return noErr;
    }
    return Base::SetProperty(inID, inScope, inElement, inData, inDataSize);
}

// ---------------------------------------------------------------------------
// Bridge plumbing

BridgeContext CinemixAU::context() {
    BridgeContext ctx;
    ctx.engine = engine_.get();
    ctx.diag = diag_.get();
    ctx.transport = transport_.get();
    return ctx;
}

void CinemixAU::HostBridge::onGesture(cinemix::ParamId param, bool begin) {
    au_->notifyParameterListeners(param,
                                  begin ? kAudioUnitParameterEvent_BeginGesture
                                        : kAudioUnitParameterEvent_EndGesture,
                                  0.f);
}

void CinemixAU::HostBridge::onParameter(cinemix::ParamId param, float value,
                                        cinemix::Origin /*origin*/) {
    au_->notifyParameterListeners(param, kAudioUnitParameterEvent_ValueChange, value);
}

void CinemixAU::HostBridge::onConnected(bool /*activated*/) {
    // The Cocoa view polls connection state; nothing to push to the host.
}

void CinemixAU::notifyParameterListeners(cinemix::ParamId param,
                                         AudioUnitParameterEventType type,
                                         AudioUnitParameterValue value) {
    // Called on the bridge worker thread. Copy the entries under the lock,
    // then invoke the host callbacks lock-free (host listeners must be
    // thread-safe; Logic's are).
    AudioUnitParameterEvent event;
    event.scope = kAudioUnitScope_Global;
    event.element = 0;
    event.parameter = param;
    event.eventType = type;
    event.eventValues.value = value;

    std::vector<ParamListenerEntry> entries;
    {
        std::lock_guard<std::mutex> lock(listenerMu_);
        entries = paramListeners_;
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        const ParamListenerEntry& e = entries[i];
        if (e.param == param || e.param == kAUParameterListener_AnyParameter) {
            if (e.proc) e.proc(e.user, this, &event);
        }
    }
}

} // namespace cinemix_mac
