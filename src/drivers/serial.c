#include <kernel/serial.h>
#include <arch/csr.h>
#include <arch/io.h>
#include <arch/plic.h>
#include <arch/spinlock.h>

/*
 * Driver for the NS16550 UART that QEMU exposes at physical 0x10000000 (which
 * the kernel page tables map 1:1, so SERIAL_BASE is directly dereferenceable).
 *
 * Receiving is interrupt driven: when bytes arrive the UART raises its IRQ,
 * the PLIC routes it to us, and serial_irq() drains the hardware FIFO into a
 * software buffer. The shell later copies that buffer out with serial_read().
 * Because serial_irq() (interrupt context) and serial_read() (shell context)
 * both touch the buffer, it is protected by a spinlock.
 */

#define BOOT_HART	0u

static struct serialdev {
	char buf[SERIAL_BUF_SIZE];
	size_t len;
	struct spinlock lock;
} dev;

static inline u8 serial_reg_read(u64 reg)
{
	return ioread8(SERIAL_BASE + reg);
}

static inline void serial_reg_write(u64 reg, u8 val)
{
	iowrite8(val, SERIAL_BASE + reg);
}

void serial_init()
{
	spin_init(&dev.lock);
	dev.len = 0;

	/* 8 data bits, no parity, 1 stop bit (8N1). This also leaves DLAB=0 so
	 * that offset 0 is the RX/TX register and offset 1 is the IER. */
	serial_reg_write(SERIAL_LCR, 0x03);

	/* turn the hardware FIFOs on and flush whatever is in them; with FIFOs
	 * enabled the UART can hand us several bytes per interrupt */
	serial_reg_write(SERIAL_FCR, SERIAL_FCR_FIFO_ENABLE |
				     SERIAL_FCR_RX_FIFO_CLEAR |
				     SERIAL_FCR_TX_FIFO_CLEAR);

	/* fire an interrupt whenever received data becomes available */
	serial_reg_write(SERIAL_IER, SERIAL_IER_ERBFI);
}

void serial_irq_enable()
{
	/* route the UART's IRQ through the PLIC to this hart:
	 *  - give it a non-zero priority (0 means "never interrupt")
	 *  - drop the hart threshold to 0 so any non-zero priority gets through
	 *  - enable this specific IRQ line for the hart */
	plic_irq_set_priority((u32)IRQ_SERIAL, 1);
	plic_hart_set_threshold(BOOT_HART, 0);
	plic_hart_enable_irq(BOOT_HART, (u32)IRQ_SERIAL);

	/* finally allow the hart to take supervisor external interrupts */
	csr_set(CSR_SIE, CSR_SIE_SEIE);
}

void serial_irq_disable()
{
	csr_clear(CSR_SIE, CSR_SIE_SEIE);
}

void serial_irq()
{
	/* interrupts are already disabled in trap context, so a plain
	 * spin_lock() is sufficient to exclude serial_read() */
	spin_lock(&dev.lock);

	/* drain the RX FIFO completely; reading RBR is what clears the
	 * "data available" condition that raised the interrupt */
	while (serial_reg_read(SERIAL_LSR) & SERIAL_LSR_DTR) {
		char c = (char)serial_reg_read(SERIAL_RBR);
		if (dev.len < SERIAL_BUF_SIZE)
			dev.buf[dev.len++] = c;
		/* otherwise the buffer is full and we drop the byte */
	}

	spin_unlock(&dev.lock);
}

size_t serial_read(char *buf)
{
	u64 flags = spin_lock_irqsave(&dev.lock);

	size_t len = dev.len;
	for (size_t i = 0; i < len; i++)
		buf[i] = dev.buf[i];
	dev.len = 0;

	spin_unlock_irqrestore(&dev.lock, flags);
	return len;
}

bool serial_has_data()
{
	u64 flags = spin_lock_irqsave(&dev.lock);
	bool has_data = dev.len > 0;
	spin_unlock_irqrestore(&dev.lock, flags);
	return has_data;
}

void serial_putc(char c)
{
	/* wait until the transmitter holding register is empty before pushing
	 * the next byte, so we never overrun the UART */
	while (!(serial_reg_read(SERIAL_LSR) & SERIAL_LSR_THRE))
		;
	serial_reg_write(SERIAL_THR, (u8)c);
}

void serial_puts(const char *str)
{
	while (*str != '\0')
		serial_putc(*str++);
}
