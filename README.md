# ESP32-S3 HID Bridge

**⚠️ This project is currently in development. Functionality is very limited and not complete.**

## Overview

This project implements a Human Interface Device (HID) bridge using the ESP32-S3 microcontroller. It allows connecting USB HID devices (like keyboards and mice) and forwarding their inputs to other devices via Bluetooth Low Energy (BLE).

## Features

- USB HID Host: Connects to USB keyboards and mice
- BLE HID Device: Emulates Bluetooth keyboard and mouse
- Power Management: Implements light sleep mode for battery efficiency
- RGB LED Indicators: Shows connection status
- Web Configuration Interface: Configure device settings via WiFi
- OTA Updates: Update firmware over-the-air

## Requirements

- ESP-IDF v5.5 (Espressif IoT Development Framework)
- Compatible USB HID devices (keyboards, mice)
- The custom PCB

## Building and Flashing

This project uses the ESP-IDF build system. \
Make sure you have ESP-IDF installed and properly set up.

```bash
# Build the project
idf.py build

# Flash to your ESP32-S3
idf.py -p (PORT) flash
```

## Usage

1. Connect a USB HID device (keyboard or mouse) to the ESP32-S3 USB port
2. The device will automatically start advertising as a BLE HID device
3. Pair with the BLE device from your computer or mobile device
4. Input from the USB device will be forwarded to the connected BLE host

### Web Configuration

To access the web configuration interface:
1. Hold the SW1 button during boot
2. Connect to the ESP32-S3 WiFi access point
3. Navigate to the configuration page in your web browser

## Power Management

The device implements power-saving features:
- Automatically enters light sleep mode after 30 seconds of inactivity
- Wakes up on USB HID input
- BLE stack is paused during inactivity to save power

## License

[License information to be added]
