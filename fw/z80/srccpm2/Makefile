CFLAGS= -O -s -Wall

TARGETS=putsys bios.bin boot60.bin boot62.bin dual_bios.bin boot.dsk dual.dsk
all: $(TARGETS)
	@echo "done"

putsys: putsys.c
	$(CC) $(CFLAGS) -o putsys putsys.c

bios.bin: bios.asm
	z80asm -vl -sn -x bios.asm

dual_bios.bin: dual_bios.asm
	z80asm -vl -sn -x dual_bios.asm

boot60.bin boot62.bin: boot.asm
	z80asm -vl -sn -dK62 boot.asm
	mv boot.bin boot62.bin
	mv boot.lis boot62.lis
	z80asm -vl -sn boot.asm
	mv boot.bin boot60.bin
	mv boot.lis boot60.lis

boot.dsk: cpm60.com bios.bin boot60.bin
	./putsys boot60.bin bios.bin cpm60.com boot.dsk

dual.dsk: cpm62.com dual_bios.bin boot62.bin
	./putsys boot62.bin dual_bios.bin cpm62.com dual.dsk

clean:
	rm -f *.lis $(TARGETS)
