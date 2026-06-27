#include <kernel/trap.h>
#include <kernel/panic.h>
#include <kernel/printf.h>
#include <kernel/serial.h>
#include <arch/csr.h>
#include <arch/timer.h>
#include <arch/plic.h>

/* defined in src/trap_entry.S */
extern void trap_entry();

/* we have not implemented SMP yet, so everything runs on hart 0 */
#define BOOT_HART	0u

/*
 * handle_exception(): handle a synchronous trap (i.e. an exception)
 *
 * We don't expect any exceptions during normal operation, so we just pretty
 * print whatever happened (this is invaluable when debugging page faults) and
 * panic. @stval holds the faulting address for access/page faults; @sepc holds
 * the address of the offending instruction.
 */
static void handle_exception(u64 scause)
{
	u64 stval = csr_read(CSR_STVAL);
	u64 sepc = csr_read(CSR_SEPC);

	switch (scause) {
	case EXCEPTION_INST_ACCESS_FAULT:
		error("instruction access fault at %p (sepc=%p)\n", stval, sepc);
		break;
	case EXCEPTION_LOAD_ACCESS_FAULT:
		error("load access fault at %p (sepc=%p)\n", stval, sepc);
		break;
	case EXCEPTION_STORE_ACCESS_FAULT:
		error("store access fault at %p (sepc=%p)\n", stval, sepc);
		break;
	case EXCEPTION_INST_PAGE_FAULT:
		error("instruction page fault at %p (sepc=%p)\n", stval, sepc);
		break;
	case EXCEPTION_LOAD_PAGE_FAULT:
		error("load page fault at %p (sepc=%p)\n", stval, sepc);
		break;
	case EXCEPTION_STORE_PAGE_FAULT:
		error("store page fault at %p (sepc=%p)\n", stval, sepc);
		break;
	default:
		error("unhandled exception: scause=%lu (sepc=%p, stval=%p)\n",
		      scause, sepc, stval);
	}

	panic("unrecoverable exception\n");
}

/*
 * handle_irq(): handle an asynchronous trap (i.e. an interrupt)
 *
 * Timer interrupts are serviced directly. External interrupts come from a
 * device and have to be routed through the PLIC: we claim the IRQ, dispatch it
 * to the right device handler and then tell the PLIC we're done.
 */
static void handle_irq(u64 scause)
{
	u32 irq;

	switch (scause) {
	case TRAP_TIMER_IRQ:
		timer_irq();
		break;
	case TRAP_EXTERNAL_IRQ:
		/* ask the PLIC which device interrupted us. A return of 0
		 * means there was nothing to claim (another hart beat us to
		 * it), so there's nothing to complete either. */
		irq = plic_hart_claim_irq(BOOT_HART);
		if (irq == 0)
			break;

		if (irq == (u32)IRQ_SERIAL)
			serial_irq();
		else
			warn("unhandled external irq: %u\n", irq);

		/* let the PLIC know we finished servicing this interrupt so it
		 * can deliver the next one */
		plic_hart_complete_irq(BOOT_HART, irq);
		break;
	default:
		warn("unhandled interrupt: scause=%p\n", scause);
	}
}

/*
 * handle_trap(): C entry point for every trap, called from trap_entry
 *
 * Bit 63 of scause distinguishes interrupts (asynchronous) from exceptions
 * (synchronous); we use it to fork to the appropriate handler.
 */
void handle_trap()
{
	u64 scause = csr_read(CSR_SCAUSE);

	if (scause & TRAP_IRQ_BIT)
		handle_irq(scause);
	else
		handle_exception(scause);
}

void trap_setup()
{
	/* point the hart at our trap handler. stvec[1:0] = 0 selects "Direct"
	 * mode, where every trap jumps to this single address; trap_entry is
	 * 4-byte aligned so the low bits are already zero. */
	csr_write(CSR_STVEC, (u64)trap_entry);

	/* keep interrupts globally disabled until the timer/serial sources have
	 * been configured; kmain re-enables them with hart_irq_enable() */
	hart_irq_disable();
}

void hart_irq_enable()
{
	csr_set(CSR_SSTATUS, CSR_SSTATUS_SIE);
}

void hart_irq_disable()
{
	csr_clear(CSR_SSTATUS, CSR_SSTATUS_SIE);
}

u64 hart_irq_save()
{
	/* atomically read sstatus and clear the SIE bit, returning the previous
	 * SIE state so it can be handed to hart_irq_restore() later */
	u64 sstatus = csr_read_clear(CSR_SSTATUS, CSR_SSTATUS_SIE);
	return sstatus & CSR_SSTATUS_SIE;
}

void hart_irq_restore(u64 flags)
{
	/* only re-enable interrupts if they were enabled when the matching
	 * hart_irq_save() ran; this keeps nested critical sections correct */
	if (flags & CSR_SSTATUS_SIE)
		csr_set(CSR_SSTATUS, CSR_SSTATUS_SIE);
}
