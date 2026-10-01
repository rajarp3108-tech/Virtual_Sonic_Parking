# Top-level Makefile.
#
# A convenience wrapper: every real Makefile lives in its own directory,
# because the driver, the application, the client and the tests have
# different rules and different prerequisites. This file only forwards.
#
#   make            build app, client and tests (no root needed)
#   make driver     build the kernel module (needs kernel headers)
#   make training   build the optional training examples
#   make test       run the test suite
#   make demo       build everything, then run a 20-iteration simulated demo
#   make clean      remove all build output
#   make help       this list
#
# Scripts are the recommended entry points for a demo:
#   ./scripts/build.sh --driver
#   ./scripts/run_demo.sh

SUBDIRS_USER := app client tests
SUBDIRS_ALL  := app client tests training_examples

.PHONY: all app client tests driver training test demo clean help \
        install-kernel-headers

all: app client tests

app client tests:
	$(MAKE) -C $@

# Kept separate from `all` on purpose: the module build needs the headers for
# the running kernel, so it fails on a machine that cannot load modules.
driver:
	$(MAKE) -C driver

training:
	$(MAKE) -C training_examples

test: tests
	@echo ""
	@./build/run_tests

demo: all
	@echo ""
	@./build/parking_monitor --simulate --iterations 20 --log logs/parking.log

clean:
	@for d in $(SUBDIRS_ALL) driver; do \
		$(MAKE) -C $$d clean 2>/dev/null || true; \
	done
	rm -rf build

# Debian/Ubuntu only; on Fedora the package is kernel-devel.
install-kernel-headers:
	sudo apt-get install -y "linux-headers-$$(uname -r)"

help:
	@echo "make            build the monitor, the client and the tests"
	@echo "make driver     build driver/parking_sensor.ko (needs headers)"
	@echo "make training   build the optional training examples"
	@echo "make test       run the test suite (must be run from the root)"
	@echo "make demo       20 simulated iterations, no root required"
	@echo "make clean      remove build/ and all object files"
	@echo ""
	@echo "Then, for the full run with the real device:"
	@echo "  sudo ./scripts/load_driver.sh"
	@echo "  ./build/parking_monitor --config app/parking.conf"
