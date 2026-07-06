# LCD Drivers

[![License: GPL v2](https://img.shields.io/badge/License-GPLv2-blue.svg)](LICENSE)
[![Status: Legacy](https://img.shields.io/badge/status-legacy-lightgrey.svg)](#-legacy-notice)
[![Platform: Embedded Linux](https://img.shields.io/badge/platform-embedded%20linux-orange.svg)](#supported-hardware)

Linux framebuffer drivers for two common LCD display controllers, **SSD1963** and **ILI9341**, used on embedded Linux boards. Both drivers expose the panels as standard Linux framebuffer devices (`/dev/fbN`), so they can be driven by ordinary userspace tooling such as Qt Embedded, DirectFB, or plain `mmap` access.

These drivers were developed at [Sirin Software](https://sirinsoftware.com) as part of embedded board bring-up work. A detailed write-up of the SSD1963 driver is available on our blog: [LCD driver development for embedded Linux board](https://sirinsoftware.com/blog/lcd-driver-development-for-embedded-linux-board/).

## ⚠️ Legacy notice

**These drivers target old kernels and are provided as-is, primarily as a reference.** They are not maintained against current mainline Linux and will very likely need porting to build on modern kernels (framebuffer, GPIO, and SPI APIs have changed substantially since these were written). They are useful as a working example of a page-based framebuffer driver with deferred I/O, and as a starting point for similar bring-up work.

## Supported hardware

| Driver | Controller | Panel | Resolution | Bus | Platform | Target kernel |
|---|---|---|---|---|---|---|
| [`ssd1963.c`](ssd1963.c) | SSD1963 | Newhaven NHD-5.7-320240WFB-CTXI-T1 | 320×240 | 8-bit parallel (8080) via GPIO | CoreWind AT91SAM9G45 (IPC-SAM9G45) | 2.6.3x |
| [`ili9341.c`](ili9341.c) | ILI9341 | Adafruit PiTFT 2.8" | 320×240 | SPI | Raspberry Pi / PiTFT | 3.x |

> Pin assignments (data and control lines) are defined near the top of each source file and must be adapted to your board's wiring.

## Building

These are out-of-tree kernel modules. You need kernel headers/source for your **target** kernel and, for cross-compilation, an appropriate toolchain.

```sh
# Native build against the running kernel:
make

# Cross-compile example (adjust to your toolchain and kernel tree):
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- KDIR=/path/to/kernel/source
```

This produces `ssd1963.ko` and `ili9341.ko`.

## Loading

```sh
# Load a module
sudo insmod ssd1963.ko      # or: sudo insmod ili9341.ko

# Confirm the framebuffer device appeared
ls -l /dev/fb*
dmesg | tail

# Unload
sudo rmmod ssd1963          # or: rmmod ili9341
```

Once loaded, the panel is available as a framebuffer device (e.g. `/dev/fb0`) and can be used by any framebuffer-aware application.

## Repository layout

```
.
├── ssd1963.c   # SSD1963 framebuffer driver (parallel, AT91SAM9G45)
├── ili9341.c   # ILI9341 framebuffer driver (SPI, PiTFT)
├── Makefile    # Out-of-tree kernel module build
├── LICENSE     # GPL-2.0
└── README.md
```

## License

Released under the **GNU General Public License v2.0**. See [LICENSE](LICENSE) for the full text.

Kernel modules are derivative works of the Linux kernel and are conventionally licensed under GPL-2.0; the `MODULE_LICENSE()` string in each source file reflects this.

## Author

Originally written by **Alex Nikitenko** (`alex.nikitenko@sirinsoftware.com`) at [Sirin Software](https://sirinsoftware.com).

## Contributing

Issues and pull requests are welcome. Because these drivers target legacy kernels, contributions that port them to newer kernel APIs — or that add support for additional boards — are especially useful. Please describe your target kernel version and hardware in any report.
