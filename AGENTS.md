# AGENTS.md

# Cinemix Automation Bridge — Agent Instructions

## Project

This project is an automation bridge for a physical 1995 D&R Cinemix
motorized mixing console.

Primary deployment target:

- 2014 Intel Mac mini
- macOS 10.13 High Sierra
- Intel x86-64
- Logic Pro / Logic Pro X
- Audio Unit v2
- CoreMIDI

The software ultimately controls real vintage hardware. Correctness,
predictability, and preservation of known console behavior take priority
over stylistic modernization.

---

## Language Baseline

First-party portable code targets **C++14**.

Do not introduce C++17 or later.

Prefer deliberate C++14 where appropriate:

- RAII
- Rule of Zero
- `std::array` for fixed-size first-party collections
- `using` over `typedef`
- `enum class`
- `constexpr` for genuinely compile-time-capable operations
- explicit ownership and lifetime contracts
- standard containers and algorithms
- scoped resource ownership
- `static_cast` rather than C-style casts in C++ code

Do not modernize mechanically.

Low-level operations remain appropriate when the problem is genuinely
low-level.

Pointers, byte operations, `memcpy`, casts required for binary I/O, and
C-compatible interfaces are not forbidden. Their use should simply be
intentional.

Avoid abstraction for abstraction's sake.

---

## Platform Compatibility

macOS 10.13 High Sierra compatibility is non-negotiable.

Do not:

- raise the deployment target
- introduce C++17+
- introduce SwiftUI
- require AUv3
- depend on modern-only CoreMIDI APIs
- depend on modern-only AppKit APIs
- replace the vendored AudioUnitSDK without explicit instruction

Apple-facing implementation may use Objective-C++ where appropriate.

Programmatic AppKit is intentional.

Interface Builder `.xib` / `.nib` files are not required.

Do not convert the UI to Swift or SwiftUI without explicit instruction.

---

## Architecture

Preserve the established architecture unless a concrete correctness defect
requires changing it.

Do not perform unsolicited repository-wide refactors.

Prefer:

1. identify the actual defect
2. trace the relevant contract
3. make the smallest coherent correction
4. add regression coverage
5. verify the result
6. stop

Do not redesign a subsystem merely because another design is possible.

In particular, do not casually redesign:

- AutomationEngine
- TransmissionScheduler
- CinemixProtocol
- ParameterMap
- MixerProfile
- FaderOscillator
- CoreMIDI integration
- AUv2 integration
- programmatic AppKit UI
- `.cmi` capture format

---

## Real-Time and Concurrency Requirements

Audio-sensitive and real-time paths must remain bounded and predictable.

Avoid on real-time paths:

- blocking filesystem I/O
- unnecessary heap allocation
- unbounded work
- inappropriate mutex contention
- expensive formatting/logging

Concurrency contracts must be explicit.

Do not use a single-producer/single-consumer structure unless the code
actually guarantees one producer and one consumer.

Do not invent complicated lock-free algorithms when a simpler ownership or
serialization model can solve the problem safely.

Correctness is more important than cleverness.

All callback lifetime relationships must be defensible during normal
operation and teardown.

---

## Hardware Behavior

The physical D&R Cinemix and verified legacy behavior are authoritative.

Preserve established behavior including:

- MIDI byte encoding
- controller mappings
- parameter mappings
- port mappings
- quantization behavior
- truncation behavior
- activation sequences
- deactivation sequences
- transmission ordering
- hardware timing requirements
- touch/fader semantics
- mixer mode behavior

Do not "fix" strange-looking protocol behavior merely because a cleaner
implementation is possible.

When behavior is uncertain, consult `_legacy_analysis/` before changing it.

Preserve hardware behavior, not legacy implementation style.

---

## Test Faders

The Test Faders feature is a deliberate live hardware test.

It must:

- move motorized faders deterministically
- affect fader position only
- remain within its configured safe range
- use the normal protocol path
- use the normal scheduler path
- stop promptly
- behave safely during teardown/disconnection

It must never randomly manipulate:

- mutes
- unrelated mixer controls
- unrelated automation parameters

Do not restore obsolete random Test Mode behavior.

---

## Transmission Scheduler

Preserve the established scheduler architecture unless a concrete defect is
demonstrated.

Position traffic must retain its logical parameter identity.

Components of a logical position operation must remain cancellable with that
position.

Do not represent position traffic as unrelated command types merely to bypass
coalescing.

Queue-full behavior must be explicit for each important traffic class.

Critical shutdown/release behavior must not disappear accidentally because a
queue is saturated.

---

## `.cmi` Capture Format

Preserve existing `.cmi` on-disk compatibility unless an actual format defect
requires a deliberate version change.

Capture/replay is not a real-time path.

Favor:

- RAII
- clear binary encoding
- reliable stream/file state
- structural validation
- small pure endian helpers
- straightforward tests

A successful `CaptureWriter` operation must not produce a structurally invalid
record that `CaptureReader` rejects.

Writer and reader must agree on:

- magic
- version
- direction
- port
- message length
- record structure

Do not build a generic serialization framework.

---

## CoreMIDI

Check meaningful `OSStatus` results.

Do not report successful initialization or connection when a required
CoreMIDI operation failed.

Partial initialization must leave objects in coherent, safely destructible
states.

Internal endpoint state must reflect actual connection state.

CoreMIDI callbacks must not outlive their callback targets.

Do not call AppKit UI code directly from arbitrary CoreMIDI callback threads.

Distinguish endpoint availability from verified physical-console
communication.

---

## Objective-C++

Keep Apple-facing code idiomatic for Objective-C++.

Do not mechanically apply portable-C++ stylistic rules where Objective-C APIs
require different conventions.

Use C++ RAII naturally where it improves ownership.

Non-owning pointers are acceptable when lifetime contracts justify them.

Prefer references where null is semantically impossible and doing so genuinely
clarifies the contract.

Programmatic AppKit is accepted and intentional.

---

## Third-Party Code

`vendor/AudioUnitSDK/` is third-party code under its existing license.

Do not stylistically modernize vendor code.

Do not apply project-wide warning cleanup or formatting to it.

Only patch vendor code when required for actual compatibility or correctness,
and document such patches explicitly.

---

## Historical Code

`_legacy_analysis/` contains historical/reference material.

It is not active implementation.

Use it to verify original Cinemix behavior when necessary.

Do not compile historical implementations into the current product unless
explicitly instructed.

Obsolete implementations should not remain beside active first-party source
in a way that makes them appear current.

---

## Platform Builds

Do not attempt to build the macOS AU target on Windows or Linux.

On non-macOS development systems:

- build portable components
- run portable tests
- run applicable sanitizers

Do not:

- create fake Apple compatibility layers
- download substitute Apple SDKs
- remove Apple functionality to make another OS compile it
- add Windows-specific workarounds to the AU target

Apple-specific validation belongs on the target Mac.

---

## Testing

Changes that correct a reproducible defect should receive focused regression
coverage where practical.

Prefer tests of contracts and observable behavior over tests of implementation
details.

Useful portable verification may include:

- unit tests
- integration tests
- queue stress tests
- malformed capture tests
- lifecycle stress tests
- AddressSanitizer
- UndefinedBehaviorSanitizer
- ThreadSanitizer

Do not claim portable tests prove behavior that depends on CoreMIDI, Logic, or
physical hardware.

---

## Verification Vocabulary

Use these terms precisely:

### IMPLEMENTED

The source implementation exists.

### COMPILED

It compiled using the applicable toolchain.

### SOFTWARE TESTED

Relevant automated/software tests passed.

### TARGET-MAC TESTED

It was actually tested on the target Intel Mac running High Sierra.

### LOGIC TESTED

The AU was actually loaded and exercised in the target Logic installation.

### HARDWARE TESTED

The implementation actually operated the physical D&R Cinemix.

Never imply a stronger verification level than actually occurred.

---

## Documentation

Documentation and comments must describe the current implementation.

The first-party portable core is C++14.

Do not leave documentation describing obsolete architecture after that
architecture has changed.

Audit changelogs are historical claims, not proof that the source currently
satisfies them.

When source and documentation disagree, investigate and make them consistent.

---

## Licensing

Preserve the project's established first-party GPLv3 licensing.

Preserve third-party licenses, including the vendored AudioUnitSDK license.

Do not replace project licensing with MIT or another license.

---

## Scope Discipline

Follow the user's requested scope.

For a surgical bug fix:

- investigate the relevant code
- modify only what is necessary
- update directly affected documentation/tests
- run applicable verification
- stop

Do not use a small task as permission for broad cleanup.

Do not perform speculative modernization after the acceptance criteria are
satisfied.

If nearby code is ugly but correct and unrelated to the requested task, leave
it alone.

The objective is a reliable vintage-hardware bridge, not a demonstration of
every available C++ technique.
