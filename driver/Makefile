# Makefile for the kernel module.
#
#   make        -> build parking_sensor.ko
#   make clean  -> remove build artefacts
#
# Note: the *building* of a kernel module needs the kernel headers. Running
# insmod/rmmod needs root and a kernel that allows module loading.

obj-m += parking_sensor.o

KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build

PWD := $(shell pwd)

CC      := gcc
CFLAGS  := -Wall -Wextra -std=gnu11

.PHONY: all clean modules_install help

all: modules

modules:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *.o *.ko *.mod *.mod.c *.mod.o .*.cmd modules.order Module.symvers

modules_install:
	sudo $(MAKE) -C $(KDIR) M=$(PWD) modules_install
	sudo depmod -a

help:
	@echo "make            build the module (needs KDIR)"
	@echo "make KVER=...   build against a specific kernel"
	@echo "make clean      remove build artefacts"
	@echo ""
	@echo "Then: sudo insmod parking_sensor.ko"
	@echo "      dmesg | tail        # look for 'parking_sensor: loaded'"
	@echo "      sudo rmmod parking_sensor"
