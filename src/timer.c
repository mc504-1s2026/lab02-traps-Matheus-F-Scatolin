#include <arch/timer.h>
#include <arch/csr.h>
#include <arch/spinlock.h>
#include <kernel/bits.h>

/*
 * Timer / alarm driver.
 *
 * The hardware gives us a single comparator (stimecmp): a timer interrupt fires
 * once when `time` reaches `stimecmp`. To support the shell's `alarm` command
 * (and potentially several pending alarms at once) we keep a small table of
 * absolute tick deadlines and always program stimecmp for the *soonest* one.
 * When it fires we mark every due alarm as fired and re-arm for the next.
 *
 * The table is touched both from interrupt context (timer_irq) and from the
 * shell (timer_set_alarm / timer_alarm_poll), so it's guarded by a spinlock.
 */

#define MAX_ALARMS	16

struct alarm {
	u64 deadline;	/* absolute tick value at which it should fire */
	bool active;
};

static struct alarm alarms[MAX_ALARMS];
static u32 alarms_fired;		/* fired but not yet reported to the shell */
static struct spinlock timer_lock;

u64 timer_read()
{
	return csr_read(CSR_TIME);
}

u64 timer_uptime()
{
	return timer_read() / TIMER_FREQ;
}

/*
 * timer_arm_next(): program stimecmp for the earliest pending alarm
 *
 * Must be called with timer_lock held. If no alarm is pending we push the
 * comparator out to the far future, which effectively disarms the timer and
 * also clears any currently-pending timer interrupt (writing stimecmp > time
 * deasserts sip[STIP]).
 */
static void timer_arm_next()
{
	u64 soonest = UINT64_MAX;

	for (size_t i = 0; i < MAX_ALARMS; i++) {
		if (alarms[i].active && alarms[i].deadline < soonest)
			soonest = alarms[i].deadline;
	}

	csr_write(CSR_STIMECMP, soonest);
}

void timer_irq_enable()
{
	/* stimecmp resets to 0, so arming the interrupt naively would fire it
	 * immediately (time >= 0). Disarm first, then enable the source. */
	csr_write(CSR_STIMECMP, UINT64_MAX);
	csr_set(CSR_SIE, CSR_SIE_STIE);
}

void timer_irq_disable()
{
	csr_clear(CSR_SIE, CSR_SIE_STIE);
}

void timer_set_alarm(u64 secs)
{
	u64 deadline = timer_read() + secs * TIMER_FREQ;
	u64 flags = spin_lock_irqsave(&timer_lock);

	for (size_t i = 0; i < MAX_ALARMS; i++) {
		if (!alarms[i].active) {
			alarms[i].deadline = deadline;
			alarms[i].active = true;
			break;
		}
	}

	timer_arm_next();
	spin_unlock_irqrestore(&timer_lock, flags);
}

void timer_irq()
{
	/* runs in interrupt context: the hart already cleared sstatus[SIE] for
	 * us, so a plain spin_lock() is enough to exclude the shell */
	u64 now = timer_read();

	spin_lock(&timer_lock);
	for (size_t i = 0; i < MAX_ALARMS; i++) {
		if (alarms[i].active && now >= alarms[i].deadline) {
			alarms[i].active = false;
			alarms_fired++;
		}
	}
	/* re-arm for whatever is left; this also acks the interrupt */
	timer_arm_next();
	spin_unlock(&timer_lock);
}

u32 timer_alarm_poll()
{
	u64 flags = spin_lock_irqsave(&timer_lock);
	u32 fired = alarms_fired;
	alarms_fired = 0;
	spin_unlock_irqrestore(&timer_lock, flags);
	return fired;
}

bool timer_alarm_pending()
{
	u64 flags = spin_lock_irqsave(&timer_lock);
	bool pending = alarms_fired > 0;
	spin_unlock_irqrestore(&timer_lock, flags);
	return pending;
}
