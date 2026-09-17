/*
 * rastertolabel_arm.c  —  native Apple-Silicon (arm64) CUPS filter for the
 * ZIJIANG / 4BARCODE 4B-2054A "LABEL" thermal printer.
 *
 * Drop-in replacement for the original Intel-only (x86_64)
 *   /Library/Printers/LABEL/Filter/rastertolabel
 * It consumes a CUPS raster stream (produced by cgpdftoraster) and emits the
 * printer's native language, TSPL2. Output is byte-identical to the original
 * filter on the default (hard-threshold) path (see docs/DRIVER-JOURNAL.md).
 *
 * Colour: the printer is physically monochrome (thermal black-on-white).
 * macOS already converts colour documents to an 8-bit GRAYSCALE raster before
 * this filter runs. By default we threshold that grayscale exactly like the
 * original (crisp, best for text/1-bit logos). Optionally you can render
 * photographic/gradient content as *dithered* grayscale:
 *
 *   dither=floyd    Floyd-Steinberg error diffusion (smooth tones)
 *   dither=ordered  Bayer 8x8 ordered halftone (regular rosette)
 *   gamma=<f>       tone lift; >1.0 darkens (helps light colours: yellow/cyan)
 *
 * Set per job:   lp -o dither=floyd -o gamma=1.4 file
 * Set per queue: lpadmin -p _4BARCODE_4B_2054A -o dither=floyd
 * Defaults (dither unset, gamma=1.0) reproduce the original bytes exactly.
 *
 * Standard CUPS filter ABI:
 *   rastertolabel job-id user title copies options [input-file]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>
#include <unistd.h>
#include <fcntl.h>
#include <cups/cups.h>
#include <cups/raster.h>

/* Fixed optical resolution of the 4B-2054A printhead (dots per inch). */
#define PRINTER_DPI 203.0

/* dither modes */
#define DITHER_OFF    0
#define DITHER_FLOYD  1
#define DITHER_ORDERED 2

static int   g_dither = DITHER_OFF;   /* default: hard threshold (identical)   */
static double g_gamma = 1.0;          /* default: no tone change (identical)   */

/* Bayer 4x4 scaled to an 8x8-ish spread via tiling (values 0..15 -> 0..255). */
static const int BAYER[64] = {
   0,128, 32,160,  8,136, 40,168,
 192, 64,224, 96,200, 72,232,104,
  48,176, 16,144, 56,184, 24,152,
 240,112,208, 80,248,120,216, 88,
  12,140, 44,172,  4,132, 36,164,
 204, 76,236,108,196, 68,228,100,
  60,188, 28,156, 52,180, 20,148,
 252,124,220, 92,244,116,212, 84
};

/* 256-entry gamma LUT (identity when gamma==1.0). Built once if needed. */
static unsigned char GAMMA_LUT[256];
static int           gamma_ready = 0;

static void ensure_gamma(void)
{
    if (gamma_ready) return;
    for (int i = 0; i < 256; i++) {
        double v = pow(i / 255.0, g_gamma) * 255.0;
        if (v < 0) v = 0; if (v > 255) v = 255;
        GAMMA_LUT[i] = (unsigned char)(v + 0.5);
    }
    gamma_ready = 1;
}

/* Apply gamma to an 8-bit gray value (no-op when gamma==1.0). */
static inline int tone(int g8)
{
    if (g_gamma == 1.0) return g8;
    ensure_gamma();
    return GAMMA_LUT[g8 & 255];
}

static inline int clampi(int v){ return v < 0 ? 0 : (v > 255 ? 255 : v); }

/* Parse "dither=..." and "gamma=..." out of the CUPS options string (argv[5]). */
static void parse_options(const char *opts)
{
    char buf[4096];
    char *tok, *save = NULL;
    if (!opts) return;
    strncpy(buf, opts, sizeof(buf) - 1); buf[sizeof(buf)-1] = '\0';
    for (tok = strtok_r(buf, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
        if (!strncasecmp(tok, "dither=", 7)) {
            const char *v = tok + 7;
            if (!strcasecmp(v, "floyd") || !strcasecmp(v, "fs") ||
                !strcasecmp(v, "error-diffusion") || !strcasecmp(v, "yes") ||
                !strcasecmp(v, "on"))
                g_dither = DITHER_FLOYD;
            else if (!strcasecmp(v, "ordered") || !strcasecmp(v, "bayer") ||
                     !strcasecmp(v, "halftone"))
                g_dither = DITHER_ORDERED;
            else
                g_dither = DITHER_OFF;
        } else if (!strncasecmp(tok, "gamma=", 6)) {
            double g = atof(tok + 6);
            if (g >= 0.1 && g <= 5.0) g_gamma = g;
        }
    }
}

/* grayscale of one pixel from the raster line (0..255; 0=black,255=white) */
static inline int pixel_gray(const unsigned char *line, int bpp, int x)
{
    if (bpp == 1) {
        int bit = (line[x >> 3] >> (7 - (x & 7))) & 1;
        return bit ? 0 : 255;           /* 1bpp device: bit 0 == black */
    } else if (bpp == 8) {
        return line[x];
    } else if (bpp == 24) {
        int r = line[x*3], g = line[x*3+1], b = line[x*3+2];
        return (r*30 + g*59 + b*11) / 100;
    }
    int r = line[x*4], g = line[x*4+1], b = line[x*4+2];
    return (r*30 + g*59 + b*11) / 100;
}

int
main(int argc, char **argv)
{
    int                fd, copies;
    cups_raster_t     *ras;
    cups_page_header_t h;

    if (argc < 6) {
        fprintf(stderr, "usage: %s job-id user title copies options [file]\n", argv[0]);
        return 1;
    }

    copies = atoi(argv[4]); if (copies < 1) copies = 1;
    parse_options(argv[5]);

    if (argc > 6) fd = open(argv[6], O_RDONLY); else fd = 0;
    if (fd < 0) { fprintf(stderr, "ERROR: Unable to open raster file\n"); return 1; }
    if ((ras = cupsRasterOpen(fd, CUPS_RASTER_READ)) == NULL) {
        fprintf(stderr, "ERROR: Unable to open raster stream\n"); return 1;
    }

    while (cupsRasterReadHeader(ras, &h)) {
        unsigned  w    = h.cupsWidth;
        unsigned  rows = h.cupsHeight;
        unsigned  bpp  = h.cupsBitsPerPixel;
        unsigned  pbpr = h.cupsBytesPerLine;
        int       bpr  = (int)((w + 7) / 8);
        int       wpad = bpr * 8;
        int       mm_w = (int)((wpad * 25.4) / PRINTER_DPI + 0.5);
        int       mm_h = (int)((rows * 25.4) / PRINTER_DPI + 0.5);
        int       y;
        unsigned char *line = malloc(pbpr ? pbpr : 1);
        unsigned char *pack = malloc(bpr ? bpr : 1);
        int *errA = NULL, *errB = NULL;              /* error-diffusion rows */
        int  use_fs = (g_dither == DITHER_FLOYD && bpp != 1);

        if (!line || !pack) { fprintf(stderr, "ERROR: out of memory\n"); return 1; }

        printf("SIZE %d mm ,%d mm\n", mm_w, mm_h);
        printf("REFERENCE 0,0\n");
        printf("GAP 3 mm,0 mm\n");
        printf("OFFSET 0 mm\n");
        printf("DENSITY 6\n");
        printf("SPEED 4\n");
        printf("SETC AUTODOTTED OFF\n");
        printf("SETC PAUSEKEY ON\n");
        printf("SETC WATERMARK OFF\n");
        printf("CLS\n");
        printf("BITMAP 0,0,%d,%u,1,", bpr, rows);

        if (use_fs) { errA = calloc(w ? w : 1, sizeof(int)); errB = calloc(w ? w : 1, sizeof(int)); }

        for (y = 0; y < (int)rows; y++) {
            int x;
            if (!cupsRasterReadPixels(ras, line, pbpr)) break;

            /* ---- default: identical to the Intel filter ---- */
            if (g_dither == DITHER_OFF && g_gamma == 1.0) {
                memset(pack, 0xFF, bpr);
                for (x = 0; x < (int)w; x++) {
                    int black = 0;
                    if (bpp == 1) {
                        black = !((line[x >> 3] >> (7 - (x & 7))) & 1);
                    } else if (bpp == 8) {
                        black = line[x] < 128;
                    } else {
                        black = pixel_gray(line, bpp, x) < 128;
                    }
                    if (black) pack[x >> 3] &= (unsigned char)~(0x80 >> (x & 7));
                }
                fwrite(pack, 1, bpr, stdout);
                continue;
            }

            /* ---- optional: gamma +/- dither ---- */
            memset(pack, 0xFF, bpr);   /* 1 = white; clear bits for black */
            if (use_fs) memset(errB, 0, w * sizeof(int));

            for (x = 0; x < (int)w; x++) {
                int v = tone(pixel_gray(line, bpp, x));
                int black;

                if (use_fs) {
                    v = clampi(v + (errA[x] >> 4));
                    black = (v < 128);
                    int out = black ? 0 : 255;
                    int e   = v - out;                  /* error to diffuse */
                    if (x + 1 < (int)w) errA[x + 1] += e * 7;   /* right (this row) */
                    if (x > 0)          errB[x - 1] += e * 3;   /* below-left  */
                    errB[x]            += e * 5;                /* below       */
                    if (x + 1 < (int)w) errB[x + 1] += e * 1;   /* below-right */
                } else if (g_dither == DITHER_ORDERED) {
                    int bv  = BAYER[((y & 7) << 3) | (x & 7)] >> 2;  /* 0..63 */
                    int thr = 128 + (bv - 31);                          /* ~97..159 */
                    black = (clampi(v) < thr);
                } else {                                /* gamma-only threshold */
                    black = (v < 128);
                }

                if (black) pack[x >> 3] &= (unsigned char)~(0x80 >> (x & 7));
            }
            if (use_fs) { int *t = errA; errA = errB; errB = t; }
            fwrite(pack, 1, bpr, stdout);
        }

        printf("\n");
        printf("PRINT %d,1\n", copies);

        free(line); free(pack); free(errA); free(errB);
    }

    fflush(stdout);
    cupsRasterClose(ras);
    if (fd != 0) close(fd);
    return ferror(stdout) ? 1 : 0;
}
