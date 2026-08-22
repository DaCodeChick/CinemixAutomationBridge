#!/bin/bash
# Builds the AUv2 component on the target Mac (macOS 10.13 High Sierra).
# See docs/BUILDING.md for the full recipe. Run from mac/.
set -euo pipefail
cd "$(dirname "$0")"

echo "== Toolchain check =="
xcrun --sdk macosx --show-sdk-path
xcrun -f clang++

echo "== Building portable core tests first (sanity on this machine) =="
if command -v cmake >/dev/null 2>&1; then
  (cd .. && cmake -S . -B build && cmake --build build -j2 && ctest --test-dir build --output-on-failure) \
    || echo "NOTE: core tests failed — do not install the AU until they pass."
else
  echo "NOTE: cmake not installed; skipping core tests."
fi

echo "== Building the Audio Unit =="
make clean >/dev/null 2>&1 || true
make

echo
echo "== Result =="
ls -la build/CinemixAutomationBridge.component/Contents/MacOS/
file build/CinemixAutomationBridge.component/Contents/MacOS/CinemixAutomationBridge

echo
echo "== Validation (auval) =="
make validate

echo
echo "== Next steps =="
echo "  make install                       # install into ~/Library/Audio/Plug-Ins/Components"
echo "  restart Logic Pro and load 'Cinemix Automation Bridge' as an instrument"
echo "  see docs/USER_GUIDE.md for the console hookup and automation workflow"
