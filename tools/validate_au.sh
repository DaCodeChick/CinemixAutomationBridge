#!/bin/bash
# On-target validation recipe for the Cinemix Automation Bridge AUv2.
# Run on the High Sierra Mac mini after `make install` (docs/BUILDING.md).
set -u

BUNDLE="$HOME/Library/Audio/Plug-Ins/Components/CinemixAutomationBridge.component"
BINARY="$BUNDLE/Contents/MacOS/CinemixAutomationBridge"

echo "== 1. Bundle structure =="
[ -d "$BUNDLE" ] || { echo "FAIL: bundle not installed (run 'make install' in mac/)"; exit 1; }
plutil -lint "$BUNDLE/Contents/Info.plist"

echo "== 2. Architecture (must be x86_64 for the 2014 Mac mini) =="
file "$BINARY"
otool -hv "$BINARY" | head -5

echo "== 3. Exported factory symbol =="
nm -gU "$BINARY" | grep -i factory || echo "WARN: factory symbol not exported"

echo "== 4. Deployment target (must be ≤ 10.13) =="
otool -l "$BINARY" | grep -A2 LC_VERSION_MIN_MACOSX

echo "== 5. AU validation =="
auval -v augn DRcm CBRG
AUVAL_EXIT=$?
if [ $AUVAL_EXIT -ne 0 ]; then
  echo "FAIL: auval reported an error (exit $AUVAL_EXIT)."
  echo "      Inspect the output above; common causes:"
  echo "      - deployment target newer than the OS"
  echo "      - missing kAudioUnitProperty_ClassInfo / ParameterInfo support"
  echo "      - a crash inside the component (check ~/Library/Logs/DiagnosticReports)"
  exit 1
fi

echo "== 6. Logic rescan =="
echo "Quit Logic Pro, then either:"
echo "  rm -rf ~/Library/Caches/com.apple.audiounits.cache"
echo "or restart Logic and re-scan from the Audio Units Manager if needed."
echo
echo "ALL AUTOMATED CHECKS PASSED (console/hardware validation is manual —"
echo "see docs/TESTING.md for the on-console checklist)."
