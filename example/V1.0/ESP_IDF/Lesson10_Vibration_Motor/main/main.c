/*
 * ESP32-S3 Watch course project.
 *
 * All source comments are written in English for code-reading practice.
 * The tutorial documents are written in Chinese for classroom delivery.
 */
#include "driver/gpio.h"                           /* Provides GPIO output control for the vibration motor. */
#include "esp_log.h"                               /* Provides serial log output for motor state changes. */
#include "freertos/FreeRTOS.h"                     /* Provides FreeRTOS timing conversion macros. */
#include "freertos/task.h"                         /* Provides vTaskDelay() for timed vibration. */

/** Tag used by ESP-IDF logs from this lesson. */
static const char *TAG = "Lesson10";
/** Vibration motor enable GPIO from the schematic. */
#define VIBRATION_GPIO GPIO_NUM_45

/**
 * Function: configure_motor_gpio
 * Description: Configures the vibration motor pin as a safe low output.
 * Parameters: None.
 * Return Value: None.
 */
static void configure_motor_gpio(void)
{
    gpio_config_t config = {0};                     /* Store the GPIO configuration for the motor output. */

    config.pin_bit_mask = 1ULL << VIBRATION_GPIO;   /* Select the vibration motor GPIO. */
    config.mode = GPIO_MODE_OUTPUT;                 /* Configure the motor pin as an output. */
    config.pull_up_en = GPIO_PULLUP_DISABLE;        /* Disable pull-up because the output drives the pin actively. */
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;    /* Disable pull-down because the output drives the pin actively. */
    config.intr_type = GPIO_INTR_DISABLE;           /* Disable interrupts because this pin is only an output. */
    ESP_ERROR_CHECK(gpio_config(&config));          /* Apply the GPIO configuration. */
    ESP_ERROR_CHECK(gpio_set_level(VIBRATION_GPIO, 0)); /* Keep the motor off during startup. */
}

/**
 * Function: app_main
 * Description: Turns the vibration motor on and off repeatedly for a simple hardware demonstration.
 * Parameters: None.
 * Return Value: None.
 */
void app_main(void)
{
    configure_motor_gpio();                         /* Prepare the vibration motor output. */
    ESP_LOGI(TAG, "Vibration motor test is running."); /* Print that the lesson has started. */

    while (true) {                                  /* Repeat the vibration pattern continuously. */
        ESP_LOGI(TAG, "MOTOR: ON");                 /* Print the motor-on event before enabling the output. */
        ESP_ERROR_CHECK(gpio_set_level(VIBRATION_GPIO, 1)); /* Drive GPIO45 high to enable the motor. */
        vTaskDelay(pdMS_TO_TICKS(600));             /* Keep the motor on long enough to feel the vibration. */
        ESP_LOGI(TAG, "MOTOR: OFF");                /* Print the motor-off event before disabling the output. */
        ESP_ERROR_CHECK(gpio_set_level(VIBRATION_GPIO, 0)); /* Drive GPIO45 low to stop the motor. */
        vTaskDelay(pdMS_TO_TICKS(2000));            /* Wait two seconds before the next vibration pulse. */
    }                                               /* End the repeating motor loop. */
}