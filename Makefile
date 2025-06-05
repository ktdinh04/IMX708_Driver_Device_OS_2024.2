# Makefile for Sony IMX708 Camera Driver
# Copyright (c) 2025 Group 5 E9 K67 OS

# Module name and objects
MODULE_NAME := imx708
obj-m += $(MODULE_NAME).o

# Kernel build directory
KERNELDIR ?= /lib/modules/$(shell uname -r)/build

# Current working directory
PWD := $(shell pwd)

# Cross-compilation support (for Raspberry Pi)
# Set these variables for cross-compilation:
# ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make
ARCH ?= $(shell uname -m | sed 's/x86_64/x86/' | sed 's/armv.*/arm/' | sed 's/aarch64/arm64/')
CROSS_COMPILE ?=

# Compiler flags
ccflags-y := -DDEBUG -g -Wall -Wextra

# Default target
all: modules

# Build kernel modules
modules:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

# Clean build artifacts
clean:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
	rm -f *.o *.ko *.mod.c *.mod *.order *.symvers .*.cmd
	rm -rf .tmp_versions/

# Install module
install: modules
	$(MAKE) -C $(KERNELDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules_install
	depmod -a

# Load module
load:
	sudo insmod $(MODULE_NAME).ko

# Unload module
unload:
	sudo rmmod $(MODULE_NAME)

# Reload module (unload then load)
reload: unload load

# Show module info
info:
	modinfo $(MODULE_NAME).ko

# Show kernel messages related to the module
dmesg:
	dmesg | grep -i imx708 | tail -20

# Check if module is loaded
status:
	lsmod | grep $(MODULE_NAME) || echo "Module not loaded"

# Create device tree overlay (for Raspberry Pi)
dtoverlay:
	@echo "Creating device tree overlay..."
	@echo "Add the following to /boot/config.txt:"
	@echo "dtoverlay=imx708"

# Help target
help:
	@echo "Available targets:"
	@echo "  all/modules  - Build the kernel module"
	@echo "  clean        - Clean build artifacts"
	@echo "  install      - Install the module"
	@echo "  load         - Load the module"
	@echo "  unload       - Unload the module"
	@echo "  reload       - Unload and load the module"
	@echo "  info         - Show module information"
	@echo "  dmesg        - Show kernel messages for the module"
	@echo "  status       - Check if module is loaded"
	@echo "  dtoverlay    - Show device tree overlay instructions"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Cross-compilation example:"
	@echo "  ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make"

# Declare phony targets
.PHONY: all modules clean install load unload reload info dmesg status dtoverlay help