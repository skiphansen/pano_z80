# Flash update simulation

`make test` runs the USB updater's flash programming code - the firmware's
real `start.s` and `spiflash.c`, linked with the real `sections.lds` - on the
real `picorv32`, `cache`, `spimemio` and `mig_picorv_bridge` RTL, wired up the
way `pano_top.v` does it, against a behavioural model of the Numonyx M25P80
that enforces the
rules of the real part (write enable before erase/program, nothing but a
status read accepted while busy, programming can only clear bits).

The CPU boots executing in place from the modelled flash, copies the
programming code to RAM, reads the flash ID and the first bytes of the
bitstream region, rewrites both flash regions and
reports the result on the LED register; the testbench then compares the flash
contents byte for byte.

It also checks that an unchanged region is not erased, that reading the ID
returns cleanly into flash code, the behaviour of the bus-fault detector with
and without the fix in `pano_top.v`, and - as negative controls - that the
configuration register polarity the code was first written with fails, and
that RAM code built with compressed instructions halts the CPU behind the DDR
bridge, which is what the first version did on the real board.

Needs Icarus Verilog (`iverilog`, `vvp`), Python 3 and the firmware's RISC-V
toolchain.  With a multilib `riscv64-elf-` toolchain:

    make test TOOLCHAIN_PREFIX=riscv64-elf- ARCHFLAGS="-march=rv32ic -mabi=ilp32"

`bram_256_72.v` here is a behavioural stand-in for the Xilinx block RAM wrapper
in `fpga/mig`.  The DDR side is the real bridge (`fpga/mig_picorv_bridge.v`)
in front of `mig_user_model.v`, a behavioural stand-in for the user interface
of the Xilinx memory controller.  So the bridge's handshake with the CPU is
tested; the controller itself and the DDR timing are not.
