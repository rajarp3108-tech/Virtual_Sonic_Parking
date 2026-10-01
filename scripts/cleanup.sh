#!/usr/bin/env bash
#
# cleanup.sh - remove everything the demo created, so the next run starts
# from a known state.
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

say() { printf '\n\033[1;34m==> %s\033[0m\n' "$1"; }
ok() { printf '\033[1;32m  [ok]   %s\033[0m\n' "$1"; }
warn() { printf '\033[1;33m[warn] %s\033[0m\n' "$1"; }

say "Stopping monitor and client processes"
for name in parking_monitor status_client; do
	if pkill -x "$name" 2>/dev/null; then
		ok "sent SIGTERM to running $name process(es)"
	else
		echo "  no $name running"
	fi
done

say "Unloading the kernel module"
if [ "$(id -u)" -eq 0 ]; then
	if lsmod 2>/dev/null | grep -q '^parking_sensor\b'; then
		rmmod parking_sensor && ok "parking_sensor unloaded"
	else
		echo "  module not loaded"
	fi
	[ -c /dev/parking_sensor ] && rm -f /dev/parking_sensor && ok "device node removed"
else
	warn "not root, skipping the module step (use: sudo $0)"
fi

say "Removing build artefacts"
make -C app clean >/dev/null 2>&1
make -C client clean >/dev/null 2>&1
make -C tests clean >/dev/null 2>&1
make -C driver clean >/dev/null 2>&1
rm -rf build
ok "binaries and objects removed"

say "Removing logs"
rm -f logs/parking.log logs/client_output.log build/*.log
ok "log files removed"

say "Done. The working tree is clean; use git status to check."
