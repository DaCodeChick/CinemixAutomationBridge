# Compatibility Model — Legacy → New Bridge

This is the decision record required by the project brief: for every
significant legacy behavior, what it does, why, what it depends on, and how the
new bridge represents it. Behavior that is intentionally changed is marked
**CHANGED** with a reason; behavior that is dropped is marked **DROPPED** with
the reason it is safe to drop.

Sources: legacy `Plugin.h`/`Plugin.cpp` (VST2.4 + VSTGUI 3.0 + RtMidi,
Windows x86, also compiled with `MACAU` at some point), `Editor.cpp/h`,
`MIDI_Cinemix.txt`, `README.md`.

## 1. Console protocol behaviors — PRESERVED

| Legacy behavior | Purpose | HW dep | Legacy dep | New component |
| --- | --- | --- | --- | --- |
| `CC127=127 ch5` (both ports) enters remote mode; `CC127=0 ch5` + `0xFF` byte exits | Console remote-control switch | yes | none | `ActivationSequence` in `AutomationEngine` |
| Activation: all strips→Write(2) → snapshot → all strips→Auto(3); master+joystick SEL 2 then 3 | Deterministic power-on-style init; console resets, mutes off, R/W LEDs on | yes | none | `AutomationEngine::activate()` (byte-exact regression test) |
| Deactivation (and plugin destruction!) sends mode 0 to everything + `CC127=0` + `0xFF` | Never leave console stranded in remote mode | yes | none | `AutomationEngine::deactivate()`, called from AU cleanup |
| Snapshot sends all 161 parameters with dedupe cache cleared | Capture full console state into the song | yes | none | `AutomationEngine::sendSnapshot()` |
| Reset All: all modes→Auto(3), all params 0 except master fader 1, then snapshot | "Faders to −∞" full reset | yes | none | `AutomationEngine::resetAll()` |
| All Mutes toggles params 72..143 + AUX + joystick mutes through host-automated writes | Global mute toggle recorded by host | yes | VST2 `setParameterAutomated` | engine command + AU listener notification |
| Strip SEL press (val 1) rotates local mode 0→1→2→3→0 and replies new mode | Manual R/W selection fallback | yes | none | `TouchModeTracker` |
| Touch (val 6) in Auto replies Write(2); release (val 5) in Auto replies Auto(3) | Touch-driven write arming | yes | none | `TouchModeTracker` (high-priority queue) |
| Master SEL press rotates master mode and applies to ALL strips (ch5 CC64 not rewritten) | Master has no touch sensor | yes | none | `TouchModeTracker::masterSel()` |
| Fader outbound: even CC only, `round(v*127)`, per-parameter dedupe | 7-bit fader positioning | yes | none | `TransmissionScheduler` coalescing (superset) |
| Fader inbound: either CC of pair = 7-bit position; master fine CC (ch5 CC1) handled explicitly | Fader read-back | yes | none | `CinemixProtocol::decode` (table + master-fine entry) |
| Mute 2=OFF 3=ON (non-3 → off); AUX n = 2n/2n+1 on CC96 ch5; joystick mutes CC24/26 ch4 | Switch encoding | yes | none | `CinemixProtocol` |
| LO ch1/ch3 port1, HI ch2/ch4 port2, master ch5 port2, joystick axes ch2 port2 | Side-based channel/port split | yes | none | `MixerProfile` + `CinemixProtocol` |
| Parameter IDs 0..160 exactly as legacy for default profile | ID stability with legacy docs; Logic automation IDs | — | none | `ParameterMap` (IDs derived from profile; default = legacy) |
| Strip naming: strips 25–28 = S1..S4 (stereo), others M1..M32 | Author's console layout | yes | none | `MixerProfile::stereoStrips` (default {24,25,26,27}) |
| Test Mode moves faders without writing host automation | Demo/diagnostic | no | VST2 audio loop as clock | `FaderOscillator` (CHANGED: worker-thread clock, paced, fader-only traveling wave — no mutes) |
| Master fader default = 1.0, everything else 0 | Legacy default state | — | none | `AutomationEngine` initial state |
| Port selection persisted by name (ini), restored on load, alert on open failure | Config persistence | no | RtMidi/ini | CHANGED: NSUserDefaults (macOS idiomatic) |
| Broadcast mode/remote commands to both ports; route position data per side | Proven-safe addressing | yes | none | transport `port` argument (0 = broadcast) |

## 2. Legacy platform/architecture behaviors — DROPPED or CHANGED

| Legacy behavior | Why it existed | Decision |
| --- | --- | --- |
| VST2.4 `AudioEffectX`/`processReplacing` producing silence, "REQUIRED BY SOME HOSTS!" | VST2 instrument slot | DROPPED — replaced by AUv2 generator (zero inputs, silence outputs). Audio path remains minimal/transparent per brief §12. |
| MIDI transmission from the audio loop (Test Mode) | Convenient sample-accurate clock | CHANGED — moved to a paced scheduler thread; audio loop does nothing but output silence (§11/§12). |
| `#ifdef RTMIDI` host-MIDI fallback path (`sendVstMidiEvent`) | No-hardware demo mode | DROPPED — CoreMIDI is the transport; harness/file transport replaces the demo path. |
| VSTGUI 3.0 36-strip skin GUI + CTouchPad/XFadeSplash | Windows-era look | DROPPED — compact AppKit status/config panel; Logic's generic AU parameter UI remains fully usable. |
| RtMidi library + port enumeration + ini in `~/Library/Audio/Presets/GSi/...` | Cross-platform MIDI | DROPPED — CoreMIDI directly; NSUserDefaults config. |
| `MACAU` Carbon view (`AudioUnitCarbonView`) | 2012-era AU | DROPPED — Cocoa view factory (`kAudioUnitProperty_CocoaUI`). |
| Windows resource skin PNGs, `.rc`, `.def`, `.vcxproj`, v120 toolset | Windows build | DROPPED — macOS-only project; Makefile + optional Xcode external-build project. |
| 1 program / `CProgram[]` preset array | VST2 boilerplate | DROPPED — AU `ClassInfo` (Logic saves plugin settings natively). |
| `setParameterAutomated` → `AUBase::SetParameter` call-back inside `setParameter` | AU param echo | CHANGED — host-facing parameter changes are applied via the AU's `SetParameter`; no recursive call needed. |
| `alert()` via CFUserNotification, `mkdir` via `system()` | 2012 UI | DROPPED — NSAlert/NSUserDefaults. |
| `getNumMidiInputChannels=1` (RTMIDI) — host MIDI disabled | RtMidi did the MIDI | DROPPED — AU has no host-MIDI stream at all. |
| `rand()`-based mute test animation, `srand(time(0))` | Demo | CHANGED — mutes removed from Test Mode entirely (brief §11); the fader wave is a deterministic pure function of time. |
| `NoteOn→fader` experiment (commented out) | Experiment | DROPPED (already dead code). |
| Pro Tools/VST-RTAS incompatibility note | Host limitation | n/a (documented as historical). |

## 3. Legacy defects — fixed deliberately

| Defect | Evidence | Fix |
| --- | --- | --- |
| `MidiController2[160]` read out of bounds in `ProcessMIDIControlChange` (array sized `NUMBER_OF_FADERS`=72) | Plugin.h:729 + Plugin.cpp | Master fine CC is an explicit table entry; reverse table is bounds-safe. |
| `SetAllChannelsMode` writes CC 64..111 on ch3 **and** ch4 regardless of side strip count (HI side has 24 faders → CC 88..111 overlap joystick SEL 88/90 and unknown addresses) | Plugin.h:302-311 | Mode sweeps are profile-scoped; joystick SEL set explicitly. Safe on legacy console (values matched), safer on other consoles. |
| Mute dedupe stores `value*127` (0/127) while sending 2/3 — works but obscure | Plugin.h:581-650 | Scheduler compares actual outbound byte values. |
| `DeactivateMixer()` leaves `FaderMode*` arrays at 3 | Plugin.h:342-361 | State tracker updated on every mode write. |
| RtMidi sends from the MIDI callback thread (touch replies) | Plugin.h:126-141 | Bridge worker thread; replies on high-priority queue. |
| No handling of running status, sysex, realtime interleave, or malformed packets (RtMidi assumed exact 3-byte messages) | Plugin.h:126-141 | Byte-stream parser: running status, sysex skip, realtime passthrough, malformed → diagnostic. |
| No feedback-loop protection: console echoes of motor moves were written back to the host as automation (`setParameterAutomated` for every inbound CC) | Plugin.h:729-760 | Origin tracking + echo suppression (hysteresis ±1 CC) + touch overrides. (See §4.) |
| Test Mode could emit ~1800 msg/s (25 Hz × 72 faders) — over DIN capacity | Plugin.h:66-77 | Scheduler caps at configurable budget (default 500 msg/s), coalesces. |
| GUI idle loop polls all 72 params every idle tick | Editor.cpp:468-480 | No full-mixer GUI; status UI subscribes to engine events. |
| Windows-only port-name trim hack for RtMidi numbering | Plugin.cpp:74-79 | n/a on CoreMIDI (unique names). |

## 4. Behavior intentionally CHANGED (with reasoning)

1. **Echo/feedback suppression.** Old: every inbound fader CC was forwarded to
   the host as an automation write, including echoes of motor moves caused by
   host automation (potential re-recording/feedback churn; harmless-looking but
   noisy). New: an untouched fader/axis report within N 7-bit steps (default 2)
   of the *last commanded* value is a motor echo — state + UI update only, no
   host write, no re-send (mutes compare exactly). A console report that
   arrives while a commanded target is still queued reflects the *previous*
   state: the pending target survives and nothing is reported. A touched fader
   always wins (touch ⇒ user-originated ⇒ host write + cancel pending
   outbound). This is the brief's §14 requirement; it is regression-tested.
2. **Pacing/coalescing.** Old: immediate send at host event rate, dedupe only.
   New: per-control latest-wins coalescing inside a FIFO main lane that
   preserves legacy byte order, plus a global message budget (default
   500 msg/s; DIN 3-byte capacity is ~1040 msg/s). Management commands
   (modes, touch replies, activate sequences) are strictly ordered and never
   coalesced; touch replies use a high-priority lane. Semantics safe because
   all coalesced values are absolute positions/states, never relative or
   edge-triggered (§13).
3. **Activation timing.** Old: entire activation burst (~200 messages) sent
   instantly. New: same bytes, same order, paced by the budget (≈0.5 s).
   Ordering is preserved; pacing protects the console's MIDI buffer.
4. **Test Mode.** Old: legacy TEST MODE ran a full `ResetAllMixer()`, set all
   strips to READ(1), then animated faders at 25 Hz **and randomly toggled
   mutes** at 10 Hz from the audio loop. New: a non-destructive, deterministic
   phase-offset **traveling wave over faders only** (amplitude limited to
   [0.2, 0.8], period 12 s, 4 waves across the console — mapping errors are
   visually obvious). No reset, no snapshot, no mute traffic; strip modes are
   moved to READ(1) for the duration (documented rationale: console-side write
   activity stays off while the motors are driven) and **restored on exit**;
   console reports during Test Mode are not forwarded to the host; the audio
   loop stays pure and the normal protocol/scheduler path is used throughout.
   Requires an activated console; stops immediately (queued positions are
   canceled on exit).
5. **Mode sweep scope** — see §3.
6. **Configuration.** Old: `~/Library/Audio/Presets/GSi/...ini` port names.
   New: NSUserDefaults (port names by role) + optional profile plist.
7. **Master fine CC.** Old: OOB array read + special case. New: explicit.
8. **Parameter display.** Old: raw `int(value*127)` for everything (mutes show
   0/127). New: faders show 0..127 CC value (honest about resolution), mutes
   show On/Off. Parameter IDs and normalization are unchanged.

## 5. Uncertain behavior — isolated, not guessed

| Item | Isolation mechanism |
| --- | --- |
| Meaning of the odd CC of each fader pair (7-bit vs 14-bit) | `MixerProfile::faderResolution` — `sevenBit` (legacy-proven default) / `fourteenBit` (unverified); decoder honors both, encoder per flag. |
| Whether console echoes positions during motorized playback | Echo suppression + touch override covers both cases; diagnostics log inbound traffic optionally. |
| Exact console-side meaning of SEL modes 0..3 beyond the LED behavior | Modes are passed through verbatim; bridge never invents mode values. |
| Other D&R consoles (Octagon/OrionX/Merlin), other LO/HI splits | `MixerProfile` describes any split; only LO=24/HI=12 is proven. |
| Whether non-CC traffic exists (sysex, notes) | Parser logs unknown/ignored messages in verbose diagnostics (§22 of brief). |
