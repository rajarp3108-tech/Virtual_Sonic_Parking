#!/usr/bin/env bash
#
# run_demo.sh - the full live demonstration from SRS section 20.1.
#
#   ./scripts/run_demo.sh              uses the real kernel driver
#   ./scripts/run_demo.sh --simulate   same user-space flow, no root needed
#   ./scripts/run_demo.sh --fast       300 ms polling for a quick demo
#
# The script performs the demo steps in order and prints the evidence, so it
# can be run live in front of the evaluator.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

MODE=device
INTERVAL=1000
ITERATIONS=34 # enough to walk 150 -> 10 -> 50 and show every state

while [ $# -gt 0 ]; do
	case "$1" in
	--simulate) MODE=simulate ;;
	--fast) INTERVAL=300 ;;
	-h | --help)
		sed -n '2,10p' "$0"
		exit 0
		;;
	*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done

LOG=logs/parking.log
CLIENT_LOG=logs/client_output.log

say() { printf '\n\033[1;34m==> %s\033[0m\n' "$1"; }
info() { printf '     %s\n' "$1"; }

cleanup() {
	if [ -n "${MONITOR_PID:-}" ] && kill -0 "$MONITOR_PID" 2>/dev/null; then
		kill -TERM "$MONITOR_PID" 2>/dev/null || true
		wait "$MONITOR_PID" 2>/dev/null || true
	fi
}
trap cleanup EXIT

mkdir -p logs
: >"$LOG"
: >"$CLIENT_LOG"

say "0. Build"
./scripts/build.sh >/dev/null
info "build/parking_monitor, build/status_client ready"

MONITOR_ARGS=(--config app/parking.conf --log "$LOG" --interval "$INTERVAL"
	--iterations "$ITERATIONS" --port 9000)

if [ "$MODE" = "simulate" ]; then
	MONITOR_ARGS+=(--simulate)
	info "mode: simulated sensor (no kernel module needed)"
elif [ "$(id -u)" -ne 0 ]; then
	MODE=simulate
	MONITOR_ARGS+=(--simulate)
	info "not root, falling back to the simulated sensor"
elif [ ! -c /dev/parking_sensor ]; then
	MODE=simulate
	MONITOR_ARGS+=(--simulate)
	info "the module is not loaded, falling back to the simulated sensor"
else
	info "mode: real kernel driver through /dev/parking_sensor"
fi

say "1. Show the project layout"
# head closes the pipe early, which kills ls with SIGPIPE; under
# `set -o pipefail` that would abort the script, so the failure is ignored.
{ ls -R --ignore=build --ignore=.git . || true; } | head -n 25

say "2. Show the configuration that drives the run"
grep -v '^\s*#' app/parking.conf | grep -v '^\s*$'

say "3. Show the device interface before starting"
if [ -c /dev/parking_sensor ]; then
	info "/dev/parking_sensor is present:"
	ls -l /dev/parking_sensor
	info "raw first 4 bytes (little endian distance):"
	od -An -tu4 -N4 /dev/parking_sensor
else
	info "no device node - using the in-process simulator"
fi

say "4. Start the monitor in the background (FR-10)"
./build/parking_monitor "${MONITOR_ARGS[@]}" &
MONITOR_PID=$!
info "monitor pid = $MONITOR_PID"
sleep 1

say "5. Watch the parking state change"
# The state machine: SAFE > 50 cm, WARNING 20..50 cm, DANGER <= 20 cm.
for _ in $(seq 1 12); do
	if ! kill -0 "$MONITOR_PID" 2>/dev/null; then
		info "monitor already finished"
		break
	fi
	sleep 1
done

say "6. Connect the TCP client (FR-13)"
# The client exits non-zero when it received nothing, and a live demo must not
# abort in the middle of the presentation, so the failure is reported, not fatal.
{ ./build/status_client --host 127.0.0.1 --port 9000 --count 3 --interval 700 ||
	info "the client received nothing this time"; } | tee -a "$CLIENT_LOG"

say "7. Process table: the monitor runs as a normal user process"
if kill -0 "$MONITOR_PID" 2>/dev/null; then
	ps -o pid,ppid,stat,cmd -p "$MONITOR_PID" 2>/dev/null ||
		info "ps produced no output"
	info "threads of the monitor:"
	THREADS=$(ls "/proc/$MONITOR_PID/task" 2>/dev/null | wc -l)
	info "  $THREADS threads (main + logger + tcp + accept)"
else
	info "monitor had already exited, so no process table to show"
fi

say "8. Show the log file (FR-09)"
if [ -s "$LOG" ]; then
	tail -n 15 "$LOG"
	info "total lines: $(wc -l <"$LOG")"
	info "state counts:"
	{ grep -o 'state=[A-Z]*' "$LOG" || true; } | sort | uniq -c |
		sed 's/^/  /'
else
	info "log is empty"
fi

say "9. Stop the monitor with a signal (FR-11)"
if kill -0 "$MONITOR_PID" 2>/dev/null; then
	kill -TERM "$MONITOR_PID"
	info "SIGTERM sent to $MONITOR_PID"
	wait "$MONITOR_PID" 2>/dev/null || true
	info "monitor exited cleanly"
	MONITOR_PID=""
else
	info "monitor had already reached its iteration limit"
fi

say "10. Confirm clean shutdown in the log"
tail -n 4 "$LOG"

say "Demo finished. Evidence saved in logs/parking.log and logs/client_output.log"
