# Computer Architecture Notes

How the hardware topics of the syllabus appear in this project. The core
claim: the project is *virtual* at the sensor, but the data path is real.

---

## 1. The path, end to end

```
 +---------+    +---------+    +------------------+    +---------------+
 | "sensor"|    |         |    |                  |    |               |
 |  value  |--->|  timer  |--->| driver state     |--->| copy_to_user  |
 | (sim.)  |    | callback|    | (kernel memory)  |    |               |
 +---------+    +---------+    +------------------+    +------+--------+
                                                                |
 +---------+    +---------+    +------------------+    +--------v-------+
 | TCP     |<---| server  |<---| StateManager      |<---| application   |
 | client  |    | thread  |    | (user memory)     |    | read() buffer |
 +---------+    +---------+    +------------------+    +----------------+

 every arrow is a real architectural step; only the leftmost box is fake
```

**Hardware (simulated) -> kernel -> user space -> log file / socket.**
Everything between those two ends is genuine Linux and genuine C++.

---

## 2. Instruction set architecture

The ISA is the contract between compiled code and the CPU.

```bash
file build/parking_monitor            # "ELF 64-bit LSB pie executable, x86-64"
readelf -h build/parking_monitor | grep Machine
objdump -d -C build/parking_monitor | head -n 40
```

What to say: the compiler translates `if (distance_cm <= danger_cm_)` into a
`cmp` and a conditional jump. The same source compiles to `cmp` + `b.le` on
ARM, or to a compare-and-branch pair on MIPS. The *semantics* are the same; the
*encoding* is architecture specific. This is why a binary is not portable and
why the project states "Linux environment" rather than "anywhere".

Other points that come up:

- **RISC vs CISC.** x86-64 is CISC with a RISC-looking front end; ARM and RISC-V
  are RISC. Both execute the same C++ source after translation.
- **System V ABI.** How arguments are passed, how the stack is aligned. This is
  the actual interface between the compiler and the C library - `printf` works
  only because both sides agree.
- **Why the kernel entry code is written in assembly.** The ABI for a syscall
  is architecture specific, and there is no C compiler involved.

---

## 3. Virtual memory and paging

Every process gets its own address space. The MMU walks a page table to
translate a virtual address into a physical one.

```
virtual address   | page number | offset
  0x7fff_9c40_1234 -> VPN 0x1FFF_E4 | 0x1234
                          |            |
                     page table      always
                     (in RAM)      added
```

Consequences visible in this project:

| Fact | Why it matters here |
|---|---|
| each process has a private address space | the monitor and the client can both use port 9000 without colliding, and neither can read the other's memory |
| kernel space is mapped into every process, but not writable from user space | a user program cannot bypass the driver by writing to a kernel address |
| a user pointer is meaningless in kernel context | this is why `copy_to_user` exists and raw `memcpy` is a bug |
| the stack grows down, the heap grows up | overflows move in opposite directions, which is why they need different mitigations |
| shared mappings are possible | the basis of shared memory (`shmget` in `ipc_and_sockets.c`) |

**Segmentation vs paging.** Segmentation gives logical units (code, data,
stack); paging gives fixed-size physical management. x86-64 has paging plus a
very lightweight segment model; the GDT is still there, but essentially only to
support the flat model and TLS. The project only needs the flat model, so a
stack or data segment is described by a base of 0 and a limit of 2^64.

---

## 4. RAM, cache and storage

Where each piece of this project's state lives:

| What | Where | Notes |
|---|---|---|
| simulated distance | kernel module static data | a few bytes, stays in L1 cache while the timer runs |
| `struct parking_sensor_data` | the monitor's stack | written once per poll |
| `TelemetryRecord` | the stack, then copied into a `std::string` | short lived |
| log line strings | the heap, then the `ofstream` buffer | the string is freed as soon as it is written |
| recent records | the logger's `std::queue` | grows if the writer falls behind |
| log file contents | page cache, then the block layer, then the disk | `flush()` only guarantees the page cache |
| the executable | on disk, demand-paged into RAM | `mmap`ed by the loader |

**Why the cache exists.** The timer callback and the module's data are tiny and
touched every 500 ms, so they stay in L1 and the access is effectively free. If
the simulation touched a 1 MB table, every pass would be a cache miss and the
`kmalloc`/cache work in `driver_notes.md` would start to matter.

**The block layer.** `ofstream::flush()` writes into the page cache; the kernel
writes the actual blocks when it decides to. `fsync()` is what forces it. The
logger deliberately does not call `fsync()` on every line - that would turn a
1 Hz log into 1 fsync per second. The trade is documented in
`app/src/logger.h`: guarantee the line reaches the file, not the platter.

---

## 5. Memory allocation and garbage collection

| | C with malloc | C++ raw new/delete | C++ RAII / containers | Java / C# |
|---|---|---|---|---|
| who frees | you | you | the scope | the runtime |
| on exception | leaked unless careful | leaked | freed | collected |
| cycles | not applicable | not applicable | handled by `weak_ptr` | **not handled** |
| predictability | good | good | good | pauses |
| cost per allocation | malloc | new | often none | more |

The project uses the third column almost everywhere. The only manual ownership
in the app is the file descriptor, and it is released in a destructor rather
than by hand. See `training_examples/cpp_advanced/custom_allocator.cpp` for
the allocator view, including a pool, which is what an embedded target would
use so that allocation cost and fragmentation are both predictable.

**GC is not free.** Collection needs to know what is reachable, which means
tracing or reference counting, which costs time, pauses, and memory for the
bookkeeping. In C++ the language says ownership is explicit, and the cost of
the alternative is paid only where you choose to pay it.

---

## 6. Cache coherence and memory barriers

Two threads in one process share L1 caches through the same L2/L3, so the
hardware already keeps the caches consistent for you. A **coherency problem**
appears when two *cores* write the same cache line, or when a core's cache is
not current.

| Mechanism | Cost | Use for |
|---|---|---|
| `volatile` | free | a single flag, nothing else |
| mutex | syscall-free spin then sleep | almost everything in this project |
| `std::atomic<T>` | a lock instruction, or a lock-free instruction | counters, one flag |
| memory fence | an instruction | ordering without a lock |

The project's `Logger` uses a mutex; its stop flag is a `std::atomic<bool>`. The
driver uses a kernel mutex. Nothing needs an explicit fence, which is the right
answer: a fence is the tool for when you have chosen the hard path.

**The classic bug `volatile` does not fix:**

```c
volatile int ready;      /* WRONG */
if (ready) use(data);    /* no barrier: data may not be visible yet */
```

`volatile` guarantees the compiler re-reads `ready`, not that another core's
writes to `data` are ordered before it. That needs a fence or an atomic. Worth
knowing, and worth being able to explain in one sentence.

---

## 7. Branch prediction, speculative execution, ILP

**Branch prediction.** The CPU guesses the outcome of a branch and runs ahead.
A misprediction costs roughly 10-20 cycles - the pipeline has to be flushed.
In `ThresholdPolicy::evaluate` there are two compares. While the distance
falls steadily through the warning band, the "danger" test is consistently
false and the "safe" test is consistently false, so both are well predicted.
The switch in `to_string` is a jump table, so it is an indirect branch, which
is harder to predict - but the value is almost always the same, so the history
still gets it right.

**Speculative execution** is what makes prediction possible: the CPU executes
down the guessed path and discards the work if the guess was wrong. Spectre
and Meltdown were side channels built on it. The mitigation is the same in both
cases: make the kernel's memory layout less guessable (KPTI, KASLR).

**Instruction level parallelism.** The CPU issues several instructions per
cycle if they are independent. Our work is trivial in this respect, but the
idea shows up in the driver: a *real* ultrasonic driver would start the echo,
then compute an elapsed time while the echo is in flight, rather than
busy-waiting for it. That is ILP in a system, and it is the difference between
`while (!ready) ;` and a timer.

**Multicore.** The monitor's three threads are the practical case: the OS puts
them on separate cores, so the log write of poll *n* overlaps the device read
of poll *n+1*. The mutex is what keeps the handoff correct.

---

## 8. I/O systems, buses and DMA

The general path, which the project's driver occupies exactly one box of:

```
CPU  <--bus-->  memory controller <--DDR-->  RAM
 |
 +--> I/O bridge --> bus (PCIe / AXI / I2C / SPI) --> device
                        |
                        +--> DMA engine: the DEVICE writes to RAM directly
```

**Two ways to move data.**

| | Programmed I/O (PIO) | DMA |
|---|---|---|
| who copies | the CPU, one word at a time | the device's DMA engine |
| CPU cost | high - every byte is a CPU action | low - the CPU sets up once |
| used for | small, infrequent | large, frequent |
| in this project | **this is the project** | not used |

This project is pure programmed I/O: the driver puts 16 bytes into the user
buffer with `copy_to_user` and the CPU does the copying. That is the right
choice - DMA has a setup cost that a 16-byte transfer cannot amortise, and it
would need real hardware to exist at all. The write-up belongs here rather than
in the code.

**Buses** are the same idea at different scales: PCIe inside a PC, AXI inside
an SoC, I2C/SPI for a sensor, CAN for a vehicle, Ethernet for a network. The
virtual driver is "wired" to nothing, which is precisely the simplification that
keeps the project buildable on a laptop.

---

## 9. FPGA and ASIC

| | ASIC | FPGA | Microcontroller / SoC |
|---|---|---|---|
| NRE cost | very high | low | - |
| Unit cost | low in volume | moderate | low |
| Performance | best | very good | good enough |
| Flexibility | none at tape-out | re-programmable | re-programmable |
| Time to market | months | hours | hours |
| Where parking sensors live | a car ECU | a sensor fusion box | **this** |

**What an FPGA or ASIC would do in a real parking sensor:** time the echo
round-trip with a hardware counter, run a matched filter over the received
samples, and decide in microseconds. An ultrasonic module in a car is normally
an MCU running firmware, not an FPGA - the computation is a correlation and a
threshold, which an MCU handles comfortably.

**Why neither appears in this project:** the sensing front end is replaced by a
timer, and the decision is a three-way threshold, which any CPU can do. Adding
accelerator hardware would demonstrate nothing that the existing data path does
not already demonstrate.
