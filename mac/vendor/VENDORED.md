# Vendored: Apple AudioUnitSDK (classic release)

Source: https://github.com/apple/AudioUnitSDK
Commit: 53ea94e ("Initial commit", 2020-08-24) — the first release of the
official SDK, which states "macOS (OS X) 10.9 / iOS 9.0 or later" as its
deployment target and is therefore usable for our macOS 10.13 High Sierra
target. Later SDK releases raised the floor to macOS 11 / C++23 and are
deliberately NOT used.

License: Apache License 2.0 (see LICENSE.txt). Used unmodified; the build
(Makefile) compiles only the sources the AU needs:

    AUBase, ComponentBase, AUScopeElement, AUOutputElement,
    AUPlugInDispatch, AUBuffer, AUBufferAllocator

The SDK requires C++17 (its own choice of std::make_unique/[[nodiscard]]);
the portable Cinemix core remains C++11 (docs/BUILDING.md).
