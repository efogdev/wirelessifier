idf_component_register(
SRCS "main.c"
     "hid_bridge.c"
     "usb/usb_hid_host.c"
     "usb/descriptor_parser.c"
     "ble/ble_hid_device.c"
     "ble/esp_hidd_prf_api.c"
     "ble/hid_dev.c"
     "ble/connection.c"
     "ble/hid_device_le_prf.c"
     "ble/hid_report_data.c"
     "web/dns_server.c"
     "web/http_server.c"
     "web/ota_server.c"
     "web/ws_server.c"
     "web/wifi_manager.c"
     "utils/adc.c"
     "utils/rgb_leds.c"
     "utils/task_monitor.c"
     "utils/temp_sensor.c"
     "utils/storage.c"
     "utils/rotary_enc.c"
  EMBED_TXTFILES
     "web/front/lib/index.min.html"
     "web/front/lib/settings.min.html"
     "web/front/lib/settings.js"
     "web/front/lib/react-dom.production.min.js"
     "web/front/lib/react.production.min.js"
     "web/front/lib/opensans-regular.woff2"
  INCLUDE_DIRS "." "ble" "usb" "utils" "web" "web/front"
  REQUIRES neopixel esp_hid bt nvs_flash esp_http_server app_update json
  PRIV_REQUIRES usb esp_adc)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=unused-const-variable)
