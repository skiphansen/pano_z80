# Simulation of the USB updater's flash programming code.
#
# Runs the firmware's real start.s + spiflash.c on the real picorv32, cache and
# spimemio RTL (glued together the way pano_top.v does it) against a
# behavioural model of the M25P80 SPI flash.  Needs Icarus Verilog and the
# RISC-V toolchain used for the firmware.
#
#   make test                      # with the default riscv32-unknown-elf- tools
#   make test TOOLCHAIN_PREFIX=riscv64-elf- ARCHFLAGS="-march=rv32ic -mabi=ilp32"

TOPDIR := ../..
FW     := $(TOPDIR)/fw/firmware
FPGA   := $(TOPDIR)/fpga

TOOLCHAIN_PREFIX ?= riscv32-unknown-elf-
ARCHFLAGS        ?= -march=rv32ic
CFLAGS = -O1 $(ARCHFLAGS) -ffreestanding -nostdlib -Wl,--no-relax -I$(FW)

RTL = tb.v m25p80_model.v mig_user_model.v bram_256_72.v $(FPGA)/picorv32.v \
      $(FPGA)/spimemio.v $(FPGA)/cache.v $(FPGA)/picosoc_mem.v \
      $(FPGA)/mig_picorv_bridge.v

test: build/tb.vvp build/tb_fix.vvp build/fw.hex build/fw_unmask.hex build/fw_oldpol.hex build/fw_rvc.hex
	python3 run.py $(TOOLCHAIN_PREFIX)

build:
	mkdir -p build

build/tb.vvp: $(RTL) | build
	iverilog -g2005 -o $@ $(RTL)

# the same, with the bus-fault detector fix that is in pano_top.v
build/tb_fix.vvp: $(RTL) | build
	iverilog -g2005 -DFAULT_FIX -o $@ $(RTL)

build/%.hex: build/%.elf
	$(TOOLCHAIN_PREFIX)objcopy -O binary $< build/$*.bin
	python3 -c "import sys; open(sys.argv[2],'w').write(''.join('%02x\n' % b for b in open(sys.argv[1],'rb').read()))" build/$*.bin $@

LINK = -Wl,-Bstatic,-T,$(FW)/sections.lds,--strip-debug
CHECK = python3 $(TOPDIR)/tools/check_ramtext.py $@ $(TOOLCHAIN_PREFIX)

# spiflash.c runs from RAM and is built without compressed instructions, the
# way fw/firmware/Makefile builds it (tools/check_ramtext.py explains why).
build/spiflash.o: $(FW)/spiflash.c $(FW)/spiflash.h | build
	$(TOOLCHAIN_PREFIX)gcc $(CFLAGS) -march=rv32i -c -o $@ $<

build/fw.elf: tb_main.c build/spiflash.o $(FW)/start.s $(FW)/sections.lds
	$(TOOLCHAIN_PREFIX)gcc $(CFLAGS) $(LINK) -o $@ $(FW)/start.s tb_main.c build/spiflash.o -lgcc
	$(CHECK)

build/fw_unmask.elf: tb_main.c build/spiflash.o $(FW)/start.s $(FW)/sections.lds
	$(TOOLCHAIN_PREFIX)gcc $(CFLAGS) -DUNMASK_AFTER_ID -DID_ONLY $(LINK) -o $@ $(FW)/start.s tb_main.c build/spiflash.o -lgcc
	$(CHECK)

# Negative control: the config register polarity the code originally had
# (bit 31 set while bit-banging).  The simulation must show this failing.
build/spiflash_oldpol.c: $(FW)/spiflash.c | build
	sed -e 's/^#define CFG_IDLE .*/#define CFG_IDLE   (CFG_MEMIO | CFG_OE0 | CFG_CS)/' \
	    -e 's/^#define CFG_SEL .*/#define CFG_SEL    (CFG_MEMIO | CFG_OE0)/' \
	    -e 's/^#define CFG_XIP .*/#define CFG_XIP    (0u)/' $< > $@

build/fw_oldpol.elf: tb_main.c build/spiflash_oldpol.c $(FW)/start.s $(FW)/sections.lds
	$(TOOLCHAIN_PREFIX)gcc $(CFLAGS) -march=rv32i -c -o build/spiflash_oldpol.o build/spiflash_oldpol.c
	$(TOOLCHAIN_PREFIX)gcc $(CFLAGS) $(LINK) -o $@ $(FW)/start.s tb_main.c build/spiflash_oldpol.o -lgcc
	$(CHECK)

# Negative control: the RAM code built with compressed instructions, as it
# was at first.  It ran in the simulation as long as the DDR was modelled as a
# plain RAM, and hung on the real board.  Not checked here: run.py makes sure
# that check_ramtext.py rejects it and that the simulation shows the hang.
build/fw_rvc.elf: tb_main.c $(FW)/spiflash.c $(FW)/spiflash.h $(FW)/start.s $(FW)/sections.lds | build
	$(TOOLCHAIN_PREFIX)gcc $(CFLAGS) -DID_ONLY $(LINK) -o $@ $(FW)/start.s tb_main.c $(FW)/spiflash.c -lgcc

clean:
	rm -rf build

.PHONY: test clean
.SECONDARY:
