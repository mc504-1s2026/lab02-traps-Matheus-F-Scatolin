#include <kernel/printf.h>
#include <kernel/mm.h>
#include <arch/timer.h>
#include <kernel/trap.h>
#include <kernel/serial.h>
#include <kernel/string.h>

#define PROMPT		"> "
#define LINE_MAX	256

/* hint the hart that it can idle until the next interrupt arrives */
static inline void hart_wait_for_irq()
{
	__asm__ __volatile__("wfi" ::: "memory");
}

/*
 * shell_exec(): run one command line
 *
 * The line is split into a command word and the rest of the line (its
 * argument). Supported commands:
 *   uptime       - seconds since boot
 *   echo [str]   - print [str] back
 *   alarm [secs] - print "alarm" [secs] seconds from now (via a timer irq)
 */
static void shell_exec(char *line)
{
	char *cmd, *args;
	char buf[32];

	/* skip leading spaces and isolate the command word */
	cmd = line;
	while (*cmd == ' ')
		cmd++;
	args = cmd;
	while (*args != '\0' && *args != ' ')
		args++;
	if (*args != '\0')
		*args++ = '\0';		/* terminate the command word */
	while (*args == ' ')		/* skip spaces before the argument */
		args++;

	if (*cmd == '\0') {
		/* empty line, nothing to do */
		return;
	} else if (strcmp(cmd, "uptime") == 0) {
		snprintf(buf, sizeof(buf), "%lus\r\n", timer_uptime());
		serial_puts(buf);
	} else if (strcmp(cmd, "echo") == 0) {
		serial_puts(args);
		serial_puts("\r\n");
	} else if (strcmp(cmd, "alarm") == 0) {
		timer_set_alarm(strtou64(args, 10));
	} else {
		serial_puts("unknown command: ");
		serial_puts(cmd);
		serial_puts("\r\n");
	}
}

/*
 * shell_run(): the interactive shell loop
 *
 * Bytes received over the serial port are buffered asynchronously by
 * serial_irq(); here we drain that buffer, echo and line-edit the input, and
 * execute a command whenever the user presses Enter ('\r'). Alarms scheduled
 * with the `alarm` command are reported here too, once their timer interrupt
 * has fired. When there's nothing to do the hart sleeps in wfi().
 */
static void shell_run()
{
	char line[LINE_MAX];
	size_t line_len = 0;
	char rx[SERIAL_BUF_SIZE];

	serial_puts(PROMPT);

	for (;;) {
		/* report alarms whose timer has fired */
		u32 fired = timer_alarm_poll();
		if (fired > 0) {
			serial_puts("\r\n");
			while (fired-- > 0)
				serial_puts("alarm\r\n");
			/* redraw the prompt and anything half-typed */
			serial_puts(PROMPT);
			for (size_t i = 0; i < line_len; i++)
				serial_putc(line[i]);
		}

		/* drain and handle everything the serial irq has buffered */
		size_t got = serial_read(rx);
		for (size_t i = 0; i < got; i++) {
			char c = rx[i];

			if (c == '\r') {
				/* Enter: finish the line and run it */
				serial_puts("\r\n");
				line[line_len] = '\0';
				shell_exec(line);
				line_len = 0;
				serial_puts(PROMPT);
			} else if (c == '\n') {
				/* '\r' already terminates lines; ignore '\n' */
			} else if (c == 0x7f || c == 0x08) {
				/* Backspace / Delete */
				if (line_len > 0) {
					line_len--;
					serial_puts("\b \b");
				}
			} else if (c >= 0x20 && c < 0x7f) {
				/* printable character: buffer and echo it */
				if (line_len < LINE_MAX - 1) {
					line[line_len++] = c;
					serial_putc(c);
				}
			}
			/* other control characters are ignored */
		}

		/*
		 * Idle until the next interrupt. Disable interrupts and
		 * re-check for work first: if an interrupt fires in this window
		 * it stays pending and wfi() returns immediately (it wakes on a
		 * pending enabled interrupt even while sstatus[SIE] is clear),
		 * so no wakeup is ever lost.
		 */
		hart_irq_disable();
		if (!serial_has_data() && !timer_alarm_pending())
			hart_wait_for_irq();
		hart_irq_enable();
	}
}

extern int _hartid[];
void kmain()
{
	printk_set_level(LOG_DEBUG);
	info("entered S-mode\n");
	info("booting on hart %d\n", _hartid[0]);
	info("setting up virtual memory...\n");
	vm_init();

	info("enabling traps...\n");
	trap_setup();
	info("enabling timer...\n");
	timer_irq_enable();
	info("enabling serial...\n");
	serial_init();
	serial_irq_enable();

	/* everything is configured: let the hart start taking interrupts */
	hart_irq_enable();

	info("starting shell\n");
	shell_run();
}
