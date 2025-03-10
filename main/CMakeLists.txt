idf_component_register(SRCS "main.c"
                           "usb_hid_host.c"
                           "ble_hid_device.c"
                           "hid_bridge.c"
                           "rgb_utils.c"
                      INCLUDE_DIRS "."
                      REQUIRES neopixel esp_hid bt nvs_flash
                      PRIV_REQUIRES usb)
