# Cinemix Automation Bridge — native AUv2 bridge for the D&R Cinemix

A **Logic Pro (AUv2 / CoreMIDI)** automation bridge for the 1995-era
**D&R Cinemix** analog console, rebuilt from the proven
[CinemixAutomationBridge](https://github.com/ZioGuido/CinemixAutomationBridge)
by GSi (Guido Scognamiglio, 2012–2017).

The goal: recover the legacy bridge's reverse-engineered hardware knowledge,
preserve its proven console behavior byte-for-byte where it matters, and
modernize the early-2000s VST2/Windows/RtMidi architecture into a native,
maintainable **C++14 core + Objective-C++ AUv2/CoreMIDI/AppKit** plugin for a
**2014 Intel Mac mini on macOS 10.13 High Sierra**.

```
 Logic Pro (automation lanes)
      │  AUv2 parameters / parameter listeners
      ▼
 Cinemix AU (augn) ── Objective-C++/AppKit panel
      │
      ▼
 Cinemix Core ── C++14 (MixerProfile · ParameterMap · AutomationEngine
      │              CinemixProtocol · TransmissionScheduler · diagnostics)
      ▼
 CoreMIDI ── 2× MIDI I/O ── DB25 ASYNC INTERFACE ── D&R Cinemix (1995)
```

## Status (honest)

* **Portable core + tests: built and passing on Linux** (93 test cases:
  protocol gold data, parser robustness, legacy parameter layout, scheduler,
  engine with byte-exact activation/deactivation sequences, echo suppression,
  feedback-loop prevention, capture/replay).
* **Hardware-free harness: passing** (scripted console loopback selftest,
  .cmi session capture/replay).
* **AUv2/CoreMIDI/AppKit sources: complete, not yet compiled** — they require
  the target Mac (Xcode 9.4/10.1 on High Sierra). No AU validation has been
  performed and none is claimed; see docs/BUILDING.md for the exact steps.

## Repository layout

| Path | Contents |
| --- | --- |
| `docs/PROTOCOL.md` | the recovered Cinemix MIDI protocol, fact-labeled (code/notes/readme/inferred) |
| `docs/COMPATIBILITY.md` | legacy → new behavior map: preserved / changed / dropped / fixed |
| `docs/ARCHITECTURE.md` | layering, threading, feedback-loop rules, budget |
| `docs/BUILDING.md` | toolchain, build steps, auval/Logic rescan, honest status matrix |
| `docs/USER_GUIDE.md` | cable wiring, activation, Logic workflow, mixer profile plist |
| `docs/TESTING.md` | test layers + on-console checklist |
| `core/` | portable C++14 core + zero-dependency test suite (CMake) |
| `tools/cinemix_harness/` | simulated console selftest, capture/replay, demo |
| `mac/` | AUv2 component, CoreMIDI transport, AppKit panel, Makefile, Info.plist, vendored Apple AudioUnitSDK (Apache-2.0) |
| `_legacy_analysis/` | the untouched legacy repository, kept for reference |
| `NOTICE` | attribution and licenses for the legacy bridge and the Apple SDK |

## Quick start (target Mac)

```sh
cd mac && make && make validate && make install
# restart Logic, load "Cinemix Automation Bridge" as an instrument,
# select the two console MIDI pairs in the panel, click Activate.
```

## Quick start (any POSIX host — core + harness)

```sh
cmake -S . -B build && cmake --build build -j4 && ctest --test-dir build
./build/tools/cinemix_harness/cinemix_harness selftest
```

## License

First-party code: **GNU GPL version 3** (see LICENSE and NOTICE). The vendored
Apple AudioUnitSDK remains Apache-2.0, and the legacy GSi material carries its
own notice. Whether GPLv3 means GPL-3.0-only or GPL-3.0-or-later is left to
the project maintainer (NOTICE).

## Design principles

1. **Do not damage or dangerously command the physical console** — release on
   plugin unload (`0xFF` last), manual activation only, paced MIDI.
2. **Preserve known-correct legacy behavior** — byte-exact sequences and value
   semantics are regression-tested against gold data.
3. **Reliable Logic automation** — console moves reach Logic as gesture +
   value-change events; host automation drives motors; feedback loops are
   prevented by design.
4. **macOS 10.13 compatibility** — deployment target 10.13, x86-64, AppKit,
   no modern-OS-only APIs.
5. **Real-time safety** — the render loop only writes silence; all MIDI work
   happens on a dedicated worker; host/audio threads only do wait-free
   atomic stores (value + dirty flag), scanned by the worker every tick.
6. **Maintainability** — small focused classes, table-driven protocol,
   explicit mixer profile, no dumping grounds.
