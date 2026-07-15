/*
 * ESP32-S3 Watch course project.
 *
 * All source comments are written in English for code-reading practice.
 * The tutorial documents are written in Chinese for classroom delivery.
 */
#include <math.h>                                    /* Provides sqrtf() and fabsf() for motion detection. */
#include "driver/i2c_master.h"                       /* Provides the ESP-IDF v5.5 I2C master driver. */
#include "esp_log.h"                                 /* Provides serial log output for acceleration reports. */
#include "freertos/FreeRTOS.h"                       /* Provides FreeRTOS timing conversion macros. */
#include "freertos/task.h"                           /* Provides vTaskDelay() for sensor polling. */

/** Tag used by ESP-IDF logs from this lesson. */
static const char *TAG = "Lesson09";
/** I2C SDA GPIO for SC7A20HTR. */
#define WATCH_I2C_SDA GPIO_NUM_4
/** I2C SCL GPIO for SC7A20HTR. */
#define WATCH_I2C_SCL GPIO_NUM_3
/** SC7A20HTR seven-bit I2C address from the verified project. */
#define SC7A20_ADDRESS 0x19
/** Expected SC7A20HTR identity value. */
#define SC7A20_WHO_AM_I_VALUE 0x11

/** I2C device handle for the accelerometer. */
static i2c_master_dev_handle_t accel_device = NULL;

/**
 * Function: write_sensor_register
 * Description: Writes one SC7A20HTR register.
 * Parameters: reg - Register address; value - Value to write.
 * Return Value: ESP-IDF error code from the I2C transaction.
 */
static esp_err_t write_sensor_register(uint8_t reg, uint8_t value)
{
    uint8_t packet[2] = {reg, value};                /* Store register address followed by data byte. */
    return i2c_master_transmit(accel_device, packet, sizeof(packet), pdMS_TO_TICKS(100)); /* Send the two-byte write packet. */
}

/**
 * Function: read_sensor_registers
 * Description: Reads a contiguous SC7A20HTR register block.
 * Parameters: reg - First register address; data - Destination buffer; length - Number of bytes to read.
 * Return Value: ESP-IDF error code from the I2C transaction.
 */
static esp_err_t read_sensor_registers(uint8_t reg, uint8_t *data, size_t length)
{
    uint8_t start_reg = reg | 0x80U;                 /* Set the auto-increment bit for multi-byte reads. */
    return i2c_master_transmit_receive(accel_device, &start_reg, 1, data, length, pdMS_TO_TICKS(100)); /* Read the requested block. */
}

/**
 * Function: decode_12bit
 * Description: Converts two left-aligned SC7A20HTR output bytes into a signed 12-bit sample.
 * Parameters: low - Low output byte; high - High output byte.
 * Return Value: Signed raw acceleration sample.
 */
static int16_t decode_12bit(uint8_t low, uint8_t high)
{
    return (int16_t)(((int16_t)((high << 8) | low)) >> 4); /* Combine bytes and shift the left-aligned 12-bit result. */
}

/**
 * Function: configure_accelerometer
 * Description: Creates the I2C bus, verifies the sensor identity, and configures the accelerometer.
 * Parameters: None.
 * Return Value: None.
 */
static void configure_accelerometer(void)
{
    i2c_master_bus_config_t bus_config = {0};         /* Store the I2C bus configuration. */
    i2c_device_config_t device_config = {0};          /* Store the sensor device configuration. */
    i2c_master_bus_handle_t bus_handle = NULL;        /* Store the created I2C bus handle. */
    uint8_t chip_id = 0;                              /* Store the WHO_AM_I register value. */

    bus_config.i2c_port = I2C_NUM_0;                  /* Use the first I2C controller. */
    bus_config.sda_io_num = WATCH_I2C_SDA;            /* Assign the board SDA pin. */
    bus_config.scl_io_num = WATCH_I2C_SCL;            /* Assign the board SCL pin. */
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;      /* Use the default I2C clock source. */
    bus_config.glitch_ignore_cnt = 7;                 /* Filter short I2C glitches. */
    bus_config.flags.enable_internal_pullup = true;   /* Enable internal pull-ups for the onboard bus. */
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle)); /* Create the I2C bus. */

    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7; /* Use seven-bit addressing. */
    device_config.device_address = SC7A20_ADDRESS;    /* Set the accelerometer I2C address. */
    device_config.scl_speed_hz = 400000;              /* Use 400 kHz I2C speed. */
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &device_config, &accel_device)); /* Attach the accelerometer device. */
    ESP_ERROR_CHECK(write_sensor_register(0x24, 0x80)); /* Reboot sensor memory before configuration. */
    vTaskDelay(pdMS_TO_TICKS(10));                    /* Wait for reboot to complete. */
    ESP_ERROR_CHECK(read_sensor_registers(0x0F, &chip_id, 1)); /* Read the sensor identity register. */
    ESP_ERROR_CHECK(chip_id == SC7A20_WHO_AM_I_VALUE ? ESP_OK : ESP_ERR_NOT_FOUND); /* Stop if the expected chip is not present. */
    ESP_ERROR_CHECK(write_sensor_register(0x20, 0x47)); /* Enable X/Y/Z at 50 Hz. */
    ESP_ERROR_CHECK(write_sensor_register(0x21, 0x00)); /* Disable the high-pass filter. */
    ESP_ERROR_CHECK(write_sensor_register(0x22, 0x00)); /* Disable interrupt routing for polling mode. */
    ESP_ERROR_CHECK(write_sensor_register(0x23, 0x88)); /* Enable block data update and high-resolution +/-2 g output. */
}

/**
 * Function: app_main
 * Description: Reads acceleration, prints physical units, and marks motion events.
 * Parameters: None.
 * Return Value: None.
 */
void app_main(void)
{
    float last_magnitude = 0.0f;                      /* Store the previous acceleration magnitude for motion detection. */

    configure_accelerometer();                       /* Initialize the SC7A20HTR accelerometer. */
    ESP_LOGI(TAG, "SC7A20HTR accelerometer is ready."); /* Print the sensor-ready message. */

    while (true) {                                   /* Poll acceleration continuously. */
        uint8_t raw[6] = {0};                        /* Store X/Y/Z low and high output bytes. */
        ESP_ERROR_CHECK(read_sensor_registers(0x28, raw, sizeof(raw))); /* Read one acceleration sample. */
        float x = decode_12bit(raw[0], raw[1]) * 0.00980665f; /* Convert raw X mg to m/s^2. */
        float y = decode_12bit(raw[2], raw[3]) * 0.00980665f; /* Convert raw Y mg to m/s^2. */
        float z = decode_12bit(raw[4], raw[5]) * 0.00980665f; /* Convert raw Z mg to m/s^2. */
        float magnitude = sqrtf((x * x) + (y * y) + (z * z)); /* Calculate the vector magnitude. */
        float delta = fabsf(magnitude - last_magnitude); /* Calculate change from the previous magnitude. */
        ESP_LOGI(TAG, "ACCEL: X=%.3f Y=%.3f Z=%.3f magnitude=%.3f delta=%.3f event=%s", x, y, z, magnitude, delta, delta > 1.5f ? "MOTION" : "STABLE"); /* Print one acceleration report. */
        last_magnitude = magnitude;                  /* Save the current magnitude for the next comparison. */
        vTaskDelay(pdMS_TO_TICKS(500));              /* Wait half a second before the next sample. */
    }                                                /* End the acceleration loop. */
}