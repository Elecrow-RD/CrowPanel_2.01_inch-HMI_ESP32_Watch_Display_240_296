/*
 * ESP32-S3 Watch course project.
 *
 * All source comments are written in English for code-reading practice.
 * The tutorial documents are written in Chinese for classroom delivery.
 */
#include <time.h>                                   /* Provides tm structure formatting helpers for teaching output. */
#include "driver/i2c_master.h"                      /* Provides the ESP-IDF v5.5 I2C master driver. */
#include "esp_log.h"                               /* Provides serial log output for RTC values. */
#include "freertos/FreeRTOS.h"                     /* Provides FreeRTOS timing conversion macros. */
#include "freertos/task.h"                         /* Provides vTaskDelay() for the RTC read loop. */

/** Tag used by ESP-IDF logs from this lesson. */
static const char *TAG = "Lesson05";
/** I2C SDA GPIO shared by RTC, touch, and accelerometer. */
#define WATCH_I2C_SDA GPIO_NUM_4
/** I2C SCL GPIO shared by RTC, touch, and accelerometer. */
#define WATCH_I2C_SCL GPIO_NUM_3
/** PCF8563 seven-bit I2C address. */
#define PCF8563_ADDRESS 0x51
/** PCF8563 seconds register address. */
#define PCF8563_SECONDS_REGISTER 0x02

/** Global I2C device handle for the RTC chip. */
static i2c_master_dev_handle_t rtc_device = NULL;

/**
 * Function: bcd_to_decimal
 * Description: Converts a PCF8563 packed-BCD register value to decimal.
 * Parameters: value - Packed-BCD value read from the RTC.
 * Return Value: Decimal version of the value.
 */
static uint8_t bcd_to_decimal(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10U) + (value & 0x0FU)); /* Convert the upper nibble and lower nibble into decimal. */
}

/**
 * Function: configure_rtc_bus
 * Description: Creates the I2C bus and attaches the PCF8563 RTC device.
 * Parameters: None.
 * Return Value: None.
 */
static void configure_rtc_bus(void)
{
    i2c_master_bus_config_t bus_config = {0};        /* Store the I2C bus configuration. */
    i2c_device_config_t device_config = {0};         /* Store the PCF8563 device configuration. */
    i2c_master_bus_handle_t bus_handle = NULL;       /* Store the created I2C bus handle. */

    bus_config.i2c_port = I2C_NUM_0;                 /* Use the first I2C controller. */
    bus_config.sda_io_num = WATCH_I2C_SDA;           /* Assign the board SDA pin. */
    bus_config.scl_io_num = WATCH_I2C_SCL;           /* Assign the board SCL pin. */
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;     /* Use the default clock source recommended by ESP-IDF. */
    bus_config.glitch_ignore_cnt = 7;                /* Filter short glitches on the I2C lines. */
    bus_config.flags.enable_internal_pullup = true;  /* Enable internal pull-ups for this small onboard bus. */
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle)); /* Create the I2C bus. */

    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7; /* Use normal seven-bit I2C addressing. */
    device_config.device_address = PCF8563_ADDRESS;  /* Set the PCF8563 address. */
    device_config.scl_speed_hz = 400000;             /* Use 400 kHz for short onboard traces. */
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &device_config, &rtc_device)); /* Add the RTC to the bus. */
}

/**
 * Function: read_rtc_time
 * Description: Reads date and time registers from PCF8563 and prints a formatted line.
 * Parameters: None.
 * Return Value: None.
 */
static void read_rtc_time(void)
{
    uint8_t start_register = PCF8563_SECONDS_REGISTER; /* Select the first RTC time register. */
    uint8_t data[7] = {0};                         /* Store seconds, minutes, hours, day, weekday, month, and year. */

    esp_err_t result = i2c_master_transmit_receive(rtc_device, &start_register, 1, data, sizeof(data), pdMS_TO_TICKS(100)); /* Read the time register block. */
    if (result != ESP_OK) {                        /* Check whether the I2C transaction failed. */
        ESP_LOGE(TAG, "RTC read failed: %s", esp_err_to_name(result)); /* Print the I2C error name. */
        return;                                    /* Leave the function so invalid data is not printed. */
    }                                              /* End the I2C error check. */

    uint8_t second = bcd_to_decimal(data[0] & 0x7FU); /* Decode seconds and mask the voltage-low flag. */
    uint8_t minute = bcd_to_decimal(data[1] & 0x7FU); /* Decode minutes from BCD. */
    uint8_t hour = bcd_to_decimal(data[2] & 0x3FU); /* Decode hours from BCD. */
    uint8_t day = bcd_to_decimal(data[3] & 0x3FU); /* Decode day of month from BCD. */
    uint8_t weekday = data[4] & 0x07U;             /* Decode weekday as a small integer. */
    uint8_t month = bcd_to_decimal(data[5] & 0x1FU); /* Decode month and mask the century bit. */
    uint16_t year = (uint16_t)(2000U + bcd_to_decimal(data[6])); /* Convert the two-digit year into a 2000-based year. */
    bool voltage_low = (data[0] & 0x80U) != 0U;    /* Decode the PCF8563 voltage-low flag. */

    ESP_LOGI(TAG, "RTC: %04u-%02u-%02u %02u:%02u:%02u weekday=%u VL=%s", year, month, day, hour, minute, second, weekday, voltage_low ? "YES" : "NO"); /* Print one complete RTC time report. */
}

/**
 * Function: app_main
 * Description: Initializes the RTC bus and reads PCF8563 time once per second.
 * Parameters: None.
 * Return Value: None.
 */
void app_main(void)
{
    configure_rtc_bus();                            /* Create the I2C bus and attach the RTC device. */
    ESP_LOGI(TAG, "PCF8563 RTC reader is running."); /* Print the lesson start message. */

    while (true) {                                  /* Keep reading the RTC for live serial output. */
        read_rtc_time();                            /* Read and print the current RTC time. */
        vTaskDelay(pdMS_TO_TICKS(1000));            /* Wait one second before the next read. */
    }                                               /* End the RTC read loop. */
}