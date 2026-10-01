#!/usr/bin/env bash
#
# build.sh - build everything that can be built on this machine.
#
#   ./scripts/build.sh            user space only (no root needed)
#   ./scripts/build.sh --driver   also build the kernel module
#
# The driver build is separate on purpose: it needs the kernel headers and
# it is the only step that may fail on a laptop.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BUILD_DRIVER=0
for arg in "$@"; do
	case "$arg" in
	--driver) BUILD_DRIVER=1 ;;
	-h | --help)
		sed -n '2,10p' "$0"
		exit 0
		;;
	*)
		echo "unknown option: $arg" >&2
		exit 2
		;;
	esac
done

say() { printf '\n\033[1;34m==> %s\033[0m\n' "$1"; }
warn() { printf '\033[1;33m[warn] %s\033[0m\n' "$1"; }
die() {
	printf '\033[1;31m[error] %s\033[0m\n' "$1" >&2
	exit 1
}

say "Toolchain"
command -v g++ >/dev/null 2>&1 || die "g++ not found. Install build-essential."
command -v make >/dev/null 2>&1 || die "make not found."
g++ --version | head -n 1
make --version | head -n 1
echo "kernel  : $(uname -sr)"

mkdir -p build logs

say "Building the C++ monitor (FR-06 .. FR-13)"
make -C app

say "Building the TCP status client (FR-13)"
make -C client

say "Building the test suite"
make -C tests

if [ "$BUILD_DRIVER" -eq 1 ]; then
	say "Building the kernel module (--driver)"
	# Building needs kernel headers and a compiler, but not root. Loading
	# is the step that needs root, and that is done by load_driver.sh.
	if ! make -C driver; then
		warn "the kernel module did not build."
		warn "this usually means the headers for $(uname -r) are missing."
		warn "everything else is built; the user-space half runs with --simulate."
	fi
fi

say "Build complete"
ls -l build/parking_monitor build/status_client build/run_tests

cat <<'EOF'

Next steps
----------
  Full run (needs root and the kernel module):
      ./scripts/load_driver.sh
      ./build/parking_monitor --config app/parking.conf

  Demo run without root (simulated sensor, same user-space code):
      ./build/parking_monitor --simulate --iterations 20
      ./build/status_client --count 3

  Tests:
      make -C tests run

  Training examples (optional, not part of the monitor):
      make -C training_examples
EOF
