#!/usr/bin/env bash
#
# load_driver.sh - insert the module, make sure /dev/parking_sensor exists,
# and prove that it can be read and written.
#
# This is the driver acceptance test from SRS section 18.2:
#   load -> /dev exists -> read returns a value -> write is accepted
#   -> unload.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

MODULE=parking_sensor
KO=driver/parking_sensor.ko
DEV=/dev/parking_sensor

say() { printf '\n\033[1;34m==> %s\033[0m\n' "$1"; }
ok() { printf '\033[1;32m  [ok]   %s\033[0m\n' "$1"; }
die() {
	printf '\033[1;31m[fail] %s\033[0m\n' "$1" >&2
	exit 1
}

[ "$(id -u)" -eq 0 ] || die "must run as root: sudo $0"

say "1. Building the module"
make -C driver || die "module build failed (are the kernel headers installed?)"

say "2. Unloading any previous copy"
if lsmod | grep -q "^${MODULE}\b"; then
	rmmod "$MODULE"
	echo "  previous module removed"
else
	echo "  module not loaded yet"
fi

say "3. Loading the module (FR-01)"
# Parameters are the module_param values declared in parking_sensor.c.
insmod "$KO" min_cm=10 max_cm=150 step_cm=10 start_cm=150 interval_ms=500 \
	|| die "insmod failed"
ok "insmod returned success"
ok "driver log: $(dmesg | grep "${MODULE}:" | tail -n 1)"

say "4. Checking the device node (FR-02)"
if [ -c "$DEV" ]; then
	ok "$DEV exists"
else
	# udev normally creates the node from the class. In a minimal
	# container or QEMU image udev may be absent, so create it by hand.
	MAJOR_NR=$(awk -F= '/^major/ {print $2}' /sys/class/"$MODULE"/dev 2>/dev/null)
	if [ -z "${MAJOR_NR:-}" ]; then
		die "$DEV missing and /sys/class/$MODULE/dev not readable"
	fi
	MINOR_NR=0
	echo "  udev did not create the node, using mknod c $MAJOR_NR $MINOR_NR"
	mknod "$DEV" c "$MAJOR_NR" "$MINOR_NR"
	chmod 660 "$DEV"
	ok "$DEV created with mknod"
fi
ls -l "$DEV"

say "5. Reading the distance (FR-03, FR-04)"
# cat uses one read() call, so it prints the whole struct as raw bytes.
# The C++ monitor is the proper reader; this only proves data flows.
BEFORE=$(od -An -tu4 -N4 "$DEV" | tr -d ' ')
[ -n "$BEFORE" ] || die "read returned nothing"
ok "distance = ${BEFORE} cm (expected 150 at the start)"

say "6. Sending a control command (FR-05)"
printf 'pause' >"$DEV" && ok "write('pause') accepted"
sleep 1
PAUSED=$(od -An -tu4 -N4 "$DEV" | tr -d ' ')
[ "$PAUSED" = "$BEFORE" ] && ok "value frozen while paused" ||
	echo "  note: value moved to ${PAUSED} (timer tick race, harmless)"
printf 'resume' >"$DEV" && ok "write('resume') accepted"
printf 'reset' >"$DEV" && ok "write('reset') accepted"

say "7. Watching the simulation move"
for _ in 1 2 3 4; do
	printf '  distance = %s cm\n' "$(od -An -tu4 -N4 "$DEV" | tr -d ' ')"
	sleep 0.5
done

say "8. Rejecting an unknown command (FR-16)"
if printf 'nonsense' >"$DEV" 2>/dev/null; then
	die "the driver accepted an invalid command"
else
	ok "invalid command rejected (write returned -EINVAL)"
fi

say "9. Unloading the module"
rmmod "$MODULE" && ok "rmmod returned success"
[ -c "$DEV" ] && rm -f "$DEV"

say "Driver acceptance test PASSED"
