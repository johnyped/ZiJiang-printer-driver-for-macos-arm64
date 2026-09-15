# LABEL — native Apple‑Silicon CUPS driver
### ZiJiang / 4BARCODE 4B‑2054A thermal label printer (queue `LABEL`)

A **native arm64** drop‑in replacement for the printer's Intel‑only CUPS filter.
No Rosetta required on Apple Silicon. Output is **byte‑for‑byte identical** to
the original filter on the production raster format.

---

## The problem this solves

The stock driver installs an **x86_64‑only** binary:

```
/Library/Printers/LABEL/Filter/rastertolabel     (built 2018, Intel only)
```

On Apple Silicon this runs only through Rosetta 2. If Rosetta is absent (fresh
macOS, a clean install, or an OS migration), CUPS cannot launch the filter and
the printer jams:

```
execv .../rastertolabel failed. err:86, Bad CPU type in executable
... Sent 0 bytes ... printer-state-reasons=com.apple.badarch-error
```

The queue sticks at *"Sending data to printer."* and **nothing prints**.
This project replaces that filter with a **universal (arm64 + x86_64)** build.

## What the filter does (reverse‑engineered)

`rastertolabel` reads a **CUPS raster** (from `cgpdftoraster`) and emits the
printer's native language, **TSPL / TSPL2**:

```
SIZE <w> mm ,<h> mm
REFERENCE 0,0
GAP 3 mm,0 mm
OFFSET 0 mm
DENSITY 6
SPEED 4
SETC AUTODOTTED OFF
SETC PAUSEKEY ON
SETC WATERMARK OFF
CLS
BITMAP 0,0,<bytesPerRow>,<rows>,1,<packed bitmap>
PRINT <copies>,1
```

Key mapping rules recovered from the original binary:

* Printhead resolution = **203 dpi**.
* `mm = round(bytesPerRow*8 * 25.4 / 203)` — width uses the **byte‑padded** pixel width.
* **BITMAP polarity is inverted:** `1 = white` (no burn), `0 = black` (burn);
  padding bits beyond the true width stay `1`. Each row therefore starts all
  `0xFF…` and bits are **cleared** for black pixels.
* The real pipeline input is **8‑bit grayscale** (`cupsBitsPerPixel=8`,
  confirmed from a live job log). 1/24/32‑bit paths are handled for robustness.

## Verification

The native filter was diffed against the working Intel filter on identical
raster input (production 8 bpp path):

```
rastertolabel (arm64)  ==  /Library/Printers/LABEL/Filter/rastertolabel (x86_64)
cmp: IDENTICAL  (79970 bytes, byte‑for‑byte)
```

Both slices of the built binary reproduce the Intel output exactly.

---

## Files

| File | Purpose |
|------|---------|
| `rastertolabel_arm.c` | Native filter source (uses `libcupsimage`) |
| `rastertolabel`       | Built universal binary (arm64 + x86_64) |
| `build.sh`            | Rebuild (works around the SDK‑27 linker bug) |
| `install.sh`          | Back up + install over the Intel filter (needs sudo) |
| `rollback.sh`         | Restore the original Intel filter |
| `AGENTS.md`           | Guidance for AI coding agents working in this repo |
| `README.md`           | This file |

## Install (one command, needs your admin password once)

```bash
cd /Users/introdexminis/Johnyped/LABEL-printer
./install.sh
```

This keeps the original at
`/Library/Printers/LABEL/Filter/rastertolabel.intel.orig`. Undo anytime:

```bash
./rollback.sh
```

### Test

```bash
# from Terminal (or just print from your label app):
printf 'NATIVE ARM TEST\n' | lp -d _4BARCODE_4B_2054A \
    -o page-width=283 -o page-length=283
lpstat -o _4BARCODE_4B_2054A          # job should clear in a few seconds
```

### Confirm the native filter ran (no badarch)

```bash
sudo cupsctl --debug-logging
printf 'VERIFY\n' | lp -d _4BARCODE_4B_2054A -o page-width=283 -o page-length=283
sleep 5
sudo grep -a "Started filter.*rastertolabel\|Sent [0-9]* bytes\|badarch\|Bad CPU" \
    /var/log/cups/error_log | tail
sudo cupsctl --no-debug-logging
```
Good = `Sent <nonzero> bytes` with **no** `Bad CPU type` / `badarch` lines.

## Build environment gotcha

`ld` (PROJECT ld‑1267) rejects the beta `MacOSX27.sdk` stubs because they list
an `arm64e.x1` target it doesn't understand (`unknown architecture`). Build
against the stable **SDK 26.5**; `build.sh` already prefers it.

## Troubleshooting

* **Bytes are sent but no physical label** → the driver is fine; check the
  label media/roll, the print head, and the **page size** your app selects
  (must match loaded label, e.g. 100×100 mm ≈ `page-width=283 page-length=283`).
* **`badarch-error` reappears** → the Intel binary was restored; re‑run
  `./install.sh`.
* **Stuck "Sending data to printer"** → `cancel -a _4BARCODE_4B_2054A &&
  cupsenable _4BARCODE_4B_2054A && cupsaccept _4BARCODE_4B_2054A`.
