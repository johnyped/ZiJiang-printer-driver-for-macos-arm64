# AGENTS.md — LABEL printer native driver

Guidance for AI coding agents working in this repository.

## What this repo is
A native **arm64** rewrite of the Intel-only CUPS filter for the ZiJiang /
4BARCODE 4B-2054A "LABEL" thermal printer. The filter converts a CUPS raster
stream into the printer's **TSPL2** command language.

## Hard invariants (do not break)
1. **Output must stay byte-for-byte identical** to the original Intel filter
   on the production 8-bit-grayscale raster. That equivalence is the entire
   point of this project — it was proven via differential diffing. Do not
   "improve" the byte stream without re-verifying against the reference.
2. The printer's optical resolution is **203 dpi**. `mm = round(bytesPerRow*8
   * 25.4 / 203)`, using the **byte-padded** pixel width for the width value.
3. **TSPL BITMAP polarity is inverted**: `1 = white (no burn)`, `0 = black
   (burn)`; bits beyond the true pixel width stay `1`. Each bitmap row starts
   all-`0xFF` and bits are **cleared** for black pixels. Getting this backwards
   produces a blank/white label — a bug we already hit and fixed once.
4. The real pipeline input is **8-bit gray** (`cupsBitsPerPixel=8`). The
   1/24/32-bit branches in `rastertolabel_arm.c` are defensive only; they are
   NOT verified identical to the Intel filter, so do not treat them as the
   reference path.
5. **Colour/grayscale dithering is OPT-IN** (`-o dither=floyd|ordered`, `-o gamma=`)
   parsed from `argv[5]`. With **no options** the filter MUST take the exact
   `gray<128` threshold branch and stay byte-identical to the Intel reference.
   Do not make dithering the default; do not route the default path through the
   error-diffusion code (its integer rounding changes bytes).


## Layout
- `rastertolabel_arm.c` — the filter source (single file, links `libcups`+`libcupsimage`).
- `rastertolabel`        — built universal binary (arm64 + x86_64). Regenerate, don't hand-edit.
- `build.sh` / `Makefile`— build the universal binary.
- `install.sh`           — back up + replace the system filter (needs sudo).
- `rollback.sh`          — restore the original Intel filter.
- `README.md`            — human-facing docs and the reverse-engineering findings.

## Build notes
- Build against the **stable SDK (MacOSX26.5.sdk)**. The beta `MacOSX27.sdk`
  `.tbd` stubs list an `arm64e.x1` target that `ld` cannot parse
  ("unknown architecture"). `make`/`build.sh` already prefer 26.5.
- Link flags come from `cups-config`; also add `-lcupsimage`.

## Testing before claiming any change works
Rebuild and diff against the reference on a real raster. The reference Intel
filter is at `/Library/Printers/LABEL/Filter/rastertolabel`; the original
(x86) copy is preserved as `...rastertolabel.intel.orig` after install.

```bash
make
# generate a production-shaped raster (8bpp gray):
printf 'GROUND TRUTH LABEL 123\n' | cupsfilter - > /tmp/in.pdf 2>/dev/null  # or use cgtexttopdf+cgpdftoraster
# then run BOTH filters on the same raster and cmp the output:
export PPD=/private/etc/cups/ppd/_4BARCODE_4B_2054A.ppd
/Library/Printers/LABEL/Filter/rastertolabel       538 "$USER" gt 1 "x" raster > ref.out
./rastertolabel                                    538 "$USER" gt 1 "x" raster > new.out
cmp ref.out new.out && echo IDENTICAL
```

End-to-end print test:
```bash
printf 'NATIVE ARM TEST\n' | lp -d _4BARCODE_4B_2054A -o page-width=283 -o page-length=283
```

## System paths
- Installed filter: `/Library/Printers/LABEL/Filter/rastertolabel` (root:admin).
- Active PPD:       `/private/etc/cups/ppd/_4BARCODE_4B_2054A.ppd` (its
  `*cupsFilter` line points at the filter path above — no reconfig needed).
- Default queue / printer name: `_4BARCODE_4B_2054A` (Description: `LABEL`).

## Editing rules
- This repo is shared; do not revert others' edits. Adjust around them.
- Prefer editing `rastertolabel_arm.c` and rebuilding over modifying the binary.
- Never leave `cupsd` in debug logging (`sudo cupsctl --no-debug-logging`).
- No destructive `rm -rf` on system printer paths; use `rollback.sh`.
