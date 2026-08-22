# Testing

## Layers and their test coverage

| Layer | Where | Runs on | Status |
| --- | --- | --- | --- |
| Protocol encode/decode gold data | `core/tests/test_protocol.cpp` | Linux | **passing here** |
| Byte-stream parser robustness (running status, sysex, realtime) | `core/tests/test_parser.cpp` | Linux | **passing here** |
| Parameter map / legacy IDs 0..160 | `core/tests/test_param_map.cpp` | Linux | **passing here** |
| Scheduler (order, coalescing, budget, cancel, priority) | `core/tests/test_scheduler.cpp` | Linux | **passing here** |
| Engine: byte-exact activation/deactivation, snapshot/reset/all-mutes, touch modes, echo suppression, pending-target protection, origins, profiles, thread mode, destructor safety | `core/tests/test_engine.cpp` | Linux | **passing here** |
| SPSC ring + .cmi capture format | `core/tests/test_ring.cpp`, `test_capture.cpp` | Linux | **passing here** |
| Scripted console scenario (loopback) | `tools/cinemix_harness selftest` | Linux | **passing here** |
| Capture/replay of a whole session | `tools/cinemix_harness selftest --out f.cmi` + `replay f.cmi` | Linux | **passing here** |
| AUv2/CoreMIDI/AppKit | `tools/validate_au.sh`, `mac/build.sh` | target Mac | **not yet run** |
| Real console | manual checklist below | target studio | **not yet run** |

Run everything portable:

```sh
cmake -S . -B build && cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/tools/cinemix_harness/cinemix_harness selftest --out build/selftest.cmi
./build/tools/cinemix_harness/cinemix_harness replay build/selftest.cmi
```

## What the regression tests pin (behavior preservation)

* **Legacy byte sequences** — the activation sequence (remote mode → WRITE
  sweep → snapshot → AUTO sweep) and deactivation sequence (`0xFF` final)
  are compared byte-for-byte against gold vectors that mirror the legacy
  plugin's `ActivateMixer`/`DeactivateMixer`; one documented deviation: mode
  sweeps are scoped to the strips that exist in the profile instead of a
  hardcoded 48 slots per side (docs/COMPATIBILITY.md §3/§4).
* **Legacy value semantics** — 7-bit truncation (`int(v*127)`), mute 2=OFF
  3=ON (non-3 ⇒ OFF), AUX 2n/2n+1, master default 1.0, dedupe keyed on the
  *parameter value* (legacy `prev_CC_Val`), not the wire byte.
* **Test Mode correctness** — deterministic waveform (period, band limits,
  phase offsets), engine-level: fader traffic only, **no mute commands**,
  READ-mode sweep without any reset/snapshot side effects, mode restore on
  exit, host-listener silence, activation requirement, echo silence.
* **Quantization edge cases** — NaN/±infinity handling, legacy truncation
  preserved, 7-bit round trips.
* **Port identity** — channel-only decoding disambiguates both console ports
  (see docs/ARCHITECTURE.md §7).
* **Producer contract** — `setHostParameter` is hammered from four threads
  at once (5000 writes each) while the engine drains: the multi-producer
  dirty-flag design must end in a consistent state (verified under
  ThreadSanitizer on Linux).
* **Capture failure semantics** — write failures (via `/dev/full`) leave the
  writer failed, never "healthy"; invalid direction/port and
  zero-length payload fields are Malformed; in-memory stringstream round trips; little-endian primitives
  with the encoded size in the type.
* **Scheduler queue-full policy** — over-cap commands are dropped and
  counted; SystemReset is always admitted.
* **Test-mode robustness** — self-stop on transport disconnect, safe
  destruction while active, mute suppression during the test.
* **Feedback-loop prevention** — console echoes of commanded positions are
  suppressed; hand moves are reported; a pending outbound target survives the
  console's report of the *previous* state; host-originated changes are never
  echoed back to the host listener.
* **Touch state machine** — touch ⇒ WRITE reply, release ⇒ AUTO reply, only
  in AUTO mode (legacy gate), SEL rotation 0→1→2→3→0, master SEL broadcasts.

## On-console checklist (target studio — manual)

Run once with the bridge in verbose/MIDI logging, then once as a normal
session:

1. Cable test (§USER_GUIDE): `CC127=127 ch5` enters remote mode; `0xFF`
   releases it.
2. `make install`, load in Logic, select the four ports, **Activate**:
   faders to −∞, mutes off, R+W LEDs on.
3. Move a fader by hand → Logic parameter UI follows (touch + move).
4. Record automation in Logic (Touch), playback → fader motors follow, no
   runaway, no stuck motors, no LED flicker storms.
5. Two faders at once; then five (expect the known 2–3-fader touch limit
   **[legacy-verified]**; SEL fallback).
6. Mutes, AUX mutes, joysticks (if fitted), master fader.
7. High-density automation (many lanes) → console keeps up; the Log pane
   shows no queue overflow warnings.
8. Unplug/replug the MIDI interface with the console active → status shows
   disconnected; re-activate manually; console never left in remote mode on
   plugin removal.
9. Close Logic → console released (`0xFF` last byte in the MIDI log).

## Capture/replay format (.cmi)

`core/include/cinemix/CaptureFile.h` — little-endian binary: magic
`CMIXCAPI`, u32 version 1, then records `{u64 timestampUs, u8 direction
(0=console→bridge, 1=bridge→console), u8 port, u8 length, bytes}`. The
harness records a full scripted session and replays the console side through
a fresh bridge for regression and documentation purposes.
