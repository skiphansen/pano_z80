#include "custom_ops.S"

.section .text

start:

j boot
nop
nop
nop

irq:
#.word 0x0000450B
	/* save registers */

 addi	sp,sp,-(32*4)
	sw gp,   0*4(sp)
	sw x1,   1*4(sp)
	sw x2,   2*4(sp)
	sw x3,   3*4(sp)
	sw x4,   4*4(sp)
	sw x5,   5*4(sp)
	sw x6,   6*4(sp)
	sw x7,   7*4(sp)
	sw x8,   8*4(sp)
	sw x9,   9*4(sp)
	sw x10, 10*4(sp)
	sw x11, 11*4(sp)
	sw x12, 12*4(sp)
	sw x13, 13*4(sp)
	sw x14, 14*4(sp)
	sw x15, 15*4(sp)
	sw x16, 16*4(sp)
	sw x17, 17*4(sp)
	sw x18, 18*4(sp)
	sw x19, 19*4(sp)
	sw x20, 20*4(sp)
	sw x21, 21*4(sp)
	sw x22, 22*4(sp)
	sw x23, 23*4(sp)
	sw x24, 24*4(sp)
	sw x25, 25*4(sp)
	sw x26, 26*4(sp)
	sw x27, 27*4(sp)
	sw x28, 28*4(sp)
	sw x29, 29*4(sp)
	sw x30, 30*4(sp)
	sw x31, 31*4(sp)

	/* call interrupt handler C function */

 addi a0, gp, 0
 addi a1, tp, 0
 call irq_handler

	lw gp,   0*4(sp)
	lw x1,   1*4(sp)
	lw x2,   2*4(sp)
	// do not restore x3 (gp)
	lw x4,   4*4(sp)
	lw x5,   5*4(sp)
	lw x6,   6*4(sp)
	lw x7,   7*4(sp)
	lw x8,   8*4(sp)
	lw x9,   9*4(sp)
	lw x10, 10*4(sp)
	lw x11, 11*4(sp)
	lw x12, 12*4(sp)
	lw x13, 13*4(sp)
	lw x14, 14*4(sp)
	lw x15, 15*4(sp)
	lw x16, 16*4(sp)
	lw x17, 17*4(sp)
	lw x18, 18*4(sp)
	lw x19, 19*4(sp)
	lw x20, 20*4(sp)
	lw x21, 21*4(sp)
	lw x22, 22*4(sp)
	lw x23, 23*4(sp)
	lw x24, 24*4(sp)
	lw x25, 25*4(sp)
	lw x26, 26*4(sp)
	lw x27, 27*4(sp)
	lw x28, 28*4(sp)
	lw x29, 29*4(sp)
	lw x30, 30*4(sp)
	lw x31, 31*4(sp)
 addi	sp,sp,(32*4)

	picorv32_retirq_insn()

boot:
# zero-initialize register file
addi x1, zero, 0
# x2 (sp) is initialized by reset
addi x3, zero, 0
addi x4, zero, 0
addi x5, zero, 0
addi x6, zero, 0
addi x7, zero, 0
addi x8, zero, 0
addi x9, zero, 0
addi x10, zero, 0
addi x11, zero, 0
addi x12, zero, 0
addi x13, zero, 0
addi x14, zero, 0
addi x15, zero, 0
addi x16, zero, 0
addi x17, zero, 0
addi x18, zero, 0
addi x19, zero, 0
addi x20, zero, 0
addi x21, zero, 0
addi x22, zero, 0
addi x23, zero, 0
addi x24, zero, 0
addi x25, zero, 0
addi x26, zero, 0
addi x27, zero, 0
addi x28, zero, 0
addi x29, zero, 0
addi x30, zero, 0
addi x31, zero, 0

# Update LEDs
# li a0, 0x03000000
# li a1, 1
# sw a1, 0(a0)

# copy data section
la a0, _sidata
la a1, _sdata
la a2, _edata
bge a1, a2, end_init_data
loop_init_data:
lw a3, 0(a0)
sw a3, 0(a1)
addi a0, a0, 4
addi a1, a1, 4
blt a1, a2, loop_init_data
end_init_data:

# zero-init bss section
la a0, _sbss
la a1, _ebss
bge a0, a1, end_init_bss
loop_init_bss:
sw zero, 0(a0)
addi a0, a0, 4
blt a0, a1, loop_init_bss
end_init_bss:

# call main
call main

