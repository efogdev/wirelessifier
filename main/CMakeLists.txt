idf_component_register(SRCS "main.c"
                           "usb/usb_hid_host.c"
                           "hid_bridge.c"
                           "rgb/rgb_utils.c"
                           "ble/ble_hid_device.c"
                           "ble/esp_hidd_prf_api.c"
                           "ble/hid_dev.c"
                           "ble/hid_device_le_prf.c"
                           "web/dns_server.c"
                           "web/http_server.c"
                           "web/ota_server.c"
                           "web/ws_server.c"
                           "utils/task_monitor.c"
                           "web/front/index.html"
                      INCLUDE_DIRS "." "ble" "usb" "rgb" "utils" "web" "web/front"
                      REQUIRES neopixel esp_hid bt nvs_flash esp_http_server app_update
                      PRIV_REQUIRES usb
                      EMBED_TXTFILES "web/front/index.html")

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=unused-const-variable)
