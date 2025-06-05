# Sony IMX708 Camera Driver

This is a Linux kernel driver for the Sony IMX708 camera sensor, commonly used in Raspberry Pi cameras.

## Prerequisites

- Linux kernel headers installed
- GCC compiler
- Make utility
- For cross-compilation: appropriate cross-compiler toolchain

### Installing Prerequisites on Ubuntu/Debian:
```bash
sudo apt update
sudo apt install build-essential linux-headers-$(uname -r)
```

### For Raspberry Pi cross-compilation:
```bash
sudo apt install gcc-aarch64-linux-gnu
```

## Building the Driver

### Native Compilation (on target system):
```bash
make
```

### Cross-compilation for Raspberry Pi (ARM64):
```bash
ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- make
```

### Cross-compilation for Raspberry Pi (ARM32):
```bash
ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- make
```

## Available Make Targets

- `make` or `make all` - Build the kernel module
- `make clean` - Clean build artifacts
- `make install` - Install the module to the system
- `make load` - Load the module into the kernel
- `make unload` - Unload the module from the kernel
- `make reload` - Unload and reload the module
- `make info` - Show module information
- `make dmesg` - Show kernel messages related to the module
- `make status` - Check if the module is currently loaded
- `make dtoverlay` - Show device tree overlay instructions
- `make help` - Show available targets

## Installation and Usage

1. Build the module:
   ```bash
   make
   ```

2. Install the module:
   ```bash
   sudo make install
   ```

3. Load the module:
   ```bash
   sudo make load
   ```

4. Check if the module is loaded:
   ```bash
   make status
   ```

5. View kernel messages:
   ```bash
   make dmesg
   ```

## Device Tree Configuration

For Raspberry Pi, you may need to add device tree overlay configuration. Add the following to `/boot/config.txt`:

```
dtoverlay=imx708
```

Or create a custom device tree overlay file.

## Module Parameters

The driver supports the following module parameter:

- `qbc_adjust`: Quad Bayer broken line correction strength [0,2-5] (default: 2)

Example:
```bash
sudo insmod imx708.ko qbc_adjust=3
```

## Debugging

To enable debug output, the module is compiled with debug flags. Check kernel messages with:
```bash
dmesg | grep imx708
```

## Troubleshooting

1. **Module fails to load**: Check kernel compatibility and prerequisites
2. **Permission denied**: Make sure to use `sudo` for loading/unloading modules
3. **Device not detected**: Verify hardware connections and device tree configuration

## File Structure

```
src/
├── imx708.c      # Main driver source code
├── Makefile      # Build configuration
└── README.md     # This file
```

## License

GPL v2 - See the source code for full license text.

## Authors

Group 5 E9 K67 OS
