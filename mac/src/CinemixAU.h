// CinemixAU — the Logic-facing Audio Unit v2 component.
//
// An 'augn' generator: zero audio inputs, stereo silence output. Its purpose
// is automation, not DSP: the render loop does nothing but zero-fill, all
// console traffic happens on the bridge worker thread, and SetParameter is
// real-time safe (atomic value + dirty flag; the worker scans per tick).
//
// Implemented but NOT compiled on the development host (Linux). Build and
// auval on the target Mac — see docs/BUILDING.md.
#ifndef CINEMIX_MAC_AU_H
#define CINEMIX_MAC_AU_H

#include <memory>
#include <mutex>
#include <vector>

#include "AUBase.h"
#include "AUPlugInDispatch.h"

#include "CinemixBridge.h"
#include "cinemix/AutomationEngine.h"
#include "cinemix/Diagnostics.h"
#include "cinemix/MixerProfile.h"

namespace cinemix_mac {

class CinemixAU : public ausdk::AUBase {
    using Base = ausdk::AUBase;

public:
    explicit CinemixAU(AudioComponentInstance ci);
    ~CinemixAU() override;

    CinemixAU(const CinemixAU&) = delete;
    CinemixAU& operator=(const CinemixAU&) = delete;

    // ---- AUBase overrides --------------------------------------------------
    bool StreamFormatWritable(AudioUnitScope /*scope*/, AudioUnitElement /*element*/) override {
        return true;
    }
    bool CanScheduleParameters() const override { return false; }

    OSStatus Initialize() override;
    void Cleanup() override;
    OSStatus Reset(AudioUnitScope /*scope*/, AudioUnitElement /*element*/) override {
        return noErr;
    }
    OSStatus Render(AudioUnitRenderActionFlags& ioActionFlags,
                    const AudioTimeStamp& inTimeStamp, UInt32 inNumberFrames) override;

    OSStatus GetParameterInfo(AudioUnitScope inScope, AudioUnitParameterID inParameterID,
                              AudioUnitParameterInfo& outInfo) override;
    OSStatus GetParameter(AudioUnitParameterID inID, AudioUnitScope inScope,
                          AudioUnitElement inElement, AudioUnitParameterValue& outValue) override;
    OSStatus SetParameter(AudioUnitParameterID inID, AudioUnitScope inScope,
                          AudioUnitElement inElement, AudioUnitParameterValue inValue,
                          UInt32 inBufferOffsetInFrames) override;
    OSStatus GetParameterValueStrings(AudioUnitScope inScope, AudioUnitParameterID inParameterID,
                                      CFArrayRef* outStrings) override;

    OSStatus GetPropertyInfo(AudioUnitPropertyID inID, AudioUnitScope inScope,
                             AudioUnitElement inElement, UInt32& outDataSize,
                             bool& outWritable) override;
    OSStatus GetProperty(AudioUnitPropertyID inID, AudioUnitScope inScope,
                         AudioUnitElement inElement, void* outData) override;
    OSStatus SetProperty(AudioUnitPropertyID inID, AudioUnitScope inScope,
                         AudioUnitElement inElement, const void* inData,
                         UInt32 inDataSize) override;

    // ---- Bridge access ------------------------------------------------------
    BridgeContext context();
    cinemix::AutomationEngine& engine() { return *engine_; }
    cinemix::Diagnostics& diag() { return *diag_; }
    CoreMidiTransport& transport() { return *transport_; }

private:
    // Forward engine notifications to host parameter listeners (this is how
    // console moves become Logic automation — gesture + value-change events).
    class HostBridge : public cinemix::AutomationEngine::Listener {
    public:
        explicit HostBridge(CinemixAU* au) : au_(au) {}
        void onGesture(cinemix::ParamId param, bool begin) override;
        void onParameter(cinemix::ParamId param, float value, cinemix::Origin origin) override;
        void onConnected(bool activated) override;
    private:
        CinemixAU* au_;
    };

    void notifyParameterListeners(cinemix::ParamId param, AudioUnitParameterEventType type,
                                  AudioUnitParameterValue value);

    struct ParamListenerEntry {
        AudioUnitParameterID param;
        AudioUnitParameterListenerProc proc;
        void* user;
    };

    cinemix::MixerProfile profile_;
    std::unique_ptr<cinemix::Diagnostics> diag_;
    std::unique_ptr<CoreMidiTransport> transport_; // destroyed AFTER engine_
    std::unique_ptr<cinemix::AutomationEngine> engine_;
    HostBridge hostBridge_;

    std::mutex listenerMu_;
    std::vector<ParamListenerEntry> paramListeners_;
};

// Component entry point; exported by the bundle (Info.plist factoryFunction).
AUSDK_COMPONENT_ENTRY(ausdk::AUBaseFactory, CinemixAU)

} // namespace cinemix_mac

#endif // CINEMIX_MAC_AU_H
