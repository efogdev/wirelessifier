idf_component_register(SRCS "main.c"
                           "usb/usb_hid_host.c"
                           "hid_bridge.c"
                           "rgb/rgb_utils.c"
                           "ble/ble_hid_device.c"
                           "ble/esp_hidd_prf_api.c"
                           "ble/hid_dev.c"
                           "ble/hid_device_le_prf.c"
                      INCLUDE_DIRS "." "ble" "usb" "rgb"
                      REQUIRES neopixel esp_hid bt nvs_flash
                      PRIV_REQUIRES usb)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=unused-const-variable)
