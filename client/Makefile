# Makefile for the TCP status client.
#
#   make      -> build ../build/status_client
#   make clean

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g
LDFLAGS  +=
LDLIBS   += -pthread

BUILD_DIR := ../build
TARGET    := $(BUILD_DIR)/status_client
OBJECTS  := $(BUILD_DIR)/status_client.o
DEPS     := $(OBJECTS:.o=.d)

.PHONY: all clean help

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(OBJECTS) -o $@ $(LDFLAGS) $(LDLIBS)

$(BUILD_DIR)/status_client.o: status_client.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -f $(OBJECTS) $(DEPS) $(TARGET)

help:
	@echo "make   build $(TARGET)"
	@echo "run    ./build/status_client --host 127.0.0.1 --port 9000 --count 5"

-include $(DEPS)
