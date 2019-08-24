OBJS = start.o firmware.o stdlib.o term.o isp1760.o i2c.o misc.o ff.o 
OBJS += ffsystem.o diskio.o usb.o usb_storage.o cpm_io.o printf.o usb_kbd.o

CFLAGS = -MD -O1 -march=rv32ic -ffreestanding -nostdlib -Wl,--no-relax
TOOLCHAIN_PREFIX = riscv32-unknown-elf-
BIN2MIF := ../../tools/bin2mif/bin2mif
BIN2C := ../../tools/bin2c/bin2c
Z80_BOOT_BIN := ../z80/srccpm2/boot.bin

XC3SPROG := xc3sprog
XC3SPROG_OPTS := -c jtaghs2 -v

all: firmware.bin firmware.lst

#override CFLAGS for native compiles
$(BIN2C) $(BIN2MIF) : CFLAGS =

$(BIN2MIF): $(BIN2MIF).cpp

$(BIN2C) : $(BIN2C).c 

firmware.bin: firmware.elf
	$(TOOLCHAIN_PREFIX)objcopy -O binary $< $@

firmware.lst : firmware.elf
	$(TOOLCHAIN_PREFIX)objdump -d $< > $@

firmware.mif: firmware.bin $(BIN2MIF)
	dd if=/dev/zero of=flash.bin bs=1k count=768
	cat firmware.bin >> flash.bin
	$(BIN2MIF) flash.bin firmware.mif 1048576 8
	cp firmware.mif ../../fpga

z80_boot.h: $(BIN2C) $(Z80_BOOT_BIN)
	$(BIN2C) $(Z80_BOOT_BIN) z80_boot.h z80_boot_img

firmware.elf: z80_boot.h $(OBJS) sections.lds
	$(TOOLCHAIN_PREFIX)gcc $(CFLAGS) -Wl,-Bstatic,-T,sections.lds,-Map,firmware.map,--strip-debug -o $@ $(OBJS) -lgcc
	size $@

%.o: %.c
	$(TOOLCHAIN_PREFIX)gcc -c $(CFLAGS) $<

%.o: %.s
	$(TOOLCHAIN_PREFIX)gcc -c $(CFLAGS) $<

prog: firmware.bin
	$(XC3SPROG) $(XC3SPROG_OPTS) -I../../fpga/xc3sprog/pano_g1.bit firmware.bin:W:786432:BIN
	$(XC3SPROG) $(XC3SPROG_OPTS) ../../xilinx/work/pano_top.bit

reload:
	$(XC3SPROG) $(XC3SPROG_OPTS) ../../xilinx/work/pano_top.bit

clean:
	rm -rf *.o *.d *.elf *.bin *.map *.lst z80_boot.h

.PHONY: clean

-include *.d

