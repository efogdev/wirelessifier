# Operations Manual

## Overview
This device acts as a USB-to-Bluetooth Low Energy (BLE) adapter for HID devices like mice and keyboards. It features configurable buttons, a rotary encoder, RGB status LEDs, and a web configuration interface.

## Basic Operation

### Power
> **Important**: long press of the rotary encoder will always restart the device, unless it sleeps. This is the only non-configurable action, however it is affected by the long press threshold preference. 

- Charging at ~5.5W with fast charge enabled, otherwise ~2.5W.
- Light sleep: Bluetooth disabled, but the slave device is still powered and serves as a wakeup source.  
- Deep sleep: only manual wakeup by pressing any button or using the rotary encoder.
- Any sleep automatically disabled when external power source is available.
- You can prevent sleep from happening until next device reboot: hold button #2 while [re]booting the device. 
- Status LEDs indicate whether the device is charging: you should see green running light animation.
- Battery level warnings:
  - Blinking yellow: a hint to charge.
  - Blinking red: battery low.

### USB connection
> **Important**: there is no way for the device to know whether the power source attached is a PSU or a PC. 
> When an external power source is detected, the data lines of the female USB connector will be routed directly to the male USB connector. 
>
> Holding the button #3 while [re]booting the device will prevent that — all the USB events will be routed through the MCU via BLE.

- Running purple light animation suggests that no slave device is detected.
- The device will automatically detect and forward input to the connected BLE host.

### Bluetooth
>The on-device controls are operational only via BLE, regardless of how the slave device events are being routed. 

- Pulsing blue light animation suggests that no host is currently connected.
- Device automatically advertises itself when no host is available.
- Transmission power and polling rate are configurable with the web configurator.


## Web Interface
1. Hold Button #4 during [re]boot to enable WiFi.
2. Connect to the open WiFi network (unless you have already configured the device to use your access point).
3. Open web browser and navigate to 192.168.4.1 (or the IP given by your router).
4. You can either setup the WiFi connection so the device operates withing your regular network, or go straight to settings skipping this step. Either way, the device is fully local and never uses internet.

## LED indicators
- Pulsing red: no USB slave device, no BLE host, not charging.
- Running green: charging.
- Running purple: please connect USB slave device.
- Pulsing blue: please connect BLE host device.
- Blinking yellow: consider charging the battery.
- Blinking red: battery low.

## Firmware Updates
1. Enable WiFi and go to the web configurator.
2. On the very bottom of the Settings page, upload a .bin file.
3. The device will restart itself with WiFi kept on.
4. Restart the page and click "Verify OTA update".

Any non-verified firmware will be rolled back on next boot.

## Miscellaneous
- To force bootloader mode: hold all buttons and the rotary encoder at the same time. But don't.
- You will need to re-bond the device with the host after renaming it in the web configurator.
