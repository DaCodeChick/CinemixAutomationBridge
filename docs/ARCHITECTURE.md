# Architecture — Cinemix Automation Bridge (macOS AUv2)

## 1. Layering

```
 Logic Pro (automation lanes, host UI)
        │  AUv2 parameter API (SetParameter / listeners)
        ▼
 ┌────────────────────────────┐   mac/ (Objective-C++, AppKit, CoreMIDI)
 │  CinemixAU  (AUv2 generator)│
 │  CinemixView (AppKit panel) │
 │  CoreMidiTransport          │
 └────────────┬───────────────┘
              │  C++ facade (CinemixBridge)
              ▼
 ┌────────────────────────────┐   core/ (portable C++11, no Apple deps)
 │  AutomationEngine          │   state + origins + commands + touch modes
 │  TouchModeTracker          │   SEL/touch state machine
 │  TransmissionScheduler     │   pacing, coalescing, priority lanes
 │  CinemixProtocol           │   domain events ↔ MIDI bytes
 │  MixerProfile              │   console configuration model
 │  ParameterMap              │   AU param IDs ↔ controls
 │  TestModeAnimator          │   demo animation
 └────────────┬───────────────┘
              │  IMidiTransport (abstract: send(port, bytes) / inbound callback)
              ▼
    CoreMIDI endpoints ──→ MIDI interface ──→ D&R Cinemix
```

* The **core** knows *what a Cinemix control means* and *how the console
  encodes it*. It compiles and runs its full test suite with no Apple APIs.
* The **protocol layer** translates `ConsoleEvent` ↔ MIDI byte sequences.
* The **AU layer** knows *how Logic represents a control* (parameter ID,
  units, display) and forwards host writes/reads.
* The **transport** knows *how bytes reach the hardware*.

## 2. Threading and real-time safety

```
 [Logic audio thread]  Render: write silence only.
                       SetParameter: atomic<float> store + lock-free push
                                     {paramId, value} into host-event SPSC ring.

 [CoreMIDI read proc]  Copy raw packet bytes into inbound SPSC byte ring.
                       No allocation, no parsing, no logging.

 [Bridge worker thread] (1 per bridge; owns engine + scheduler)
   1. drain inbound byte ring → run-length-safe parser → ConsoleEvents
   2. drain host-event ring → engine.onHostParameter
   3. engine logic (touch modes, origins, echo suppression, commands)
   4. scheduler tick (1 ms): outbound budget → MIDIPacketList → MIDISend
   5. notify AU listeners (begin/change/end gesture) for user-originated changes

 [UI thread]           AppKit; enqueues commands to worker via mutex/condvar
                       queue; subscribes to diagnostics ring.
```

* The audio callback allocates nothing and blocks on nothing.
* All MIDI I/O happens on the worker thread; CoreMIDI's `MIDISend` is
  thread-safe and non-blocking in practice.
* Queues are fixed-size; overflow is counted and reported in diagnostics
  (never fatal, never blocking on the audio thread).

## 3. Data model (core/cinemix)

* `MixerProfile` — immutable description: LO/HI strip counts, stereo strip
  indices, presence flags (master, joysticks 1/2, AUX 1..10), fader
  resolution, channel/CC/port scheme, pacing parameters. Default = legacy
  author's console (LO24/HI12/S1-4), reproducing legacy parameter IDs 0..160.
* `ConsoleEvent { ControlRef control; uint8_t midiValue; }` with
  `ControlRef { ControlClass; strip; path; index; }` — Cinemix concepts, never
  raw bytes at the engine level.
* `CinemixProtocol` — pure table-driven encode/decode:
  * encode: `ConsoleCommand` (position/mute/mode/remote/reset) → `MidiMessage{port, bytes}`
  * decode: `MidiMessage` → decoded `ConsoleEvent` or `Unknown`/`Ignored`
    classification (for diagnostics).
  * reverse lookup table (16 channels × 128 CCs → control) built once from the
    profile; `0xFF`/system handling separate.
* `ParameterMap` — AU parameter id ↔ `ControlRef` ↔ name/display info.
  IDs derived from the profile in the legacy order (faders, mutes, AUX,
  joysticks, master).
* `AutomationEngine` — owns per-parameter state with **change origin**
  (`Host`, `Console`, `UserInterface`, `Internal`), the touch mode tracker,
  activation/deactivation/snapshot/reset/all-mutes/test-mode commands, and
  echo-suppression logic. Emits events to an `AutomationListener`
  (the AU implements it to drive Logic).
* `TransmissionScheduler` — two FIFO lanes: **high** (touch replies — never
  delayed, never coalesced) and **main** (everything else, preserving legacy
  byte order); position updates coalesce latest-wins per parameter; a global
  credit budget caps the send rate. High/command messages are never dropped;
  position overflow coalesces by design.
* `IMidiTransport` — `send(port, message)`, `setIncomingHandler`, `connected`
  state, `name` lookups. Implementations: `CoreMidiTransport` (mac/),
  `LoopbackTransport` + `CaptureReplayTransport` (tools/; used by tests and
  the hardware-free harness).
* `Diagnostics` — leveled sink; UI tail ring; unknown inbound CCs logged with
  raw bytes at `Verbose`.

## 4. Feedback-loop prevention (brief §14)

Rules, all regression-tested:

1. Host write (`SetParameter`) → update state (origin `Host`) → outbound,
   unless the value is unchanged in the parameter domain (legacy
   `prev_CC_Val` semantics — the dedupe reference is the *parameter value*,
   never the wire byte, so mute/AUX re-commands work correctly).
2. **Echo suppression**: an untouched fader/axis report within N 7-bit steps
   (default 2, configurable) of the *last commanded* value is a motor echo:
   state + UI update only — no host notification, no re-send. Mutes compare
   exactly.
3. **Pending-target protection**: if we have commanded a value that has not
   reached the wire yet, a console report reflects the *previous* command
   (motor still traveling): update the visible state silently, keep the
   pending target, do not notify.
4. **Touch wins**: a touched control's reports are always user-originated:
   cancel any pending outbound target for it and notify the host (with begin/
   end gesture events from the touch sensor).
5. Untouched deviation with nothing pending (hand move on the master fader,
   mutes, missed touches): user-originated — notify host, cancel pending,
   never re-send.
6. Commands (activate/reset/snapshot) send their own deterministic sequences;
   snapshot/reset/all-mutes emit ordinary parameter notifications (the host
   decides whether to record — same effective behavior as legacy
   `setParameterAutomated`).
7. Test mode sends MIDI only; never notifies the host.

The loop "console echo → host write → outbound → echo" dies at rule 1: the
host write of the same value is not re-sent, exactly as in the legacy
bridge's dedupe.

## 5. AUv2 specifics

* Component type `augn` (generator), subtype `DRcm` (kept from legacy),
  manufacturer `CBRG`, name "Cinemix Automation Bridge", v2.0.0.
* Built on **Apple's AudioUnitSDK** (vendored under `mac/vendor/`, Apache-2.0,
  the classic 2020 initial release targeting macOS 10.9+) — the same AUBase
  scaffolding AUv2 plugins were built on in the Logic era. The vendored SDK is
  used **unmodified**; it requires C++17, so the Apple layers compile as C++17
  while the portable core stays C++11.
* Overrides: parameter info/list (legacy IDs 0..160 for the default profile),
  silence render, ClassInfo (AUBase), CocoaUI factory (programmatic AppKit
  panel: port pickers, Activate/Deactivate, Send Snapshot, Reset All,
  All Mutes, Test Mode, status, live diagnostics log with level filter),
  `kAudioUnitProperty_ParameterListener` bookkeeping — console moves reach
  Logic as begin/change/end gesture events (touch automation).
* Configuration persisted with NSUserDefaults (port names by role,
  diagnostics level) + optional profile plist in Application Support.
* On component close/dispose the engine destructor **deactivates the console**
  (legacy destructor behavior — safety requirement #1).

## 6. Bandwidth budget (brief §13)

* DIN MIDI ≈ 3-byte message / 0.96 ms → ~1040 msg/s theoretical.
* Default budget: **500 msg/s**, tick 1 ms, fractional credit accumulation
  (maxBurst 8 messages). Two FIFO lanes: **high** (touch replies — jump the
  queue, latency-sensitive mode replies) and **main** (everything else,
  preserving the legacy byte order of multi-step sequences). Position updates
  coalesce latest-wins inside the main lane; coalescing guarantees the final
  value always lands. Configurable in the profile.
* Touch replies queue ahead of bulk traffic; activation sequences are sent
  paced (≈0.6 s) instead of the legacy instant blast.

## 7. Testing (brief §20/§21)

* `tests/` — zero-dependency C++11 test binary (CTest): protocol encode/decode
  gold data (including the exact legacy activation/deactivation byte
  sequences), running status, sysex/realtime robustness, parameter map IDs,
  round-trips, scheduler ordering/coalescing/budget, engine origins +
  echo suppression + touch modes, profiles, capture/replay format.
* `tools/cinemix_harness` — CLI: `--selftest` (scripted console scenario over
  a loopback transport), `--capture FILE.cmi`, `--replay FILE.cmi` (feeds
  recorded traffic through the real core and logs every decoded event and
  every outbound byte). Runs on any POSIX box; on the Mac it can also drive
  CoreMIDI virtual endpoints.
* `tools/validate_au.sh` — the exact on-target validation recipe (auval,
  Logic rescan) documented in BUILDING.md.
