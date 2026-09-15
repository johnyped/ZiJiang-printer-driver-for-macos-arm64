#!/bin/bash
# Install the native/universal rastertolabel over the Intel-only one.
# Original is preserved at /Library/Printers/LABEL/Filter/rastertolabel.intel.orig
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
DEST="/Library/Printers/LABEL/Filter/rastertolabel"
[ -x "$HERE/rastertolabel" ] || { echo "run ./build.sh first"; exit 1; }
if [ ! -f "$DEST.intel.orig" ]; then
  sudo cp -p "$DEST" "$DEST.intel.orig"
  echo "backed up original -> $DEST.intel.orig"
fi
sudo cp "$HERE/rastertolabel" "$DEST"
sudo chown root:wheel "$DEST"; sudo chmod 755 "$DEST"
echo "installed native filter:"; lipo -archs "$DEST"
PRINTER="${PRINTER:-_4BARCODE_4B_2054A}"
cupsenable "$PRINTER" 2>/dev/null || true
cupsaccept "$PRINTER" 2>/dev/null || true
echo "test print with:"
echo "  printf 'NATIVE ARM TEST\\n' | lp -d $PRINTER -o page-width=283 -o page-length=283"
