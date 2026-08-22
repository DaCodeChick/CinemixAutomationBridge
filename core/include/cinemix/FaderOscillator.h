// FaderOscillator — the Test Mode waveform generator.
//
// Test Mode is a live physical fader diagnostic: a deterministic phase-offset
// traveling wave across the console's motorized faders, so mapping errors are
// visually obvious (adjacent faders move at visibly different phases):
//
//   Fader 01    ~~~~~~~~
//   Fader 02      ~~~~~~~~
//   Fader 03        ~~~~~~~~
//
// Design rules (docs/COMPATIBILITY.md §4, docs/USER_GUIDE.md):
//   * moves faders ONLY — no mutes, no joysticks, no master;
//   * deterministic (pure function of time — restart-safe, testable);
//   * amplitude limited to [0.2, 0.8] of the fader travel (safe limits);
//   * values flow through the normal engine dedupe/quantization and the
//     normal transmission scheduler (MIDI bandwidth respected by design);
//   * the engine decides the update cadence; this class only computes values.
#ifndef CINEMIX_FADER_OSCILLATOR_H
#define CINEMIX_FADER_OSCILLATOR_H

#include <cmath>
#include <cstddef>

namespace cinemix {

class FaderOscillator {
public:
    // Waveform parameters. constexpr functions rather than static data
    // members: this class is header-only, and pre-C++17 static constexpr data
    // members would need out-of-class definitions if ever odr-used.
    static constexpr float periodSeconds() noexcept { return 12.0f; }
    // Travel: value = 0.5 ± amplitude. 0.30 keeps the faders inside
    // [0.2, 0.8] — never slamming against the end stops.
    static constexpr float amplitude() noexcept { return 0.30f; }
    // Number of full wave cycles laid across the console from first to last
    // fader. 4 cycles across 72 faders = 18 faders per cycle: mapping errors
    // (wrong fader moving) are immediately visible as phase discontinuities.
    static constexpr float wavesAcrossConsole() noexcept { return 4.0f; }

    explicit FaderOscillator(std::size_t faderCount) noexcept : faderCount_(faderCount) {}

    // Deterministic waveform value for `faderIndex` at `elapsedSeconds`.
    // Always within [0.5 - amplitude, 0.5 + amplitude].
    float valueAt(double elapsedSeconds, std::size_t faderIndex) const noexcept {
        const double count = static_cast<double>(faderCount_ == 0 ? 1 : faderCount_);
        const double phase =
            kTwoPi * (elapsedSeconds / static_cast<double>(periodSeconds()) -
                      static_cast<double>(wavesAcrossConsole()) *
                          static_cast<double>(faderIndex) / count);
        return 0.5f + amplitude() * static_cast<float>(std::sin(phase));
    }

    std::size_t faderCount() const noexcept { return faderCount_; }

private:
    constexpr static double kTwoPi = 6.283185307179586476925286766559;

    std::size_t faderCount_;
};

} // namespace cinemix

#endif // CINEMIX_FADER_OSCILLATOR_H
