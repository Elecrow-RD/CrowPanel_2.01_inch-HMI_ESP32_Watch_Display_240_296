/*
 * ESP32-S3 Watch course project.
 *
 * All source comments are written in English for code-reading practice.
 * The tutorial documents are written in Chinese for classroom delivery.
 */
#include "driver/gpio.h"                           /* Provides GPIO configuration and reading APIs. */
#include "esp_log.h"                               /* Provides serial log output for encoder events. */
#include "freertos/FreeRTOS.h"                     /* Provides FreeRTOS timing conversion macros. */
#include "freertos/task.h"                         /* Provides vTaskDelay() for the polling loop. */

/** Tag used by ESP-IDF logs from this lesson. */
static const char *TAG = "Lesson04";
/** Encoder A phase GPIO from the schematic. */
#define ENCODER_A_GPIO GPIO_NUM_20
/** Encoder B phase GPIO from the schematic. */
#define ENCODER_B_GPIO GPIO_NUM_19
/** Encoder push key GPIO from the schematic. */
#define ENCODER_BUTTON_GPIO GPIO_NUM_18

/**
 * Function: configure_encoder_gpio
 * Description: Configures the two encoder phases and the encoder push key as pulled-up inputs.
 * Parameters: None.
 * Return Value: None.
 */
static void configure_encoder_gpio(void)
{
    gpio_config_t config = {0};                     /* Store the shared GPIO input configuration. */

    config.pin_bit_mask = (1ULL << ENCODER_A_GPIO) | (1ULL << ENCODER_B_GPIO) | (1ULL << ENCODER_BUTTON_GPIO); /* Select all encoder pins. */
    config.mode = GPIO_MODE_INPUT;                  /* Configure encoder pins as inputs. */
    config.pull_up_en = GPIO_PULLUP_ENABLE;         /* Enable pull-ups because the encoder switches pull the pins low. */
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;    /* Disable pull-downs to avoid a divider against the pull-ups. */
    config.intr_type = GPIO_INTR_DISABLE;           /* Disable interrupts so the lesson can focus on simple polling. */
    ESP_ERROR_CHECK(gpio_config(&config));          /* Apply the GPIO configuration. */
}

/**
 * Function: app_main
 * Description: Reads the rotary encoder direction and encoder button state, then prints events to the serial monitor.
 * Parameters: None.
 * Return Value: None.
 */
void app_main(void)
{
    int32_t position = 0;                            /* Store the accumulated encoder position. */
    int last_state = 0;                              /* Store the last two-bit A/B state. */
    int last_button = 1;                             /* Store the last encoder button level. */

    configure_encoder_gpio();                       /* Prepare all encoder GPIOs before reading them. */
    last_state = (gpio_get_level(ENCODER_A_GPIO) << 1) | gpio_get_level(ENCODER_B_GPIO); /* Capture the starting quadrature state. */
    ESP_LOGI(TAG, "Rotary encoder and encoder button are ready."); /* Print that the lesson is running. */

    while (true) {                                  /* Keep polling the encoder and button. */
        int current_state = (gpio_get_level(ENCODER_A_GPIO) << 1) | gpio_get_level(ENCODER_B_GPIO); /* Read the current two-bit quadrature state. */
        int button = gpio_get_level(ENCODER_BUTTON_GPIO); /* Read the active-low encoder push button. */

        if (current_state != last_state) {          /* Check whether either encoder phase changed. */
            int transition = (last_state << 2) | current_state; /* Combine previous and current states to classify direction. */
            if (transition == 0x01 || transition == 0x07 || transition == 0x0E || transition == 0x08) { /* Detect one clockwise quadrature step. */
                position++;                         /* Increase the position for clockwise movement. */
                ESP_LOGI(TAG, "ENCODER: direction=CW position=%ld", (long)position); /* Print the clockwise event. */
            } else if (transition == 0x02 || transition == 0x0B || transition == 0x0D || transition == 0x04) { /* Detect one counterclockwise step. */
                position--;                         /* Decrease the position for counterclockwise movement. */
                ESP_LOGI(TAG, "ENCODER: direction=CCW position=%ld", (long)position); /* Print the counterclockwise event. */
            }                                       /* End the direction classification. */
            last_state = current_state;             /* Store the current state for the next transition. */
        }                                           /* End the encoder movement check. */

        if (button != last_button) {                /* Check whether the encoder button changed state. */
            last_button = button;                   /* Save the new button level. */
            ESP_LOGI(TAG, "ENCODER_BUTTON: %s", button == 0 ? "PRESSED" : "RELEASED"); /* Print only the encoder button state. */
        }                                           /* End the button event check. */

        vTaskDelay(pdMS_TO_TICKS(5));               /* Poll quickly enough to catch manual encoder movement. */
    }                                               /* End the main polling loop. */
}