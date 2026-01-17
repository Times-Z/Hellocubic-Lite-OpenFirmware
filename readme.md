# Geekmagic open firmware

> This repo documents the LCD interface inside the **HelloCubic Lite** cube (ESP8266) from [GeekMagicClock](https://github.com/GeekMagicClock/HelloCubic-Lite)

> It also comptabile with the **Smalltv-Ultra** from [GeekMagicClock](https://github.com/GeekMagicClock/smalltv-ultra)

[![Latest Release](https://img.shields.io/github/v/release/Times-Z/GeekMagic-Open-Firmware?label=Latest%20Version&color=c56a90&style=for-the-badge&logo=star)](https://github.com/Times-Z/GeekMagic-Open-Firmware/releases)
[![Build status main](https://img.shields.io/github/actions/workflow/status/Times-Z/GeekMagic-Open-Firmware/.github/workflows/ci.yml?branch=main&label=Pipeline%20Status%20main&style=for-the-badge&logo=star)](https://github.com/Times-Z/GeekMagic-Open-Firmware/actions)

[![Build status develop](https://img.shields.io/github/actions/workflow/status/Times-Z/GeekMagic-Open-Firmware/.github/workflows/ci.yml?branch=develop&label=Pipeline%20Status%20develop&style=for-the-badge&logo=star)](https://github.com/Times-Z/GeekMagic-Open-Firmware/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg?style=for-the-badge)](LICENSE)

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
- [What's next ?](#whats-next)
- [The firmware](#plateformio-firmware)
- [Install guide](#installation-guide)
- [License](#license)
- [Support](#support)

<div align="center">
   <img src=".github/assets/01-showcase.jpg" alt="HelloCubic Lite Showcase" width="1000" />
   <a href="https://www.youtube.com/watch?v=ga3Aeqo_rWA" target="_blank" ><img src=".github/assets/01-showcase.gif" alt="HelloCubic Lite Showcase" width="1000" /></a>
   <br>
    <em>open firmware showcase working on both hellocubic and smalltv</em>
</div>

## Important information

**Warning: I am not responsible for bricking your devices. Flash at your own risk**

**I recommend making a complete [backup](backup/readme.md) of your flash before doing anything**
**I've upload my factory backup (version 7.0.17 for cube and 9.0.40 for small tv); it might be useful. Backup tested and approved, it works**

## Teardown

> Version i've bought :
>
> - Cube : https://www.aliexpress.com/item/1005007793281982.html
> - SmallTV : https://www.aliexpress.com/item/1005008671174445.html

- **MCU**: ESP8266
- **LCD controller**: ST7789 (RGB565)
- **Case**: 3d printed

<div align="center">
   <img src=".github/assets/02-disassembly-cube.jpg" alt="Cube Disassembly" width="1000" />
   <img src=".github/assets/02-disassembly-tv.jpg" alt="Cube Disassembly" width="1000" />
   <br>
   <em>Cube & TV Disassembly</em>
</div>

## Screen hardware configuration

### Display specifications

- **Controller**: ST7789
- **Resolution**: 240x240 pixels
- **Color Format**: RGB565 (16-bit color)
- **Interface**: SPI (Serial Peripheral Interface)
- **SPI Speed**: up to 80 MHz (40 MHz is more stable)
- **Rotation**: Upside-down for cube display, normal for the small tv

### Pin wiring

The wiring is the same form small TV and cube

The display is connected to the ESP8266 using the following GPIO pins:

| Function      | GPIO Pin | Description                                           |
| ------------- | -------- | ----------------------------------------------------- |
| **MOSI**      | GPIO 13  | SPI Master Out Slave In (data from ESP8266 to screen) |
| **SCK**       | GPIO 14  | SPI Clock                                             |
| **CS**        | GPIO 2   | Chip Select (Active HIGH)                             |
| **DC**        | GPIO 0   | Data/Command select (LOW=command, HIGH=data)          |
| **RST**       | GPIO 15  | Reset pin                                             |
| **Backlight** | GPIO 5   | Backlight control (Active LOW)                        |

<div align="center">
   <img src=".github/assets/03-pinout.jpg" alt="Pinout Diagram" width="1000" />
   <br>
   <em>Pin Wiring Diagram (exact same for small tv)</em>
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

I really like ESP devices and I really enjoy working with ESP-IDF, so I’m planning ~~if possible (I haven’t checked compatibility yet, I’m still new to this world)~~ to create a firmware close to the original one in terms of features, but fully open source ofc \o/

Since ESP IDF is not compatible with esp8266, i'm going to build the firmware on top of [plateformIO](https://platformio.org/)

That’s the project, at least

## PlateformIO Firmware

This is the "real" firmware I want to improve, with clean and reliable code

### Stack

| Component         | Technology / Library                                                     | Main Role                               |
| ----------------- | ------------------------------------------------------------------------ | --------------------------------------- |
| Microcontroller   | ESP8266 (esp12e)                                                         | Main hardware platform                  |
| Build environment | PlatformIO                                                               | Project management, build, upload       |
| Framework         | Arduino Framework                                                        | Software base for ESP8266               |
| Filesystem        | LittleFS                                                                 | Local storage LittleFS                  |
| Graphics display  | Arduino_GFX Library                                                      | ST7789 display management (SPI, RGB565) |
| Web UI (frontend) | [Pico.css](https://picocss.com/docs), [Alpine.js](https://alpinejs.dev/) | Minimalist web user interface           |

## Installation Guide

To use the open-source firmware on your compatible GeekMagic device, follow these steps:

### 1. Clone the repository

```bash
git clone https://github.com/Times-Z/GeekMagic-Open-Firmware.git
```

### 2. Configure the JSON file

```bash
cp data/config-{hellocubic|smalltv}.example data/config.json
```

You can edit this JSON file to configure your firmware, for example by modifying `wifi_ssid` and `wifi_password` so that your device connects to your network

### 3. Build the firmware and filesystem

```bash
pio run && pio run --target buildfs

# or using devcontainer aliases

build && buildfs
```

The generated files will be located in:

```
.pio/build/esp12e/
```

### 4. Flash the firmware

There are two possible flashing methods:

- **OTA (Over-The-Air)** – no prerequisites
- **USB** – requires a USB-to-TTL converter to connect to the ESP

---

### OTA Flashing

Flashing is done in two steps

#### Step 1: Flash the firmware

Go to:

```
http://{your_geekmagic_ip}/update
```

This is the update endpoint of the original firmware. Upload the `firmware.bin` file

Once this step is complete, the device will reboot. Depending on the model, the screen orientation may be correct or not — this is normal

At this point, only the firmware has been flashed. The filesystem (configuration, web app) still needs to be flashed

#### Step 2: Flash the filesystem

The firmware will create a Wi‑Fi access point with the following credentials:

- **SSID:** `GeekMagic`
- **Password:** `$str0ngPa$$w0rd`

Connect to this access point and go to:

```
http://192.168.4.1/legacyupdate
```

From there, select and flash the filesystem using the `littlefs.bin` file

Once the device reboots, the setup is complete!

## License

This project is licensed under the **MIT License** - see the [LICENSE](LICENSE) file for details

---

## Support

- Found a bug or question ? [Open an issue](https://github.com/Times-Z/GeekMagic-Open-Firmware/issues)

---

<div align="center">

**Made with ❤️**

[Star us on GitHub](https://github.com/Times-Z/GeekMagic-Open-Firmware.git) if you find this project useful !

This project took me a lot of time !

</div>
