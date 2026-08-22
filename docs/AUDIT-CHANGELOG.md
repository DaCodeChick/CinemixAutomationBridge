# Audit & Correction Pass — Change Log

Surgical corrections from the second-pass audit. Architecture, protocol
behavior and regression compatibility were preserved unless a change is
listed here.

## Standard compliance

### First-party code is now C++14
**Problem:** the project requirement is conservative C++14; build files, docs
and comments declared C++11.
**Why it matters:** the stated engineering baseline was inconsistent with the
implementation.
**Change:** `core/CMakeLists.txt`, `tools/cinemix_harness/CMakeLists.txt`
(`CXX_STANDARD 14`, `cxx_std_14`), `mac/Makefile` (core + non-AU glue
`-std=c++14`), all doc/comment claims.
**Vendor boundary kept:** `vendor/AudioUnitSDK` and the AU component that
includes its headers remain gnu++17 (Apple's own C++17 requirement, including
a C99 designated-initializer extension). First-party code was NOT raised to
C++17 to accommodate the vendor (brief §22).

## Test Mode correction (brief §11–13)

**Problem:** the inherited Test Mode did a full console reset and randomly
toggled mutes — destructive, unrelated to a fader diagnostic.
**Why it matters:** Test Mode drives physical motors; it must not alter
unrelated console state.
**Change:** replaced `TestModeAnimator` (fader ramp + `rand()` mutes) with
`FaderOscillator` — a deterministic phase-offset traveling wave over faders
only: period 12 s, amplitude 0.30 (travel [0.2, 0.8] — never end-stops),
4 wave cycles across the console so mapping errors are visually obvious.
**Behavior preserved/changed:** changed — mutes removed, reset/snapshot
removed, mode handling scoped: strips go to READ(1) for the duration
(console-side write activity stays off — same reason the legacy used READ)
and previous modes are restored on exit; queued positions are canceled on
exit (immediate stop); Test Mode now requires an activated console; console
reports during Test Mode are not forwarded to the host (test motion can never
be recorded as automation). Legacy activation/deactivation/snapshot bytes are
untouched.
**Validation:** new engine tests (fader-only traffic, no mute CCs, READ sweep
without reset, mode restore, listener silence, activation requirement) and a
new `test_oscillator` suite (determinism, band limits, periodicity, phase
offsets) — all passing, ASan/UBSan-clean.

## C++ modernization (first-party only)

### constexpr / noexcept
**Change:** `clamp01`, `quantize7`, `normalize7`, `faderHysteresis`,
`muteByte`, `auxMuteByte`, `Diagnostics::levelName`, protocol CC helpers,
`MixerProfile` derived accessors, `ControlRef`, `MidiMessage::isSystemReset`
are now C++14 `constexpr` and `noexcept` where genuinely applicable.
`MidiMessage::controlChange/systemReset` are plain `noexcept` functions:
mutating `std::array` elements is not a C++14 constant-expression capability
(C++17 library feature) — documented in code.
**Pre-C++17 ODR rule honored:** `AutomationEngine`'s static constexpr data
members got out-of-class definitions (the linker caught an odr-use of
`kOscillatorStepPeriod`); the header-only `FaderOscillator` uses constexpr
functions instead of static data members (inline by definition in C++14).

### std::array for fixed buffers
**Change:** `MidiMessage::data`, `MidiParser::pendingData_`, capture-format
buffers and the fake-console byte triplets now use `std::array`. Raw pointers
remain only where C APIs demand them (`FILE*`, CoreMIDI packet lists) and the
SPSC drain interface (justified: allocation-free bounded consumer, brief §15).

### Cast audit
**Change:** functional/C-style casts converted to `static_cast` across the
core, tests and harness (~280 sites); `-Wold-style-cast -Wconversion
-Wsign-conversion` now keep them out (all first-party targets build warning-
free). ObjC `(NSView*)sender`-style casts in AppKit code were left as
idiomatic ObjC.

### typedef → using
**Change:** `ParamId`, `MidiByte`, `Diagnostics::Sink` and the parser callback
aliases use `using`. Strong-type assessment (brief §7): `enum class` already
covers ControlClass/Origin/StripPath/ConsoleSide; channel/CC/port remain
`uint8_t` at the wire boundary because they interop with CoreMIDI/table
indices and every interface names them — wrapping them would add churn
without closing a demonstrated bug class. Documented decision, not an
oversight.

### Warning policy
**Change:** first-party targets compile with `-Wall -Wextra -Wpedantic
-Wconversion -Wsign-conversion -Wold-style-cast -Wshadow` (CMake; the
macOS Makefile uses the supported subset). Not `-Werror`; vendored SDK
excluded. Fixes included a real shadowing bug (`Diagnostics::log` parameter
`level` shadowed `level()`).

### NaN / conversion audit
**Change:** `clamp01` defines NaN and ±∞ deterministically (`!(v > 0)` catches
NaN) so the legacy truncation `int(v * 127)` can never hit undefined
float→int conversion. Legacy truncation is unchanged for finite values.
**Validation:** dedicated quantization tests.

## .cmi capture modernization (format byte-identical)

**Problem:** C-style byte shuffling, manual `fclose`, output-parameter
parsing, implicit endianness.
**Change:** split into a pure record codec (`capture_detail`: value-returning
`EncodedRecord`/`DecodedRecord`, explicit little-endian shifts, bounds +
length validation, Truncated/Malformed statuses) and a file layer with RAII
ownership (`std::unique_ptr<FILE, FileCloser>`), clean-EOF vs truncated-record
distinction, and explicit unsupported-version rejection. On-disk format is
unchanged; existing captures remain readable.
**Validation:** codec round-trip, LE primitives, truncation/malformed cases,
version rejection, file round trip, truncated-stream behavior — all passing.

## CoreMIDI port identity (audited, no change)

**Finding:** source-port identity is NOT required for decoding — the two
console halves use disjoint MIDI channels (LO 1/3, HI 2/4, master 5) and the
legacy RtMidi callbacks discarded source identity too. Channel-based decoding
through one input port is correct by design.
**Change:** documented in docs/ARCHITECTURE.md §7; regression test added
(`protocol: channel-only decoding disambiguates both console ports`).

## AUv2 / Core Foundation contracts (documented, not behavior-changed)

**Change:** `CinemixAU.mm` now documents the ownership contracts it relies on:
`GetParameterInfo` name (AU allocates via `CFBridgingRetain`, host releases),
`kAudioUnitProperty_CocoaUI` (AU creates both CF objects, host releases), and
the parameter-listener add/remove heuristic (both
`AudioUnitAdd/RemoveParameterListener` set the same property with identical
bookkeeping structs; the classic SDK provides no other authoritative
mechanism). All three are marked **REQUIRES LOGIC VALIDATION** — none is
claimed tested. `CoreMidiTransport` output-endpoint refs are atomic (UI
thread writes, worker reads).

## Lifecycle contract (resolved)

**Problem:** header and destructor made contradictory claims about who
deactivates.
**Change:** one authoritative contract, documented in `AutomationEngine.h` and
`docs/ARCHITECTURE.md`: `deactivate()` is the orderly path (UI button, AU
disposal, destructor all use it); the destructor additionally runs the same
release sequence as a defensive safety net if the console is still in remote
mode, then stops the worker and detaches the transport callback. Test Mode is
terminated inside deactivation. Explicit deactivation is recommended, never
required for safety.

## Licensing

**Problem:** NOTICE described first-party code as MIT; the repository's
LICENSE is GPLv3.
**Change:** NOTICE/README now state first-party = GPL-3.0, with the
-only/-or-later choice explicitly flagged as un-established and left to the
maintainer (brief §20). Vendored AudioUnitSDK stays Apache-2.0; legacy GSi
material keeps its own attribution. No vendored file was modified.

## Verification

* 75 test cases across 8 suites, all passing (Linux, gcc 16, C++14).
* ASan + UBSan pass on every suite.
* Harness selftest, capture/replay and demo pass.
* First-party core/harness build warning-free under the strong warning set.
* macOS sources remain implemented-but-uncompiled here; nothing in this pass
  changed that status, and no AU/Logic/hardware validation is claimed.
