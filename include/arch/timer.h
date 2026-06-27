#ifndef __TICK_H__
#define __TICK_H__

#include <kernel/types.h>

/* the `time` CSR counts at a fixed 10MHz on the QEMU virt machine */
#define TIMER_FREQ 10000000

/* enable/disable the supervisor timer interrupt source (sie[STIE]) */
void timer_irq_enable();
void timer_irq_disable();

/* timer interrupt handler, called from the trap dispatcher */
void timer_irq();

/* read the raw monotonic tick counter (the `time` CSR) */
u64 timer_read();

/* seconds elapsed since the hart booted (time starts at 0 on reset) */
u64 timer_uptime();

/* schedule an alarm to fire @secs seconds from now */
void timer_set_alarm(u64 secs);

/*
 * timer_alarm_poll(): consume alarms that have fired
 *
 * Returns the number of alarms that have fired since the last call and resets
 * the counter. Meant to be called from non-interrupt context (e.g. the shell
 * loop), which is responsible for actually reporting them.
 */
u32 timer_alarm_poll();

/*
 * timer_alarm_pending(): check whether any fired alarm is waiting to be polled
 *
 * Non-destructive peek used to decide whether the hart can go to sleep.
 */
bool timer_alarm_pending();

#endif
