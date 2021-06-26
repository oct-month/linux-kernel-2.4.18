#define KERNEL
#include <linux/config.h>
#include <linux/types.h>
#include <linux/ctype.h>	/* for isdigit() and friends */
#include <linux/fs.h>
#include <linux/mm.h>		/* for verify_area */
#include <linux/errno.h>	/* for -EBUSY */
#include <linux/ioport.h>	/* for check_region, request_region */
#include <linux/delay.h>	/* for loops_per_sec */
#include <asm/segment.h>	/* for put_user_byte */
#include <asm/io.h>		/* for inb_p, outb_p, inb, outb, etc... */
#include <linux/wait.h>		/* for wait_queue */
#include <linux/vt_kern.h>	/* kd_mksound */
#include <linux/init.h>		/* for __init */
#include <linux/version.h>
#include <linux/serial.h>	/* for rs_table, serial constants & 
				   serial_uart_config */
#include <linux/serial_reg.h>	/* for more serial constants */
#if (LINUX_VERSION_CODE >= 0x20300)	/* v 2.3.x */
#include <linux/serialP.h>	/* for struct serial_state */
#endif
#include <asm/serial.h>
#include <linux/speakup.h>

#define synthBufferSize 8192	/* currently 8K bytes */
struct spk_synth *synth = NULL;
unsigned char synth_jiffy_delta = HZ/20;
int synth_port_tts = 0;
volatile int synth_timer_active = 0;	/* indicates when a timer is set */
#if (LINUX_VERSION_CODE < 0x20300)	/* is it a 2.2.x kernel? */
struct wait_queue *synth_sleeping_list = NULL;
#else				/* nope it's 2.3.x */
DECLARE_WAIT_QUEUE_HEAD (synth_sleeping_list);
#endif
struct timer_list synth_timer;
unsigned short synth_delay_time = 500;	/* time to schedule handler */
unsigned short synth_trigger_time = 50;
unsigned short synth_full_time = 1000;	/* time before coming back when synth is full */
char synth_buffering = 1;	/* flag to indicate we're buffering */
unsigned char synth_buffer[synthBufferSize];	/* guess what this is for! */
unsigned short synth_end_of_buffer = synthBufferSize;
volatile unsigned short synth_queued_bytes = 0, synth_sent_bytes = 0;
/* synth_queued_bytes points to top of chars queued,
 * synth_sent_bytes points at top of bytes sent by synth->catch_up() */

void
initialize_uart(struct serial_state *ser)
{
	int baud = 9600;
	int quot = 0;
	unsigned cval = 0;
	int     cflag = CREAD | HUPCL | CLOCAL | B9600 | CS8;

	/*
	 *	Divisor, bytesize and parity
	 */
	quot = ser->baud_base / baud;
	cval = cflag & (CSIZE | CSTOPB);
#if defined(__powerpc__) || defined(__alpha__)
	cval >>= 8;
#else /* !__powerpc__ && !__alpha__ */
	cval >>= 4;
#endif /* !__powerpc__ && !__alpha__ */
	if (cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(cflag & PARODD))
		cval |= UART_LCR_EPAR;

	/*
	 *	Disable UART interrupts, set DTR and RTS high
	 *	and set speed.
	 */
	outb(cval | UART_LCR_DLAB, ser->port + UART_LCR);	/* set DLAB */
	outb(quot & 0xff, ser->port + UART_DLL);	/* LS of divisor */
	outb(quot >> 8, ser->port + UART_DLM);		/* MS of divisor */
	outb(cval, ser->port + UART_LCR);		/* reset DLAB */
	outb(0, ser->port + UART_IER);
	outb(UART_MCR_DTR | UART_MCR_RTS, ser->port + UART_MCR);
}

/* sleep for ms milliseconds */
void
synth_delay (int ms)
{
	synth_timer.expires = jiffies + (ms * HZ + 1000 - HZ) / 1000;
# if (LINUX_VERSION_CODE >= 0x20300)	/* is it a 2.3.x kernel? */
	if (!synth_timer.list.prev)
		add_timer (&synth_timer);
# else
	if (!synth_timer.prev)
		add_timer (&synth_timer);
# endif
	synth_timer_active++;
}

void
synth_stop_timer (void)
{
	if (synth_timer_active)
		del_timer (&synth_timer);
}

void
synth_buffer_add (char ch)
{
	/* someone needs to take a nap for a while. */
	if (synth_queued_bytes + 1 == synth_end_of_buffer - 100
	    && (!waitqueue_active (&synth_sleeping_list))) {
		interruptible_sleep_on (&synth_sleeping_list);
	}
	*(synth_buffer + synth_queued_bytes++) = ch;
}

void
synth_write (const char *buf, size_t count)
{
	while (count--)
		synth->write (*buf++);
}

#if LINUX_VERSION_CODE >= 0x20300
static struct resource synth_res;
#endif

int __init
synth_request_region (unsigned long start, unsigned long n)
{
#if (LINUX_VERSION_CODE >= 0x20300)	/* is it a 2.3.x kernel? */
	struct resource *parent = &ioport_resource;

	memset (&synth_res, 0, sizeof (synth_res));
	synth_res.name = synth->name;
	synth_res.start = start;
	synth_res.end = start + n - 1;
	synth_res.flags = IORESOURCE_BUSY;

	return request_resource (parent, &synth_res);
#else
	if (check_region (start, n))
		return -EBUSY;
	request_region (start, n, synth->name);
	return 0;
#endif
}

int __init
synth_release_region (unsigned long start, unsigned long n)
{
#if (LINUX_VERSION_CODE >= 0x20300)
	return release_resource (&synth_res);
#else
	release_region (start, n);
	return 0;
#endif
}

#ifdef CONFIG_PROC_FS

// /proc/synth-specific code

#include <linux/proc_fs.h>
#include <asm/uaccess.h>
// #include <limits.h>

#define UCHAR_MAX 255
#define USHRT_MAX 65535

static struct proc_dir_entry *proc_speakup_synth;

// this is the read handler for /proc/speakup/synth-specific vars
static int
speakup_vars_read_proc (char *page, char **start, off_t off,
			int count, int *eof, void *data)
{
	struct proc_dir_entry *ent = data;

	*start = 0;
	*eof = 1;

	if (!strcmp (ent->name, "jiffy_delta"))
		return sprintf (page, "%d\n", synth_jiffy_delta);
	if (!strcmp (ent->name, "delay_time"))
		return sprintf (page, "%d\n", synth_delay_time);
	if (!strcmp (ent->name, "queued_bytes"))
		return sprintf (page, "%d\n", synth_queued_bytes);
	if (!strcmp (ent->name, "sent_bytes"))
		return sprintf (page, "%d\n", synth_sent_bytes);
	if (!strcmp (ent->name, "trigger_time"))
		return sprintf (page, "%d\n", synth_trigger_time);
	if (!strcmp (ent->name, "full_time"))
		return sprintf (page, "%d\n", synth_full_time);
	if (!strcmp (ent->name, "version"))
		return sprintf (page, "%s\n", synth->version);

	return sprintf (page, "%s slipped through the cracks!\n", ent->name);
}

#define RESET_DEFAULT -4

// this is the write handler for /proc/speakup/synth-specific vars
static int
speakup_vars_write_proc (struct file *file, const char *buffer,
			 unsigned long count, void *data)
{
	struct proc_dir_entry *ent = data;
	int i, len = count, val = 0, ret = -ERANGE;
	long min = 0, max = 0;
	char *page;

	if (!(page = (char *) __get_free_page (GFP_KERNEL)))
		return -ENOMEM;
	if (copy_from_user (page, buffer, count)) {
		ret = -EFAULT;
		goto out;
	}
	// lose the newline
	if (page[len - 1] == '\n')
		--len;

	// trigger_time is an unsigned short 0 <= val <= USHRT_MAX
	if (!strcmp (ent->name, "trigger_time")) {
		if (!len) {
			synth_trigger_time = synth->trigger_time;
			ret = RESET_DEFAULT;
			goto out;
		}
		max = USHRT_MAX;
		// 1 to 5 digits
		if (len < 1 || len > 5)
			goto out;
		for (i = 0; i < len; ++i) {
			if (!isdigit (page[i]))
				goto out;
			val *= 10;
			val += page[i] - '0';
		}
		if (val < min || val > max)
			goto out;
		synth_trigger_time = val;
		ret = count;
	}
	// full_time is an unsigned short 0 <= val <= USHRT_MAX
	else if (!strcmp (ent->name, "full_time")) {
		if (!len) {
			synth_full_time = synth->full_time;
			ret = RESET_DEFAULT;
			goto out;
		}
		max = USHRT_MAX;
		// 1 to 5 digits
		if (len < 1 || len > 5)
			goto out;
		for (i = 0; i < len; ++i) {
			if (!isdigit (page[i]))
				goto out;
			val *= 10;
			val += page[i] - '0';
		}
		if (val < min || val > max)
			goto out;
		synth_full_time = val;
		ret = count;
	}
	// jiffy_delta is an unsigned char 0 <= val <= UCHAR_MAX
	else if (!strcmp (ent->name, "jiffy_delta")) {
		if (!len) {
			synth_jiffy_delta = synth->jiffy_delta;
			ret = RESET_DEFAULT;
			goto out;
		}
		max = UCHAR_MAX;
		// 1 to 3 digits
		if (len < 1 || len > 3)
			goto out;
		for (i = 0; i < len; ++i) {
			if (!isdigit (page[i]))
				goto out;
			val *= 10;
			val += page[i] - '0';
		}
		if (val < min || val > max)
			goto out;
		synth_jiffy_delta = val;
		ret = count;
	}
	// delay_time is an unsigned short 0 <= val <= USHRT_MAX
	else if (!strcmp (ent->name, "delay_time")) {
		if (!len) {
			synth_delay_time = synth->delay_time;
			ret = RESET_DEFAULT;
			goto out;
		}
		max = USHRT_MAX;
		// 1 to 5 digits
		if (len < 1 || len > 5)
			goto out;
		for (i = 0; i < len; ++i) {
			if (!isdigit (page[i]))
				goto out;
			val *= 10;
			val += page[i] - '0';
		}
		if (val < min || val > max)
			goto out;
		synth_delay_time = val;
		ret = count;
	}

      out:
	if (ret == -ERANGE) {
		printk (KERN_ALERT
			"setting %s (%.*s): value out of range (%ld-%ld)\n",
			ent->name, len, page, min, max);
		ret = count;
	} else if (ret == RESET_DEFAULT) {
		printk (KERN_ALERT "%s reset to default value\n", ent->name);
		ret = count;
	}
	free_page ((unsigned long) page);
	return ret;
}

#define MIN(a,b) ( ((a) < (b))?(a):(b) )

// this is the write handler for /proc/speakup/synth-specific/direct
static int
speakup_direct_write_proc (struct file *file, const char *buffer,
			   unsigned long count, void *data)
{
	static const int max_buf_len = 100;
	unsigned char buf[max_buf_len + 1];
	int ret = count;
	extern char *xlate (char *);

	if (copy_from_user (buf, buffer, count = (MIN (count, max_buf_len))))
		return -EFAULT;
	buf[count] = '\0';
	xlate (buf);
	synth_write (buf, strlen (buf));
	return ret;
}

char *read_only[] = {
	"queued_bytes", "sent_bytes", "version", NULL
};

char *root_writable[] = {
	"delay_time", "full_time", "jiffy_delta", "trigger_time", NULL
};

// called by proc_speakup_init() to initialize the /proc/speakup/synth-specific subtree
void
proc_speakup_synth_init (void)
{
	struct proc_dir_entry *ent;
	char *dir = "speakup/";
	char path[80];
	int i;

	sprintf (path, "%s%s", dir, synth->proc_name);
	proc_speakup_synth = create_proc_entry (path, S_IFDIR, 0);
	if (!proc_speakup_synth) {
		printk (KERN_WARNING "Unable to create /proc/%s entry.\n",
			path);
		return;
	}

	for (i = 0; read_only[i]; ++i) {
		sprintf (path, "%s%s/%s", dir, synth->proc_name, read_only[i]);
		ent = create_proc_entry (path, S_IFREG | S_IRUGO, 0);
		ent->read_proc = speakup_vars_read_proc;
		ent->data = (void *) ent;
	}

	for (i = 0; root_writable[i]; ++i) {
		sprintf (path, "%s%s/%s", dir, synth->proc_name, root_writable[i]);
		ent = create_proc_entry (path, S_IFREG | S_IRUGO | S_IWUSR, 0);
		ent->read_proc = speakup_vars_read_proc;
		ent->write_proc = speakup_vars_write_proc;
		ent->data = (void *) ent;
	}

	sprintf (path, "%s%s/direct", dir, synth->proc_name);
	ent = create_proc_entry (path, S_IFREG | S_IWUGO, 0);
	ent->write_proc = speakup_direct_write_proc;
}

#endif

int __init
synth_init (void)
{
	if (synth->probe () < 0) {
		printk ("%s: device probe failed\n", synth->name);
		return -ENODEV;
	}
	init_timer (&synth_timer);
	synth_timer.function = synth->catch_up;
#if (LINUX_VERSION_CODE >= 0x20300)	/* it's a 2.3.x kernel */
	init_waitqueue_head (&synth_sleeping_list);
#else				/* it's a 2.2.x kernel */
	init_waitqueue (&synth_sleeping_list);
#endif
	synth_delay_time = synth->delay_time;
	synth_trigger_time = synth->trigger_time;
	synth_jiffy_delta = synth->jiffy_delta;
	synth_full_time = synth->full_time;
	return 0;
}
