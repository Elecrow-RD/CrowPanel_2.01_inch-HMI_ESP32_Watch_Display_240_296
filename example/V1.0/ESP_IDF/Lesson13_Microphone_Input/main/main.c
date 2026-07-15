/*
 * ESP32-S3 Watch course project.
 *
 * All source comments are written in English for code-reading practice.
 * The tutorial documents are written in Chinese for classroom delivery.
 */
#include <math.h>                                   /* Provides sqrtf() for RMS audio level calculation. */
#include "driver/gpio.h"                            /* Provides GPIO output control for peripheral power. */
#include "driver/i2s_pdm.h"                         /* Provides ESP-IDF PDM microphone receive driver. */
#include "esp_log.h"                                /* Provides serial log output for microphone levels. */
#include "freertos/FreeRTOS.h"                      /* Provides FreeRTOS timing conversion macros. */
#include "freertos/task.h"                          /* Provides vTaskDelay() for periodic capture. */

/** Tag used by ESP-IDF logs from this lesson. */
static const char *TAG = "Lesson13";
/** PDM microphone clock GPIO from the schematic. */
#define PDM_CLOCK_GPIO GPIO_NUM_12
/** PDM microphone data GPIO from the schematic. */
#define PDM_DATA_GPIO GPIO_NUM_11
/** Shared peripheral power GPIO. */
#define PERIPHERAL_POWER_GPIO GPIO_NUM_47
/** Microphone sample rate in Hz. */
#define SAMPLE_RATE_HZ 16000
/** Number of samples captured per analysis block. */
#define SAMPLE_COUNT 512

/** I2S receive channel handle for the PDM microphone. */
static i2s_chan_handle_t rx_channel = NULL;
/** PCM sample buffer filled by the I2S PDM driver. */
static int16_t sample_buffer[SAMPLE_COUNT] = {0};

/**
 * Function: configure_microphone
 * Description: Enables peripheral power and configures the PDM microphone input.
 * Parameters: None.
 * Return Value: None.
 */
static void configure_microphone(void)
{
    gpio_config_t gpio_config_data = {0};            /* Store the peripheral-power GPIO configuration. */
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER); /* Create the default I2S channel configuration. */
    i2s_pdm_rx_config_t pdm_config = {               /* Store PDM receive configuration. */
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ), /* Set the PDM microphone sample rate. */
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO), /* Use 16-bit mono PCM output. */
        .gpio_cfg = {                                /* Store PDM microphone GPIO assignments. */
            .clk = PDM_CLOCK_GPIO,                   /* Assign the PDM clock pin. */
            .din = PDM_DATA_GPIO,                    /* Assign the PDM data input pin. */
            .invert_flags = {                        /* Store optional PDM inversion flags. */
                .clk_inv = false                     /* Keep PDM clock polarity normal. */
            }                                        /* Finish PDM inversion flag configuration. */
        }                                            /* Finish PDM GPIO configuration. */
    };                                               /* Finish PDM receive configuration. */

    gpio_config_data.pin_bit_mask = 1ULL << PERIPHERAL_POWER_GPIO; /* Select the shared peripheral power pin. */
    gpio_config_data.mode = GPIO_MODE_OUTPUT;        /* Configure the power pin as an output. */
    gpio_config_data.pull_up_en = GPIO_PULLUP_DISABLE; /* Disable pull-up on a driven output. */
    gpio_config_data.pull_down_en = GPIO_PULLDOWN_DISABLE; /* Disable pull-down on a driven output. */
    gpio_config_data.intr_type = GPIO_INTR_DISABLE;  /* Disable interrupts on the power pin. */
    ESP_ERROR_CHECK(gpio_config(&gpio_config_data)); /* Apply the GPIO configuration. */
    ESP_ERROR_CHECK(gpio_set_level(PERIPHERAL_POWER_GPIO, 1)); /* Enable the shared peripheral power rail. */

    ESP_ERROR_CHECK(i2s_new_channel(&channel_config, NULL, &rx_channel)); /* Create an I2S receive channel. */
    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(rx_channel, &pdm_config)); /* Initialize PDM receive mode. */
    ESP_ERROR_CHECK(i2s_channel_enable(rx_channel)); /* Enable the microphone receive channel. */
}

/**
 * Function: analyze_audio_block
 * Description: Captures one PCM block and prints peak and RMS level.
 * Parameters: None.
 * Return Value: None.
 */
static void analyze_audio_block(void)
{
    size_t bytes_read = 0;                            /* Store the number of bytes returned by the I2S driver. */
    int32_t peak = 0;                                 /* Store the largest absolute sample in this block. */
    double square_sum = 0.0;                          /* Accumulate squared sample values for RMS calculation. */

    ESP_ERROR_CHECK(i2s_channel_read(rx_channel, sample_buffer, sizeof(sample_buffer), &bytes_read, pdMS_TO_TICKS(1000))); /* Read one block of microphone samples. */
    size_t samples_read = bytes_read / sizeof(sample_buffer[0]); /* Convert byte count to sample count. */

    for (size_t index = 0; index < samples_read; index++) { /* Visit every captured sample. */
        int32_t sample = sample_buffer[index];       /* Read the sample into a wider signed integer. */
        int32_t absolute_value = sample < 0 ? -sample : sample; /* Convert the sample to absolute amplitude. */
        if (absolute_value > peak) {                 /* Check whether this sample is the largest so far. */
            peak = absolute_value;                   /* Save the new peak amplitude. */
        }                                           /* End the peak update check. */
        square_sum += (double)sample * (double)sample; /* Add the squared sample value to the RMS accumulator. */
    }                                               /* End the sample analysis loop. */

    float rms = samples_read > 0 ? sqrtf((float)(square_sum / (double)samples_read)) : 0.0f; /* Convert mean square amplitude to RMS. */
    ESP_LOGI(TAG, "MICROPHONE: samples=%u peak=%ld rms=%.1f", (unsigned)samples_read, (long)peak, rms); /* Print one microphone analysis line. */
}

/**
 * Function: app_main
 * Description: Captures PDM microphone audio and prints simple level statistics.
 * Parameters: None.
 * Return Value: None.
 */
void app_main(void)
{
    configure_microphone();                          /* Prepare the PDM microphone. */
    ESP_LOGI(TAG, "PDM microphone input is ready."); /* Print the lesson start message. */

    while (true) {                                   /* Repeat microphone analysis continuously. */
        analyze_audio_block();                       /* Capture and analyze one audio block. */
        vTaskDelay(pdMS_TO_TICKS(500));              /* Wait half a second before the next block. */
    }                                                /* End the microphone loop. */
}