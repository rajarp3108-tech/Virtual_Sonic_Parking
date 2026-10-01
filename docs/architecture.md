# System Architecture

## 1. One-line summary

```
simulated distance in the kernel
   -> /dev/parking_sensor (character device)
      -> C++ monitor (state machine + logger + TCP server)
         -> log file  and  TCP status client
```

Everything below expands that one line.

## 2. Layered view

```
+=====================================================================+
|  L4  Presentation          terminal output, log file view, status   |
|                           client output                             |
+---------------------------------------------------------------------+
|  L3  Service              TCPStatusServer   Logger                 |
|                           (TCP/IPv4)         (file + writer thread)  |
+---------------------------------------------------------------------+
|  L2  Business logic       StateManager + ThresholdPolicy           |
|                           distance -> SAFE / WARNING / DANGER        |
+---------------------------------------------------------------------+
|  L1  Abstraction          ISensorSource (interface)                |
|                           StatePolicy  (interface)                 |
+---------------------------------------------------------------------+
|  L0  System / device      DeviceSensorReader  -> open/read/write   |
|                           kernel driver      -> char device + timer |
+=====================================================================+
|  Hardware (simulated)     a "sensor" that changes value on a timer  |
+=====================================================================
```

Each layer only knows the layer below it through an interface. That is why
`--simulate` can replace the driver without touching a single line of the
state machine, the logger or the server.

## 3. Component responsibilities

| Component | File | Owns | Must not |
|---|---|---|---|
| `parking_sensor` (driver) | `driver/parking_sensor.c` | the simulated distance, the timer, the device node | interpret the distance, log to a file, open a socket |
| `DeviceSensorReader` | `app/src/sensor_reader.cpp` | one file descriptor | decide what a state is |
| `StateManager` + `ThresholdPolicy` | `app/src/state_manager.cpp` | the current state, the transition counter | touch the file descriptor or the socket |
| `Logger` | `app/src/logger.cpp` | the log file, the queue, the writer thread | block the polling loop |
| `TCPStatusServer` | `app/src/tcp_status_server.cpp` | the listening socket, the accept thread | read the device or write the log |
| `ParkingMonitor` | `app/src/parking_monitor.cpp` | the control loop and the stop flag | implement any rule itself |
| `main` | `app/src/main.cpp` | argv parsing, object construction, signals | contain business logic |

`Driver must not interpret` is the important one. The driver answers "the
distance is 37 cm". Deciding that 37 cm means WARNING is the application's
job. This split is what the driver chapter of the syllabus is about, and it
is the first question an interviewer will ask.

## 4. Runtime flow of one poll

```
t=0     kernel timer fires in parking_timer_callback()
        distance_cm += direction * step_cm          (under the mutex)
        distance_cm reflected at min_cm / max_cm
        mod_timer() re-arms the same timer for +interval_ms

t=0+    monitor thread wakes from its 50 ms sleep slice
        read(fd, &data, sizeof data)
        driver: mutex_lock -> fill struct -> copy_to_user -> mutex_unlock
        app:    data.distance_cm = 37

t=0+    StateManager::update(37)
        ThresholdPolicy::evaluate(37) -> WARNING
        previous state was SAFE -> event = "state_change", transitions_++

t=0+    TelemetryRecord(37, WARNING, "state_change")
        Logger::log()  -> queue.push() -> notify writer thread
        TCPStatusServer::publish(37, WARNING) -> latest_message_

t=0+    writer thread pops the queue, formats the line, writes + flushes
```

Between `t=0` and the next `t=0+interval_ms` the CPU is idle. There is no
busy-wait anywhere: the kernel uses a timer, and the application sleeps. That
is the answer to the "performance / no busy waiting" non-functional
requirement.

## 5. Thread model

The monitor is one process with three threads:

| Thread | Created by | Job | Sync |
|---|---|---|---|
| main | `main()` | the control loop | reads its own fields only |
| logger writer | `Logger::start()` | format and write log lines | `mutex` + `condition_variable` over `pending_` |
| TCP accept | `TCPStatusServer::start()` | accept, send snapshot, close | `mutex` over `latest_message_` |

Why not a third one for the socket? Because a request is a single
write-and-close, so the accept thread can serve it inline. Adding a thread
per client would be a real improvement for many clients, and it is listed in
the future scope of the report.

Signal safety: the handler in `main.cpp` only does
`g_stop_requested.store(true)`. Everything else - closing sockets, joining
threads, flushing the file - happens in the control loop, which is ordinary
code. This is why the log always ends with a clean `monitor_stop` line.

## 6. The user/kernel boundary

```
   user space                          kernel space
   --------------------------------    --------------------------------
   open("/dev/parking_sensor", O_RDWR) -> alloc_chrdev_region + cdev_add
   read(fd, &data, 16)              -> parking_read()  -> copy_to_user()
   write(fd, "reset", 5)            -> parking_write() -> copy_from_user()
   close(fd)                        -> parking_release()
```

Points worth stating in a viva:

- the application **never** sees the driver's C variables; it sees a fixed
  struct declared in the shared header `include/parking_sensor.h`;
- `copy_to_user` / `copy_from_user` are mandatory, because user addresses are
  not valid in kernel context - a raw `memcpy` to a user pointer would be a
  kernel bug;
- the mutex in the driver is needed because two execution contexts touch the
  same state: the timer callback and the syscall. Sleeping is allowed in both,
  which is why a mutex is right here and a spinlock would be wrong.

## 7. Design principles actually applied

| Principle | Where | Evidence |
|---|---|---|
| SRP | one class per job: reader, policy, state, logger, server, monitor | `app/src/*.cpp` |
| OCP | new threshold rule = new class implementing `StatePolicy` | `AlwaysSafePolicy` in `tests/test_state_manager.cpp` |
| LSP | `DeviceSensorReader` and `SimulatedSensorReader` are both usable as `ISensorSource` | `tests/test_sensor_and_tcp.cpp` |
| ISP | the two interfaces are small on purpose (1-3 methods) | `app/include/sensor_reader.h` |
| DIP | `ParkingMonitor` depends on `ISensorSource`, not on `DeviceSensorReader` | `parking_monitor.cpp` constructor |
| DRY | the log line is formatted once, in `format_record()` | reused by `Logger` and by the test |
| KISS | no framework, no external library, plain `g++` + `make` | `app/Makefile` |
| RAII | the fd closes in the destructor; the socket closes in `stop()`; threads join | `sensor_reader.cpp`, `tcp_status_server.cpp` |

## 8. Why there is a simulator as well as a driver

The driver is the product, and it is what the demo shows. The in-process
`SimulatedSensorReader` exists for two practical reasons:

1. **repeatability of the tests** - `tests/` proves the state machine works
   without root, without QEMU and without a kernel build;
2. **a demo on any machine** - `./build/parking_monitor --simulate` runs the
   complete user-space flow on a laptop, which is what to use when the college
   machine will not let you load a module.

It implements the same interface, so it is substitutable, and the live path
never uses it unless `--simulate` is given.
