# Building the Cinemix Automation Bridge

## What builds where

| Component | Language | Standard | Builds on | Status |
| --- | --- | --- | --- | --- |
| `core/` (portable Cinemix core) | C++14 | C++14 | Linux/macOS | **built and tested on Linux (93 test cases)** |
| `tools/cinemix_harness` | C++14 | C++14 | Linux/macOS | **built and tested on Linux** |
| `mac/` AUv2 component (includes SDK headers) | ObjC++ | gnu++17 | macOS only | **implemented; compile/validate on the target Mac** |
| `mac/` CoreMIDI/Config/AppKit glue | ObjC++ | C++14 | macOS only | **implemented; compile/validate on the target Mac** |
| `mac/vendor/AudioUnitSDK` (Apple, Apache-2.0) | C++17 | gnu++17 | macOS only | vendored from Apple's initial 2020 release (targets macOS 10.9+) |

The language split is intentional (brief §22): first-party code — the
portable core, the harness and the CoreMIDI/AppKit/Config glue — is
conservative **C++14**. Only the vendored Apple SDK and the AU component that
includes its headers compile as gnu++17 (the SDK requires C++17 *as published
by Apple*, plus a C99 designated-initializer extension Apple clang accepts in
GNU mode). The core is never raised to C++17 because of the vendor; nothing
in it depends on anything Apple-specific.

## Building and testing the core + harness (any POSIX host)

```sh
cmake -S . -B build
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/tools/cinemix_harness/cinemix_harness selftest
./build/tools/cinemix_harness/cinemix_harness replay build/selftest.cmi
./build/tools/cinemix_harness/cinemix_harness demo
```

## Building the Audio Unit (target Mac: macOS 10.13 High Sierra)

### Toolchain

* Xcode **9.4.1, 10.0 or 10.1** (the last Xcode versions that run on 10.13.6)
  or, at minimum, the matching **Command Line Tools** (`xcode-select --install`).
* `xcrun` must resolve: `xcrun --sdk macosx --show-sdk-path` must print a path.
* The build is **x86-64 only and intentionally so** (2014 Mac mini, brief §24).

```sh
cd mac
./build.sh            # core tests (if cmake is present) + component build + auval
# or step by step:
make                  # builds build/CinemixAutomationBridge.component
make validate         # auval -v augn DRcm CBRG
make install          # copies to ~/Library/Audio/Plug-Ins/Components
```

The Makefile is the canonical build path. It:
* compiles the portable core and the non-AU glue as C++14, the vendored SDK
  and the AU component as gnu++17;
* links a Mach-O bundle with `-mmacosx-version-min=10.13 -arch x86_64`;
* assembles `CinemixAutomationBridge.component` from `Resources/Info.plist`;
* lints the plist (`plutil`) and ad-hoc signs the bundle.

### Loading into Logic Pro

1. Quit Logic.
2. `rm -rf ~/Library/Caches/com.apple.audiounits.cache` (the 10.13-era AU cache).
3. Relaunch Logic. If the plugin does not appear, use Logic's **Audio Units
   Manager** to rescan ("Logic Pro → Preferences → Audio Units Manager").
4. The plugin loads as an **instrument** ("Cinemix Automation Bridge",
   manufacturer CBRG) — do not create a MIDI track for it (legacy behavior).

### Component identity

`augn / DRcm / CBRG` — the subtype `DRcm` is kept from the legacy VST
(`_Plugin_Unique_ID_`) for continuity; the manufacturer is new, so there is no
conflict with the old GSi AU.

### Debugging

* Diagnostics go to `Console.app` (subsystem
  `org.cinemixbridge.CinemixAutomationBridge`) and to the plugin's Log pane.
* Crashes appear under `~/Library/Logs/DiagnosticReports/`.
* `auval -v augn DRcm CBRG` exercises parameter discovery, ClassInfo
  round-trip, render, and the CocoaUI handoff outside Logic.
* Set the AU's log level to "MIDI in/out" to watch raw console traffic.

## Honest status matrix (brief §31)

| Claim | Status |
| --- | --- |
| Portable core compiles (C++14, `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wold-style-cast -Wshadow`) | **verified here (Linux, gcc 16, ASan/UBSan; engine suite also ThreadSanitizer-clean)** |
| Core test suite (56 cases: protocol, parser, map, scheduler, engine, capture, ring) | **verified here** |
| Harness selftest/capture/replay | **verified here** |
| AUv2 component builds | **not verified — requires the target Mac** |
| `auval` passes | **not verified** |
| Logic loads the plugin / automation lanes work | **not verified** |
| MIDI reaches the console / faders move | **not verified — needs the real console** |
| Bidirectional automation on hardware | **not verified** |

Do not claim any of the unverified rows without running them on the machine
(brief §31, §33).
