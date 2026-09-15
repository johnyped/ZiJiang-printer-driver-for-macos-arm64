# Native arm64 (+ x86_64) CUPS filter for the ZiJiang / 4BARCODE 4B-2054A LABEL printer.
#
#   make            build the universal binary (./rastertolabel)
#   make arm64      build only the arm64 slice
#   make test       build, then byte-diff vs the installed reference filter
#   make install    back up the Intel filter and install this one (sudo)
#   make uninstall  restore the Intel filter (sudo)  [alias: rollback]
#   make clean      remove build artifacts

SRC     := rastertolabel_arm.c
BIN     := rastertolabel
OUTDIR  := .build-objects
MINOS   := 13.0

# Prefer the stable SDK: beta MacOSX27 .tbd stubs break ld ("arm64e.x1 unknown").
SDK265  := $(shell xcode-select -p 2>/dev/null)/SDKs/MacOSX26.5.sdk
SDK     := $(shell test -d "$(SDK265)" && echo "$(SDK265)" || xcrun --show-sdk-path --sdk macosx)

CFLAGS  := -mmacosx-version-min=$(MINOS) -isysroot $(SDK) -O2 -w $(CUPSCFLAGS)
CUPSCFLAGS := $(shell cups-config --cflags 2>/dev/null)
LDLIBS  := $(shell cups-config --libs 2>/dev/null) -lcupsimage

DEST    := /Library/Printers/LABEL/Filter/rastertolabel
QUEUE   ?= _4BARCODE_4B_2054A

.PHONY: all universal arm64 x86_64 test install uninstall rollback clean help

all: universal

universal: $(OUTDIR)/arm64 $(OUTDIR)/x86_64
	lipo -create -output $(BIN) $(OUTDIR)/arm64 $(OUTDIR)/x86_64
	@echo "built: $$(lipo -archs $(BIN))"

arm64:  $(OUTDIR)/arm64
x86_64: $(OUTDIR)/x86_64

$(OUTDIR):
	@mkdir -p $(OUTDIR)

$(OUTDIR)/arm64: $(SRC) | $(OUTDIR)
	cc -arch arm64  $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

$(OUTDIR)/x86_64: $(SRC) | $(OUTDIR)
	cc -arch x86_64 $(CFLAGS) -o $@ $(SRC) $(LDLIBS)

# Byte-for-byte diff of the native filter against the installed Intel reference
# on a freshly produced 8bpp-gray raster. Prints IDENTICAL or the first diff.
test: universal
	@printf 'GROUND TRUTH LABEL 123\n' > /tmp/lp_gt.txt
	@/usr/libexec/cups/filter/cgtexttopdf 538 "$$USER" gt 1 "page-width=283 page-length=283" /tmp/lp_gt.txt > /tmp/lp_gt.pdf 2>/dev/null
	@/usr/libexec/cups/filter/cgpdftoraster 538 "$$USER" gt 1 "pages=1" /tmp/lp_gt.pdf > /tmp/lp_gt.ras 2>/dev/null
	@export PPD=/private/etc/cups/ppd/$(QUEUE).ppd; \
	  $(DEST) 538 "$$USER" gt 1 "PageSize=w283h283" /tmp/lp_gt.ras > /tmp/lp_gt.ref.out 2>/dev/null; \
	  ./$(BIN) 538 "$$USER" gt 1 "PageSize=w283h283" /tmp/lp_gt.ras > /tmp/lp_gt.new.out 2>/dev/null; \
	  if cmp -s /tmp/lp_gt.ref.out /tmp/lp_gt.new.out; then \
	    echo "IDENTICAL  ($$(wc -c </tmp/lp_gt.ref.out) bytes)"; \
	  else \
	    echo "DIFFER:"; cmp /tmp/lp_gt.ref.out /tmp/lp_gt.new.out || true; \
	  fi
install: universal
	@test -f "$(DEST).intel.orig" || sudo cp -p "$(DEST)" "$(DEST).intel.orig"
	sudo cp $(BIN) $(DEST)
	sudo chown root:wheel $(DEST)
	sudo chmod 755 $(DEST)
	@echo "installed:"; lipo -archs $(DEST)
	@-cancel -a $(QUEUE) 2>/dev/null
	@-cupsenable $(QUEUE) 2>/dev/null; cupsaccept $(QUEUE) 2>/dev/null
	@echo "test: printf 'NATIVE ARM TEST\\n' | lp -d $(QUEUE) -o page-width=283 -o page-length=283"

uninstall rollback:
	@test -f "$(DEST).intel.orig" || { echo "no backup at $(DEST).intel.orig"; exit 1; }
	sudo cp -p "$(DEST).intel.orig" "$(DEST)"
	@echo "rolled back to original Intel filter."

clean:
	@rm -rf $(OUTDIR) $(BIN)
	@echo "cleaned"

help:
	@echo "targets: all universal arm64 x86_64 test install uninstall rollback clean"
