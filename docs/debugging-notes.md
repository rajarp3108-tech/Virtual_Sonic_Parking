# Debugging Notes

Tools required by SRS sections 10 and 18.4, and how this project uses each one.
Run the automated version with `./scripts/debug_helper.sh all`.

## 1. gdb - user-space debugging

Batch session (no interactive window needed, and the output goes straight into
the report):

```bash
gdb -q -batch \
    -ex 'set pagination off' \
    -ex 'break ParkingMonitor::run' \
    -ex 'run --simulate --iterations 2' \
    -ex 'bt' \
    -ex 'print iterations_' \
    -ex 'print config_.poll_interval_ms' \
    -ex 'break StateManager::update' \
    -ex 'continue' \
    -ex 'print distance_cm' \
    ./build/parking_monitor
```

Useful things to try:

| Task | Command |
|---|---|
| break on a state change | `break StateManager::update` then `print distance_cm` |
| see which state was chosen | `print next` (a `ParkingState` enum) |
| watch the transition counter | `watch sm.transitions_` |
| print the whole record | `print rec` |
| backtrace at a crash | `bt full` |
| all threads | `thread apply all bt` |
| core dump | `ulimit -c unlimited` then `gdb ./build/parking_monitor core` |

## 2. dmesg - kernel side

```bash
dmesg | grep parking_sensor          # every driver message
dmesg -w                            # follow live while the module runs
dmesg | tail -n 20                  # look for BUG, WARNING, oops, call trace
dmesg | grep -i -E 'bug|oops|warn'  # must be empty after a clean test
```

A clean run ends with:

```
parking_sensor: loaded. /dev/parking_sensor major 240 minor 0, range 10..150 cm step 10 cm every 500 ms
parking_sensor: distance = 140 cm (tick 1)
...
parking_sensor: device opened (major 240 minor 0)
parking_sensor: device closed
parking_sensor: unloaded
```

If `rmmod` prints `Module in use`, the monitor still holds the device open.
That is the fd RAII working: close the monitor first, then unload.

## 3. sysfs - the driver's own view

```bash
ls -l /sys/class/parking_sensor/            # the class created by class_create
cat /sys/class/parking_sensor/dev            # "240:0" - major:minor
cat /sys/class/parking_sensor/uevent         # what udev reacts to

# the module parameters currently in force
for p in min_cm max_cm step_cm start_cm interval_ms verbose; do
    printf '%-12s = %s\n' "$p" "$(cat /sys/module/parking_sensor/parameters/$p)"
done
```

Changing a parameter needs a reload, because the parameters are declared `0444`
(read-only) - see section 3 of `training_examples/driver_notes/driver_notes.md`
for why a runtime write would be a data race:

```bash
sudo rmmod parking_sensor
sudo insmod driver/parking_sensor.ko interval_ms=2000
cat /sys/module/parking_sensor/parameters/interval_ms    # 2000
```

Attempting the write anyway is instructive, because the kernel refuses it:

```bash
echo 2000 | sudo tee /sys/module/parking_sensor/parameters/interval_ms
# tee: /sys/module/parking_sensor/parameters/interval_ms: Permission denied
#     [read-only file system]  (or: Invalid argument, if the mode were 0644)
```

## 4. readelf - what the compiler produced

```bash
readelf -h build/parking_monitor            # ELF header: class, machine, entry point
readelf -S build/parking_monitor | grep -E '\.(text|data|bss)'
readelf -d build/parking_monitor            # dynamic section -> needed libraries
readelf --syms build/parking_monitor | grep -i parking   # symbol table
```

What to say about the output: `ELF64`, `x86-64` (or `AArch64`) tells you the
ISA; `.text` is the machine code, `.rodata` holds the string literals such as
`"distance="`; `.bss` is zero-initialised data that costs no file space -
which is where a driver's uninitialised state would live.

## 5. nm - symbols and sizes

```bash
nm -C build/parking_monitor | grep ' T ' | head          # defined functions
nm -C build/parking_monitor | grep 'vtable for'          # the polymorphic classes
nm -C -S --size-sort build/parking_monitor | tail -n 15  # biggest functions
nm build/parking_sensor.ko | grep -i parking             # driver symbols
```

`vtable for ThresholdPolicy`, `vtable for ISensorSource` and
`vtable for StatePolicy` prove the interfaces exist in the binary: the
compiler emitted virtual tables for them, because `evaluate()` and friends are
virtual.

## 6. objdump - the actual machine instructions

```bash
objdump -d -C build/parking_monitor | awk '/<StateManager::update/,/ret/'
objdump -d -C build/parking_monitor | grep -A5 '<ThresholdPolicy::evaluate'
objdump -d driver/parking_sensor.ko | grep -A20 '<parking_read>'
```

A good viva answer: `StateManager::update` compiles to a small sequence of
compare-and-branch instructions plus a load and a store on the member
`current_`; there is no dynamic dispatch, because `update()` is not virtual -
the class is concrete. The vtable only exists for the two interfaces.

## 7. strace - what the syscalls actually do

```bash
strace -e trace=openat,read,write,close ./build/parking_monitor --simulate --iterations 2
```

Shows the `openat("/dev/parking_sensor")`, then a `read()` per poll, then the
socket calls, then `close()`. This is the clearest proof of the file-descriptor
path and it needs no root.

## 8. Common problems and where to look

| Symptom | Likely cause | Check |
|---|---|---|
| `cannot open /dev/parking_sensor: No such file or directory` | module not loaded, or udev did not create the node | `lsmod \| grep parking`, `ls -l /dev/parking_sensor`, `dmesg \| tail` |
| `Invalid argument` on read | buffer smaller than `struct parking_sensor_data` | only `DeviceSensorReader` and `od` should read the device |
| `bind(...): Address already in use` | port busy, or a previous run is in `TIME_WAIT` | `ss -tlnp \| grep 9000`; `SO_REUSEADDR` is already set |
| monitor prints nothing after `[monitor] running` | poll interval is huge, or the device always errors | check `read_errors` in the summary line, `cat logs/parking.log` |
| `write('nonsense')` succeeds | you are writing to a regular file, not the device | `ls -l` the path; `write` to `/dev/...` must fail with `EINVAL` |
| `rmmod: Module in use` | the monitor still has the fd open | stop the monitor first |
| client gets nothing | the monitor is not running, or the port differs | `ss -tlnp \| grep <port>`, check the client's `--port` |
| build fails with `No rule to make target` | run `make` from the sub-directory, or use the scripts | `make -C app`, `./scripts/build.sh` |
| `make -C driver` fails | kernel headers for the running kernel are missing | `sudo apt install linux-headers-$(uname -r)` |

## 9. Evidence to paste into the report

```bash
./scripts/build.sh                                  # build log
./scripts/debug_helper.sh build                    # readelf / nm / objdump / size
make -C tests run                                   # test summary
sudo ./scripts/load_driver.sh                       # driver acceptance
dmesg | grep parking_sensor | tail -n 30           # kernel messages
strace -e trace=read,write -f ./build/parking_monitor --simulate --iterations 2
```

Paste the useful output into `docs/test-report.md` and commit it. A report
with real command output is the difference between "I built it" and "I
understood it".
