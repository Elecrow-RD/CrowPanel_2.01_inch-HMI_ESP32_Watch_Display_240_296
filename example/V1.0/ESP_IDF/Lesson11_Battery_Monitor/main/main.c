/*
 * ESP32-S3 Watch course project.
 *
 * All source comments are written in English for code-reading practice.
 * The tutorial documents are written in Chinese for classroom delivery.
 */
#include "driver/gpio.h"                            /* Provides GPIO input control for charge status. */
#include "esp_adc/adc_cali.h"                       /* Provides calibrated ADC raw-to-voltage conversion. */
#include "esp_adc/adc_cali_scheme.h"                /* Provides the ESP32-S3 ADC curve-fitting calibration scheme. */
#include "esp_adc/adc_oneshot.h"                    /* Provides ESP-IDF one-shot ADC reading APIs. */
#include "esp_log.h"                                /* Provides serial log output for battery reports. */
#include "freertos/FreeRTOS.h"                      /* Provides FreeRTOS timing conversion macros. */
#include "freertos/task.h"                          /* Provides vTaskDelay() for periodic monitoring. */
#include <stdbool.h>                                /* Provides the bool type used by the calibration-ready flag. */

/** Tag used by ESP-IDF logs from this lesson. */
static const char *TAG = "Lesson11";
/** Battery voltage ADC GPIO from the schematic. */
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0
/** Charge-status GPIO from the schematic. */
#define BATTERY_CHARGE_GPIO GPIO_NUM_15
/** Number of ADC samples averaged for one battery report. */
#define BATTERY_SAMPLE_COUNT 20
/** Delay between ADC samples in milliseconds. */
#define BATTERY_SAMPLE_DELAY_MS 5
/** Upper resistor value in the battery divider, in kilo-ohms. */
#define BATTERY_UPPER_RESISTOR_KOHM 100.0f
/** Lower resistor value in the battery divider, in kilo-ohms. */
#define BATTERY_LOWER_RESISTOR_KOHM 137.0f
/** Divider ratio from battery voltage to ADC input voltage. */
#define BATTERY_DIVIDER_RATIO (BATTERY_LOWER_RESISTOR_KOHM / (BATTERY_UPPER_RESISTOR_KOHM + BATTERY_LOWER_RESISTOR_KOHM))

/** ADC unit handle used by one-shot reads. */
static adc_oneshot_unit_handle_t adc_handle = NULL;
/** ADC calibration handle used to convert raw ADC values into millivolts. */
static adc_cali_handle_t adc_cali_handle = NULL;
/** Flag that records whether ADC calibration is available on this chip. */
static bool adc_calibration_ready = false;

/**
 * Function: configure_battery_monitor
 * Description: Configures GPIO15 charge status and GPIO1 ADC reading.
 * Parameters: None.
 * Return Value: None.
 */
static void configure_battery_monitor(void)
{
    adc_oneshot_unit_init_cfg_t unit_config = {0};   /* Store ADC unit configuration. */
    adc_oneshot_chan_cfg_t channel_config = {0};     /* Store ADC channel configuration. */
    adc_cali_curve_fitting_config_t calibration_config = {0}; /* Store ADC calibration configuration. */
    gpio_config_t gpio_config_data = {0};            /* Store charge-status GPIO configuration. */

    unit_config.unit_id = ADC_UNIT_1;                /* Use ADC1 because GPIO1 belongs to ADC1. */
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_config, &adc_handle)); /* Create the ADC one-shot unit. */
    channel_config.bitwidth = ADC_BITWIDTH_DEFAULT;  /* Use the default ADC resolution. */
    channel_config.atten = ADC_ATTEN_DB_12;          /* Use 12 dB attenuation for battery-divider voltage range. */
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, BATTERY_ADC_CHANNEL, &channel_config)); /* Configure GPIO1 ADC channel. */

    calibration_config.unit_id = ADC_UNIT_1;         /* Calibrate the same ADC unit used for battery reading. */
    calibration_config.chan = BATTERY_ADC_CHANNEL;   /* Calibrate the GPIO1 ADC channel. */
    calibration_config.atten = ADC_ATTEN_DB_12;      /* Match calibration attenuation to the ADC channel configuration. */
    calibration_config.bitwidth = ADC_BITWIDTH_DEFAULT; /* Match calibration bit width to the ADC channel configuration. */
    adc_calibration_ready = adc_cali_create_scheme_curve_fitting(&calibration_config, &adc_cali_handle) == ESP_OK; /* Try to create ADC calibration and remember whether it works. */
    ESP_LOGI(TAG, "ADC calibration: %s", adc_calibration_ready ? "READY" : "UNAVAILABLE"); /* Print whether calibrated millivolts can be used. */

    gpio_config_data.pin_bit_mask = 1ULL << BATTERY_CHARGE_GPIO; /* Select the charge-status pin. */
    gpio_config_data.mode = GPIO_MODE_INPUT;         /* Configure charge status as input. */
    gpio_config_data.pull_up_en = GPIO_PULLUP_ENABLE; /* Enable pull-up for open-drain style status output. */
    gpio_config_data.pull_down_en = GPIO_PULLDOWN_DISABLE; /* Disable pull-down to avoid loading the status pin. */
    gpio_config_data.intr_type = GPIO_INTR_DISABLE;  /* Disable interrupts because the lesson polls the pin. */
    ESP_ERROR_CHECK(gpio_config(&gpio_config_data)); /* Apply the GPIO configuration. */
}

/**
 * Function: read_battery_voltage
 * Description: Averages ADC samples, converts them to ADC millivolts, and estimates battery voltage.
 * Parameters:
 *   averaged_raw: Output pointer that receives the averaged raw ADC value.
 *   adc_millivolts: Output pointer that receives the estimated ADC input voltage in millivolts.
 * Return Value: Estimated battery voltage in volts.
 */
static float read_battery_voltage(int *averaged_raw, int *adc_millivolts)
{
    int raw_sum = 0;                                  /* Accumulate raw ADC samples for averaging. */
    int voltage_sum = 0;                              /* Accumulate calibrated or estimated millivolt samples. */

    for (int sample_index = 0; sample_index < BATTERY_SAMPLE_COUNT; sample_index++) { /* Take the configured number of samples. */
        int raw = 0;                                  /* Store one raw ADC reading. */
        int millivolts = 0;                           /* Store one ADC voltage reading in millivolts. */

        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, BATTERY_ADC_CHANNEL, &raw)); /* Read one raw ADC sample from GPIO1. */
        if (adc_calibration_ready) {                  /* Check whether calibrated conversion is available. */
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, raw, &millivolts)); /* Convert raw ADC value to calibrated millivolts. */
        } else {                                      /* Handle chips or eFuses without calibration support. */
            millivolts = (raw * 3300) / 4095;         /* Estimate millivolts from a 12-bit raw value and 3.3 V reference. */
        }                                             /* End calibrated-or-estimated conversion. */
        raw_sum += raw;                               /* Add the raw sample to the raw average sum. */
        voltage_sum += millivolts;                    /* Add the millivolt sample to the voltage average sum. */
        vTaskDelay(pdMS_TO_TICKS(BATTERY_SAMPLE_DELAY_MS)); /* Wait briefly before the next sample. */
    }                                                 /* End ADC averaging loop. */

    *averaged_raw = raw_sum / BATTERY_SAMPLE_COUNT;   /* Return the averaged raw ADC value. */
    *adc_millivolts = voltage_sum / BATTERY_SAMPLE_COUNT; /* Return the averaged ADC input voltage. */
    return ((float)(*adc_millivolts) / 1000.0f) / BATTERY_DIVIDER_RATIO; /* Convert divider voltage back to battery voltage. */
}

/**
 * Function: app_main
 * Description: Reads battery voltage and charge-status pin periodically.
 * Parameters: None.
 * Return Value: None.
 */
void app_main(void)
{
    configure_battery_monitor();                    /* Prepare ADC and charge status input. */
    ESP_LOGI(TAG, "Battery monitor is running.");   /* Print the lesson start message. */

    while (true) {                                  /* Keep printing battery information. */
        int averaged_raw = 0;                       /* Store the averaged raw ADC reading. */
        int adc_millivolts = 0;                     /* Store the averaged ADC input voltage. */
        int charge_level = gpio_get_level(BATTERY_CHARGE_GPIO); /* Read the charge-status logic level. */
        float battery_voltage = read_battery_voltage(&averaged_raw, &adc_millivolts); /* Read and estimate battery voltage. */
        ESP_LOGI(TAG, "BATTERY: raw=%d adc=%d mV voltage=%.3f V charge_pin=%d state=%s", averaged_raw, adc_millivolts, battery_voltage, charge_level, charge_level == 0 ? "CHARGING_OR_ACTIVE_LOW" : "IDLE_OR_PULLUP"); /* Print voltage and charge status. */
        vTaskDelay(pdMS_TO_TICKS(1000));            /* Wait one second before the next reading. */
    }                                               /* End the battery monitor loop. */
}
