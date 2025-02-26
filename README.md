# Adept Wireless Extension: USB to BLE HID Bridge

## ⚠️ DISCLAIMER ⚠️
**This project is currently in development and does not work yet.** 

## Overview
Adept Wireless Extension creates a bridge between USB HID devices (keyboards, mice) and Bluetooth Low Energy (BLE) HID devices. This allows you to connect USB input devices wirelessly to BLE-capable systems like computers, tablets, and smartphones.

## Features (Planned)
- USB HID Host support for keyboards and mice
- BLE HID Device emulation
- Seamless bridging of HID reports between USB and BLE
- LED status indicators using WS2812B RGB LEDs

## Build and Upload

### Prerequisites
- [PlatformIO](https://platformio.org/) installed
- The Extension PCB

### Building the Project
```bash
# Clone the repository
git clone https://github.com/efogdev/adept-wireless-ext.git
cd adept-wireless-ext

# Build the project
pio run
```

### Combined Build, Upload, and Monitor
```bash
# Build, upload, and start monitoring in one command
pio run -t upload -t monitor
```

## Development Environment
This project uses PlatformIO with ESP-IDF framework. The configuration in `platformio.ini` enables:
- USB HID host functionality
- BLE in BLE-only mode (no classic Bluetooth)
- GATT Server for HID device emulation

## License
TBD
