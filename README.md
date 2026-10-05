# Virtual Ultrasonic Sensor

![Language](https://img.shields.io/badge/language-C%2B%2B17-blue)
![Language](https://img.shields.io/badge/driver-C%20(kernel%20module)-orange)
![Platform](https://img.shields.io/badge/platform-Linux-lightgrey)
![Build](https://img.shields.io/badge/build-make%20%26%26%20g%2B%2B-green)

A Linux virtual ultrasonic parking sensor: a character-device driver that
simulates a distance value, a C++ monitoring application that turns it into
`SAFE` / `WARNING` / `DANGER` and logs it, and a TCP client that reads the
current status.

Built for a college software-engineering project. **No external libraries, no
frameworks, no services** - only the Linux kernel, the C++17 standard library
and POSIX. Everything builds with `g++` and `make`.

```
  simulated distance (kernel timer)
        |
        v
  /dev/parking_sensor      driver/parking_sensor.c
        |  read() -> struct parking_sensor_data
        v
  ISensorSource            app/src/sensor_reader.cpp
        |
        v
  ThresholdPolicy          app/src/state_manager.cpp   distance -> state
        |
        v
  Logger  +  TCPStatusServer          app/src/logger.cpp, tcp_status_server.cpp
        |                              |
        v                              v
  logs/parking.log            ./build/status_client
```

## What it does

| | |
|---|---|
| Simulates a distance | walks 150 cm down to 10 cm and back, on a kernel timer |
| Classifies it | `> 50` SAFE, `21..50` WARNING, `<= 20` DANGER |
| Logs it | appends a timestamped line, written by a dedicated thread |
| Publishes it | serves `STATUS distance=37 state=WARNING` over IPv4 TCP |
| Stops cleanly | `SIGINT` / `SIGTERM`, `--iterations`, or daemon mode |

## Quick start

### Everything runs without root

The driver needs root and kernel headers. The application half does not, so
`--simulate` substitutes an in-process sensor that implements the same
`ISensorSource` interface. This exercises the real state machine, logger, TCP
server and shutdown path:

```bash
make                 # builds build/parking_monitor, status_client, run_tests
make test            # runs the 41-test suite

# terminal 1
./build/parking_monitor --simulate --iterations 20

# terminal 2
./build/status_client --port 9000 --count 3
```

Watch the log while it runs:

```bash
tail -f logs/parking.log
# 2026-09-28 19:02:11 | distance=80 | state=SAFE | event=periodic_read
# 2026-09-28 19:02:12 | distance=60 | state=SAFE | event=periodic_read
# 2026-09-28 19:02:13 | distance=50 | state=WARNING | event=state_change
```

### The full run with the driver

```bash
make driver                  # needs the headers for `uname -r`
sudo ./scripts/load_driver.sh        # insmod + acceptance test
./build/parking_monitor --config app/parking.conf
```

If module loading is not permitted on the machine, the simulated run above is
the demonstration, and the report says so plainly rather than claiming a run
that did not happen.

## Layout

```
include/parking_sensor.h   protocol shared by the driver and user space
driver/                    the kernel module and its Makefile
app/                       the C++ monitor (include/, src/, parking.conf)
client/                    the TCP status client
tests/                     the test suite and its one-page harness
scripts/                   build, load, demo, clean, test, debug helpers
docs/                      report, requirements, demo script, viva Q&A
uml/                       PlantUML sources; docs/uml.md has the ASCII version
training_examples/         syllabus topics the project itself does not need
logs/                      runtime output (git-ignored)
```

`training_examples/` is optional and separate. Nothing in the monitor depends
on it. It exists so the syllabus topics a parking sensor has no use for - tries,
memory pools, QEMU, jiffies - are demonstrated honestly instead of being
claimed.

## Command line

```
--config <file>   configuration file (key = value, # comments)
--device <path>   device path (default /dev/parking_sensor)
--log <file>      log file (default logs/parking.log)
--interval <ms>   polling interval (default 1000)
--warning <cm>    WARNING threshold (default 50)
--danger <cm>     DANGER threshold (default 20)
--host <ip>       TCP bind address (default 127.0.0.1)
--port <n>        TCP port (default 9000)
--no-tcp          do not start the status server
--simulate        use the in-process sensor instead of the driver
--daemon          detach into the background
--iterations <n>  stop after n polls (0 = forever)
```

The configuration file is read first, then the command line overrides it. Bad
values are reported and the default is kept, so a typo never stops the monitor.

## Scripts

| Script | What it does |
|---|---|
| `scripts/build.sh` | builds everything; `--driver` also builds the module |
| `scripts/load_driver.sh` | `insmod`, checks `/dev`, reads, writes, `rmmod` |
| `scripts/run_demo.sh` | scripted presentation run, driver or simulated |
| `scripts/test_all.sh` | build, unit tests, and a live client/server check |
| `scripts/cleanup.sh` | unload the module and remove all build output |
| `scripts/debug_helper.sh` | `dmesg`, `strace`, `gdb`, sysfs and log inspection |

## Documentation

| File | Purpose |
|---|---|
| `docs/architecture.md` | layers, components, the control loop, design decisions |
| `docs/requirements.md` | every SRS requirement mapped to code and a test |
| `docs/test-report.md` | the test suite, what it covers, how to run it |
| `docs/demo-script.md` | a timed presentation script, word for word |
| `docs/interview-qa.md` | the questions an examiner actually asks |
| `docs/syllabus-traceability.md` | every syllabus topic mapped to a file |
| `docs/stage-evidence.md` | what was done in which stage, for the journal |
| `docs/debugging-notes.md` | `dmesg`, `strace`, `gdb`, `readelf`, sysfs |
| `docs/uml.md` | use case, class, sequence and state diagrams (ASCII) |

## Requirements

- Linux (any distribution); the driver needs the headers for the running kernel
- `g++` with C++17 support, `make`
- root, to load the module - not needed for the simulated run
- `strace`, `gdb`, `od` for the debugging helpers (optional)

## Design decisions worth knowing

- **The driver does not interpret anything.** It reports a distance. Deciding
  that 37 cm is WARNING belongs to user space, and that split is the point of
  the whole project.
- **Interfaces, not `if` statements.** `ISensorSource` is what lets
  `--simulate` swap the driver out; `StatePolicy` is what makes the thresholds
  testable and replaceable.
- **Plain TCP on loopback, no TLS.** The payload is a distance and a state.
  40 bytes a second on `127.0.0.1` cannot be reached from the network, and
  adding a TLS stack would add a dependency and no security.
- **Tests without a test framework.** A 60-line harness in `tests/` keeps the
  build to `g++` and `make`, which is what the environment guarantees.
- **Failures are visible.** A missing device, a bad config value or a failed
  read produces a message and a counter, never a silent wrong state.

## Licence

The driver is `MODULE_LICENSE("GPL")`, as kernel code must be. Everything else
is part of the project submission.
