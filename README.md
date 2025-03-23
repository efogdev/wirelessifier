# Wirelessifier: USB-to-BLE bridge

**⚠️ This project is currently in development. Functionality is limited.**

## Overview

This project implements a Human Interface Device (HID) bridge using the ESP32-S3 microcontroller. It allows connecting USB HID devices (like keyboards and mice) and forwarding their inputs to other devices via Bluetooth Low Energy (BLE).

## Features

- Acts as USB HID Host
- Acts as BLE keyboard and mouse
- Implements light and deep sleep modes for battery efficiency
- Comprehensive status info with RGB LED array
- Web configuration interface via WiFi
- OTA updates

## Requirements

- ESP-IDF v5.5 (Espressif IoT Development Framework)
- The Wirelessifier PCB

## Building and Flashing

This project uses the ESP-IDF build system. \
Make sure you have ESP-IDF installed and properly set up.

```bash
# Build the project
make build

# Flash 
make flash
```

## Usage

1. The device will automatically start advertising as a BLE HID device
2. Connect a USB HID device (keyboard or mouse) to the ESP32-S3 USB port
3. Pair with the BLE device from your computer or mobile device
4. Input from the USB device will be forwarded to the connected BLE host

### Web Configuration

To access the web configuration interface:
1. Hold the SW1 button during boot
2. Connect to the open access point
3. Navigate to 192.168.4.1 in your web browser

## License

MIT
