#!/bin/bash
# Build a UNIVERSAL (arm64 + x86_64) drop-in replacement for the Intel-only
# Zijiang / 4BARCODE 4B-2054A "LABEL" CUPS filter. On Apple Silicon the arm64
# slice runs natively, so Rosetta is not needed.
#
# NOTE: build against a STABLE SDK. The beta MacOSX27.sdk .tbd stubs list an
# "arm64e.x1" target that ld cannot parse ("unknown architecture"); SDK 26.5
# links cleanly.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$HERE/.build-objects"
SDK="$(xcode-select -p 2>/dev/null)/SDKs/MacOSX26.5.sdk"
[ -d "$SDK" ] || SDK="$(xcrun --show-sdk-path --sdk macosx)"
EXTRA=""
case "$SDK" in *MacOSX27*) EXTRA="-target arm64-apple-macos13";; esac
CFLAGS="$(cups-config --cflags)"
LIBS="$(cups-config --libs) -lcupsimage -lm"
cc -arch arm64  -mmacosx-version-min=13.0 -isysroot "$SDK" -O2 $EXTRA -w -o "$HERE/.build-objects/arm64"  "$HERE/rastertolabel_arm.c" $CFLAGS $LIBS
cc -arch x86_64 -mmacosx-version-min=13.0 -isysroot "$SDK" -O2 $EXTRA -w -o "$HERE/.build-objects/x86_64" "$HERE/rastertolabel_arm.c" $CFLAGS $LIBS
lipo -create -output "$HERE/rastertolabel" "$HERE/.build-objects/arm64" "$HERE/.build-objects/x86_64"
echo "built: $(lipo -archs "$HERE/rastertolabel")"
