// TestModeAnimator — reproduces the legacy TEST MODE animation:
//   * fader ramp: ramp += 0.05 per 25 Hz tick, y = (ramp * (1-|ramp|) * 2) + 0.5,
//     wrap ramp -2..1 (legacy Plugin.h TestModeON/processReplacing);
//   * mutes: random 50% toggle per 10 Hz tick.
//
// CHANGED vs legacy: clocked by the bridge worker thread (the audio loop is
// left pure — docs/COMPATIBILITY.md §4), and the PRNG is a deterministic
// xorshift instead of rand(), so tests are reproducible. Outbound pacing is
// the scheduler's job (the legacy animation could exceed DIN bandwidth).
#ifndef CINEMIX_TEST_MODE_ANIMATOR_H
#define CINEMIX_TEST_MODE_ANIMATOR_H

#include <chrono>
#include <cstdint>
#include <vector>

namespace cinemix {

class TestModeAnimator {
public:
    TestModeAnimator(size_t faderCount, size_t muteCount);

    // Advance; returns per-fader and per-mute updates that are due. Uses a
    // steady clock; call frequently (worker tick).
    void step(std::vector<std::pair<size_t, float>>& faderUpdates,
              std::vector<std::pair<size_t, bool>>& muteUpdates);

    float faderValue(size_t i) const { return faders_[i]; }
    bool muteValue(size_t i) const { return mutes_[i]; }

    // Same math as the legacy ramp for the i-th fader at reset.
    static float initialRamp(size_t i, size_t faderCount) {
        const float scaleP = 2.f / float(faderCount);
        return -2.f + scaleP * float(i);
    }

private:
    typedef std::chrono::steady_clock Clock;
    std::vector<float> faders_;
    std::vector<bool> mutes_;
    uint32_t rng_;
    Clock::time_point lastFader_;
    Clock::time_point lastMute_;
    bool firstStep_;
};

} // namespace cinemix

#endif // CINEMIX_TEST_MODE_ANIMATOR_H
