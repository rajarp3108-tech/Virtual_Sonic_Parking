# Training Examples

Nothing in this directory is part of the parking monitor. The monitor has no
dependency on any of it, and none of it is required to build or run the core
project.

These files exist because the syllabus asks for topics the project itself has
no reason to contain. A parking sensor does not need a trie or a memory pool.
Rather than pretend otherwise, those topics are demonstrated here, in isolation,
where they can be read, built and run on their own.

## Layout

| File | Covers |
|---|---|
| `cpp_ds/data_structures.cpp` | arrays, linked list, stack, circular queue, BST, hash table, trie, graph BFS/DFS, searching and sorting |
| `cpp_advanced/oop_and_stl.cpp` | inheritance, virtual and pure virtual, multiple and multilevel inheritance, RTTI, `dynamic_cast`, static members, friend functions, templates, STL containers, iterators, algorithms, the three smart pointers, new/delete |
| `cpp_advanced/custom_allocator.cpp` | allocator concepts, an allocator-aware container, a fixed-block memory pool, placement new |
| `system_programming/process_lifecycle.c` | `fork`, double fork, `exec`, `wait`, process states, zombie and orphan reaping, `sigaction` |
| `system_programming/ipc_and_sockets.c` | `pipe`, System V message queue, shared memory with a semaphore, UDP next to the project's TCP |
| `driver_notes/mini_locking.c` | `pthread_mutex_t`, `pthread_spinlock_t`, C11 atomics, and the pattern the monitor actually uses |
| `system_programming/system_programming_notes.md` | `mmap`, file locking, async I/O, System V vs POSIX signals, completion notification, syscall anatomy, kernel vs user space, daemon checklist |
| `system_programming/linux_notes.md` | Linux stack, the shell commands the project uses, `/dev` `/proc` `/sys`, permissions, bash scripting, cron, git |
| `networking/network_notes.md` | OSI/TCP-IP mapping, TCP handshake, IPv4 vs IPv6, subnetting, NAT, firewall and VPN, application protocols, Ethernet vs Wi-Fi, 5G and IoT |
| `architecture/architecture_notes.md` | ISA, virtual memory and paging, RAM/cache/storage, allocators and GC, cache coherency, branch prediction and ILP, I/O and DMA, FPGA and ASIC |
| `driver_notes/driver_notes.md` | mutex vs spinlock vs RCU, interrupts and bottom halves, jiffies vs hrtimer, kobject/kset/class/sysfs, `kmalloc` vs `vmalloc`, GPIO/I2C/SPI, testing a driver in QEMU, a second character device |

## Build and run

```bash
make -C training_examples          # everything into build/training/
make -C training_examples clean

./build/training/data_structures
./build/training/oop_and_stl
./build/training/custom_allocator
./build/training/process_lifecycle
./build/training/ipc_and_sockets
./build/training/mini_locking
```

The C examples use `pthread`, so `-pthread` is in `CFLAGS`. `ipc_and_sockets`
creates and removes its own System V objects, so it can be run repeatedly, and
`mini_locking` finishes in well under a second.

## How these connect to the deliverable

| If you are asked about... | Point at |
|---|---|
| why the monitor uses a thread queue instead of a message queue | the header comment of `ipc_and_sockets.c` |
| why the driver uses a mutex and not a spinlock | section 1 of `driver_notes.md` |
| why the distance is read with `read()` and not `mmap` | section 1 of `system_programming_notes.md` |
| why `SIGINT` is handled with `sigaction` and not `signal` | section 4 of `system_programming_notes.md`, and `process_lifecycle.c` |
| why the app is built C++ but the driver is C | section 7 of `system_programming_notes.md` |
| why the network layer is plain TCP on loopback | sections 1 and 6 of `network_notes.md` |
| why there is no real sensor hardware | sections 7 and 8 of `driver_notes.md` |
| where RAII shows up in the deliverable | `oop_and_stl.cpp`, and the `unique_ptr` use throughout `app/src` |

## Honest limitations

- These are demonstrations, not library code. Several examples take a
  shortcut that is fine for a single runnable program and wrong in production;
  where that happens, the comment says so.
- The concurrency examples demonstrate the concepts. `mini_locking` will
  usually show a correct total even in the unlocked case, because the lost
  update is not guaranteed on any single run. The point is the *reason* the
  lock is there, not the number that appears.
- The kernel-specific notes are documentation only. Nothing in this directory
  compiles against kernel headers.
