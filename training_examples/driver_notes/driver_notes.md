# Linux Character Device Driver Notes

The driver itself is `driver/parking_sensor.c` - that is the core of the
project and it is fully implemented. This file covers the driver topics from the
syllabus that the core does not need, so they are documented rather than
implemented.

Rule followed in this project: **a topic is only in the driver if the parking
sensor actually requires it.** A 150 cm to 10 cm simulation does not need
spinlocks, workqueues or sysfs attributes, so writing them would be
demonstration code pretending to be product code.

---

## 1. Synchronisation primitives

| Primitive | Sleeps? | Context allowed | Use when |
|---|---|---|---|
| `mutex` | yes | process context | **this project** - two contexts share state, both may sleep |
| `spinlock` | no | process or interrupt | the critical section is a handful of instructions and may run with interrupts off |
| `semaphore` | yes | process context | a counting lock, or a resource with a limit |
| `atomic` | no | any | one value, no compound operation |
| `rcu` | no | any | read-mostly data replaced wholesale |

**The rule of thumb in one sentence:** *if you can sleep, use a mutex; if you
cannot, use a spinlock.*

The project's driver needs a mutex because the timer callback and the
`read`/`write` syscalls both touch `distance_cm`, `direction`, `tick_count` and
`paused`, and both may sleep. A spinlock would be wrong - not because it would
deadlock, but because there is no reason to spin when sleeping is allowed.

### Atomic operations

```c
static atomic_t open_count = ATOMIC_INIT(0);
atomic_inc(&open_count);
pr_info("opens so far: %d\n", atomic_read(&open_count));
```

`atomic_t` guarantees the increment is not lost when two CPUs race. It is *not*
a general lock: it does not make a sequence of operations atomic, and a
`if (count == 0) count++;` on a plain `int` is still broken.

### Memory barriers

```c
smp_wmb();   /* write barrier  - my writes are visible before later ones */
smp_rmb();   /* read barrier   - earlier reads complete before later ones */
smp_mb();    /* full barrier   - both */
```

On x86 most barriers compile to nothing, because the architecture is already
strongly ordered - which is exactly why barrier bugs show up on ARM and not on a
desktop. Our driver needs none: the mutex provides the ordering.

---

## 2. Interrupts, top half, bottom half

```
device event
    |
    v
top half (hard IRQ)      <- must be fast, may not sleep
    ack the device, send the data to a bottom half
    |
    +--> tasklet / softirq        (same CPU, soon, may be deferred)
    +--> workqueue                 (a kernel thread, may sleep)
    +--> threaded IRQ              (a kernel thread from the start)
```

| Mechanism | Runs on | May sleep | When to choose |
|---|---|---|---|
| top half | the interrupted CPU | no | acknowledge the device, nothing else |
| softirq | the same CPU, on return from the interrupt | no | short work, needs low latency |
| tasklet | like a softirq, serialised per CPU | no | superseded by softirqs in modern kernels |
| workqueue | a worker thread | **yes** | anything longer, or anything that allocates |
| threaded IRQ | a dedicated kernel thread | yes | the simplest correct choice today |

**Where this project sits:** it does not use interrupts at all. A kernel timer
callback *is* interrupt-like - something asynchronous happens and the kernel
reacts - but it is a softirq context, so it must obey the softirq rules: no
sleeping, keep it short. Ours only does arithmetic and a `printk`, which is
compliant.

A real ultrasonic driver would need a GPIO interrupt on the echo line, with a
short top half and a workqueue for the measurement.

---

## 3. Jiffies, HZ and high-resolution timers

```c
#include <linux/jiffies.h>

unsigned long t0 = jiffies;
unsigned long elapsed_ms = jiffies_to_msecs(jiffies - t0);

msecs_to_jiffies(500);   /* 500 ms as a jiffy count - used by this project */
```

- **jiffy** - one tick of the kernel timer, `HZ` ticks per second.
  `CONFIG_HZ` is 250, 300 or 1000 on most modern kernels.
- Resolution is therefore 1/HZ - 1 ms at `HZ=1000`, 4 ms at `HZ=250`.
- **High-resolution timers** (`hrtimer`, `timer_setup` on a
  `ktime`-based clock) give nanosecond range and are what you use for anything
  with a real timing requirement.

**In this project:** `msecs_to_jiffies(interval_ms)` is correct and simple. A
500 ms period on a 250 Hz kernel becomes 125 jiffies - perfectly accurate for a
0.5 s simulation. If the interval had to be 1 ms, jiffies would round badly and
`hrtimer` would be the right answer.

`mod_timer()` from inside the callback is the standard one-shot pattern. The
alternative is `timer_setup` plus a repeating setup done *before* the return,
which avoids one function call; not worth it here.

---

## 4. Device model, kobjects, ksets, sysfs

```
kobject  = the base object every sysfs-visible thing hangs off
  -> knows its own name, its parent, and its lifetime (refcounted)
kset     = a set of kobjects of the same kind (drivers, devices, classes)
class    = a group of devices of the same type    <- THIS PROJECT
device   = one instance, with its dev_t and attributes
```

What our driver registers:

```c
parking_class = class_create("parking_sensor");                    /* a class */
parking_device = device_create(parking_class, NULL, devno, NULL,
                               "parking_sensor");                  /* a device */
```

That single pair is what makes `/dev/parking_sensor` appear, and what creates
`/sys/class/parking_sensor/`. Everything else in the device model - `bus_type`,
`platform_driver`, `kobject` attributes, `device_remove_file` for
`/sys/.../distance` - follows the same pattern with more bookkeeping.

**Why the project stops there.** A sysfs `distance` attribute would be nice,
but the data is already readable through the device file, and adding a second
interface doubles the code to test for no new capability.

The `device_create` call *could* pass attribute arrays that export
`min_cm`/`max_cm` per device. The module parameters in `/sys/module/.../parameters`
already do the same job, so it would be a duplicate.

**Permissions of those parameters, and why they are `0444`.** The third argument
of `module_param(min_cm, int, 0444)` becomes the file mode of the sysfs entry.
`0644` would make it writable, and `echo 2000 > /sys/module/parking_sensor/parameters/interval_ms`
would take effect immediately - but the timer callback reads these variables
without a lock, so a write arriving from `echo` while the timer is running is a
data race with no way to validate the new range. The project therefore makes
them read-only and sets them at load time:

```bash
sudo insmod driver/parking_sensor.ko interval_ms=2000 min_cm=10 max_cm=150
```

The refusal is part of the demonstration: `echo 2000 | sudo tee .../interval_ms`
prints `Permission denied`, which is the kernel enforcing the choice.

---

## 5. Kernel memory: `kmalloc` versus `vmalloc`

| | `kmalloc` | `vmalloc` |
|---|---|---|
| returns | physically contiguous | virtually contiguous only |
| size | `GFP_KERNEL` typically under 4 KB (page order limited) | large sizes, many pages |
| mapping | no page table work - the same physical pages | maps pages into a new virtual range |
| cost | cheap | page table work, TLB pressure |
| `free()` | `kfree` | `vfree` |

```c
int* p = kmalloc(64 * sizeof(int), GFP_KERNEL);   /* small, fast */
int* big = vmalloc(4 * 1024 * 1024);              /* 4 MB, needs mapping */
```

**In this project, neither is used** - and that is a deliberate, defensible
choice. The driver state is six `int`s and a `long`, so it is declared
`static`:

- no allocation, so no failure path to handle;
- no free, so no use-after-free to worry about on `rmmod`;
- the memory lives in the module's own section and is released with it.

An allocation would only be justified if the sensor kept a history buffer. If
that were added, the rule would be: `kmalloc` for a few kilobytes of sensor
history, `vmalloc` for a large ring buffer, and a `GFP_ATOMIC` variant in any
interrupt context.

---

## 6. Memory barriers and cache coherency in the kernel

The kernel runs on every core, so two CPUs can touch the same data. The
mutex provides ordering, so plain shared state is safe here. What is *not*
safe without care:

```c
/* WRONG: reader may see the pointer but not the data it points to. */
data = new_buffer;
published = 1;              /* no barrier between the two writes */

/* RIGHT: the writer must publish in the right order. */
smp_wmb();                  /* everything above is visible before this */
published = 1;
```

On the ARM side, `smp_wmb()` is a real instruction (`dmb ishst`); on x86 it is
usually nothing. Kernel code must never assume it can be omitted, because the
same source is compiled for every architecture.

---

## 7. GPIO, I2C, SPI and platform drivers

A real ultrasonic sensor (HC-SR04 style) works like this:

```
1. TRIG pin  -> a 10 microsecond pulse
2. wait for the ECHO pin to go high   <- this is where the interrupt happens
3. measure how long ECHO stays high
4. distance_cm = high_time_us * 0.0343 / 2
```

| Bus | Pins | Typical speed | Used for |
|---|---|---|---|
| GPIO | 1 per signal | instant | TRIG, ECHO |
| I2C | 2 shared (SDA, SCL) | 100 kHz / 400 kHz | accelerometers, temperature sensors |
| SPI | 4+ shared, chip-select per device | MHz | fast ADCs, displays |
| platform bus | none - internal | - | SoC peripherals, not a real wire |

The driver's shape would then be: a `platform_driver` with a `probe()` that
requests the GPIO resources, an IRQ handler for the ECHO edge, and a `kthread`
that runs the trigger-measure loop.

**None of that is in this project**, and the reason should be given: there is no
sensor hardware, so there is no bus, no pin, and no edge. A simulated driver
that pretended to manage GPIOs would be describing hardware that does not
exist. What *is* demonstrated is the part that a real driver shares: device
registration, a file interface, kernel/user data transfer, and a kernel
execution context that delivers data on its own schedule.

---

## 8. QEMU for driver testing

Loading a module needs a kernel you are allowed to modify. The ways to get
one, in increasing order of effort:

1. **A virtual machine or QEMU with your own kernel.**
   ```bash
   # build a kernel
   make -C /usr/src/linux defconfig
   sudo make -C /usr/src/linux -j$(nproc)
   # boot it under QEMU
   qemu-system-x86_64 -kernel arch/x86/boot/bzImage -initrd initrd.img \
       -append "console=ttyS0" -m 512 -nographic
   ```
   Inside, share the project with 9p or a second disk image, then `insmod`.

2. **A container - no.** A container shares the host kernel, so it cannot
   `insmod` anything. `SYS_MODULE` being dropped is a feature.

3. **The user-space half only.** No privileges needed:
   ```bash
   ./build/parking_monitor --simulate --iterations 30
   ./build/status_client --count 3
   ```

**What to do in a college lab.** Try option 1 once, with the college's
prebuilt QEMU image if one exists. If the machine will not let you, use
option 3 and write honestly in the report: *"the driver is complete and was
reviewed against the kernel documentation, but the college VM does not permit
module loading, so the demonstrated end-to-end run uses the simulated sensor
which exercises the identical user-space code path."* That is a far better
answer than claiming a demonstration that did not happen.

---

## 9. Module skeleton for a second device

The project has one device. A second one shows that the pattern is reusable -
and this is the *only* extra driver code worth adding, because it is 30 lines
and it proves the structure is not a one-off trick:

```c
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#define DEV_NAME "parking_button"

static dev_t devno;
static struct cdev cdev;
static struct class *cls;
static int pressed;

static int btn_open(struct inode *i, struct file *f) { return 0; }
static int btn_release(struct inode *i, struct file *f) { return 0; }

static ssize_t btn_read(struct file *f, char __user *buf, size_t count,
                        loff_t *p)
{
        if (count < sizeof(pressed)) return -EINVAL;
        if (copy_to_user(buf, &pressed, sizeof(pressed))) return -EFAULT;
        return sizeof(pressed);
}

static ssize_t btn_write(struct file *f, const char __user *buf, size_t count,
                         loff_t *p)
{
        int value;
        if (count != sizeof(value)) return -EINVAL;
        if (copy_from_user(&value, buf, sizeof(value))) return -EFAULT;
        pressed = !!value;
        return count;
}

static const struct file_operations fops = {
        .owner = THIS_MODULE, .open = btn_open, .read = btn_read,
        .write = btn_write, .release = btn_release,
};

static int __init btn_init(void)
{
        if (alloc_chrdev_region(&devno, 0, 1, DEV_NAME)) return -ENOMEM;
        cdev_init(&cdev, &fops);
        cdev.owner = THIS_MODULE;
        if (cdev_add(&cdev, devno, 1)) goto err_region;
        cls = class_create(DEV_NAME);
        if (IS_ERR(cls)) goto err_cdev;
        if (IS_ERR(device_create(cls, NULL, devno, NULL, DEV_NAME)))
                goto err_class;
        pr_info(DEV_NAME ": ready\n");
        return 0;
err_class: class_destroy(cls);
err_cdev:  cdev_del(&cdev);
err_region: unregister_chrdev_region(devno, 1);
        return -ENODEV;
}

static void __exit btn_exit(void)
{
        device_destroy(cls, devno);
        class_destroy(cls);
        cdev_del(&cdev);
        unregister_chrdev_region(devno, 1);
}

module_init(btn_init);
module_exit(btn_exit);
MODULE_LICENSE("GPL");
```

The `pressed` variable is deliberately not protected by a mutex here, which is
a real weakness - use `READ_ONCE`/`WRITE_ONCE` or a mutex once there is more
than one execution context. It is a teaching skeleton, not production code.
