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
| `docs/DRIVER-JOURNAL.md`| Full engineering journal: diagnosis, reverse-engineering, testing (with diagrams) |
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

## Printer hardware & connectivity

Model: **4BARCODE / ZiJiang 4B‑2054A** (OEM: ZiJiang; Seagull/BarTender driver line).
This unit is a **4B‑2054A** (USB serial `254AWE…`, model token `254A`).

### Interfaces (4B‑2054 family product matrix)
| Variant SKU | USB | Ethernet (LAN) | Wi‑Fi | Bluetooth |
|---|---|---|---|---|
| **2054A‑USB** | ✓ | — | — | — |
| **2054A‑LAN** | ✓ | ✓ | — | — |
| 2054K‑USB | ✓ | — | — | — |
| 2054K‑LAN | ✓ | ✓ | — | — |
| 2054K‑WF | ✓ | ✓ | ✓ | — |
| 2054K‑AP | ✓ | ✓ | ✓ (AP mode) | — |
| 2054K‑BT | ✓ | — | — | ✓ |

* Base spec: `Interface: USB 2.0`. The physical **platform** (per the 4B‑2054A
  series manual, Rear View) exposes **USB + RS‑232C (DB‑9) + Ethernet + SD card**;
  **Wi‑Fi/Bluetooth are SKU options**, seen only on the **‑K** line (`‑WF/‑AP/‑BT`).
* Bluetooth is rare on these desktop label printers; **Ethernet** is the common
  non‑USB option. The **"A"** models are USB (optionally USB+LAN).

### What this Mac sees (USB)
* USB vendor `0x2D84` (11652), product `0xB488` (46184), strings "4BARCODE / 4B‑2054A".
* `bNumConfigurations=1`, full‑speed (12 Mbps), single printer interface → the
  USB connection exposes **no** network/BT function.
* macOS CUPS connection = `direct` (USB).

### Investigation cues for later
* **To know if THIS unit is USB‑only vs USB+LAN:** inspect the rear panel.
  RJ‑45 jack (+/‑ LEDs) → `2054A‑LAN`. Antenna/`WF`/`AP`/`BT` label → wireless.
  Only USB‑B + power barrel → `2054A‑USB`.
* **Is it already on the LAN?** `arp -a | grep -iE 'zijiang|barcode|printer|00:1d|0c:08'`
* **Read the 1284 DeviceID** (may reveal interfaces/protocol):
  `system_profiler SPUSBDataType | grep -A12 2054`  ·  or a USB control string read.
* **Enable network printing** if it has Ethernet (no new driver needed — same PPD
  + this filter works over socket):
  `sudo lpadmin -p LABEL_NET -E -v socket://<printer-ip>:9100 -P /private/etc/cups/ppd/_4BARCODE_4B_2054A.ppd`
* Sources: ARKSCAN/4BARCODE "2054" product spec (variant matrix), and the
  4B‑2054A series User Manual (Rear View / interface setup chapters).

## Colour documents → grayscale (dithering)

The 4B‑2054A is **physically monochrome** (direct thermal: burn or no burn, black
on white). It can never print colour. macOS already converts colour documents to
an **8‑bit grayscale** raster (via `cgpdftoraster`) before this filter runs.

By default the filter **hard‑thresholds** that grayscale (same as the original
Intel filter, `black = gray < 128`) — best for text and 1‑bit logos, but it makes
photos/gradients come out as harsh black/white blocks, and very light colours
(yellow/cyan) can disappear.

Two **opt-in** rendering modes improve photographic/gradient content:

| Option | Effect |
|---|---|
| `dither=floyd` | Floyd–Steinberg error diffusion — smooth photographic tones |
| `dither=ordered` | Bayer 8×8 ordered halftone — regular rosette, faster |
| `gamma=<1.0–5.0>` | Tone lift; `>1.0` darkens so light colours survive as dots |

**Default (no options) reproduces the original bytes exactly** — see `make test`.

### Use it
```bash
# one job:
some-color-file | lp -d _4BARCODE_4B_2054A -o dither=floyd
lp -d _4BARCODE_4B_2054A -o dither=floyd -o gamma=1.4 photo.pdf
# make it the queue default (every print gets dithered grayscale):
sudo lpadmin -p _4BARCODE_4B_2054A -o dither=floyd
# revert to crisp threshold:
sudo lpadmin -p _4BARCODE_4B_2054A -o dither=off
```
Verified on a colour gradient raster (798×798): hard-threshold shows ~5
black/white transitions per row (blocky); `dither=floyd` ~371 (smooth), `dither=ordered`
~139 (rosette) — all at the correct mid-tone ink density (~0.24).

## Troubleshooting

* **Bytes are sent but no physical label** → the driver is fine; check the
  label media/roll, the print head, and the **page size** your app selects
  (must match loaded label, e.g. 100×100 mm ≈ `page-width=283 page-length=283`).
* **`badarch-error` reappears** → the Intel binary was restored; re‑run
  `./install.sh`.
* **Stuck "Sending data to printer"** → `cancel -a _4BARCODE_4B_2054A &&
  cupsenable _4BARCODE_4B_2054A && cupsaccept _4BARCODE_4B_2054A`.
