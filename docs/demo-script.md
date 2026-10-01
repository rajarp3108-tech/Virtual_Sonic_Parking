# Demonstration Script

Ten minutes, live, in front of the evaluator. Run it once before you present so
you know the timings. The full automated version is `scripts/run_demo.sh`; this
document tells you **what to say** while it runs.

## Before you start

- Terminal large enough to read from the back of the room.
- Two terminals: **A** for the monitor, **B** for everything else.
- `sudo` password ready (only if you are doing the driver part).
- `cd` into the project root in both terminals.

---

## Step 1 - What the project is (30 s, terminal B)

```
$ pwd && ls
$ cat README.md | head -n 25
```

> "This is a Linux virtual ultrasonic parking sensor. A simulated sensor value
> is produced in the kernel, exposed as a character device, read by a C++
> application that turns it into a safe/warning/danger state, logs it, and
> publishes it over TCP. No external library is used - only g++, make and the
> C++ standard library."

## Step 2 - Show the architecture (30 s, terminal B)

```
$ cat docs/architecture.md | sed -n '1,40p'
$ ls uml/
```

> "Four layers, one direction. The driver does not interpret anything; the
> application never touches the driver's variables. Everything crosses the
> boundary through one struct and one file descriptor."

## Step 3 - Build (1 min, terminal B)

```
$ ./scripts/build.sh
```

> "Three binaries: the monitor, the TCP client, and the test runner. The
> kernel module is a separate step because it is the only one that needs the
> kernel headers."

## Step 4 - Load the driver (1 min, terminal B) - needs sudo

```
$ sudo ./scripts/load_driver.sh
```

Call out as it scrolls:

> "The module registers a character device and creates `/dev/parking_sensor`
> through a device class - that class is what makes the node appear. Then a
> kernel timer moves the distance: down to `min_cm`, then back up to `max_cm`.
> And notice step 8: an unknown command is rejected with `-EINVAL` instead of
> being silently ignored."

```
$ dmesg | tail -n 10
```

> "Every driver message is `printk`, and it lands in the kernel ring buffer -
> that is the evidence for FR-01 and FR-03."

```
$ od -An -tu4 -N4 /dev/parking_sensor
```

> "`od` reads the first four bytes of the struct, which is the distance. The
> real reader is the C++ application, this is just a quick sanity check."

## Step 5 - Run the tests (1 min, terminal B)

```
$ ./scripts/test_all.sh unit
```

> "41 test cases with no test framework - the harness is 60 lines in
> `tests/test_harness.h`. The important one is the boundary table: 51 cm is
> SAFE, 50 cm is WARNING, 21 cm is WARNING, 20 cm is DANGER. The boundaries are
> where state machines normally have bugs, so they are tested explicitly."

## Step 6 - Run the monitor and watch the state change (2 min, terminal A)

```
$ ./build/parking_monitor --config app/parking.conf
```

Point at the screen as it happens:

```
[monitor] source      = device
[monitor] policy      = ThresholdPolicy(SAFE if d > 50 cm, DANGER if d <= 20 cm, otherwise WARNING)
[monitor] running - press Ctrl-C to stop
[monitor] distance=140 cm state=SAFE
[monitor] distance=90 cm state=SAFE
[monitor] STATE CHANGE -> WARNING at 50 cm
[monitor] distance=40 cm state=WARNING
[monitor] STATE CHANGE -> DANGER at 20 cm
[monitor] distance=10 cm state=DANGER
[monitor] STATE CHANGE -> WARNING at 30 cm
[monitor] STATE CHANGE -> SAFE at 60 cm
```

> "The driver moves 10 cm every 500 ms and the monitor polls every second, so
> the numbers step by ten. Watch the transition lines - the monitor only calls
> out a line when the state actually changes, everything else is a periodic
> read. That is FR-08."

## Step 7 - The log file (45 s, terminal B)

```
$ tail -n 12 logs/parking.log
$ grep -o 'state=[A-Z]*' logs/parking.log | sort | uniq -c
$ wc -l logs/parking.log
```

> "One line per poll, in the exact format the SRS appendix asks for:
> timestamp, distance, state, event. The disk write happens on a separate
> thread fed by a queue, so the polling loop never blocks on I/O."

## Step 8 - The TCP client (1 min, terminal B)

While the monitor is still running in terminal A:

```
$ ./build/status_client --host 127.0.0.1 --port 9000 --count 3 --interval 1000
[client] connected to 127.0.0.1:9000
[client] <- STATUS distance=37 state=WARNING
[client] connected to 127.0.0.1:9000
[client] <- STATUS distance=18 state=DANGER
[client] done, 3/3 status messages received
```

> "Plain IPv4 TCP, no HTTP. The server publishes a snapshot on every poll and
> sends that snapshot to whoever connects, so a client that connects late still
> gets a valid reading. Note that each request is a new connection - that is the
> disconnect/reconnect case from the test plan."

```
$ ss -tlnp | grep 9000
```

> "One listening socket, owned by the monitor process."

## Step 9 - Threads and processes (30 s, terminal B)

```
$ ps -o pid,ppid,stat,cmd -C parking_monitor
$ ls /proc/$(pgrep parking_monitor)/task | wc -l
$ cat /proc/$(pgrep parking_monitor)/status | grep Threads
```

> "Three threads in one process: the control loop, the log writer, and the TCP
> accept thread. That is the multithreading and ILP part of the syllabus -
> separate concerns, separate threads, one mutex-protected queue between them."

## Step 10 - Clean shutdown (45 s, terminal A)

Press **Ctrl-C**, then in terminal B:

```
$ tail -n 4 logs/parking.log
$ pgrep parking_monitor      # nothing
```

> "Ctrl-C sets one atomic flag - the signal handler does nothing else, because
> only setting a flag is async-signal-safe. The loop notices, closes the socket,
> drains the queue, joins the writer thread and writes `monitor_stop`. Every one
> of those steps is a destructor or an explicit stop, so nothing leaks. That is
> FR-11."

## Step 11 - Daemon mode (30 s, optional)

```
$ ./build/parking_monitor --simulate --daemon --port 9001
[main] running in background as pid 12345
$ ./build/status_client --port 9001 --count 2
$ kill -TERM 12345
```

> "Double fork, `setsid`, standard streams redirected. The shell prompt comes
> straight back, which is what FR-10 asks for."

## Step 12 - Binutils evidence (30 s, optional but impressive)

```
$ ./scripts/debug_helper.sh build
```

> "`readelf` shows the ELF header and the section table, `nm` shows the C++
> symbols and the vtables, `objdump` shows the disassembly of
> `StateManager::update`. That is the link back to the computer-architecture
> part of the syllabus: the compiled code is machine instructions, and these
> are the tools that let you look at it."

## Step 13 - Close (20 s)

> "To summarise: a character device driver produces simulated distance values
> with a kernel timer; a C++ application reads them, applies a three-state
> machine, logs every reading and publishes the status over TCP. Four UML
> diagrams, 41 tests, six development stages in Git, and the syllabus
> traceability table. Questions?"

---

## Fallback plan

| Problem | What to do |
|---|---|
| `insmod: Operation not permitted` | secure boot or container. Say "the user-space half needs no root" and run Step 6 with `--simulate`. The state machine, logger and TCP path are identical. |
| `/dev/parking_sensor` not created | udev missing in the VM. `scripts/load_driver.sh` falls back to `mknod`; if that also fails, use `--simulate`. |
| `make -C driver` fails | kernel headers missing. `sudo apt install linux-headers-$(uname -r)`. Meanwhile run everything else. |
| port 9000 busy | `--port 9100` on the monitor and on the client. |
| the machine is slow | use `--interval 300` and `--iterations 34`, or `--fast` with `run_demo.sh`. |

## Screenshot checklist

Shoot these while the demo runs, and commit them to `docs/stage-evidence/`:

1. `dmesg` showing the driver load banner.
2. `ls -l /dev/parking_sensor`.
3. The monitor showing a `STATE CHANGE -> DANGER` line.
4. `tail logs/parking.log`.
5. The client receiving `STATUS ...`.
6. The unit test summary.
7. `git log --oneline --graph --all`.
