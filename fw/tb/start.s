#define regnum_q0   0
#define regnum_a0  10

.section .text

start:

j boot
nop
nop
nop

irq:
#.word 0x0000450B
addi a0, gp, 0
call irq_handler

boot:
# call main
call main

