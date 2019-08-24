CC = gcc
BIN2RAM_INIT := ../../../tools/bin2ram_init/bin2ram_init

CFLAGS= -O -s -Wall

all: putsys bios.bin boot.bin
	@echo "done"

putsys: putsys.c
	$(CC) $(CFLAGS) -o putsys putsys.c

bios.bin: bios.asm
	z80asm -vl -sn -x bios.asm

boot.bin: boot.asm
	z80asm -fb -vl -sn boot.asm
	$(BIN2RAM_INIT) boot.bin boot.txt

$(BIN2RAM_INIT) : $(BIN2RAM_INIT).c

clean:
	rm -f *.lis bios.bin boot.bin putsys putsys.exe
