#include "adc.h"
#include <soc/gpio_num.h>
#include "const.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "ADC";

static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_bat_handle = NULL;
static adc_cali_handle_t adc1_cali_vin_handle = NULL;
static bool do_calibration_bat = false;
static bool do_calibration_vin = false;

static bool adc_calibration_init(const adc_unit_t unit, const adc_channel_t channel, const adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

    ESP_LOGI(TAG, "Calibration scheme version is %s", "Curve Fitting");
    const adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = channel,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
        calibrated = true;
    }

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

void adc_deinit(void) {
    adc_oneshot_del_unit(adc1_handle);
}

esp_err_t adc_init(void)
{
    esp_err_t ret;

    const adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ret = adc_oneshot_new_unit(&init_config1, &adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC1 init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    const adc_oneshot_chan_cfg_t config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    
    ret = adc_oneshot_config_channel(adc1_handle, ADC_CHAN_BAT, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC1 channel BAT config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = adc_oneshot_config_channel(adc1_handle, ADC_CHAN_VIN, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ADC1 channel VIN config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    do_calibration_bat = adc_calibration_init(ADC_UNIT_1, ADC_CHAN_BAT, ADC_ATTEN_DB_12, &adc1_cali_bat_handle);
    do_calibration_vin = adc_calibration_init(ADC_UNIT_1, ADC_CHAN_VIN, ADC_ATTEN_DB_12, &adc1_cali_vin_handle);

    return ESP_OK;
}

uint32_t adc_read_channel(const adc_channel_t chan)
{
    int adc_raw;
    int voltage = 0;
    int adc_sum = 0;

    for (int i = 0; i < ADC_MULTISAMPLE; i++) {
        const esp_err_t ret = adc_oneshot_read(adc1_handle, chan, &adc_raw);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ADC1 read BAT failed: %s", esp_err_to_name(ret));
            return 0;
        }
        adc_sum += adc_raw;
    }
    adc_raw = adc_sum / ADC_MULTISAMPLE;

    if (do_calibration_bat && chan == ADC_CHAN_BAT) {
        const esp_err_t ret = adc_cali_raw_to_voltage(adc1_cali_bat_handle, adc_raw, &voltage);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ADC1 BAT calibration failed: %s", esp_err_to_name(ret));
            return (adc_raw * 3300) / 4095;
        }
    } else if (do_calibration_bat && chan == ADC_CHAN_VIN) {
        const esp_err_t ret = adc_cali_raw_to_voltage(adc1_cali_vin_handle, adc_raw, &voltage);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ADC1 VIN calibration failed: %s", esp_err_to_name(ret));
            return (adc_raw * 3300) / 4095;
        }
    } else {
        voltage = (adc_raw * 3300) / 4095;
    }

    return (uint32_t) voltage;
}
