#include "cinemix/TestModeAnimator.h"

#include <cmath>

namespace cinemix {

namespace {
// Legacy ramp → value: y = (ramp * (1 - |ramp|) * 2) + 0.5
inline float rampToValue(float ramp) {
    return (ramp * (1.f - std::fabs(ramp)) * 2.f) + 0.5f;
}
} // namespace

TestModeAnimator::TestModeAnimator(size_t faderCount, size_t muteCount)
    : faders_(faderCount), mutes_(muteCount), rng_(0x5EEDu), firstStep_(true) {
    // faders_ stores the *ramp accumulator*, exactly like the legacy
    // Anim_ramp array; the value shown/sent is rampToValue(ramp).
    for (size_t i = 0; i < faders_.size(); ++i)
        faders_[i] = initialRamp(i, faderCount);
    for (size_t i = 0; i < mutes_.size(); ++i) mutes_[i] = false;
}

void TestModeAnimator::step(std::vector<std::pair<size_t, float>>& faderUpdates,
                            std::vector<std::pair<size_t, bool>>& muteUpdates) {
    faderUpdates.clear();
    muteUpdates.clear();
    const Clock::time_point now = Clock::now();
    if (firstStep_) {
        lastFader_ = lastMute_ = now;
        firstStep_ = false;
        return;
    }

    const std::chrono::milliseconds faderPeriod(40);  // 25 Hz
    const std::chrono::milliseconds mutePeriod(100);  // 10 Hz

    if (now - lastFader_ >= faderPeriod) {
        lastFader_ += faderPeriod;
        for (size_t i = 0; i < faders_.size(); ++i) {
            float ramp = faders_[i] + 0.05f;
            if (ramp > 1.f) ramp -= 2.f; // legacy wrap
            faders_[i] = ramp;
            faderUpdates.push_back(std::make_pair(i, rampToValue(ramp)));
        }
    }

    if (now - lastMute_ >= mutePeriod) {
        lastMute_ += mutePeriod;
        for (size_t i = 0; i < mutes_.size(); ++i) {
            // xorshift32 (deterministic, replaces legacy rand())
            rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
            mutes_[i] = (rng_ & 0x8000u) != 0;
            muteUpdates.push_back(std::make_pair(i, mutes_[i]));
        }
    }
}

} // namespace cinemix
