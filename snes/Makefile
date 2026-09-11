SHELL := /bin/bash
.SHELLFLAGS := -o pipefail -c
.DELETE_ON_ERROR:
.PRECIOUS: %.o65.log %.ips.log

IPS = header.ips
O65 = reset.o65 main.o65 font.o65 palette.o65 data.o65 const.o65 logo.o65 logospr.o65 dma.o65 filesel.o65 pad.o65 time.o65 sysinfo.o65 spc700.o65 spcplay.o65 menu.o65 menudata.o65 ui.o65 fxpakos.o65 nmihook.o65 common.o65 savestate.o65 near.o65 # gfx.o65 # vars.o65

OBJS = $(IPS) $(O65)
# stopgap measure to create both files as long as they are identical...

OUT = menu.bin m3nu.bin

LOG = $(OBJS:=.log)
MAP = $(IPS:.ips=.map) $(O65:.o65=.map)

DEPDIR := .dep
GENDEPFLAGS = -MM -MP -MF $(DEPDIR)/$(@F).d -MT $(@F)

all: $(OUT) $(MAP)

smc: menu.bin
	cat menu.bin sd2snes.rom > menu.smc

map: $(MAP)

$(MAP) &: menu.bin $(LOG) link.log utils/mkmap.sh
	utils/mkmap.sh $(OBJS)

# generate menu.bin / m3nu.bin / link.log
$(OUT) link.log &: $(OBJS)
	sneslink -fsmc -o menu.bin $^ 2>&1 | tee link.log
	! grep -Eq "^(Error:|ERROR:|O65 linker:)" link.log || { rm -f $(OUT); exit 1; }
	cp menu.bin m3nu.bin

# Generic rule to create .o65[.log] out from .a65
%.o65 %.o65.log &: %.a65
	cpp $(GENDEPFLAGS) $<
	snescom -J -Wall -o $(@:.log=) $< 2>&1 | tee $(@:.log=).log
	! grep -q "^Error:" $(@:.log=).log || { rm -f $(@:.log=); exit 1; }

# Generic rule to create .ips out from .a65
%.ips %.ips.log &: %.a65
	cpp $(GENDEPFLAGS) $<
	snescom -I -J -Wall -o $(@:.log=) $< 2>&1 | tee $(@:.log=).log
	! grep -q "^Error:" $(@:.log=).log || { rm -f $(@:.log=); exit 1; }

clean:
	rm -f -- *~ *.ips *.o65 $(OUT) *.o65.log *.ips.log

-include $(info $(DEPDIR)) $(shell mkdir $(DEPDIR) 2>/dev/null) $(wildcard $(DEPDIR)/*)

art:
	python3 ../tools/fxpakos/make_test_cover.py ../.build/art/starfall.png
	python3 ../tools/fxpakos/compile_art.py ../.build/art/starfall.png ../.build/art/starfall
	python3 ../tools/fxpakos/bundle_cover.py ../.build/art/starfall fxpakos_cover.i65

.PHONY: all smc map clean art
