# Interview and Viva Questions

Answer these out loud before the exam. Each answer is short on purpose: if you
cannot say it in three sentences, you do not understand it yet.

## A. The eight questions from the SRS

**Why a character device?**
> A character device gives a simple file-like interface between user space and
> the kernel: one special file, ordinary `open`/`read`/`write`/`close`, and the
> kernel can deliver data on its own schedule. A block device would imply
> fixed-size blocks and buffering, which is wrong for a single continuous
> sensor value. It is exactly the mechanism a real ultrasonic driver would use.

**Why a kernel timer?**
> Because the "sensor" is simulated. A `timer_list` re-armed inside its own
> callback advances the distance every 500 ms without any hardware and without
> the CPU spinning. The application only reads whatever value is current, so
> the two sides are genuinely decoupled.

**Why C++?**
> The monitor is a set of collaborating objects with clear lifetimes: reader,
> state machine, logger, server. C++ gives me constructors and destructors for
> RAII, `std::unique_ptr` for ownership, the STL for the queue and the string
> formatting, `std::thread` plus a mutex for the logger, and exceptions for the
> recoverable errors. C would make me manage every one of those by hand.

**Why a state machine?**
> The parking response is genuinely discrete: safe, warning, danger. Modelling
> it as three explicit states with thresholds makes the behaviour reviewable
> and testable, and it means an out-of-range value cannot fall into an
> undefined branch. I also get a change counter and a clean `state_change`
> event for free.

**Why TCP?**
> It is the simplest reliable way to show the data leaving the process, and it
> is a taught topic. A fixed one-line message format means the client needs no
> parser. UDP would fit too, but TCP gives the ordered stream the demo needs.

**Where is the hardware concept in a virtual project?**
> The hardware is replaced by a timer, but every architectural step is real:
> a value is produced, it is read through an I/O path, it crosses the
> kernel/user boundary with `copy_to_user`, and it is processed in user space.
> That is the same data path as a real ultrasonic sensor - only the front end
> is simulated.

**What is user space versus kernel space?**
> The kernel runs with full privilege and owns the hardware; user space is
> where normal programs run with restricted access. The driver runs in kernel
> space, the monitor in user space, and they can only meet at a defined
> interface - here one device file and one shared struct. User pointers are
> invalid in kernel context, which is exactly why `copy_to_user` and
> `copy_from_user` exist.

**What is Git used for?**
> History, staged development and evidence. I worked on short-lived feature
> branches, merged into `main` per stage, and every commit maps to a stage of
> the plan. If something breaks I can find the commit that introduced it, and
> the examiner can see the six stages as they were actually built.

## B. Driver questions

**What does `alloc_chrdev_region` do?**
> Asks the kernel for a free major/minor pair. Nothing is created on disk yet.

**And `cdev_add`?**
> It binds our `file_operations` structure to that device number, so the
> kernel knows which `open`, `read`, `write` and `release` functions to call.

**How does `/dev/parking_sensor` appear?**
> `class_create()` plus `device_create()` register the device with a class.
> udev is watching the class directory and creates the node with the right
> major/minor. Without udev, I can create it manually with
> `mknod /dev/parking_sensor c <major> <minor>` - the script does that as a
> fallback.

**Why `copy_to_user` and not `memcpy`?**
> Because the destination is a user-space address. The user page tables are not
> mapped in kernel context, so a direct `memcpy` would dereference an address
> that is not valid here. `copy_to_user` validates and copies, and it returns
> the number of bytes it could *not* copy, which I check.

**Why a mutex and not a spinlock?**
> Two contexts touch the shared state: the timer callback and the read/write
> syscalls. Both can sleep, so blocking is allowed and a mutex is correct. A
> spinlock is only right where sleeping is forbidden - an interrupt handler.

**What does `module_param` give me?**
> Runtime configuration. `min_cm`, `max_cm`, `step_cm`, `start_cm` and
> `interval_ms` are all `insmod` parameters, so I can change the simulation
> without recompiling: `insmod parking_sensor.ko step_cm=5 interval_ms=100`.

**Why `del_timer_sync` in the exit path?**
> It deletes the timer *and* waits for a callback that is already running to
> finish. Without the sync part the module memory could be freed while the
> callback was still executing - a classic use-after-free kernel bug.

## C. C++ and design questions

**What is RAII, and where did I use it?**
> A resource is owned by an object, so it is released by the destructor no
> matter which path leaves the scope. The file descriptor in
> `DeviceSensorReader` closes in the destructor, the log file and the writer
> thread are released in `Logger::stop()`, and the listening socket in
> `TCPStatusServer::stop()`.

**What is the difference between a shared pointer and a unique pointer here?**
> The monitor uses `unique_ptr` for everything it owns, because the ownership
> is genuinely single: only the monitor may destroy the logger. A `shared_ptr`
> would allow two objects to destroy the logger and invite a dangling call. I
> only reach for shared ownership when two objects truly must both own
> something, and here nothing does.

**Why is the state rule behind an interface?**
> So the rule can change without touching anything else. Adding a "vibration"
> rule means writing one new class that implements `StatePolicy`; the monitor,
> the logger and the socket code are untouched. That is the Open/Closed
> Principle, and `tests/test_state_manager.cpp` proves it with an
> `AlwaysSafePolicy`.

**What is the Dependency Inversion Principle doing in my code?**
> `ParkingMonitor` depends on the `ISensorSource` interface, not on
> `DeviceSensorReader`. So the monitor can run against the real device, against
> the simulator, or against a fake in a test - which is exactly how
> `test_monitor_survives_a_failing_sensor` works.

**How is the shared data between threads protected?**
> The log queue is guarded by one mutex, and a condition variable lets the
> writer thread sleep until work arrives instead of polling. The TCP snapshot
> has its own smaller mutex. I do not lock across I/O: the worker copies the
> line out of the lock and writes to the file outside it.

**Why a `queue` and not a `vector`?**
> The producer and consumer both add and remove at the same end, which is
> first-in-first-out. A queue gives that behaviour and the ownership semantics
> I want for free.

**What does the destructor of `ParkingMonitor` actually do?**
> It calls `request_stop()`. All the real work happens in `run()`, which
> stops the server, then stops the logger, then returns; after that the
> `unique_ptr` members release their objects in reverse order of construction.
> Keeping shutdown in one place avoids two different orders being used.

## D. Linux system programming questions

**How do I stop the program cleanly?**
> Install a handler for SIGINT and SIGTERM that sets one `std::atomic<bool>`.
> The control loop polls that flag, and when it sees it the normal shutdown
> path runs. The handler itself does nothing else, because only setting a flag
> is async-signal-safe - no `printf`, no `malloc`.

**Why `sleep` in slices?**
> `std::this_thread::sleep_for(500ms)` in one call would delay noticing the
> stop flag by up to half a second, and would also make shutdown feel slow. I
> sleep in 50 ms slices and re-check the flag between them.

**What is the double fork for?**
> The first fork lets the shell continue while the child keeps running.
> `setsid()` makes the child a session leader with no controlling terminal.
> The second fork stops the session leader from ever acquiring a terminal
> again, which is what makes it a proper daemon.

**What is a file descriptor?**
> A small non-negative integer that the kernel uses as an index into the
> process's table of open files. `open()` returns one, `read`/`write`/`close`
> use it. Descriptors 0, 1 and 2 are stdin, stdout and stderr - which is why
> the daemon redirects 1 and 2 to the log file.

**What is the user/kernel boundary in terms of system calls?**
> Every `read()` on the device enters the kernel: the arguments are validated,
> the driver's `parking_read` runs in kernel context, the data is copied with
> `copy_to_user`, and control returns to user space. The application's own
> variables are never touched by the kernel directly.

## E. Networking questions

**What happens between `connect()` and the first byte received?**
> TCP: a SYN, a SYN-ACK, an ACK - three-way handshake. Then the server's
> accept thread wakes, sends its snapshot, and closes. On the client side the
> bytes arrive in the receive buffer and `recv()` returns them.

**Why `SO_REUSEADDR`?**
> Without it, restarting the server within a minute fails with `EADDRINUSE`
> because the previous connection is still in `TIME_WAIT`. It does not allow
> two live listeners on the same port - that is `SO_REUSEPORT`, a different
> option.

**Why `MSG_NOSIGNAL`?**
> If a client disappears while I am sending, `send()` would normally raise
> `SIGPIPE` and kill the process. `MSG_NOSIGNAL` turns that into an `EPIPE`
> return value I can handle. I also ignore `SIGPIPE` globally for the same
> reason.

**How does this project map onto the OSI model?**

| Layer | What is used here |
|---|---|
| 7 Application | the `STATUS distance=.. state=..` message |
| 6 Presentation | none - the format is ASCII and needs no translation |
| 5 Session | connection lifetime, one request per connection |
| 4 Transport | TCP, port 9000, ordered reliable delivery |
| 3 Network | IPv4, 127.0.0.1, the routing decision to loopback |
| 2 Data link | loopback interface, no frames on a wire |
| 1 Physical | none locally; Ethernet/Wi-Fi would carry it in a real deployment |

**IPv4 versus IPv6?**
> IPv4 is 32 bits and has been exhausted; IPv6 is 128 bits. My server calls
> `socket(AF_INET, ...)` and `inet_pton(AF_INET, ...)`, so it is IPv4 only.
> Supporting IPv6 means `AF_INET6` and a dual-stack `socket(AF_INET6, ...)`
> with `IPV6_V6ONLY` off. The status message itself would not change at all -
> the address family is the only real difference in this design.

**A small subnetting example for the demo network**

```
Network 192.168.10.0/24
  .1        gateway router
  .10       monitor (server)          TCP 9000
  .20       status client
  broadcast .255
host range .2 - .254  ->  253 usable addresses
```

/24 leaves 8 bits for hosts, so `2^8 - 2 = 254` addresses, minus the network
and broadcast address = 253 usable.

## F. Computer architecture questions

**Where does my data physically live?**
> The simulated value is in kernel memory (a static `int` in the module). It is
> copied into a stack struct in user space, then copied into a
> `TelemetryRecord`, then formatted into a `std::string` on the heap, then
> written to a page-cache buffer that the kernel later flushes to the disk. The
> executable itself and its shared libraries are demand-paged from the
> executable file.

**What is virtual memory doing for me?**
> Every process gets its own address space. The monitor can believe it owns
> address 0x7fff... and so can the client, and neither can read the other's
> memory. That is also why the kernel must not dereference a user pointer
> directly.

**What is a cache, and does my logger care?**
> The cache holds recently used memory close to the CPU. The logger writes
> through a `std::ofstream` buffer and flushes each line, so most of the work
> is a copy into a kernel page cache, and the actual disk write happens later
> in another context. That is a deliberate trade: I give up a little I/O
> batching to guarantee the newest event is never lost.

**What about branch prediction in my own code?**
> `ThresholdPolicy::evaluate` is a two-compare chain. The common case in a
> steady approach is "not danger, not safe" so the WARNING branch is taken
> last; at 150 cm falling in 10 cm steps it is rarely predictable until the
> state settles, then fully predictable. I have not optimised for it - the
> function is a handful of instructions and clarity matters more.

**Where is the multicore parallelism in my project?**
> Three threads in one process. The OS can run them on different cores, which
> is why the log write of iteration *n* overlaps the read of iteration *n+1*.
> The shared queue is what creates the possibility of a race, and the mutex is
> what prevents it.

## G. Testing questions

**What is a unit test versus an integration test here?**
> A unit test exercises one class with no I/O - the threshold table is the
> clearest example, it is pure logic. An integration test assembles the real
> classes and checks that data survives the whole path, for example that
> DANGER actually appears in the log file with `distance=20`.

**How do you test a kernel driver?**
> In order: build, `insmod`, check the node exists, read it, write the control
> commands, verify the value moves, try an invalid command, then `rmmod` and
> check `dmesg` for errors. `scripts/load_driver.sh` does exactly that sequence
> so the result is repeatable.

**How do you test error handling?**
> By making the error happen. `FailingSource` in
> `test_monitor_integration.cpp` throws on every read, and the test asserts the
> monitor completed its iterations, counted 3 read errors and stayed alive -
> which is FR-16.

**What would break this project in production?**
> A real ultrasonic sensor needs a GPIO or I2C read, a timestamped echo
> measurement and filtering, and a kernel thread rather than a timer. The
> driver would grow, but the user-space half would not change at all, because
> it only sees a distance in centimetres. That separation is the main design
> result of the project.

## H. Questions I should ask them

- "Which part should I go deeper on - the driver or the application?"
- "Is the viva about what you built, or about the syllabus topics it covers?"
- "Do you want the diagrams in the report or as a separate pack?"
