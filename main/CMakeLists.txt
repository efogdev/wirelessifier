idf_component_register(SRCS "main.c"
                           "hid_bridge.c"
                           "usb/usb_hid_host.c"
                           "rgb/rgb_utils.c"
                           "ble/ble_hid_device.c"
                           "ble/esp_hidd_prf_api.c"
                           "ble/hid_dev.c"
                           "ble/hid_device_le_prf.c"
                           "web/dns_server.c"
                           "web/http_server.c"
                           "web/ota_server.c"
                           "web/ws_server.c"
                           "web/wifi_manager.c"
                           "utils/task_monitor.c"
                           "utils/temp_sensor.c"
                           "utils/storage.c"
                         EMBED_TXTFILES "web/front/index.html" 
                           "web/front/settings.html"
                           "web/front/lib/babel.min.js"
                           "web/front/lib/react-dom.production.min.js"
                           "web/front/lib/react.production.min.js"  
                      INCLUDE_DIRS "." "ble" "usb" "rgb" "utils" "web" "web/front"
                      REQUIRES neopixel esp_hid bt nvs_flash esp_http_server app_update json
                      PRIV_REQUIRES usb
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=unused-const-variable)
