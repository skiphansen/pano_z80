# Native tests for the F11 USB update menu (../flash_update.c).
#
# The menu's parsing and validation logic is plain C, so it is compiled for the
# host - with AddressSanitizer and UBSan - against stub USB / FatFs / flash
# layers and fed one good PANOUPD.BIN and a series of deliberately damaged
# ones (bad CRCs, truncation, wild offsets, a bitstream for the wrong FPGA, a
# flash that does not answer ...).  The .bit parser is also fuzzed, and the
# firmware's crc32() is checked against zlib.
#
#   make test [BIT=path/to/pano_top.bit] [FW=path/to/firmware.bin]

TOPDIR := ../../..
BIT ?= $(TOPDIR)/xilinx/work/pano_top.bit
FW  ?= ../firmware.bin
HOSTCC ?= cc

test: build/harness
	./build/harness crcselftest
	./build/harness datetest
	python3 run_tests.py $(BIT) $(FW)
	./build/harness fuzzbit=$(BIT)

build/harness: harness.c ../flash_update.c ../fwinfo.c ../fwinfo.h ../spiflash.c ../spiflash.h stubs/*.h
	mkdir -p build
	cp ../flash_update.c build/flash_update_copy.c
	cp ../fwinfo.c build/fwinfo_copy.c
	sed -n '/^uint32_t crc32(const uint8_t \*buf, uint32_t len)$$/,$$p' ../spiflash.c > build/crc32_extract.c
	$(HOSTCC) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra \
	    -Wno-unused-parameter -Wno-unused-result -iquote stubs -iquote .. -iquote . \
	    -o $@ harness.c -lz

clean:
	rm -rf build

.PHONY: test clean
