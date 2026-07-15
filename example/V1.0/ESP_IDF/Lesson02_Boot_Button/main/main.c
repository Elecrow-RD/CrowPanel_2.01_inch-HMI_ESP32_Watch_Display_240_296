/*
 * ESP32-S3 Watch course project.
 *
 * All source comments are written in English for code-reading practice.
 * The tutorial documents are written in Chinese for classroom delivery.
 */
#include "driver/gpio.h"                           /* Provides GPIO configuration and reading APIs. */
#include "esp_log.h"                               /* Provides serial log output for button events. */
#include "freertos/FreeRTOS.h"                     /* Provides FreeRTOS timing conversion macros. */
#include "freertos/task.h"                         /* Provides vTaskDelay() for polling debounce. */
#include <stdbool.h>                               /* Provides the bool type used by the button state machine. */
#include <stdio.h>                                 /* Provides printf() and fflush() for direct monitor output. */

/** Tag used by ESP-IDF logs from this lesson. */
static const char *TAG = "Lesson02";
/** BOOT key GPIO from the verified hardware reference. */
#define BOOT_BUTTON_GPIO GPIO_NUM_0
/** BOOT key active level because the key shorts GPIO0 to ground. */
#define BOOT_BUTTON_ACTIVE_LEVEL 0
/** Debounce time used to reject mechanical contact bounce. */
#define BOOT_DEBOUNCE_MS 30
/** Maximum release time that still counts as a click. */
#define BOOT_CLICK_MS 400
/** Press time that counts as the start of a long press. */
#define BOOT_LONG_PRESS_MS 1000
/** Time window used to combine two clicks into one double-click event. */
#define BOOT_DOUBLE_CLICK_MS 450
/** Polling period used by the software button state machine. */
#define BOOT_POLL_MS 5

/**
 * Function: print_boot_event
 * Description: Prints one BOOT key event immediately through the ESP-IDF monitor.
 * Parameters:
 *   event_text: Text label that describes the detected BOOT key event.
 * Return Value: None.
 */
static void print_boot_event(const char *event_text)
{
    printf("BOOT_BUTTON: %s\n", event_text);        /* Print the BOOT event in the same style as the Arduino lesson. */
    fflush(stdout);                                 /* Flush stdout so the message appears immediately in the monitor. */
}

/**
 * Function: configure_boot_button
 * Description: Configures only the BOOT key; Reset and Power are intentionally not printed in this lesson.
 * Parameters: None.
 * Return Value: None.
 */
static void configure_boot_button(void)
{
    gpio_config_t config = {0};                     /* Store the GPIO configuration for the BOOT input. */

    ESP_ERROR_CHECK(gpio_reset_pin(BOOT_BUTTON_GPIO)); /* Reset GPIO0 to a clean input state before applying the lesson configuration. */
    config.pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO; /* Select GPIO0 as the only configured pin. */
    config.mode = GPIO_MODE_INPUT;                  /* Configure the BOOT key as an input. */
    config.pull_up_en = GPIO_PULLUP_ENABLE;         /* Enable the internal pull-up for the active-low BOOT key. */
    config.pull_down_en = GPIO_PULLDOWN_DISABLE;    /* Disable pull-down because it would fight the pull-up. */
    config.intr_type = GPIO_INTR_DISABLE;           /* Disable interrupts because this beginner lesson uses polling. */
    ESP_ERROR_CHECK(gpio_config(&config));          /* Apply the GPIO configuration and stop on errors. */
}

/**
 * Function: app_main
 * Description: Polls the BOOT key and prints state changes to the serial monitor.
 * Parameters: None.
 * Return Value: None.
 */
void app_main(void)
{
    int last_raw_level = 1;                          /* Store the most recent raw GPIO level. */
    int stable_level = 1;                            /* Store the debounced GPIO level. */
    int debounce_elapsed_ms = 0;                     /* Count how long the raw level has stayed unchanged. */
    int press_elapsed_ms = 0;                        /* Count how long the current stable press has lasted. */
    int click_wait_elapsed_ms = 0;                   /* Count how long the code has waited for a possible second click. */
    bool button_is_pressed = false;                  /* Track whether the stable BOOT state is currently pressed. */
    bool long_press_reported = false;                /* Track whether LONG_PRESS_START was already printed for this press. */
    bool waiting_for_second_click = false;           /* Track whether one short click is waiting to become CLICK or DOUBLE_CLICK. */

    configure_boot_button();                        /* Prepare GPIO0 before reading the key. */
    ESP_LOGI(TAG, "Only the BOOT key is printed in this lesson."); /* Explain the course scope through ESP-IDF logging. */
    ESP_LOGI(TAG, "Reset restarts the program; Power is handled by the board power circuit."); /* Explain why Reset and Power are not printed. */
    print_boot_event("READY");                      /* Print a direct startup line so the user can confirm monitor output works. */

    while (true) {                                  /* Keep polling so every press can be observed. */
        int raw_level = gpio_get_level(BOOT_BUTTON_GPIO); /* Read the current electrical level of GPIO0. */

        if (raw_level != last_raw_level) {          /* Check whether the raw key level just changed. */
            last_raw_level = raw_level;             /* Remember the new raw level. */
            debounce_elapsed_ms = 0;                /* Restart the debounce timer for the new level. */
        } else if (debounce_elapsed_ms < BOOT_DEBOUNCE_MS) { /* Check whether the raw level is still inside the debounce window. */
            debounce_elapsed_ms += BOOT_POLL_MS;    /* Accumulate stable time in polling-period steps. */
        } else if (stable_level != raw_level) {     /* Check whether the debounced state should be updated. */
            stable_level = raw_level;               /* Save the new stable key level. */
            button_is_pressed = stable_level == BOOT_BUTTON_ACTIVE_LEVEL; /* Convert active-low level into a pressed flag. */
            if (button_is_pressed) {                /* Check whether this stable change is a press. */
                press_elapsed_ms = 0;               /* Restart the press duration counter. */
                long_press_reported = false;        /* Allow one long-press-start event for this new press. */
                print_boot_event("PRESSED");        /* Print the immediate press event. */
            } else {                                /* Handle a stable release. */
                print_boot_event("RELEASED");       /* Print the release event. */
                if (long_press_reported) {          /* Check whether this release ends a long press. */
                    print_boot_event("LONG_PRESS_STOP"); /* Print the long-press-stop event. */
                    waiting_for_second_click = false; /* Prevent the long press from also becoming a click. */
                } else if (press_elapsed_ms <= BOOT_CLICK_MS) { /* Check whether the press duration is short enough for a click. */
                    if (waiting_for_second_click) {  /* Check whether this is the second click inside the double-click window. */
                        print_boot_event("DOUBLE_CLICK"); /* Print the double-click event immediately. */
                        waiting_for_second_click = false; /* Clear the pending single-click state. */
                    } else {                        /* Handle the first short click. */
                        waiting_for_second_click = true; /* Wait briefly to see whether a second click follows. */
                        click_wait_elapsed_ms = 0;  /* Start the double-click timeout window. */
                    }                               /* End first-click handling. */
                }                                   /* End short-click handling. */
            }                                       /* End press/release handling. */
        }                                           /* End the debounce decision chain. */

        if (button_is_pressed) {                    /* Check whether the button remains held. */
            press_elapsed_ms += BOOT_POLL_MS;       /* Accumulate the current press duration. */
            if (!long_press_reported && press_elapsed_ms >= BOOT_LONG_PRESS_MS) { /* Check whether the long-press threshold was crossed. */
                long_press_reported = true;         /* Remember that the long-press-start event has been printed. */
                waiting_for_second_click = false;   /* Cancel pending click detection because this is now a long press. */
                print_boot_event("LONG_PRESS_START"); /* Print the long-press-start event. */
            }                                       /* End long-press threshold handling. */
        } else if (waiting_for_second_click) {      /* Check whether one click is waiting for a second click. */
            click_wait_elapsed_ms += BOOT_POLL_MS;  /* Accumulate the double-click wait time. */
            if (click_wait_elapsed_ms >= BOOT_DOUBLE_CLICK_MS) { /* Check whether the second-click window expired. */
                print_boot_event("CLICK");          /* Print a single click after confirming no second click arrived. */
                waiting_for_second_click = false;   /* Clear the pending click state. */
            }                                       /* End click timeout handling. */
        }                                           /* End active timing updates. */

        vTaskDelay(pdMS_TO_TICKS(BOOT_POLL_MS));    /* Wait one polling period before sampling the BOOT key again. */
    }                                               /* End the polling loop. */
}
