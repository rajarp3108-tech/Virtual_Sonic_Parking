#!/usr/bin/env bash
#
# test_all.sh - run the whole test strategy from SRS section 18.
#
#   ./scripts/test_all.sh unit          state machine, config, log, TCP, sensor
#   ./scripts/test_all.sh integration   the object graph end to end
#   ./scripts/test_all.sh system        monitor + client over a real socket
#   ./scripts/test_all.sh driver        module load/read/write/unload (root)
#   ./scripts/test_all.sh all           every group this machine can run
set -uo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

GROUP="${1:-all}"
PASS=0
FAIL=0
SKIP=0

say() { printf '\n\033[1;34m==> %s\033[0m\n' "$1"; }

record() {
	# record <exit-status> <description>
	if [ "$1" -eq 0 ]; then
		PASS=$((PASS + 1))
		printf '\033[1;32m  [PASS] %s\033[0m\n' "$2"
	else
		FAIL=$((FAIL + 1))
		printf '\033[1;31m  [FAIL] %s\033[0m\n' "$2"
	fi
}

skip() {
	SKIP=$((SKIP + 1))
	printf '\033[1;33m  [SKIP] %s\033[0m\n' "$1"
}

build_tests() {
	say "Building the test suite"
	if make -C tests >/dev/null 2>&1; then
		record 0 "test suite compiled"
		return 0
	fi
	record 1 "test suite did not compile"
	return 1
}

# The suite is one binary with one main() (tests/test_main.cpp), so there is no
# way to run only part of it. Both groups therefore run all 41 cases; the
# difference is only which part of the output you read:
#   unit        -> tests/test_state_manager.cpp, test_config.cpp,
#                  test_logger_and_telemetry.cpp, test_sensor_and_tcp.cpp
#   integration -> tests/test_monitor_integration.cpp (the object graph)
# Run the binary directly and grep the file name to see one part.
group_unit() {
	build_tests || return
	say "Unit tests: threshold logic, config parsing, log format, TCP message"
	./build/run_tests
	record $? "run_tests (all 41 cases, see the note in this script)"
}

group_integration() {
	build_tests || return
	say "Integration tests: sensor -> state -> log -> TCP"
	./build/run_tests
	record $? "run_tests (the integration cases are in test_monitor_integration.cpp)"
}

group_driver() {
	say "Driver tests: load, /dev node, read, write, unload"
	if [ "$(id -u)" -ne 0 ]; then
		# Not a failure: the machine simply is not allowed to load
		# modules, which is the normal case on a laptop.
		skip "needs root - re-run with: sudo $0 driver"
		return
	fi
	./scripts/load_driver.sh >/dev/null 2>&1
	record $? "driver acceptance test (scripts/load_driver.sh)"
}

group_system() {
	say "System tests: full process + socket behaviour"
	if ! ./scripts/build.sh >/dev/null 2>&1; then
		record 1 "build failed"
		return
	fi
	record 0 "user-space build"

	mkdir -p logs
	# Remove the previous run's files first, otherwise a grep below can
	# succeed on stale content and report a pass that did not happen.
	rm -f logs/system_test.log logs/system_test_tcp.log

	# 1. a finite foreground run terminates on its own
	./build/parking_monitor --simulate --no-tcp --iterations 3 \
		--log logs/system_test.log >/dev/null
	record $? "monitor completed 3 iterations and exited"

	# 2. the three states really appear in the log
	if [ -f logs/system_test.log ]; then
		grep -q 'state=SAFE' logs/system_test.log &&
			grep -q 'event=state_change' logs/system_test.log
		record $? "log contains SAFE readings and state_change events"
	else
		record 1 "log file was not created"
	fi

	# 3. a client receives the status over a real TCP socket
	./build/parking_monitor --simulate --port 9401 --iterations 60 \
		--interval 100 --log logs/system_test_tcp.log >/dev/null &
	local mon_pid=$!
	sleep 1
	./build/status_client --port 9401 --count 2 --interval 200 >/dev/null 2>&1
	record $? "client received STATUS over TCP"

	# 4. SIGTERM produces a clean shutdown
	kill -TERM "$mon_pid" 2>/dev/null
	wait "$mon_pid" 2>/dev/null
	record $? "monitor stopped on SIGTERM"

	# 5. and the log records the shutdown event
	grep -q 'monitor_stop' logs/system_test_tcp.log
	record $? "clean shutdown recorded in the log"
}

say "Test run: group = $GROUP"

case "$GROUP" in
unit) group_unit ;;
integration) group_integration ;;
driver) group_driver ;;
system) group_system ;;
all)
	group_unit
	group_system
	group_driver
	;;
*)
	echo "unknown group: $GROUP (use unit | integration | driver | system | all)"
	exit 2
	;;
esac

say "Summary: $PASS passed, $FAIL failed, $SKIP skipped"
[ "$FAIL" -eq 0 ]
