# You may need to change this to match your JTAG adapter
XC3SPROG_OPTS := -c jtaghs2 -v

ifeq ($(TOPDIR),)
TOPDIR := .
endif

XC3SPROG := xc3sprog
XC3SPROG_BIT_FILE := $(TOPDIR)/fpga/xc3sprog/pano_g1.bit
BIN2MIF := $(TOPDIR)/tools/bin2mif/bin2mif
BIN2C := $(TOPDIR)/tools/bin2c/bin2c
BIT_FILE := $(TOPDIR)/xilinx/work/pano_top
FW_BIN := $(TOPDIR)/fw/firmware/firmware.bin
MCS_FILE := $(TOPDIR)/xilinx/pano_z80.mcs

prog_msc:  $(MCS_FILE)
	$(XC3SPROG) $(XC3SPROG_OPTS) -I$(XC3SPROG_BIT_FILE) $(MCS_FILE):W:0:MCS
	$(XC3SPROG) $(XC3SPROG_OPTS) $(BIT_FILE).bit

prog_all: $(FW_BIN) $(BIT_FILE).bit
	$(XC3SPROG) $(XC3SPROG_OPTS) -I$(XC3SPROG_BIT_FILE) $(BIT_FILE):W:0:BIT
	$(XC3SPROG) $(XC3SPROG_OPTS) -I$(XC3SPROG_BIT_FILE) $(FW_BIN):W:786432:BIN
	$(XC3SPROG) $(XC3SPROG_OPTS) $(BIT_FILE).bit

prog_fpga: $(BIT_FILE).bit
	$(XC3SPROG) $(XC3SPROG_OPTS) -I$(XC3SPROG_BIT_FILE) $(BIT_FILE):W:0:BIT
	$(XC3SPROG) $(XC3SPROG_OPTS) $(BIT_FILE).bit

prog_fw: $(FW_BIN)
	$(XC3SPROG) $(XC3SPROG_OPTS) -I$(XC3SPROG_BIT_FILE) $(FW_BIN):W:786432:BIN
	$(XC3SPROG) $(XC3SPROG_OPTS) $(BIT_FILE).bit

reload:
	$(XC3SPROG) $(XC3SPROG_OPTS) $(BIT_FILE).bit

# Combined bitstream + firmware image for the USB updater: copy PANOUPD.BIN to
# the root of the USB drive and press F11 on the Pano.
UPDATE_IMG := $(TOPDIR)/PANOUPD.BIN

update: $(FW_BIN) $(BIT_FILE).bit
	python3 $(TOPDIR)/tools/mkupdate.py -o $(UPDATE_IMG) --bit $(BIT_FILE).bit --fw $(FW_BIN)

#override CFLAGS for native compiles
$(BIN2C) $(BIN2MIF) : CFLAGS =

$(BIN2MIF): $(BIN2MIF).cpp

$(BIN2C) : $(BIN2C).c 

.PHONY: prog_all reload update

