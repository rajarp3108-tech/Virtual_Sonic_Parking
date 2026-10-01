# Full Syllabus Traceability

The requirement is "cover all topics studied in the 20-day programme". The
strategy in the SRS is deliberate and is repeated here: each topic is either

- **C** - a mandatory core part of the parking sensor, or
- **D** - explained in the project documentation / design, or
- **T** - a small self-contained demonstration in `training_examples/`.

Nothing outside the supplied syllabus is used, and no external technology is
introduced as a dependency.

## Days 1-2 - Computer Architecture: hardware

| Topic | Type | Where |
|---|---|---|
| Architecture basics, CPU -> memory -> I/O path | C | the whole data flow, `docs/architecture.md` sections 3-6 |
| ISA, compiled code as machine instructions | D | `docs/interview-qa.md` section F, `readelf`/`objdump` output via `scripts/debug_helper.sh build` |
| Virtual memory and paging, user vs kernel address space | C + D | `docs/architecture.md` section 6, `docs/interview-qa.md` F |
| RAM, cache, storage - where state, logs and the executable live | D | `docs/architecture.md` F, `docs/interview-qa.md` F |
| Memory allocation and garbage collection vs RAII | D + T | `training_examples/cpp_advanced/custom_allocator.cpp` |
| Cache coherence with a shared queue | D | `docs/architecture.md` section 5, `docs/interview-qa.md` C |
| Branch prediction and speculative execution | D | `docs/interview-qa.md` F |
| ILP and multicore: threads on separate cores | C | three threads: control loop, logger writer, TCP accept |
| I/O systems, buses, DMA | D | `docs/architecture.md` section 6 (why this driver needs no DMA) |
| FPGA / ASIC | D | `training_examples/architecture/architecture_notes.md` |

## Days 3-4 - Computer Architecture: networking

| Topic | Type | Where |
|---|---|---|
| Networking basics, OSI and TCP/IP models | D + C | mapping table in `docs/interview-qa.md` E; real IPv4 TCP server and client |
| IPv4 TCP client/server | **C (mandatory)** | `app/src/tcp_status_server.cpp`, `client/status_client.cpp` |
| IPv4 versus IPv6 | D | `docs/interview-qa.md` E; `training_examples/networking/network_notes.md` |
| Subnetting | D | worked /24 example in `docs/interview-qa.md` E |
| NAT, firewall, VPN | D | `training_examples/networking/network_notes.md` |
| HTTP, FTP, SMTP, SNMP, SIP | D | comparison table in `training_examples/networking/network_notes.md` |
| SSL/TLS, network security | D | `training_examples/networking/network_notes.md` (why plain TCP is enough here) |
| Wired Ethernet versus Wi-Fi, fibre, powerline, cellular | D | `training_examples/networking/network_notes.md` |
| 5G and IoT | D | `training_examples/networking/network_notes.md` (parking sensor as an IoT node) |

## Days 5-6 - Linux and Git

| Topic | Type | Where |
|---|---|---|
| Linux history, philosophy, distributions | D | `training_examples/system_programming/linux_notes.md` |
| Terminal commands, file operations, hierarchy, permissions | C | `/dev/parking_sensor` permissions; every `scripts/*.sh` |
| grep, sed, awk, find, tar/gzip, man | D | used inside `scripts/load_driver.sh`, `scripts/debug_helper.sh`; listed in `training_examples/system_programming/linux_notes.md` |
| Bash scripting | C | six scripts in `scripts/` |
| cron | D | `training_examples/system_programming/linux_notes.md` (the monitor as a cron-driven job) |
| Git init / add / commit / log / reset / revert | C | `docs/stage-evidence.md`, the branch plan in section 2 |
| Branching, merge, rebase, conflicts, pull requests | C | feature branches per stage, `docs/stage-evidence.md` |

## Days 7-10 - C++ Programming

| Topic | Type | Where |
|---|---|---|
| Arrays | T | `training_examples/cpp_ds/data_structures.cpp` |
| Linked list | T | same file |
| Stack | T + C | same file; also the call stack explanation in `docs/architecture.md` |
| Queue | **C** | `std::queue<std::string> pending_` in `app/include/logger.h` |
| Circular queue | T | `training_examples/cpp_ds/data_structures.cpp` |
| Priority queue / heap | T | same file |
| Deque | T + C | same file; `std::deque` mentioned for recent telemetry |
| Tree | T | BST in `data_structures.cpp` |
| Graph | T | BFS and DFS in `data_structures.cpp` |
| Hash table, set | T | `data_structures.cpp` |
| Trie | T | `data_structures.cpp` |
| Searching and sorting | T | `sort`, `find`, `lower_bound` in `data_structures.cpp` |
| OOP basics, encapsulation, abstraction, composition | **C** | `docs/architecture.md` section 7, `uml/class_diagram.puml` |
| Advanced OOP: inheritance, virtual/pure virtual, multiple and multilevel inheritance, RTTI, `dynamic_cast`, static members, friend functions, templates | T | `training_examples/cpp_advanced/oop_and_stl.cpp` |
| File I/O | **C** | `fstream` in `app/src/logger.cpp` and `app/src/config.cpp` |
| Exceptions | **C** | `try/catch` in `main.cpp`, `parking_monitor.cpp`, `sensor_reader.cpp` |
| STL containers, iterators, algorithms | **C** + T | `queue`, `string`, `vector`; algorithms in `oop_and_stl.cpp` |
| Smart pointers: unique, shared, weak | **C** + T | `unique_ptr` throughout the app; all three in `oop_and_stl.cpp` |
| Multithreading, mutex, condition variable | **C** | `app/src/logger.cpp`; the 4-thread test in `test_logger_and_telemetry.cpp` |
| RAII | **C** | `DeviceSensorReader` destructor closes the fd; `docs/interview-qa.md` C |
| new / delete | T | `oop_and_stl.cpp` |
| Custom allocator concepts | T | `training_examples/cpp_advanced/custom_allocator.cpp` |
| Compilation and linking, Makefile | **C** | four Makefiles; `scripts/build.sh` |
| Debugging, optimisation, coding standards | **C** | `-Wall -Wextra -Wpedantic`, `scripts/debug_helper.sh gdb`, consistent formatting throughout |

## Days 11-13 - Linux System Programming

| Topic | Type | Where |
|---|---|---|
| Kernel vs user space, privileges, system calls | **C** | `docs/architecture.md` section 6 |
| Segmentation and paging | D | `docs/interview-qa.md` F |
| File descriptors | **C** | `open`/`read`/`write`/`close` in `sensor_reader.cpp` |
| Process states, scheduling | C + D | the three threads; `docs/architecture.md` section 5 |
| fork / exec / process tree | **C** + T | double fork in `main.cpp`; `training_examples/system_programming/process_lifecycle.c` |
| ps, top, kill, nice, renice | D | demo script steps 9-10, `run_demo.sh` |
| Sockets: TCP and UDP | **C** (TCP) + T (UDP) | TCP in the app; a UDP echo in `training_examples/system_programming/ipc_and_sockets.c` |
| IPC: pipes, message queues, shared memory | T | `training_examples/system_programming/ipc_and_sockets.c` |
| POSIX APIs | **C** | `<unistd.h>`, `<fcntl.h>`, `<sys/socket.h>`, `<pthread.h>` equivalents |
| `mmap` | T | `training_examples/system_programming/system_programming_notes.md` |
| File locking | T + D | `flock` in the same notes; the device node's own locking |
| Asynchronous I/O | D | `system_programming_notes.md` (select/epoll vs our thread-per-concern) |
| System V versus POSIX signals | D + C | `sigaction` (POSIX) in the app; System V compared in the notes |
| Signals, daemons | **C** | `main.cpp` |
| gdb, dmesg, binutils, core dumps | **C** | `scripts/debug_helper.sh` |
| Custom system call anatomy | D | `system_programming_notes.md` (write a syscall by hand, and why we do not need one) |

## Days 14-15 - Linux Device Drivers

| Topic | Type | Where |
|---|---|---|
| Kernel modules, `insmod` / `rmmod`, `printk` | **C** | `driver/parking_sensor.c`; `scripts/load_driver.sh` |
| Module parameters | **C** | six `module_param` declarations |
| Character devices, open/read/write | **C** | `parking_fops` |
| `copy_to_user` / `copy_from_user` | **C** | `parking_read`, `parking_write` |
| Kernel timer | **C** | `timer_setup` + `mod_timer` |
| Mutex | **C** | `DEFINE_MUTEX(parking_lock)` |
| Kernel data types (`u32`, `size_t`, kernel coding style) | **C** | driver code style; `size_t` / `ssize_t` in the syscall signatures |
| QEMU testing | D | `docs/stage-evidence.md` stage 4, `training_examples/driver_notes/driver_notes.md` |
| Spinlocks, semaphores, atomics | T | `training_examples/driver_notes/driver_notes.md` + `training_examples/driver_notes/mini_locking.c` (runnable user-space versions) |
| Interrupts, top half / bottom half, softirqs, tasklets, threaded IRQ, workqueues | T | `driver_notes.md` |
| jiffies, HZ, high-resolution timers | T | `driver_notes.md` (the project uses `jiffies` in the core) |
| Device model, kobject/kset, class, sysfs | **C** (class) + T (rest) | `class_create`/`device_create` in the core; kobjects in `driver_notes.md` |
| `kmalloc` vs `vmalloc`, memory barriers, cache coherency | T | `driver_notes.md` |
| GPIO, I2C, SPI, platform drivers | T (concept) | `driver_notes.md` - how a real ultrasonic sensor would be wired |

## Days 16-17 - Software Architecture

| Topic | Type | Where |
|---|---|---|
| System software vs application software | **C** | the driver / application split, `docs/architecture.md` section 3 |
| Proprietary, open source, freeware | D | `docs/architecture.md` (Linux kernel = open source; this driver is GPL) |
| Web / desktop / mobile application types | D | this is a system-level CLI and device driver, not any of the three |
| Design patterns and anti-patterns | D | patterns used: Strategy (`StatePolicy`), Adapter (`ISensorSource`), Observer-ish (`Logger`), Singleton avoided on purpose. Anti-patterns rejected: globals, fat `main`, busy-wait |
| MVC | D | `docs/architecture.md` - the CLI needs no framework, but the log file is effectively the view of the telemetry |
| SOA, microservices, event-driven architecture | D | `training_examples/architecture/architecture_notes.md` - this system is a single-process monolith, and why that is correct here |
| Docker, Kubernetes | D | same notes (why a kernel module is a poor container citizen) |
| SOLID, DRY, KISS | **C** | `docs/architecture.md` section 7, with file-level evidence |

## Days 18-19 - SDLC and Agile

| Topic | Type | Where |
|---|---|---|
| SDLC phases | **C** | the six-stage plan, `docs/stage-evidence.md` |
| Requirement gathering and specification | **C** | the supplied SRS plus `docs/requirements.md` |
| Traceability | **C** | this file and `docs/requirements.md` |
| Embedded / real-time requirements | D | `docs/requirements.md` section 5 |
| System design, hardware/software co-design | **C** | `docs/architecture.md` |
| UML: use case, class, state, sequence | **C** | `uml/*.puml` and `docs/uml.md` |
| Scrum, Kanban, hybrid | D | `docs/stage-evidence.md` section 4 (the backlog and review cadence) |

## Day 20 - Capstone

| Deliverable | Where |
|---|---|
| Objectives | `docs/requirements.md`, SRS section 2 |
| Requirements | `docs/requirements.md` (FR-01..FR-17) |
| Planning and timeline | `docs/stage-evidence.md` |
| Architecture | `docs/architecture.md` |
| UML | `uml/`, `docs/uml.md` |
| Environment | `README.md`, `docs/test-report.md` section 2.1 |
| Version control | Git, `docs/stage-evidence.md` |
| Progress review | `docs/stage-evidence.md` |
| Challenges and solutions | `docs/test-report.md` section 2.6 |
| Next steps | SRS section 22.2, `docs/architecture.md` |
| Tests, report, presentation | `docs/test-report.md`, `docs/demo-script.md` |

## Summary

| Type | Count | Where |
|---|---|---|
| Core implementation | 34 topics | `driver/`, `app/`, `client/`, `scripts/`, `tests/` |
| Documented design | 30 topics | `docs/`, `training_examples/*/**.md` |
| Training demonstration | 33 topics | `training_examples/` |

The core product stays small enough to explain line by line, which is the
point of the exercise: coverage comes from evidence, not from bloat.
