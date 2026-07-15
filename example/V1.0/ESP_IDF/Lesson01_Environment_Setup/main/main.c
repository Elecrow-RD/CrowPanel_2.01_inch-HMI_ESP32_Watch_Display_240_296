/*
 * ESP32-S3 Watch course project.
 *
 * All source comments are written in English for code-reading practice.
 * The tutorial documents are written in Chinese for classroom delivery.
 */
#include "esp_chip_info.h"                         /* Provides chip model, core count, and feature flags. */
#include "esp_flash.h"                             /* Provides the flash-size query used by this environment lesson. */
#include "esp_heap_caps.h"                         /* Provides heap information for internal RAM and PSRAM. */
#include "esp_log.h"                               /* Provides ESP-IDF logging macros for serial output. */
#include "esp_timer.h"                             /* Provides a millisecond-style uptime source. */
#include "freertos/FreeRTOS.h"                     /* Provides FreeRTOS tick and delay definitions. */
#include "freertos/task.h"                         /* Provides vTaskDelay() for the main teaching loop. */

/** Tag used by ESP-IDF logs from this lesson. */
static const char *TAG = "Lesson01";

/**
 * Function: print_chip_summary
 * Description: Prints the ESP32-S3, flash, heap, and uptime information used to verify the ESP-IDF environment.
 * Parameters: None.
 * Return Value: None.
 */
static void print_chip_summary(void)
{
    esp_chip_info_t chip_info = {0};                /* Store the chip information returned by ESP-IDF. */
    uint32_t flash_size = 0;                        /* Store the detected external flash size in bytes. */
    uint64_t uptime_ms = 0;                         /* Store the current runtime in milliseconds. */

    esp_chip_info(&chip_info);                      /* Ask ESP-IDF to fill the chip information structure. */
    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &flash_size)); /* Read the size of the boot flash chip. */
    uptime_ms = (uint64_t)(esp_timer_get_time() / 1000); /* Convert the ESP timer value from microseconds to milliseconds. */

    ESP_LOGI(TAG, "ESP-IDF v5.5.4 environment check"); /* Print the fixed ESP-IDF version expected by this course. */
    ESP_LOGI(TAG, "Chip model=%s cores=%d revision=%d", CONFIG_IDF_TARGET, chip_info.cores, chip_info.revision); /* Print the target name, CPU core count, and chip revision. */
    ESP_LOGI(TAG, "Flash size=%lu bytes", (unsigned long)flash_size); /* Print the detected 16 MB flash capacity. */
    ESP_LOGI(TAG, "Internal heap=%lu bytes", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL)); /* Print available internal RAM for small fast allocations. */
    ESP_LOGI(TAG, "PSRAM heap=%lu bytes", (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM)); /* Print available external PSRAM to confirm PSRAM is enabled. */
    ESP_LOGI(TAG, "Uptime=%llu ms", (unsigned long long)uptime_ms); /* Print the running time so the user sees repeated serial output. */
}

/**
 * Function: app_main
 * Description: Entry point that repeatedly prints system information to the serial monitor.
 * Parameters: None.
 * Return Value: None.
 */
void app_main(void)
{
    while (true) {                                  /* Keep the environment check running for continuous serial observation. */
        print_chip_summary();                      /* Print one complete environment report. */
        vTaskDelay(pdMS_TO_TICKS(2000));           /* Wait two seconds before printing the next report. */
    }                                              /* End the repeating environment loop. */
}