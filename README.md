# Wirelessifier: USB-to-BLE bridge

**⚠️ This project is currently in active development. Functionality is limited.**

## Overview

This project implements a Human Interface Device (HID) bridge using the ESP32-S3 microcontroller. It allows connecting USB HID devices (like keyboards and mice) and forwarding their inputs to other devices via Bluetooth Low Energy (BLE 4.2).

## Features

- Acts as USB HID Host
- Acts as BLE keyboard and mouse
- Light and deep sleep modes 
- RGB LED array animations
- Web configuration interface via WiFi
- OTA updates

## Disclaimer

This project hardly has any real-world use. The reason is really simple: esp32-s3 sucks in terms of power consumption, for example, with a 0.2W Ploopy Adept connected as the slave device, we get only ~11 hours of active work with a 1500 mAh battery. Consider this a PoC or something. I personally see this as a compilation of the esp-idf examples into something usable (the "architecture" sucks hard though).

## Requirements

- ESP-IDF v5.5 (Espressif IoT Development Framework)
- The Wirelessifier PCB

![image](https://github.com/user-attachments/assets/04ed4701-40f1-4cf3-a14b-b6491a4c47bb)


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
