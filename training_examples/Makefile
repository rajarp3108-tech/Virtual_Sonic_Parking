# Makefiles for the training examples.
#
# Each sub-directory is independent. They are NOT part of the parking monitor
# build; the monitor has no dependency on anything in here.
#
#   make -C training_examples/cpp_ds
#   make -C training_examples/cpp_advanced
#   make -C training_examples/system_programming
#   make -C training_examples            (builds all three)
#   make -C training_examples clean

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O2 -g
CC       ?= gcc
CFLAGS   ?= -std=c11 -Wall -Wextra -O2 -g -pthread
BUILD_DIR := ../../build/training

DS_BIN   := $(BUILD_DIR)/data_structures
ADV_BINS := $(BUILD_DIR)/oop_and_stl $(BUILD_DIR)/custom_allocator
C_BINS   := $(BUILD_DIR)/process_lifecycle $(BUILD_DIR)/ipc_and_sockets \
            $(BUILD_DIR)/mini_locking

ALL_BINS := $(DS_BIN) $(ADV_BINS) $(C_BINS)

.PHONY: all clean help

all: $(ALL_BINS)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%: %.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@

$(BUILD_DIR)/%: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "make           build every training example into ../../build/training"
	@echo "make clean     remove them"
	@echo ""
	@echo "Run them from the repository root, for example:"
	@echo "  ./build/training/data_structures"
	@echo "  ./build/training/process_lifecycle"
	@echo "  ./build/training/mini_locking"
