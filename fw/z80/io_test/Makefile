BIN2RAM_INIT := ../../../tools/bin2ram_init/bin2ram_init

io_test.bin: io_test.asm $(BIN2RAM_INIT)
	z80asm -vl -sn -fb io_test.asm
	$(BIN2RAM_INIT) io_test.bin ram_init.txt

$(BIN2RAM_INIT) : $(BIN2RAM_INIT).c


