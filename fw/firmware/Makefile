TOPDIR := ../..

OBJS = start.o firmware.o isp1760.o i2c.o misc.o ff.o
OBJS += ffsystem.o diskio.o usb.o usb_storage.o cpm_io.o printf.o usb_kbd.o
OBJS += vt100.o rtc.o strptime.o gmtime.o mktime.o gets.o c_locale.o stdlib_char.o stdlib_str.o
OBJS += spiflash.o flash_update.o fwinfo.o setup.o messages.o

CFLAGS = -MD -O1 -march=rv32ic -ffreestanding -nostdlib -Wl,--no-relax
TOOLCHAIN_PREFIX = riscv32-unknown-elf-

all: firmware.bin firmware.lst check-ramtext

firmware.bin: firmware.elf
	$(TOOLCHAIN_PREFIX)objcopy -O binary $< $@

# The SPI flash programming code used by the USB updater (spiflash.c) has to
# run from RAM and must not touch flash while it works.  Verify that against
# the linked image: a stray call into flash would brick the board mid-update.
check-ramtext: firmware.elf
	python3 $(TOPDIR)/tools/check_ramtext.py $< $(TOOLCHAIN_PREFIX)

firmware.lst : firmware.elf
	$(TOOLCHAIN_PREFIX)objdump -d $< > $@

firmware.mif: firmware.bin $(BIN2MIF)
	dd if=/dev/zero of=flash.bin bs=1k count=768
	cat firmware.bin >> flash.bin
	$(BIN2MIF) flash.bin firmware.mif 1048576 8
	cp firmware.mif ../../fpga

firmware.elf: $(OBJS) sections.lds
	$(TOOLCHAIN_PREFIX)gcc $(CFLAGS) -Wl,-Bstatic,-T,sections.lds,-Map,firmware.map,--strip-debug -o $@ $(OBJS) -lgcc
	size $@

%.o: %.c
	$(TOOLCHAIN_PREFIX)gcc -c $(CFLAGS) $<

# spiflash.c is the code that runs from RAM.  It is built without compressed
# instructions: the DDR bridge cannot serve the fetch of a 32-bit instruction
# that straddles a word boundary (tools/check_ramtext.py explains, and checks).
spiflash.o: spiflash.c
	$(TOOLCHAIN_PREFIX)gcc -c $(CFLAGS) -march=rv32i $<

# every translation is part of messages.o
messages.o: messages.c messages.h $(wildcard lang/*.h)
	$(TOOLCHAIN_PREFIX)gcc -c $(CFLAGS) $<

# The version and build time shown at power-on and by the USB updater.
# fwinfo.c is recompiled on every build so that the time is the build time.
# "-dirty" means uncommitted changes to the firmware sources; a change
# elsewhere, like a rebuilt bitstream, does not make the firmware a different
# one.  (At most 23 characters fit.)
FW_GITREV := $(shell git describe --always 2>/dev/null | cut -c1-17)
ifeq ($(strip $(FW_GITREV)),)
FW_VERSION ?= unknown
else
FW_VERSION ?= $(FW_GITREV)$(shell git diff --quiet HEAD -- . || echo -dirty)
endif
FW_BUILT := $(shell date -u "+%Y-%m-%d %H:%M UTC")

fwinfo.o: fwinfo.c FORCE
	$(TOOLCHAIN_PREFIX)gcc -c $(CFLAGS) -DFW_VERSION='"$(FW_VERSION)"' -DFW_BUILT='"$(FW_BUILT)"' $<

FORCE:

%.o: %.s
	$(TOOLCHAIN_PREFIX)gcc -c $(CFLAGS) $<

prog: firmware.bin
	make -C $(TOPDIR) prog_fw

clean:
	rm -rf *.o *.d *.elf *.bin *.img *.map *.lst

.PHONY: clean check-ramtext FORCE

-include *.d
-include $(TOPDIR)/Makefile
