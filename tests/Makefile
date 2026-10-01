# Makefile for the test suite.
#
#   make          -> build ../build/run_tests
#   make run      -> build and run every test from the repository root
#   make clean
#
# No external test framework is used: tests/test_harness.h provides
# CHECK / CHECK_EQ / TEST_LIST, so the suite builds with plain g++.
#
# Exactly one main() exists in the whole suite: test_main.cpp. Each
# test_*.cpp registers its tests with TEST_LIST and defines no main, so all
# five files link into a single binary.
#
# The application sources are compiled without src/main.cpp, because the
# tests provide their own object graph.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g
CPPFLAGS += -I. -I../app/include -I../include
LDFLAGS  +=
LDLIBS   += -pthread

BUILD_DIR := ../build
TARGET    := $(BUILD_DIR)/run_tests

APP_SOURCES := ../app/src/telemetry.cpp \
               ../app/src/config.cpp \
               ../app/src/sensor_reader.cpp \
               ../app/src/state_manager.cpp \
               ../app/src/logger.cpp \
               ../app/src/tcp_status_server.cpp \
               ../app/src/parking_monitor.cpp

TEST_SOURCES := test_main.cpp \
                test_state_manager.cpp \
                test_config.cpp \
                test_logger_and_telemetry.cpp \
                test_sensor_and_tcp.cpp \
                test_monitor_integration.cpp

OBJECTS := $(patsubst %.cpp,$(BUILD_DIR)/tests_%.o,$(TEST_SOURCES)) \
           $(patsubst ../app/src/%.cpp,$(BUILD_DIR)/tests_app_%.o,$(APP_SOURCES))
DEPS    := $(OBJECTS:.o=.d)

.PHONY: all run clean help

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/tests_%.o: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/tests_app_%.o: ../app/src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# The tests write their temporary files into build/, so run from the root.
run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJECTS) $(DEPS) $(TARGET)

help:
	@echo "make     build ../build/run_tests"
	@echo "make run build and run all tests (exit 0 = all passed)"
	@echo ""
	@echo "The suite must be run from the repository root, because the"
	@echo "tests write temporary files into build/."
	@echo ""
	@echo "  ./build/run_tests          # everything"
	@echo "  ./build/run_tests 2>&1 | grep '\\[ RUN'   # list the test names"

-include $(DEPS)
