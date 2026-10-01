/*
 * parking_sensor.c - Virtual ultrasonic parking sensor character device.
 *
 * Role in the system: this module is the "device". It does no interpretation
 * of the data. It only maintains a simulated distance value, advances it with
 * a kernel timer, and exposes it to user space through /dev/parking_sensor.
 *
 * Kernel topics used (mandatory core of the project):
 *   - module init/exit, module parameters, MODULE_LICENSE
 *   - printk logging
 *   - character device: alloc_chrdev_region / cdev_init / cdev_add / cdev_del
 *   - device class + device_create (this is what makes /dev/parking_sensor)
 *   - file_operations: open, read, write, release
 *   - copy_to_user / copy_from_user
 *   - kernel timer (timer_setup / mod_timer / del_timer_sync)
 *   - mutex to protect the state shared by the timer callback and syscalls
 *
 * Run:  sudo insmod driver/parking_sensor.ko
 * Test: cat /dev/parking_sensor      (or use the C++ monitor)
 * Stop: sudo rmmod parking_sensor
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/uaccess.h>

#include <linux/version.h>

#include "../include/parking_sensor.h"

#define DRV_NAME PARKING_SENSOR_NAME

/*
 * class_create() changed signature in Linux 6.4: before that it took only the
 * class name, from 6.4 onwards it also takes the owning module. Both spellings
 * are kept so the same source builds on an older lab kernel and a current
 * distribution. Without this the build fails with "too few arguments" on 6.4+.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
#define parking_class_create(name) class_create(name, THIS_MODULE)
#else
#define parking_class_create(name) class_create(name)
#endif

/* ------------------------------------------------------------------ */
/* Module parameters - change behaviour without recompiling           */
/*                                                                   */
/* Declared 0444 (read-only) on purpose. These values are read by the */
/* timer callback without a lock, so a write arriving through sysfs   */
/* while the timer is running would be a data race with no way to     */
/* validate the new range. They are set with `insmod parking_sensor.ko*/
/* interval_ms=1000` instead. Re-insmod to change them.               */
/* ------------------------------------------------------------------ */
static int min_cm = 10;		 /* closest simulated obstacle  */
static int max_cm = 150;		 /* farthest simulated obstacle */
static int step_cm = 10;		 /* movement per timer tick     */
static int start_cm = 150;	 /* initial value               */
static unsigned long interval_ms = 500; /* timer period              */
static int verbose = 1;		 /* 0 = quiet driver              */

module_param(min_cm, int, 0444);
MODULE_PARM_DESC(min_cm, "Lower bound of the simulated distance in cm");
module_param(max_cm, int, 0444);
MODULE_PARM_DESC(max_cm, "Upper bound of the simulated distance in cm");
module_param(step_cm, int, 0444);
MODULE_PARM_DESC(step_cm, "Distance change applied on every timer tick");
module_param(start_cm, int, 0444);
MODULE_PARM_DESC(start_cm, "Distance value when the module is loaded");
module_param(interval_ms, ulong, 0444);
MODULE_PARM_DESC(interval_ms, "Timer period in milliseconds");
module_param(verbose, int, 0444);
MODULE_PARM_DESC(verbose, "1 = print every simulated distance change");

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */
static dev_t devno;			/* allocated major/minor     */
static struct cdev parking_cdev;
static struct class *parking_class;
static struct device *parking_device;
static struct timer_list parking_timer;

/* Protected by parking_lock. The timer callback and the syscalls both
 * touch these fields, so a mutex is the correct primitive here: both
 * run in a context where sleeping is allowed. */
static DEFINE_MUTEX(parking_lock);
static int distance_cm;
static int direction;			/* -1 = approaching, +1 = receding */
static long tick_count;
static int paused;

/* ------------------------------------------------------------------ */
/* Simulation                                                          */
/* ------------------------------------------------------------------ */
static void parking_timer_callback(struct timer_list *unused)
{
	int snapshot_cm;
	long snapshot_tick;

	(void)unused;

	mutex_lock(&parking_lock);
	tick_count++;
	if (!paused) {
		distance_cm += direction * step_cm;

		/* Reflect at both ends of the range: down to min_cm, then back
		 * up to max_cm. This produces SAFE -> WARNING -> DANGER and
		 * the reverse, which is exactly what the state machine needs. */
		if (distance_cm <= min_cm) {
			distance_cm = min_cm;
			direction = 1;
		} else if (distance_cm >= max_cm) {
			distance_cm = max_cm;
			direction = -1;
		}
	}
	/* Snapshot the values for the printk. Reading them after the unlock
	 * would be a data race with parking_read() and parking_write(). */
	snapshot_cm = distance_cm;
	snapshot_tick = tick_count;
	mutex_unlock(&parking_lock);

	if (verbose)
		pr_info(DRV_NAME ": distance = %d cm (tick %ld)\n",
			snapshot_cm, snapshot_tick);

	/* Re-arm for the next tick. mod_timer() is safe to call from the
	 * callback itself; it simply re-programs the same timer. */
	mod_timer(&parking_timer, jiffies + msecs_to_jiffies(interval_ms));
}

static void parking_reset_locked(void)
{
	distance_cm = max_cm;
	direction = -1;
	tick_count = 0;
	paused = 0;
}

/* ------------------------------------------------------------------ */
/* File operations                                                     */
/* ------------------------------------------------------------------ */
static int parking_open(struct inode *inode, struct file *filp)
{
	pr_info(DRV_NAME ": device opened (major %d minor %d)\n",
		MAJOR(inode->i_rdev), MINOR(inode->i_rdev));
	filp->private_data = NULL;
	return 0;
}

static int parking_release(struct inode *inode, struct file *filp)
{
	(void)inode;
	(void)filp;
	pr_info(DRV_NAME ": device closed\n");
	return 0;
}

static ssize_t parking_read(struct file *filp, char __user *buf, size_t count,
			    loff_t *ppos)
{
	struct parking_sensor_data data;

	(void)filp;
	(void)ppos;

	/* Reject a buffer that cannot hold the fixed telemetry record
	 * instead of silently returning a partial value. */
	if (count < sizeof(data))
		return -EINVAL;

	mutex_lock(&parking_lock);
	data.distance_cm = distance_cm;
	data.min_cm = min_cm;
	data.max_cm = max_cm;
	data.step_cm = step_cm;
	data.ticks = (int)tick_count;
	data.paused = paused;
	mutex_unlock(&parking_lock);

	/* The only safe way to move data from kernel space to user space. */
	if (copy_to_user(buf, &data, sizeof(data)))
		return -EFAULT;

	return sizeof(data);
}

static ssize_t parking_write(struct file *filp, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	char cmd[32];
	char clean[32];
	size_t len;

	(void)filp;
	(void)ppos;

	if (count == 0 || count >= sizeof(cmd))
		return -EINVAL;

	/* The only safe way to move data from user space to kernel space. */
	if (copy_from_user(cmd, buf, count))
		return -EFAULT;

	cmd[count] = '\0';

	/* Strip a trailing newline so "reset\n" from the shell works. */
	len = strnlen(cmd, sizeof(clean) - 1);
	while (len > 0 && (cmd[len - 1] == '\n' || cmd[len - 1] == '\r' ||
			   cmd[len - 1] == ' ' || cmd[len - 1] == '\t'))
		len--;
	memcpy(clean, cmd, len);
	clean[len] = '\0';

	if (strcmp(clean, PARKING_SENSOR_CMD_RESET) == 0) {
		mutex_lock(&parking_lock);
		parking_reset_locked();
		mutex_unlock(&parking_lock);
		pr_info(DRV_NAME ": simulation reset by user\n");
		return count;
	}

	if (strcmp(clean, PARKING_SENSOR_CMD_PAUSE) == 0) {
		mutex_lock(&parking_lock);
		paused = 1;
		mutex_unlock(&parking_lock);
		pr_info(DRV_NAME ": simulation paused\n");
		return count;
	}

	if (strcmp(clean, PARKING_SENSOR_CMD_RESUME) == 0) {
		mutex_lock(&parking_lock);
		paused = 0;
		mutex_unlock(&parking_lock);
		pr_info(DRV_NAME ": simulation resumed\n");
		return count;
	}

	/* Controlled rejection: an unknown command must not be ignored. */
	pr_warn(DRV_NAME ": unsupported command '%s'\n", clean);
	return -EINVAL;
}

static const struct file_operations parking_fops = {
	.owner = THIS_MODULE,
	.open = parking_open,
	.read = parking_read,
	.write = parking_write,
	.release = parking_release,
};

/* ------------------------------------------------------------------ */
/* Module init / exit                                                  */
/* ------------------------------------------------------------------ */
static int __init parking_sensor_init(void)
{
	int ret;

	if (min_cm < 0 || max_cm <= min_cm || step_cm <= 0) {
		pr_err(DRV_NAME ": invalid parameters min=%d max=%d step=%d\n",
		       min_cm, max_cm, step_cm);
		return -EINVAL;
	}

	/* A zero period would fire the timer as fast as possible and a huge
	 * one would overflow msecs_to_jiffies(), so both are rejected. */
	if (interval_ms == 0 || interval_ms > 60000) {
		pr_err(DRV_NAME ": interval_ms must be 1..60000, got %lu\n",
		       interval_ms);
		return -EINVAL;
	}

	if (start_cm < min_cm)
		start_cm = min_cm;
	if (start_cm > max_cm)
		start_cm = max_cm;

	distance_cm = start_cm;
	direction = -1;
	tick_count = 0;
	paused = 0;

	/* 1. Ask the kernel for a free major/minor pair. */
	ret = alloc_chrdev_region(&devno, 0, 1, DRV_NAME);
	if (ret < 0) {
		pr_err(DRV_NAME ": alloc_chrdev_region failed (%d)\n", ret);
		return ret;
	}

	/* 2. Bind our file_operations to that device number. */
	cdev_init(&parking_cdev, &parking_fops);
	parking_cdev.owner = THIS_MODULE;

	ret = cdev_add(&parking_cdev, devno, 1);
	if (ret < 0) {
		pr_err(DRV_NAME ": cdev_add failed (%d)\n", ret);
		goto err_region;
	}

	/* 3. Create the class. udev watches this and creates the device
	 *    node /dev/parking_sensor with the right major/minor. */
	parking_class = parking_class_create(PARKING_SENSOR_CLASS);
	if (IS_ERR(parking_class)) {
		ret = PTR_ERR(parking_class);
		pr_err(DRV_NAME ": class_create failed (%d)\n", ret);
		goto err_cdev;
	}

	parking_device = device_create(parking_class, NULL, devno, NULL,
				       PARKING_SENSOR_NAME);
	if (IS_ERR(parking_device)) {
		ret = PTR_ERR(parking_device);
		pr_err(DRV_NAME ": device_create failed (%d)\n", ret);
		goto err_class;
	}

	/* 4. Arm the timer that plays the role of the hardware sensor. */
	timer_setup(&parking_timer, parking_timer_callback, 0);
	mod_timer(&parking_timer, jiffies + msecs_to_jiffies(interval_ms));

	pr_info(DRV_NAME ": loaded. %s major %d minor %d, range %d..%d cm step %d cm every %lu ms\n",
		PARKING_SENSOR_DEVNAME, MAJOR(devno), MINOR(devno), min_cm,
		max_cm, step_cm, interval_ms);
	return 0;

err_class:
	class_destroy(parking_class);
err_cdev:
	cdev_del(&parking_cdev);
err_region:
	unregister_chrdev_region(devno, 1);
	return ret;
}

static void __exit parking_sensor_exit(void)
{
	/* del_timer_sync() waits for a running callback to finish, so the
	 * module can never be freed while the timer still uses it. */
	del_timer_sync(&parking_timer);

	if (parking_device)
		device_destroy(parking_class, devno);
	if (parking_class)
		class_destroy(parking_class);
	cdev_del(&parking_cdev);
	unregister_chrdev_region(devno, 1);

	pr_info(DRV_NAME ": unloaded\n");
}

module_init(parking_sensor_init);
module_exit(parking_sensor_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Parking Sensor Project");
MODULE_DESCRIPTION("Virtual ultrasonic parking sensor character device");
MODULE_VERSION("1.0");
