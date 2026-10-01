# Requirements and Traceability

The SRS is the source of truth: `Virtual_Ultrasonic_Parking_Sensor_SRS_4_10.pdf`.
This file maps every functional requirement to the code that satisfies it, the
test that checks it and the evidence to show in the report.

## 1. Functional requirements

| ID | Requirement | Implementation | Test / evidence |
|---|---|---|---|
| FR-01 | Support loading and unloading the character driver | `driver/parking_sensor.c`: `module_init` / `module_exit`, `insmod` / `rmmod` | `scripts/load_driver.sh` steps 3 and 9; `dmesg \| grep parking_sensor` |
| FR-02 | Expose `/dev/parking_sensor` | `alloc_chrdev_region` + `cdev_add` + `class_create` + `device_create` | `scripts/load_driver.sh` step 4; `ls -l /dev/parking_sensor` |
| FR-03 | Provide simulated distance values in cm | `parking_timer_callback()` walks `distance_cm` between `min_cm` and `max_cm` | `scripts/load_driver.sh` step 7; module params in step 3 |
| FR-04 | User space can read the current distance | `parking_read()` + `copy_to_user`; `DeviceSensorReader::read_distance_cm()` | `scripts/load_driver.sh` step 5; `tests/test_sensor_and_tcp.cpp` |
| FR-05 | Simple control command through `write()` | `parking_write()` + `copy_from_user`: `reset`, `pause`, `resume`; unknown command -> `-EINVAL` | `scripts/load_driver.sh` steps 6 and 8 |
| FR-06 | Application reads the sensor periodically | `ParkingMonitor::run()` loop, `poll_interval_ms` from the config | `docs/test-report.md` system test |
| FR-07 | Map distance to SAFE / WARNING / DANGER | `ThresholdPolicy::evaluate()` | `tests/test_state_manager.cpp::test_threshold_boundaries` |
| FR-08 | Detect and log state transitions | `StateManager::update()` sets `event = "state_change"` and bumps `transitions_` | `test_steady_state_is_a_periodic_read`, `test_full_transition_sequence` |
| FR-09 | Append timestamped readings/events to a log file | `Logger` (queue + writer thread, append mode) | `test_logger_writes_every_line`, `test_logger_appends_instead_of_truncating` |
| FR-10 | Background / daemon-style execution | `--daemon`, double `fork()` + `setsid()` in `main.cpp` | demo step 4; `ps -o pid,ppid,stat,cmd` |
| FR-11 | Handle a termination signal, release resources | `handle_stop_signal()` sets an atomic flag; destructors + `stop()` unwind | `test_all.sh system` step 4; `monitor_stop` in the log |
| FR-12 | IPv4 TCP server for the current status | `TCPStatusServer`: `socket` / `bind` / `listen` / `accept` / `send` | `test_server_reports_the_published_status` |
| FR-13 | A client connects and displays the status | `client/status_client.cpp` | `test_all.sh system` step 3; demo step 6 |
| FR-14 | Thresholds and interval in a configuration file | `MonitorConfig::load()`, `app/parking.conf` | `tests/test_config.cpp` (7 cases) |
| FR-15 | Foreground demo mode for the presentation | default is foreground; `scripts/run_demo.sh` | `docs/demo-script.md` |
| FR-16 | Report failures without crashing | `try/catch` in `main()` and in the loop; `read_errors_` counter; `-EINVAL` from the driver | `test_monitor_survives_a_failing_sensor` |
| FR-17 | Evidence: screenshots, logs, tests, Git history | `logs/`, `docs/test-report.md`, `docs/stage-evidence.md`, Git log | `docs/stage-evidence.md` |

## 2. Non-functional requirements

| Requirement | How it is met | Where to look |
|---|---|---|
| Usability | every message is prefixed `[monitor]`, `[tcp]`, `[config]`, `[main]`; `--help` on every binary | `main.cpp` `print_usage()` |
| Reliability | one failed read is counted and retried; socket errors are logged, not fatal | `parking_monitor.cpp` catch block |
| Performance | timer in the kernel, `sleep` in user space, no busy-wait; disk I/O moved to a second thread | `docs/architecture.md` section 4 |
| Maintainability | one class per responsibility, clear headers, no global state except the stop flag | `app/src/` |
| Portability | targets Linux; the user-space half is plain POSIX C++ and also runs on macOS/WSL | `app/Makefile` |
| Traceability | this table + `docs/syllabus-traceability.md` | - |

## 3. Data format requirements

| Item | Specification | Implementation |
|---|---|---|
| Telemetry record | timestamp, distance, state, event | `struct TelemetryRecord` in `app/include/telemetry.h` |
| Device read payload | fixed struct so one `copy_to_user()` is enough | `struct parking_sensor_data` in `include/parking_sensor.h` |
| Log line | `YYYY-MM-DD HH:MM:SS \| distance=N \| state=X \| event=Y` | `format_record()` in `app/src/telemetry.cpp` |
| TCP message | `STATUS distance=N state=X\n` | `format_status_message()` in `app/src/tcp_status_server.cpp` |
| Config file | `key = value`, `#` comments, blanks ignored | `MonitorConfig::load()` |

## 4. Acceptance criteria

| Criterion (SRS 21) | Status | Proof |
|---|---|---|
| Driver loads/unloads without fatal errors | done | `scripts/load_driver.sh`, `dmesg` output |
| `/dev/parking_sensor` accessible to the monitor | done | `ls -l /dev/parking_sensor`, monitor starts without error |
| Distance values change as configured | done | demo step 5, decreasing then increasing |
| Correct SAFE / WARNING / DANGER states | done | `tests/test_state_manager.cpp`, log file |
| State changes and readings are logged | done | `logs/parking.log` |
| TCP client receives the status | done | `logs/client_output.log` |
| Clean shutdown on the intended signal | done | `monitor_stop` is the last log line |
| At least one unit, one integration and one end-to-end test recorded | done | `docs/test-report.md` |
| Git history shows the six stages | done | `docs/stage-evidence.md`, `git log --oneline` |
| UML, requirements, test results, traceability in the repo | done | `uml/`, `docs/` |

## 5. Out of scope, and why

| Excluded | Reason |
|---|---|
| real ultrasonic hardware, GPIO, ECU | not available in a college lab; the driver simulates the device instead |
| custom system calls, interrupt controller work | needs kernel patches; adds risk without improving the demonstration |
| cryptography, authentication, TLS | SRS allows plain local TCP for the demo |
| databases, machine learning, cloud, Docker/Kubernetes | outside the topic boundary given for this project |
| spinlocks, tasklets, softirqs, workqueues in the core | real driver topics, but they are not needed by a 150 cm -> 10 cm simulation; they are written up in `training_examples/driver_notes/` instead |
