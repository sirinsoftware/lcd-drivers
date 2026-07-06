# Out-of-tree build for the SSD1963 and ILI9341 framebuffer drivers.
#
# Native build (against the currently running kernel):
#     make
#
# Cross-compile (adjust toolchain prefix and kernel tree to your target):
#     make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- KDIR=/path/to/kernel
#
# Clean:
#     make clean

obj-m += ssd1963.o
obj-m += ili9341.o

# Kernel build tree. Override with KDIR=... for cross-compilation against a
# specific target kernel source/headers directory.
KDIR ?= /lib/modules/$(shell uname -r)/build

# Directory containing this Makefile and the driver sources.
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all clean
