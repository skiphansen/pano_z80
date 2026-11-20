# Native tests for the USB update menu (../flash_update.c), for the
# settings file and setup menu (../setup.c) and for the console (../vt100.c).
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

test: build/harness build/setup_harness build/vt_harness
	./build/harness crcselftest
	./build/harness datetest
	python3 run_tests.py $(BIT) $(FW)
	./build/harness fuzzbit=$(BIT)
	python3 run_setup_tests.py
	python3 run_vt_tests.py

# The settings file and the setup menu (../setup.c), the same way.
build/setup_harness: setup_harness.c ../setup.c ../setup.h stubs_setup/*.h
	mkdir -p build
	cp ../setup.c build/setup_copy.c
	$(HOSTCC) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra \
	    -Wno-unused-parameter -iquote stubs_setup -iquote .. -iquote . \
	    -o $@ setup_harness.c

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

# The console (../vt100.c): terminal emulations, screen save / restore.
build/vt_harness: vt_harness.c ../vt100.c ../vt100.h stubs_vt/*.h
	mkdir -p build
	cp ../vt100.c build/vt100_copy.c
	$(HOSTCC) -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -w \
	    -iquote stubs_vt -iquote .. -iquote . -o $@ vt_harness.c
