# this is what will be loaded into the stvec CSR; the
# trap handler address must be 4-byte aligned:
# https://docs.riscv.org/reference/isa/priv/supervisor.html#12-1-1-2-supervisor-trap-vector-base-address-stvec-register
#
# Upon a trap the hart jumps here with the interrupted context still live in
# the registers. handle_trap() is a regular C function, so it will preserve the
# callee-saved registers (sp, gp, tp, s0-s11) on its own; we only have to spill
# the caller-saved registers it is allowed to clobber (ra, t0-t6, a0-a7) so that
# the interrupted code resumes untouched after the sret.
.globl trap_entry
.align 4
trap_entry:

    # make room for the 16 caller-saved registers (16 * 8 = 128 bytes, which
    # also keeps the stack 16-byte aligned as required by the ABI)
    addi sp, sp, -128
    sd ra,    0(sp)
    sd t0,    8(sp)
    sd t1,   16(sp)
    sd t2,   24(sp)
    sd a0,   32(sp)
    sd a1,   40(sp)
    sd a2,   48(sp)
    sd a3,   56(sp)
    sd a4,   64(sp)
    sd a5,   72(sp)
    sd a6,   80(sp)
    sd a7,   88(sp)
    sd t3,   96(sp)
    sd t4,  104(sp)
    sd t5,  112(sp)
    sd t6,  120(sp)

    call handle_trap

    ld ra,    0(sp)
    ld t0,    8(sp)
    ld t1,   16(sp)
    ld t2,   24(sp)
    ld a0,   32(sp)
    ld a1,   40(sp)
    ld a2,   48(sp)
    ld a3,   56(sp)
    ld a4,   64(sp)
    ld a5,   72(sp)
    ld a6,   80(sp)
    ld a7,   88(sp)
    ld t3,   96(sp)
    ld t4,  104(sp)
    ld t5,  112(sp)
    ld t6,  120(sp)
    addi sp, sp, 128

    # sret restores sstatus.SIE from sstatus.SPIE and returns to sepc, resuming
    # exactly where the interrupted code left off.
    sret
