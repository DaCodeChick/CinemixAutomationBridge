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

---

# Second Surgical Audit — Correctness Pass

## SPSC producer contract (host parameter writes) — FIXED

**Finding:** `setHostParameter()` was documented "any thread", but it pushed
into a single-producer ring; the only caller is AUv2 `SetParameter`, which the
host may invoke from several threads concurrently (audio thread plus UI
threads). Two concurrent producers could pass the fullness check and stomp
the same slot — a real lost-update/race defect.
**Severity:** high (concurrency contract violation, silent lost automation).
**Evidence:** `CinemixAU::SetParameter → AutomationEngine::setHostParameter`;
Apple's AUv2 `SetParameter` has no single-caller contract.
**Correction:** removed the host-event ring entirely. `setHostParameter` is
now two atomic stores (value + per-parameter dirty flag); the worker — which
already wakes every 1 ms — exchanges the flags and processes the latest
values. Wait-free, multi-producer safe, latest-wins by construction, and no
overflow loss is possible. The SPSC byte ring remains only for inbound MIDI,
whose single producer is CoreMIDI's serialized read proc (documented at
`handleIncoming`).
**Tests:** 4-thread × 5000-write stress test added; the whole engine suite
(26 cases, including worker-thread and destructor tests) passes under
ThreadSanitizer on Linux.
**Target validation required:** none (pure portable core).

## Scheduler position-command modeling — FIXED

**Finding:** position updates were stored in `OutboundCommand` with
`kind = SetMode` ("irrelevant for positions") — a value lying about its
meaning.
**Correction:** added `CommandKind::PositionUpdate` and use it; no behavioral
change.

## Scheduler queue-full policy — MADE EXPLICIT

**Finding:** lane overflow behavior was implicit (unbounded deque).
**Correction:** both lanes are capped at 1024 entries (far above the largest
legitimate burst). Over-cap commands are dropped and counted with a
rate-limited warning; `SystemReset` is safety-critical and instead evicts the
oldest entry; the high lane drops the NEWEST reply (absolute mode state —
superseded by the next one) and never reorders older replies. Policy
documented in `TransmissionScheduler.h` and regression-tested.

## CaptureFile — genuine second pass

**Finding:** write failures were invisible (short `fwrite` unchecked, `ok()`
kept reporting success); direction/port fields were not validated although
`Malformed` implied they were; endian helpers took unchecked output pointers;
ownership used hand-rolled `FILE*` deleters.
**Correction:** switched the file layer to `std::ofstream`/`std::ifstream`
(RAII, explicit stream state; borrowed-stream constructors enable in-memory
`std::stringstream` testing — brief §7 decision documented in the header);
pure codecs now return fixed-size `std::array` values (`encodeU32Le`/
`encodeU64Le`/`decode…`) — the encoded size lives in the type and there is
no unchecked output pointer; `decodeRecord` validates direction ∈ {0,1} and
port ∈ {0,1,2}; the writer flushes after every write so buffered-stream
write failures (e.g. ENOSPC on `/dev/full`) mark the writer failed, `ok()`
stays false and later writes are no-ops. Format unchanged.
**Tests:** `/dev/full` failure-state test, invalid direction/port tests,
stringstream round trip, truncated/malformed/version cases — all passing.

## CoreMIDI startup and partial initialization — FIXED

**Finding:** `MIDIOutputPortCreate` results were ignored, so `start()` could
report success without the required output ports; cleanup was duplicated in
the destructor and not failure-path safe.
**Correction:** every CoreMIDI creation result is checked; any failure
disposes everything created so far via one idempotent `shutdown()` (also the
destructor's single path) and returns false with diagnostics.
`MIDIPortConnectSource` failures are diagnosed and leave the role
unconnected rather than half-claimed.
**Target validation required:** yes — CoreMIDI failure paths cannot be
reproduced on Linux; marked for the target Mac.

## Topology handling — MADE COHERENT (polling-only)

**Finding:** a notification callback API (`topologyDirty_`, handler, user
pointer) existed but was never invoked — dead infrastructure beside the UI's
0.5 s polling.
**Correction:** removed the dead callback API entirely; the Cocoa view's
polling timer is now the single documented topology mechanism (it only
rebuilds popups whose item lists actually changed). No AppKit code is ever
called from a CoreMIDI callback thread.

## Test Faders robustness — HARDENED

**Finding:** endpoint loss mid-test left the oscillator computing against a
dead transport; the UI label "Test Mode" understated that physical motors
move.
**Correction:** the oscillator step checks `transport.connected()` and
self-stops (modes restored, queued positions canceled) on disconnect; the
UI control is now labeled **Test Faders** / **Stop Fader Test**; status shows
"fader test running". Destructor-while-active and mute-suppression behavior
regression-tested.

## UI lifetime — FIXED

**Finding:** the refresh timer strongly retained the view (dealloc
unreachable — leak and post-window refresh); the log queue was manually
`new`/`delete`d.
**Correction:** block-based `NSTimer` with a weak capture (10.12+),
invalidated in `viewWillMoveToWindow:` and `-dealloc`; the log deque is now
a C++ value member (RAII, ARC-safe).

## Dead code removed

- `EventKind::JoyMuteChanged` (never produced — joystick mutes decode as
  `MuteChanged`).
- `kCinemixComponentDescription` (unused; identity documented instead).
- CoreMIDI topology notification members (see above).
- `TestModeAnimator` was already removed in the first audit — confirmed no
  active-source remnant remains (grep-verified; legacy material lives in
  `_legacy_analysis/`).

## Error handling / misc

- `AutomationEngine::start()` now catches `std::system_error` from thread
  creation, resets to a coherent stopped state, logs and rethrows.
- Inbound CCs are now logged at the `MidiIn` diagnostics level (the level
  previously existed with no producer).
- `Diagnostics::log` shadowing bug was already fixed in pass 1; re-verified
  under `-Wshadow`.
- ObjC casts (`(NSButton*)sender`) remain idiomatic Objective-C by decision
  (brief §19) — `-Wold-style-cast` is a C++-side policy, not ObjC syntax.

## Verification

- 83 test cases across 8 suites: all passing (Linux, gcc 16, C++14).
- ASan + UBSan clean on every suite; ThreadSanitizer clean on the engine
  suite (26 cases including the 4-thread producer stress test).
- Harness selftest, capture/replay, demo: passing.
- Zero warnings under the first-party warning set.
- macOS/AU/CoreMIDI failure paths remain IMPLEMENTED, NOT TARGET-MAC TESTED;
  nothing in this pass claims otherwise.


---

# Third Surgical Audit — Contract Closure Pass

## .cmi binary-format contracts closed

**Finding:** the decoder rejected length > 3 but accepted length 0 although
the documented format says 1..3 — "Malformed" still overstated the
validation; the record codec and reader assembled buffers with raw offsets
(`data[8]`, `data[9]`, `header[8+i]`) and the writer assembled a synthetic
header buffer merely to write it once.
**Correction:** every field the format constrains is now enforced (direction
∈ {0,1}, port ∈ {0,1,2}, payload length ∈ 1..3). All layout offsets are
derived constants (`kTimestampSize` → `kDirectionOffset` → … → 
`kPayloadOffset` → `kRecordHeaderSize` → `kMaxRecordSize`); no consumer
repeats a raw layout number, and static_asserts pin them in the tests. The
header is written directly (magic bytes, then the encoded version) — no
synthetic assembly buffer. `encodeRecord` documents its 1..3 precondition
(assert in debug) and `CaptureWriter::writeEvent` skips structurally invalid
payloads without marking the writer failed (caller bug ≠ I/O failure); the
writer can therefore never emit a record the decoder would reject.
**Tests:** zero-length → Malformed, layout static_asserts, invalid-payload
skip round trip, `/dev/full` failure latching, direction/port rejection,
stringstream round trip — all passing.

## Rule of Zero

**Finding:** `CaptureWriter`/`CaptureReader` declared out-of-line defaulted
destructors although every member is RAII-owned and the stream types are
complete in the header (the user-declared destructors also suppressed
implicit move generation without reason).
**Correction:** destructor declarations/definitions removed; members own
everything; copy remains deleted (reference member), moves available.
**Tests:** unchanged behavior, full suite green.

## Diagnostics formatting — no fixed buffers, no truncation

**Finding:** engine/scheduler diagnostics and strip/AUX naming still flowed
through `char buf[N]` + `snprintf` (arbitrary sizes, silent truncation risk);
the Cocoa view converted `char buf[512]` → `std::string`.
**Correction:** all replaced by direct `std::string` construction (small
`hexByte` helpers for MIDI bytes). Remaining raw character buffers are
documented C-API necessities only: `mkstemp` template (tests) and
`CFStringGetCString` (CoreMIDI endpoint names). `std::memcpy` is fully gone
from CaptureFile (no copies remain to make).

## AU / transport / UI state contracts made explicit

**Finding:** the AU constructor called `transport_->start()` ignoring the
result; the status line said "outputs connected", overstating what endpoint
selection proves.
**Correction:** the AU now treats transport setup failure as an explicit
diagnosed state (plugin loads, core runs, console unreachable — logged and
shown, never silent). The view's terminology is precise and documented in
code: ACTIVE = remote mode commanded (not physical proof), "MIDI outputs
selected" = CoreMIDI destinations configured. `CoreMidiTransport::shutdown()`
documents the callback-lifetime invariant: CoreMIDI issues no read-proc
invocations after port/client disposal, so the `this` captured by the read
proc cannot outlive the object.

## Scheduler queue-full behavior: high-lane test

**Finding:** the high-lane saturation policy (drop NEWEST reply, never
reorder older ones) was documented but untested.
**Correction:** regression test added (1100 replies → cap, drop counted,
order preserved).

## Verification

* 87 test cases across 8 suites, all passing (Linux, gcc 16, C++14,
  first-party warning set, zero warnings).
* ASan/UBSan clean on every suite; ThreadSanitizer clean on the engine suite
  (multi-producer stress, worker lifecycle, teardown cases).
* Harness selftest, capture/replay, demo: passing.
* Verification categories remain strict: macOS/AU/CoreMIDI/Logic/hardware
  items are IMPLEMENTED only and explicitly marked for target validation.


---

# Final Pre-Hardware Correction Pass

## Finding 1 — CoreMIDI / AutomationEngine teardown race
**Finding:** the engine destructor cleared `transport_.onIncoming` while the
CoreMIDI input port was still alive, and the engine could be destroyed while a
read-proc callback was in flight — a `std::function` data race plus potential
use-after-destroy of the engine.
**Confirmed:** yes — destructor assigned `onIncoming = nullptr` before any
input-port disposal, and the AU destroyed the engine before the transport.
**Correction:** new `IMidiTransport::stopInbound()` — quiesce the inbound path
(disconnect sources + dispose the input port; CoreMIDI guarantees no further
read-proc invocations after dispose). The engine destructor now calls
`stopInbound()` FIRST, then detaches `onIncoming`, then runs the safety
deactivation (outbound path still alive). `CoreMidiTransport::shutdown()`
reuses the same step; `FakeTransport`/`RecordingTransport` implement it.
**Tests:** portable test asserts inbound delivery is a no-op after
`stopInbound()`; real quiescence is `TARGET-MAC TEST REQUIRED` (CoreMIDI's
disposed-port contract cannot be reproduced on Linux).
**Remaining target validation:** CoreMIDI callback quiescence on macOS.

## Finding 2 — `workerRunning_` cross-thread data race
**Finding:** `workerRunning_` was a plain `bool` written by the worker thread
and read/written by lifecycle threads — a C++ data race. `listener_` had the
same problem (worker reads vs destructor nulling).
**Confirmed:** yes (traced in source).
**Correction:** `workerRunning_` → `std::atomic<bool>`; `listener_` →
`std::atomic<Listener*>` (in-class `{false}`/`{nullptr}` initialization;
acquire/release ordering). `stopRequested_` remains a plain bool because every
access is under `cmdMu_`. `listener_` reads load once into a local before
invocation.
**Tests:** repeated start/stop/start/stop/destroy stress test added; the whole
engine suite (27 cases) is ThreadSanitizer-clean on Linux.
**Remaining target validation:** none (portable).

## Finding 3 — CaptureWriter could emit records the reader rejects
**Finding:** the writer validated only payload length, so invalid direction/port
events serialized successfully and were then rejected by the reader.
**Confirmed:** yes.
**Correction:** single source of truth — `capture_detail::isValidDirection` /
`isValidPort` / `isValidPayloadLength` are used by BOTH the writer and the
decoder. `writeEvent` skips structurally invalid records (caller bug, not an
I/O failure; `ok()` stays true).
**Tests:** writer-rejects-invalid-direction/port/length round trip (only the
valid record survives and is accepted); decoder malformed cases retained.
**Remaining target validation:** none (portable).

## Finding 4 — CaptureWriter move/Rule-of-Zero documentation incorrect
**Finding:** the header claimed "move construction remains available" after the
destructor was removed, but the user-declared deleted copy constructor
suppresses the implicit move constructor.
**Confirmed:** yes — the class was effectively non-movable, contradicting the
comment.
**Correction:** the contract is now explicit: non-copyable AND non-movable
(move operations `= delete`d); the comment states the actual C++14 behavior.
**Tests:** `static_assert(!std::is_copy/move_constructible/assignable)` for
writer and reader.
**Remaining target validation:** none (portable).

## Finding 5 — 14-bit fine CC escaped position cancellation
**Finding:** the 14-bit fine CC was queued as `SetMode` with `kNoParam`, so
`cancelPosition`/`cancelAllPositions` removed the coarse component but left the
fine component orphaned to transmit.
**Confirmed:** yes.
**Correction:** new `CommandKind::PositionFine` and
`TransmissionScheduler::enqueuePositionContinuation(param, message)` — the fine
CC keeps its `ParamId` (cancellable, included in `hasPending`) but is not
coalesced independently, and is enqueued immediately after its coarse
component (MSB→LSB order preserved). The engine's 14-bit path uses it; no
legacy byte representation changed.
**Tests:** coarse/fine ordering, `cancelPosition` removes both components,
`cancelAllPositions` removes both, isolation (canceling A leaves B intact).
**Remaining target validation:** none for the plumbing; the 14-bit on-wire
behavior itself remains unverified on hardware (as before).

## Finding 6 — `selectInputs()` return value did not mean connected
**Finding:** the Boolean reflected whether the endpoint was FOUND, not whether
`MIDIPortConnectSource` succeeded.
**Confirmed:** yes.
**Correction:** `selectInputs()` now returns true only when every non-empty
requested input is found AND connected; connect failures log the `OSStatus`
and leave `src_` = 0. Contract documented in the header (outputs have no
separate connect step — `connected()` gates activation).
**Remaining target validation:** CoreMIDI connect-failure paths (macOS only).

## Adjacent checks
* **A (stale threading docs):** corrected "lock-free enqueue/push / host event
  queue" wording in `CinemixAU.h/.mm`, `AutomationEngine.h`, README to the
  actual atomic value + dirty-flag design.
* **B (HostBridge lifetime):** verified — the AU destructor body destroys the
  engine (joining the worker) while `HostBridge` is still alive; `HostBridge`
  is destroyed only afterward. Left nested; pointer `CinemixAU* au_` kept
  (reference conversion is optional polish, not a defect).
* **C (Rule of Zero):** no new explicitly-defaulted destructors introduced;
  the capture classes are now explicitly non-movable (see Finding 4).
* **D (no new C buffers):** none introduced; direct string construction kept.

## Verification
* 93 test cases across 8 suites, all passing (Linux, gcc 16, C++14, zero
  warnings under the first-party warning set).
* ASan/UBSan clean on every suite; ThreadSanitizer clean on the engine suite
  (27 cases: multi-producer stress, worker lifecycle, repeated
  start/stop/destroy).
* Harness selftest, capture/replay, demo: passing.
* macOS/AU/CoreMIDI/Logic/hardware items remain IMPLEMENTED and explicitly
  marked TARGET-MAC TEST REQUIRED — nothing is claimed as HARDWARE/LOGIC/
  TARGET-MAC TESTED.
