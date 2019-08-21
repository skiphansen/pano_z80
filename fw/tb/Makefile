OBJS = start.o tb.o

CFLAGS = -MD -O1 -march=rv32ic -ffreestanding -nostdlib -Wl,--no-relax -I../firmware
TOOLCHAIN_PREFIX = riscv32-unknown-elf-
BIN2MIF := ../../tools/bin2mif/bin2mif
BIN2C := ../../tools/bin2c/bin2c
BIN2RAM_INIT := ../../tools/bin2ram_init/bin2ram_init

XC3SPROG := xc3sprog
XC3SPROG_OPTS := -c jtaghs2 -v

all: firmware.bin firmware.lst ram_init.txt

#override CFLAGS for native compiles
$(BIN2C) $(BIN2MIF) $(BIN2RAM_INIT) : CFLAGS =

$(BIN2MIF): $(BIN2MIF).cpp

$(BIN2C) : $(BIN2C).c 

$(BIN2RAM_INIT) : $(BIN2RAM_INIT).c

firmware.bin: firmware.elf
	$(TOOLCHAIN_PREFIX)objcopy -O binary $< $@

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

ram_init.txt: $(BIN2RAM_INIT) firmware.bin
	$(BIN2RAM_INIT) firmware.bin ram_init.txt

%.o: %.c
	$(TOOLCHAIN_PREFIX)gcc -c $(CFLAGS) $<

%.o: %.s
	$(TOOLCHAIN_PREFIX)gcc -c $(CFLAGS) $<

clean:
	rm -rf *.o *.d *.elf *.bin *.map *.lst z80_boot.h

.PHONY: clean

-include *.d

