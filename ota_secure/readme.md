# Hellocubic Lite — ota_secure

This document explains how to install the required Arduino core and libraries, and how to compile the `ota_secure.ino` firmware using `arduino-cli`
This firmware was originally intended solely for hardware debugging/testing; it provides a way to flash the firmware over-the-air (OTA) to avoid connecting physical pins to the device

## Prerequisites

- Install Arduino CLI if your not using devcontainer : https://arduino.github.io/arduino-cli/latest/installation/
- Install ESP8266 core and libraries
    ```bash
    arduino-cli core update-index
    arduino-cli config init
    arduino-cli config set board_manager.additional_urls https://arduino.esp8266.com/stable/package_esp8266com_index.json
    arduino-cli core update-index
    arduino-cli core install esp8266:esp8266@3.1.2 --additional-urls https://arduino.esp8266.com/stable/package_esp8266com_index.json

    arduino-cli lib install "GFX Library for Arduino"
    arduino-cli lib install AnimatedGIF
    ```

## Add your wifi creds

If you want to connect direcly on the webui from your others devices

See [ota_secure.ino#L13](ota_secure/ota_secure.ino#L13) for where to set your WiFi credentials
```C
const char *DEFAULT_SSID = "MyWifiSSID";
const char *DEFAULT_PASSWORD = "My$tr0ngPassw0rd";
```

## Compile the firmware

From the root folder, run:

```bash
arduino-cli compile --fqbn esp8266:esp8266:generic:FlashMode=dio,FlashFreq=80,eesz=4M2M ota_secure --output-dir build
```

## Upload

If you want to upload the compiled firmware to your ESP8266 by serial port, run:

```bash
arduino-cli upload -p /dev/ttyUSB0 --fqbn esp8266:esp8266:generic:FlashMode=dio,FlashFreq=80,eesz=4M2M ota_secure
```

Otherwise you can just upload the .bin file directly on the official firmware via the webui

## Gifs

A set of gifs is provided with the good scale (max 240x240) to test your screen
