#ifndef __SERIAL_H__
#define __SERIAL_H__

#include <kernel/types.h>

/* this IRQ number is not magical: it's extracted from the device tree,
 * which describes the hardware that the QEMU virtual machine is emulating.
 *
 * get the device tree by running:
 * $ qemu-system-riscv64 -machine virt,dumpdtb=virt.dtb && dtc -qI dtb virt.dtb
 *
 * then look for the node "serial@10000000", which has an "interrupts" property
 */
#define IRQ_SERIAL	0xaUL

#define SERIAL_BASE	((void*)0x10000000)

/* size of the driver's internal receive buffer (bytes) */
#define SERIAL_BUF_SIZE	256

/* https://courses.grainger.illinois.edu/ece391/su2025/docs/NS16550A.pdf */
/* Receiver Buffer Register (read-only) */
#define SERIAL_RBR		0x0UL
/* Transmitter Holding Register (write-only) */
#define SERIAL_THR		0x0UL
/* Interrupt Enable Register */

#define SERIAL_IER		0x1UL
/* Enable Received Data Available Interrupt */
#define SERIAL_IER_ERBFI	(1UL << 0)
/* Enable Transmitter Holding Register Interrupt */
#define SERIAL_IER_ETBEI	(1UL << 1)
/* Enable Receiver Line Status Interrupt */
#define SERIAL_IER_ELSI		(1UL << 2)
/* Enable Modem Status Interrupt */
#define SERIAL_IER_EDSSI	(1UL << 3)

/* Interrupt Identification Register (read-only) */
#define SERIAL_IIR		0x2UL
/* Interrupt Pending */
#define SERIAL_IIR_PENDING	(1UL << 0)
/* ID of the highest priority pending interrupt */
/* 0b0000 (Highest) Receiver Line Status:
 *	- Source: overrun/parity/framing/errors or break interrupt
 *	- Clear: reading the line status register
 * 0b0110 (Second) Received Data Available:
 *	- Source: receiver data available or FIFO trigger level reached
 *	- Clear: reading SERIAL_RBR or getting FIFO to drop below trigger
 * 0b1100 (Second) Character Timeout Indication
 */
#define SERIAL_IIR_ID0		(1UL << 1)
#define SERIAL_IIR_ID1		(1UL << 2)
#define SERIAL_IIR_ID2		(1UL << 3)
#define SERIAL_IIR_FIFO_EN0	(1UL << 6)
#define SERIAL_IIR_FIFO_EN1	(1UL << 7)
#define SERIAL_IIR_ID_MASK	(SERIAL_IIR_ID0 | SERIAL_IIR_ID1 | SERIAL_IIR_ID2)
#define SERIAL_IIR_ID_SHIFT	1

/* FIFO Control Register (write-only) */
#define SERIAL_FCR			0x2UL
#define SERIAL_FCR_FIFO_ENABLE		(1UL << 0)
#define SERIAL_FCR_RX_FIFO_CLEAR	(1UL << 1)
#define SERIAL_FCR_TX_FIFO_CLEAR	(1UL << 2)

/* Line Control Register */
#define SERIAL_LCR	0x3UL
/* MODEM Control Register */
#define SERIAL_MCR	0x4UL

/* Line Status Register */
#define SERIAL_LSR	0x5UL

/* Data Ready */
#define SERIAL_LSR_DTR		(1UL << 0)
/* Overrun Error */
#define SERIAL_LSR_OE		(1UL << 1)
/* Parity Error */
#define SERIAL_LSR_PE		(1UL << 2)
/* Framing Error */
#define SERIAL_LSR_FE		(1UL << 3)
/* Break Interrupt */
#define SERIAL_LSR_BI		(1UL << 4)
/* Transmitter Holding Register */
#define SERIAL_LSR_THRE		(1UL << 5)
/* Transmitter Empty */
#define SERIAL_LSR_TEMT		(1UL << 6)
/* Error in RCVR FIFO */
#define SERIAL_LSR_RXERR	(1UL << 7)

/* MODEM Status Register */
#define SERIAL_MSR	0x6UL
/* Scratch Register */
#define SERIAL_SCR	0x7UL

/*
 * serial_init(): initialize the serial port driver
 *
 * Initialize the internal state for the serial port device driver. This should
 * be called before all other serial_* functions.
 */
void serial_init();

/*
 * serial_read(): read all available serial data.
 * @buf: buffer to write the data into.
 *
 * This will flush the driver's internal buffer; @buf is assumed to be
 * large enough to fit the data. This is obviously very wrong and very
 * dangerous but we will implement a ring buffer inthe driver so that
 * we can do serial_read(char *buf, size_t len) instead.
 */
size_t serial_read(char *buf);

/*
 * serial_has_data(): check whether the receive buffer holds unread data
 *
 * Non-destructive peek, used by the shell to decide whether it can sleep.
 */
bool serial_has_data();

/*
 * serial_putc(): write a character to the serial port
 * @s: character to be sent out
 */
void serial_putc(char c);

/*
 * serial_puts(): write a string to the serial port
 * @s: string to be sent out
 */
void serial_puts(const char *s);

/*
 * serial_irq(): the interrupt handler for the serial device
 */
void serial_irq();

/*
 * serial_irq_enable(): enable the serial device interrupt
 */
void serial_irq_enable();

/*
 * serial_irq_enable(): disable the serial device interrupt
 */
void serial_irq_disable();

#endif
