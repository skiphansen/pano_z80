# Simulation of the sound and Z80 speed logic (fpga/audio_beeper.v and the
# port 32h, sound and speed parts of fpga/cpm_io.v).  Needs Icarus Verilog.
#
#   make test

FPGA := ../../fpga
RTL  := tb.v $(FPGA)/audio_beeper.v $(FPGA)/cpm_io.v

test: build/tb.vvp
	@vvp -n build/tb.vvp | grep -v '^Z80 \|^riscv ' | tee build/result.txt
	@! grep -q FAIL build/result.txt

build:
	mkdir -p build

build/tb.vvp: $(RTL) | build
	iverilog -g2005 -o $@ $(RTL)

clean:
	rm -rf build

.PHONY: test clean
