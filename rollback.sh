#!/bin/bash
# Restore the original Intel filter.
set -euo pipefail
DEST="/Library/Printers/LABEL/Filter/rastertolabel"
[ -f "$DEST.intel.orig" ] || { echo "no backup at $DEST.intel.orig"; exit 1; }
sudo cp -p "$DEST.intel.orig" "$DEST"
echo "rolled back to original Intel filter."
