/*
 * rastertolabel_arm.c  —  native Apple-Silicon (arm64) CUPS filter for the
 * ZIJIANG / 4BARCODE 4B-2054A "LABEL" thermal printer.
 *
 * Drop-in replacement for the original Intel-only (x86_64)
 *   /Library/Printers/LABEL/Filter/rastertolabel
 * It consumes a CUPS raster stream (produced by cgpdftoraster) and emits the
 * same TSPL2 command language the printer understands:
 *
 *   SIZE <w> mm ,<h> mm
 *   REFERENCE 0,0
 *   GAP 3 mm,0 mm
 *   OFFSET 0 mm
 *   DENSITY 6
 *   SPEED 4
 *   SETC AUTODOTTED OFF
 *   SETC PAUSEKEY ON
 *   SETC WATERMARK OFF
 *   CLS
 *   BITMAP 0,0,<bytesPerRow>,<rows>,1,<packed bitmap>
 *   PRINT <copies>,1
 *
 * This reproduces the original filter byte-for-byte (see VERIFY.md).
 *
 * Standard CUPS filter ABI:
 *   rastertolabel job-id user title copies options [input-file]
 * Reads raster from stdin (fd 0) when no input file is given.
 *
 * Requires Rosetta for the ORIGINAL filter; this native build needs none.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <cups/cups.h>
#include <cups/raster.h>

/* Fixed optical resolution of the 4B-2054A printhead (dots per inch). */
#define PRINTER_DPI 203.0

int
main(int argc, char **argv)
{
    int               fd;
    int               copies;
    cups_raster_t    *ras;
    cups_page_header_t h;

    if (argc < 6) {
        fprintf(stderr, "usage: %s job-id user title copies options [file]\n",
                argv[0]);
        return 1;
    }

    copies = atoi(argv[4]);
    if (copies < 1) copies = 1;

    if (argc > 6)
        fd = open(argv[6], O_RDONLY);
    else
        fd = 0;

    if (fd < 0) {
        fprintf(stderr, "ERROR: Unable to open raster file\n");
        return 1;
    }

    if ((ras = cupsRasterOpen(fd, CUPS_RASTER_READ)) == NULL) {
        fprintf(stderr, "ERROR: Unable to open raster stream\n");
        return 1;
    }

    while (cupsRasterReadHeader(ras, &h)) {
        unsigned    w    = h.cupsWidth;             /* pixels  */
        unsigned    rows = h.cupsHeight;            /* pixels  */
        unsigned    bpp  = h.cupsBitsPerPixel;      /* 1,8,24,32 */
        unsigned    pbpr = h.cupsBytesPerLine;      /* raster bytes/row */
        int         bpr  = (int)((w + 7) / 8);      /* packed bitmap bytes/row */
        int         wpad = bpr * 8;
        int         mm_w = (int)((wpad * 25.4) / PRINTER_DPI + 0.5);
        int         mm_h = (int)((rows * 25.4) / PRINTER_DPI + 0.5);
        int         y;
        unsigned char *line = malloc(pbpr ? pbpr : 1);
        unsigned char *pack = malloc(bpr ? bpr : 1);

        if (!line || !pack) {
            fprintf(stderr, "ERROR: out of memory\n");
            return 1;
        }

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

        for (y = 0; y < (int)rows; y++) {
            int x;
            if (!cupsRasterReadPixels(ras, line, pbpr))
                break;

            /* TSPL BITMAP polarity: bit 1 = white (no burn), bit 0 = black
               (burn). Pad bits beyond the true width stay white (1). So start
               every row all-ones and clear one bit per black pixel. */
            memset(pack, 0xFF, bpr);
            for (x = 0; x < (int)w; x++) {
                int black = 0;
                if (bpp == 1) {
                    /* 1bpp device raster: bit 0 == black in the source */
                    black = !((line[x >> 3] >> (7 - (x & 7))) & 1);
                } else if (bpp == 8) {
                    black = line[x] < 128;
                } else if (bpp == 24) {
                    unsigned r = line[x * 3 + 0], g = line[x * 3 + 1], b = line[x * 3 + 2];
                    black = ((r * 30 + g * 59 + b * 11) / 100) < 128;
                } else { /* 32 */
                    unsigned r = line[x * 4 + 0], g = line[x * 4 + 1], b = line[x * 4 + 2];
                    black = ((r * 30 + g * 59 + b * 11) / 100) < 128;
                }
                if (black)
                    pack[x >> 3] &= (unsigned char)~(0x80 >> (x & 7));
            }
            fwrite(pack, 1, bpr, stdout);
        }
        printf("\n");
        printf("PRINT %d,1\n", copies);

        free(line);
        free(pack);
    }

    fflush(stdout);
    cupsRasterClose(ras);
    if (fd != 0) close(fd);
    return ferror(stdout) ? 1 : 0;
}
