# Makefile for the C++ parking monitor.
#
#   make            -> build build/parking_monitor
#   make test-build -> build the unit test binary as a dependency check
#   make clean
#
# No external library is used: only the C++ standard library and POSIX.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g
CPPFLAGS += -Iinclude -I../include
LDFLAGS  +=
LDLIBS   += -pthread

BUILD_DIR := ../build
TARGET    := $(BUILD_DIR)/parking_monitor

SOURCES := src/telemetry.cpp \
           src/config.cpp \
           src/sensor_reader.cpp \
           src/state_manager.cpp \
           src/logger.cpp \
           src/tcp_status_server.cpp \
           src/parking_monitor.cpp \
           src/main.cpp

OBJECTS := $(patsubst src/%.cpp,$(BUILD_DIR)/app_%.o,$(SOURCES))
DEPS    := $(OBJECTS:.o=.d)

.PHONY: all clean rebuild help

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/app_%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -f $(OBJECTS) $(DEPS) $(TARGET)

rebuild: clean all

help:
	@echo "make       build $(TARGET)"
	@echo "make clean remove objects and the binary"
	@echo ""
	@echo "Run from the repository root:"
	@echo "  ./build/parking_monitor --simulate --iterations 20"
	@echo "  ./build/parking_monitor --config app/parking.conf"

-include $(DEPS)
