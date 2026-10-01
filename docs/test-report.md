# Test Plan and Test Report

Two parts: the **plan** (what should be tested and how) and the **report**
(fill in the actual numbers from your run - do not leave it as a template).

Run everything with:

```bash
./scripts/build.sh
./scripts/test_all.sh all          # add: sudo ./scripts/test_all.sh driver
```

---

# Part 1 - Test Plan

## 1.1 Strategy

Four levels, matching SRS section 18.

| Level | Scope | How it runs | Needs root |
|---|---|---|---|
| Unit | one class, no I/O | `build/run_tests` | no |
| Integration | the whole object graph, simulated sensor | `build/run_tests` | no |
| Driver | the kernel module | `scripts/load_driver.sh` | yes |
| System | real processes, real TCP socket, signals | `scripts/test_all.sh system` | no |

Test data is deterministic. The simulated sensor moves 10 cm per read from
150 cm down to 10 cm and back, so every expected value can be written down in
advance instead of being discovered while running the test.

**One binary, one `main()`.** All the unit and integration cases link into
`build/run_tests`. Each `test_*.cpp` registers its cases with `TEST_LIST` and
defines no `main` of its own; `tests/test_main.cpp` owns the single entry point
and runs the combined registry. That is deliberate - it keeps the build to
`g++` and `make`, and it means a case can never be added to a file and silently
missed from a hand-maintained list. It also means there is no way to run only
part of the suite, which is why `test_all.sh unit` and `test_all.sh integration`
both execute the whole binary.

## 1.2 Unit test cases

### State machine (FR-07, FR-08) - `tests/test_state_manager.cpp`

| Case | Input | Expected |
|---|---|---|
| threshold boundaries | 150, 51, 50, 49, 21, 20, 19, 10, 0 | SAFE, SAFE, WARNING, WARNING, WARNING, DANGER, DANGER, DANGER, DANGER |
| invalid policy arguments | `ThresholdPolicy(20, 50)` | throws `std::invalid_argument` |
| custom thresholds | policy(100, 60) with 120 / 100 / 60 | SAFE / WARNING / DANGER |
| substitutable policy | `AlwaysSafePolicy` with 1 and 999 cm | SAFE both times |
| first reading is an event | `update(120)` | returns true, event `state_change` |
| steady state | `update(120)` then `update(110)` | returns false, event `periodic_read` |
| full sequence | 150,120,90,60,50,40,30,20,10,20,...,150 | exactly 4 transitions, ends in SAFE |
| reset | after transitions | `initialised == false`, counter 0 |
| corrupt reading | -5 cm | DANGER (no fall-through to SAFE) |

### Configuration (FR-14) - `tests/test_config.cpp`

| Case | Input | Expected |
|---|---|---|
| no file argument | `load("")` | built-in defaults |
| missing file | non-existent path | defaults, no exception |
| full file | all keys, inline comment | every value applied |
| junk lines | no `=`, unknown key, blank lines | ignored with a message, good keys still applied |
| bad value | `poll_interval_ms = soon` | default kept, `tcp_port = 12345` still applied |
| impossible thresholds | warning 20, danger 50 | repaired to 50 / 20 |
| bad port / tiny interval | 99999 / 1 ms | repaired to 9000 / 1000 |
| boolean spellings | `yes` | true |

### Log and telemetry (FR-09) - `tests/test_logger_and_telemetry.cpp`

| Case | Expected |
|---|---|
| state names | `SAFE`, `WARNING`, `DANGER` exactly |
| record format | contains `distance=80 \| state=SAFE \| event=periodic_read` |
| timestamp shape | 19 characters, separators at 4, 7, 10, 13, 16 |
| empty event | rendered as `event=none` |
| 20 records + 1 event | exactly 21 lines in the file |
| append mode | a second run adds to the file, does not truncate |
| 4 threads x 25 records | 100 complete lines, no interleaving (mutex works) |
| bad path | throws, no crash |

### Sensor and TCP (FR-03, FR-12, FR-13) - `tests/test_sensor_and_tcp.cpp`

| Case | Expected |
|---|---|
| simulator start | first read is 140 (moved one step from 150) |
| range over 60 reads | lowest 10, highest 150 |
| 500 random-length reads | always within `[min, max]` |
| reset | ticks 0, next read 140 |
| message format | `STATUS distance=37 state=WARNING\n` etc. |
| real client | connects to an ephemeral port, receives the published status |
| reconnect | a second client sees the updated status |
| idle server | starts and stops with no client ever connecting |
| bad bind address | `not-an-ip` throws |
| port already in use | second server throws |

### End-to-end integration - `tests/test_monitor_integration.cpp`

| Case | Expected |
|---|---|
| 30 polls | the log contains SAFE, WARNING and DANGER, plus `monitor_start` / `monitor_stop` |
| first DANGER line | `distance=20` exactly, never 19 or 21 |
| sequence shape | readings fall, reach the minimum, then rise |
| failing sensor | 3 iterations, 3 read errors, monitor survives (FR-16) |

## 1.3 Driver test cases - `scripts/load_driver.sh`

| Step | Check | Expected |
|---|---|---|
| 1 | build the module | `parking_sensor.ko` produced |
| 2 | module already loaded | removed cleanly first |
| 3 | `insmod` with parameters | success, `dmesg` shows the load banner |
| 4 | `/dev/parking_sensor` exists | `c` type, correct major/minor (mknod fallback) |
| 5 | `read` | returns 150 cm (the `start_cm` parameter) |
| 6 | `write` pause / resume / reset | all accepted, value frozen while paused |
| 7 | 4 reads over 2 s | the value decreases by `step_cm` each time |
| 8 | `write nonsense` | rejected, `write` returns `-EINVAL` |
| 9 | `rmmod` | success, `dmesg` shows `unloaded`, no oops in the log |

## 1.4 System test cases - `scripts/test_all.sh system`

| Case | Expected |
|---|---|
| foreground run, 3 iterations | process exits by itself, exit code 0 |
| log content | `state=SAFE` and `event=state_change` present |
| monitor + client on port 9401 | client receives `STATUS ...` |
| `kill -TERM` | process exits 0 |
| shutdown record | `monitor_stop` in the log |

## 1.5 Entry and exit criteria

- **Entry:** `make` succeeds with no warnings, `build/parking_monitor --help` runs.
- **Exit:** every case above passes, zero unexplained failures, results written below.

---

# Part 2 - Test Report

> Fill this in from your own run. Keep the raw output in `logs/` and commit it.
> A report full of real numbers is worth more than a perfect template.

## 2.1 Environment

| Item | Value |
|---|---|
| Date | _(fill in)_ |
| OS / kernel | _(fill in: `uname -srm`)_ |
| Distribution | _(fill in: `cat /etc/os-release`)_ |
| Compiler | _(fill in: `g++ --version`)_ |
| Make | _(fill in: `make --version`)_ |
| Kernel headers | _(fill in: `ls /lib/modules/$(uname -r)/build`)_ |
| Git commit under test | _(fill in: `git rev-parse --short HEAD`)_ |

## 2.2 Build result

```
$ ./scripts/build.sh
==> Toolchain
g++ (Ubuntu ...) 11.4.0
GNU Make 4.3
kernel  : Linux 5.15.0-... x86_64
==> Building the C++ monitor (FR-06 .. FR-13)
...
==> Build complete
```

Compile warnings: **(fill in: zero / list them)**
Observed result: **(fill in: PASS / FAIL)**

## 2.3 Unit and integration test result

```
$ make -C tests run
  [ RUN  ] test_threshold_boundaries
  [  OK  ] test_threshold_boundaries
  ...
<N> checks run, 0 failed
ALL TESTS PASSED
```

| Suite | Tests | Checks | Failed | Result |
|---|---|---|---|---|
| state manager | 9 | _(fill)_ | 0 | PASS |
| config parsing | 9 | _(fill)_ | 0 | PASS |
| log and telemetry | 9 | _(fill)_ | 0 | PASS |
| sensor and TCP | 10 | _(fill)_ | 0 | PASS |
| monitor integration | 4 | _(fill)_ | 0 | PASS |
| **total** | **41** | _(fill)_ | **0** | **PASS** |

## 2.4 Driver test result

```
$ sudo ./scripts/load_driver.sh
==> 3. Loading the module (FR-01)
  [ok]   insmod returned success
  [ok]   driver log: parking_sensor: loaded. /dev/parking_sensor major 240 minor 0, range 10..150 cm step 10 cm every 500 ms
...
==> Driver acceptance test PASSED
```

| Step | Expected | Observed | Result |
|---|---|---|---|
| insmod | success | _(fill)_ | _(fill)_ |
| device node | `/dev/parking_sensor` | _(fill)_ | _(fill)_ |
| first read | 150 cm | _(fill)_ | _(fill)_ |
| pause | value frozen | _(fill)_ | _(fill)_ |
| movement | decreasing | _(fill)_ | _(fill)_ |
| invalid command | rejected | _(fill)_ | _(fill)_ |
| rmmod | clean unload | _(fill)_ | _(fill)_ |

Kernel warnings observed in `dmesg` after the run: **(fill in: none / list)**

## 2.5 System test result

| Case | Expected | Observed | Result |
|---|---|---|---|
| 3-iteration run | exits 0 | _(fill)_ | _(fill)_ |
| states in log | SAFE + state_change | _(fill)_ | _(fill)_ |
| TCP client | receives STATUS | _(fill)_ | _(fill)_ |
| SIGTERM | exits 0 | _(fill)_ | _(fill)_ |
| shutdown logged | `monitor_stop` | _(fill)_ | _(fill)_ |

## 2.6 Defects found and fixed

Record real bugs you hit, not invented ones. This table is worth more than a
perfect test run because it shows you debugged rather than copied.

| # | Symptom | Root cause | Fix | Commit |
|---|---|---|---|---|
| 1 | _(example: client got no data when the monitor was stopped first)_ | _(example: accept thread was blocked and the socket was not shut down)_ | _(example: `shutdown(listen_fd_)` before `join()`)_ | _(commit sha)_ |
| 2 | | | | |
| 3 | | | | |

## 2.7 Conclusion

Requirements verified without root: **(list FR ids)**
Requirements verified with root: **(list FR ids)**
Known limitations: see `docs/requirements.md` section 5.

Overall result: **PASS / FAIL**
