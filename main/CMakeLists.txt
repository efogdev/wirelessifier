idf_component_register(
SRCS "main.c"
     "hid_bridge.c"
     "usb/usb_hid_host.c"
     "usb/descriptor_parser.c"
     "ble/ble_hid_device.c"
     "ble/esp_hidd_prf_api.c"
     "ble/hid_dev.c"
     "ble/hid_actions.c"
     "ble/connection.c"
     "ble/hid_device_le_prf.c"
     "ble/hid_report_data.c"
     "web/http_server.c"
     "web/ota_server.c"
     "web/ws_server.c"
     "web/wifi_manager.c"
     "utils/vmon.c"
     "utils/ulp.c"
     "utils/buttons.c"
     "utils/adc.c"
     "utils/rgb_leds.c"
     "utils/task_monitor.c"
     "utils/temp_sensor.c"
     "utils/storage.c"
     "utils/rotary_enc.c"
  EMBED_TXTFILES
     "web/front/lib/gz/index.min.html.gz"
     "web/front/lib/gz/settings.min.html.gz"
     "web/front/lib/gz/settings.js.gz"
     "web/front/lib/gz/action-config.js.gz"
     "web/front/lib/gz/react-dom.production.min.js.gz"
     "web/front/lib/gz/react.production.min.js.gz"
     "web/front/lib/gz/opensans-regular.woff2.gz"
  INCLUDE_DIRS "." "ble" "usb" "utils" "web" "web/front"
  PRIV_REQUIRES neopixel esp_http_server app_update json esp_hid bt nvs_flash usb esp_adc ulp soc driver)

set(ulp_app_name ulp_bat)
set(ulp_s_sources "ulp/bat.S")
set(ulp_exp_dep_srcs "utils/ulp.c")
ulp_embed_binary(${ulp_app_name} "${ulp_s_sources}" "${ulp_exp_dep_srcs}")
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=unused-const-variable)
