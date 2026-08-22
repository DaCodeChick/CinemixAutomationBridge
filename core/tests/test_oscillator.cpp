// FaderOscillator tests — the Test Mode waveform must be deterministic,
// fader-only, band-limited, and phase-offset across the console.
#include <cmath>

#include "TestFramework.h"
#include "cinemix/FaderOscillator.h"

using namespace cinemix;

namespace {

TEST_CASE("oscillator: deterministic pure function of time") {
    FaderOscillator oscillator(72);
    for (double t = 0.0; t < 30.0; t += 0.7) {
        for (size_t i = 0; i < 8; ++i)
            CHECK_EQ(oscillator.valueAt(t, i), oscillator.valueAt(t, i)); // repeat call
    }
}

TEST_CASE("oscillator: values stay inside the safe band [0.2, 0.8]") {
    FaderOscillator oscillator(72);
    float minValue = 2.0f, maxValue = -2.0f;
    for (double t = 0.0; t < FaderOscillator::periodSeconds(); t += 0.05) {
        for (size_t i = 0; i < 72; ++i) {
            const float value = oscillator.valueAt(t, i);
            if (value < minValue) minValue = value;
            if (value > maxValue) maxValue = value;
        }
    }
    CHECK(minValue >= 0.2f - 1e-4f);
    CHECK(maxValue <= 0.8f + 1e-4f);
    CHECK_NEAR(minValue, 0.5f - FaderOscillator::amplitude(), 1e-3f);
    CHECK_NEAR(maxValue, 0.5f + FaderOscillator::amplitude(), 1e-3f);
}

TEST_CASE("oscillator: periodic with the documented period") {
    FaderOscillator oscillator(72);
    const double period = static_cast<double>(FaderOscillator::periodSeconds());
    for (size_t i = 0; i < 72; i += 5) {
        const float at0 = oscillator.valueAt(0.0, i);
        const float atPeriod = oscillator.valueAt(period, i);
        CHECK_NEAR(at0, atPeriod, 1e-4);
    }
}

TEST_CASE("oscillator: phase offset makes adjacent faders move differently") {
    FaderOscillator oscillator(72);
    // At t=0 not all faders share the same value (a traveling wave).
    size_t differing = 0;
    for (size_t i = 0; i + 1 < 72; ++i) {
        const float a = oscillator.valueAt(0.0, i);
        const float b = oscillator.valueAt(0.0, i + 1);
        if (std::fabs(a - b) > 1e-3f) ++differing;
    }
    // 4 waves across 72 faders: the vast majority of neighbors differ.
    CHECK(differing > 60);

    // The first fader and fader 18 (one full wave cycle away, 4 cycles per
    // 72 faders) share the same phase.
    const float first = oscillator.valueAt(0.0, 0);
    const float oneWaveAway = oscillator.valueAt(0.0, 18);
    CHECK_NEAR(first, oneWaveAway, 1e-4);
}

TEST_CASE("oscillator: handles degenerate fader counts") {
    FaderOscillator one(1);
    CHECK_NEAR(one.valueAt(0.0, 0), 0.5f, 1e-6f); // phase 0 → sine 0
    FaderOscillator zero(0);
    CHECK(zero.valueAt(0.0, 0) >= 0.2f && zero.valueAt(0.0, 0) <= 0.8f);
}

} // namespace

int main(int argc, char** argv) {
    return testfw::Registry::instance().runAll(argc > 1 ? argv[1] : nullptr);
}
