# HelloCubic-Lite open firmware

> This repo documents the LCD interface inside the **HelloCubic Lite** cube (ESP8266) from [GeekMagicClock](https://github.com/GeekMagicClock/HelloCubic-Lite)

## Table of Contents

- [Important information](#important-information)
- [Teardown](#teardown)
- [Screen hardware configuration](#screen-hardware-configuration)
   - [Display specifications](#display-specifications)
   - [Pin wiring](#pin-wiring)
   - [Important configuration details](#important-configuration-details)
- [How the screen works](#how-the-screen-works)
   - [Initialization sequence](#initialization-sequence)
   - [Communication protocol](#communication-protocol)
   - [Drawing to the screen](#drawing-to-the-screen)
   - [Color format](#color-format)
   - [Performance optimizations](#performance-optimizations)
- [What's next ?](#whats-next-)
- [License](#license)
- [Support](#support)

<div align="center">
   <img src=".github/assets/01-showcase.jpg" alt="HelloCubic Lite Showcase" width="320" />
   <img src=".github/assets/04-gif_display.gif" alt="GIF Display Demo" width="320" />
   <br>
    <em>open firmware showcase</em>
</div>

## Important information

**Warning: I am not responsible for bricking your devices. Flash at your own risk**

**A basic firmware based on Arduino is available in [ota_secure](ota_secure/readme.md) to check if your hardware is compatible**

**I recommend making a complete [backup](backup/readme.md) of your flash before doing anything**
**I've upload my factory backup (7.0.17); it might be useful. Tested and approved, it works**

## Teardown

- **MCU**: ESP8266
- **LCD controller**: ST7789 (RGB565)
- **Case**: 3d printed

<div align="center">
   <img src=".github/assets/02-disassembly.jpg" alt="Cube Disassembly" width="1000" />
   <br>
   <em>Cube Disassembly</em>
</div>

## Screen hardware configuration

### Display specifications
- **Controller**: ST7789
- **Resolution**: 240x240 pixels
- **Color Format**: RGB565 (16-bit color)
- **Interface**: SPI (Serial Peripheral Interface)
- **SPI Speed**: 80 MHz
- **Rotation**: Upside-down for cube display

### Pin wiring

The display is connected to the ESP8266 using the following GPIO pins:

| Function | GPIO Pin | Description |
|----------|----------|-------------|
| **MOSI** | GPIO 13 | SPI Master Out Slave In (data from ESP8266 to screen) |
| **SCK** | GPIO 14 | SPI Clock |
| **CS** | GPIO 2 | Chip Select (Active HIGH) |
| **DC** | GPIO 0 | Data/Command select (LOW=command, HIGH=data) |
| **RST** | GPIO 15 | Reset pin |
| **Backlight** | GPIO 5 | Backlight control (Active LOW) |

<div align="center">
   <img src=".github/assets/03-pinout.jpg" alt="Pinout Diagram" width="1000" />
   <br>
   <em>Pin Wiring Diagram</em>
</div>

### Important configuration details

**Chip select (CS) polarity**: This board uses **active-high** CS, which is non-standard. Most SPI displays use active-low CS. The CS pin must be driven HIGH to select the display

**SPI mode**: SPI Mode 0 (CPOL=0, CPHA=0)

**Data/command pin**: LOW for commands, HIGH for data

**Backlight**: Active-low control - set GPIO 5 LOW to turn the backlight on, HIGH to turn it off

## How the screen works

### Initialization sequence

1. **Backlight control**: GPIO 5 is set as output and driven LOW to turn on the backlight
2. **Hardware reset**: The RST pin (GPIO 15) is toggled to reset the ST7789 controller
3. **SPI bus setup**: Hardware SPI is initialized with 80 MHz clock speed and Mode 0
4. **Display controller init**: The ST7789 is configured using a vendor-specific initialization sequence that includes:
   - Sleep out (0x11)
   - Porch settings (0xB2)
   - Tearing effect on (0x35)
   - Memory access control/MADCTL (0x36)
   - Color mode to 16-bit RGB565 (0x3A)
   - Various power control settings (0xB7, 0xBB, 0xC0-0xC6, 0xD0, 0xD6)
   - Gamma correction settings (0xE0, 0xE1, 0xE4)
   - Display inversion on (0x21)
   - Display on (0x29)
   - Full window setup and RAMWR command (0x2A, 0x2B, 0x2C)
5. **Rotation aplied**: Display rotation is set to mode 4 for proper orientation with cube

### Communication protocol

The display uses **SPI** protocol for communication:

1. **Chip select**: CS is driven HIGH to select the display
2. **Command/data mode**: The DC pin indicates whether the data on MOSI is a command (DC=LOW) or pixel data (DC=HIGH)
3. **Data transfer**: Data is shifted out on the MOSI pin, synchronized with the SCK clock signal
4. **Chip deselect**: CS can be kept HIGH during continuous operations for better performance, or dropped LOW between operations

### Drawing to the screen

The firmware uses the Arduino_GFX library with a custom ESP8266SPIWithCustomCS bus driver that handles the active-high CS polarity. Graphics operations:

1. **Begin write**: Assert CS (set HIGH)
2. **Write commands**: Set DC LOW, send command bytes via SPI
3. **Write data**: Set DC HIGH, send pixel data via SPI
4. **End write**: Optionally deassert CS (set LOW) - can be kept asserted for continuous operations

### Color format

Colors are encoded in RGB565 format (16-bit):
- Red: 5 bits (bits 15-11)
- Green: 6 bits (bits 10-5)
- Blue: 5 bits (bits 4-0)

Example colors:
- Black: 0x0000
- White: 0xFFFF
- Red: 0xF800
- Green: 0x07E0
- Blue: 0x001F

### Performance optimizations

- **High SPI speed**: 80 MHz clock for fast data transfer
- **CS kept asserted**: During continuous operations, CS stays HIGH to reduce overhead
- **Hardware SPI**: Uses ESP8266's hardware SPI peripheral for efficient transfers
- **Batch writes**: Multiple operations are batched between beginWrite/endWrite calls
- **Direct frame buffer writes**: GIF frames are streamed directly to avoid intermediate buffering

## What's next ?
Okay, now we have a minimal firmware that works.

I really like ESP devices and I really enjoy working with ESP-IDF, so I’m planning — if possible (I haven’t checked compatibility yet, I’m still new to this world) — to create a firmware close to the original one in terms of features, but fully open source ofc \o/
That’s the project, at least

## License

This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details

---

## Support

- Found a bug or question ? [Open an issue](https://github.com/Times-Z/Hellocubic-Lite-OpenFirmware/issues)

---

<div align="center">

   **Made with ❤️**

   [Star us on GitHub](https://github.com/Times-Z/Hellocubic-Lite-OpenFirmware.git) if you find this project useful !

   This project took me a lot of time !

</div>
