// Config implementation — macOS only, not compiled on Linux.
#import <Foundation/Foundation.h>
#import <os/log.h>

#include "Config.h"

namespace cinemix_mac {
namespace config {

void installDefaultLogSink(cinemix::Diagnostics& diag) {
    os_log_t log = os_log_create("org.cinemixbridge.CinemixAutomationBridge", "bridge");
    diag.setSink([log](cinemix::Diagnostics::Level level, const std::string& message) {
        const char* msg = message.c_str();
        switch (level) {
        case cinemix::Diagnostics::Level::Error:
            os_log_error(log, "%{public}s", msg);
            break;
        case cinemix::Diagnostics::Level::Warning:
            os_log(log, "warning: %{public}s", msg);
            break;
        case cinemix::Diagnostics::Level::Info:
        default:
            os_log(log, "%{public}s", msg);
            break;
        }
    });
}

namespace {

NSString* ns(const std::string& s) {
    return [NSString stringWithUTF8String:s.c_str()];
}
std::string stdStr(NSString* s) {
    if (!s) return "";
    return [s UTF8String] ?: "";
}

const char* kIn1 = "CinemixIn1";
const char* kIn2 = "CinemixIn2";
const char* kOut1 = "CinemixOut1";
const char* kOut2 = "CinemixOut2";
const char* kDiagLevel = "CinemixDiagLevel";

std::string getString(const char* key) {
    NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
    return stdStr([d stringForKey:[NSString stringWithUTF8String:key]]);
}
void setString(const char* key, const std::string& value) {
    NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
    [d setObject:ns(value) forKey:[NSString stringWithUTF8String:key]];
}

} // namespace

std::string input1Name() { return getString(kIn1); }
std::string input2Name() { return getString(kIn2); }
std::string output1Name() { return getString(kOut1); }
std::string output2Name() { return getString(kOut2); }
void setInput1Name(const std::string& n) { setString(kIn1, n); }
void setInput2Name(const std::string& n) { setString(kIn2, n); }
void setOutput1Name(const std::string& n) { setString(kOut1, n); }
void setOutput2Name(const std::string& n) { setString(kOut2, n); }

int diagnosticsLevel() {
    NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
    if ([d objectForKey:[NSString stringWithUTF8String:kDiagLevel]] == nil) return 2; // Info
    return (int)[d integerForKey:[NSString stringWithUTF8String:kDiagLevel]];
}
void setDiagnosticsLevel(int level) {
    NSUserDefaults* d = [NSUserDefaults standardUserDefaults];
    [d setInteger:level forKey:[NSString stringWithUTF8String:kDiagLevel]];
}

cinemix::MixerProfile loadProfile() {
    cinemix::MixerProfile profile = cinemix::MixerProfile::legacyDefault();

    NSArray* paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,
                                                         NSUserDomainMask, YES);
    NSString* dir = ([paths count] > 0) ? [paths objectAtIndex:0] : nil;
    NSString* path = [dir stringByAppendingPathComponent:
                              @"CinemixAutomationBridge/profile.plist"];
    NSDictionary* plist = [NSDictionary dictionaryWithContentsOfFile:path];
    if (!plist) return profile; // no custom profile: legacy default

    NSNumber* lo = [plist objectForKey:@"loStrips"];
    NSNumber* hi = [plist objectForKey:@"hiStrips"];
    if (lo && [lo integerValue] > 0) profile.loStrips = (uint16_t)[lo integerValue];
    if (hi && [hi integerValue] > 0) profile.hiStrips = (uint16_t)[hi integerValue];

    NSArray* stereo = [plist objectForKey:@"stereoStrips"];
    if (stereo) {
        profile.stereoStrips.clear();
        for (NSNumber* n in stereo) profile.stereoStrips.push_back((uint16_t)[n integerValue]);
    }
    NSNumber* joy1 = [plist objectForKey:@"hasJoystick1"];
    if (joy1) profile.hasJoystick1 = [joy1 boolValue];
    NSNumber* joy2 = [plist objectForKey:@"hasJoystick2"];
    if (joy2) profile.hasJoystick2 = [joy2 boolValue];
    NSNumber* aux = [plist objectForKey:@"auxMuteCount"];
    if (aux) profile.auxMuteCount = (uint16_t)[aux integerValue];
    NSNumber* master = [plist objectForKey:@"hasMasterFader"];
    if (master) profile.hasMasterFader = [master boolValue];

    NSString* res = [plist objectForKey:@"faderResolution"];
    if ([res isEqualToString:@"fourteenBit"])
        profile.faderResolution = cinemix::FaderResolution::FourteenBit;
    NSNumber* hyst = [plist objectForKey:@"echoHysteresisSteps"];
    if (hyst) profile.echoHysteresisSteps = (uint8_t)[hyst integerValue];
    NSNumber* budget = [plist objectForKey:@"budgetMessagesPerSecond"];
    if (budget) profile.budgetMessagesPerSecond = (uint32_t)[budget integerValue];
    NSString* name = [plist objectForKey:@"name"];
    if (name) profile.name = stdStr(name);
    return profile;
}

} // namespace config
} // namespace cinemix_mac
