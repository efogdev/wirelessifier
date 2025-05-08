#include "temp_sensor.h"

#include "const.h"
#include "esp_log.h"

static const char *TAG = "TempSensor";
static temperature_sensor_handle_t temp_sensor = NULL;
static bool is_initialized = false;

esp_err_t temp_sensor_init(void)
{
    if (is_initialized) {
        return ESP_OK;
    }

    const temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    esp_err_t ret = temperature_sensor_install(&temp_sensor_config, &temp_sensor);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to install temperature sensor: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = temperature_sensor_enable(temp_sensor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable temperature sensor: %s", esp_err_to_name(ret));
        return ret;
    }

    if (VERBOSE) {
        ESP_LOGI(TAG, "Temperature sensor initialized");
    }

    is_initialized = true;
    return ESP_OK;
}

esp_err_t temp_sensor_get_temperature(float *temperature)
{
    if (!is_initialized) {
        ESP_LOGE(TAG, "Temperature sensor not initialized");
        temp_sensor_init();
        return ESP_ERR_INVALID_STATE;
    }

    if (temperature == NULL) {
        ESP_LOGE(TAG, "Invalid temperature pointer");
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t ret = temperature_sensor_get_celsius(temp_sensor, temperature);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read temperature: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t temp_sensor_deinit(void)
{
    if (!is_initialized) {
        return ESP_OK;
    }

    esp_err_t ret = temperature_sensor_disable(temp_sensor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable temperature sensor: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = temperature_sensor_uninstall(temp_sensor);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to uninstall temperature sensor: %s", esp_err_to_name(ret));
        return ret;
    }

    temp_sensor = NULL;
    is_initialized = false;

    if (VERBOSE) {
        ESP_LOGI(TAG, "Temperature sensor deinitialized");
    }

    return ESP_OK;
}
