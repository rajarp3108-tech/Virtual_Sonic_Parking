#!/usr/bin/env bash
#
# debug_helper.sh - the binutils / gdb / dmesg evidence required by
# SRS sections 10 and 18.4. Run it after ./scripts/build.sh.
#
#   ./scripts/debug_helper.sh build     readelf, nm, objdump, size, ldd
#   ./scripts/debug_helper.sh symbols   nm output per object
#   ./scripts/debug_helper.sh gdb       batch gdb session on the monitor
#   ./scripts/debug_helper.sh driver    dmesg / module info
#   ./scripts/debug_helper.sh all       everything
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BIN=build/parking_monitor
CLIENT=build/status_client

say() { printf '\n\033[1;34m==> %s\033[0m\n' "$1"; }
need() {
	command -v "$1" >/dev/null 2>&1 || {
		printf '\033[1;33m[warn] %s not installed, skipping\033[0m\n' "$1"
		return 1
	}
}

group_build() {
	say "file / dynamic dependencies"
	need file || return
	file "$BIN" "$CLIENT"

	say "ldd (shared library dependencies)"
	need ldd || return
	ldd "$BIN"

	say "readelf -h (ELF header: proves the ISA and endianness)"
	need readelf || return
	readelf -h "$BIN" | sed -n '1,20p'

	say "readelf -S (section table: .text / .data / .bss / .debug_info)"
	need readelf || return
	readelf -S "$BIN" | grep -E '\.(text|data|bss|rodata|debug_info)' ||
		echo "  (build with -g to get debug sections)"

	say "nm -C (demangled C++ symbols: classes, methods, virtual tables)"
	need nm || return
	nm -C "$BIN" | grep -E 'ParkingMonitor|StateManager|Logger|TCPStatus' |
		head -n 20
	echo "  ..."
	nm -C "$BIN" | grep 'vtable for' | head -n 10

	say "nm --size-sort -C (ten largest symbols)"
	need nm || return
	nm -C -S --size-sort "$BIN" | tail -n 10

	say "objdump -d (disassembly of StateManager::evaluate)"
	need objdump || return
	objdump -d -C "$BIN" | awk '/<StateManager::update/,/^$/' | head -n 40

	say "size (text/data/bss per section)"
	need size || return
	size "$BIN" "$CLIENT"
}

group_symbols() {
	say "nm per application object file"
	need nm || return
	for obj in build/app_*.o; do
		[ -f "$obj" ] || continue
		printf '\n--- %s ---\n' "$obj"
		nm -C "$obj" | grep ' T \| W ' | head -n 8
	done
}

group_gdb() {
	say "gdb: breakpoints, backtrace and a static analysis pass"
	need gdb || return
	[ -x "$BIN" ] || {
		echo "  build $BIN first: ./scripts/build.sh"
		return
	}

	# A batch session: set a breakpoint, run, print the state, backtrace.
	gdb -q -batch \
		-ex 'set pagination off' \
		-ex 'break ParkingMonitor::run' \
		-ex 'run --simulate --iterations 2' \
		-ex 'info breakpoints' \
		-ex 'bt' \
		-ex 'print iterations_' \
		-ex 'continue' \
		"$BIN" 2>&1 | tail -n 30

	say "gdb: does a coredump exist? (enable with: ulimit -c unlimited)"
	ls -l core* coredump* 2>/dev/null || echo "  no core file present"
}

group_driver() {
	say "module: is it loaded?"
	lsmod | grep parking_sensor || echo "  module not loaded"

	say "dmesg: the driver's own printk messages"
	dmesg 2>/dev/null | grep -i parking_sensor | tail -n 20 ||
		echo "  (dmesg needs permission, or the module is not loaded)"

	say "sysfs: the device class created by class_create()"
	ls -l /sys/class/parking_sensor 2>/dev/null ||
		echo "  /sys/class/parking_sensor not present"

	say "device major/minor"
	cat /sys/class/parking_sensor/dev 2>/dev/null ||
		echo "  not available"

	say "module parameters (module_param values currently in force)"
	for p in min_cm max_cm step_cm start_cm interval_ms verbose; do
		printf '  %-12s = %s\n' "$p" \
			"$(cat /sys/module/parking_sensor/parameters/$p 2>/dev/null || echo n/a)"
	done

	say "module info"
	modinfo driver/parking_sensor.ko 2>/dev/null | head -n 20 ||
		echo "  modinfo unavailable (module not built)"
}

GROUP="${1:-all}"
case "$GROUP" in
build) group_build ;;
symbols) group_symbols ;;
gdb) group_gdb ;;
driver) group_driver ;;
all)
	group_build
	group_symbols
	group_driver
	group_gdb
	;;
*)
	echo "unknown group: $GROUP (use build | symbols | gdb | driver | all)"
	exit 2
	;;
esac

say "debug_helper finished"
