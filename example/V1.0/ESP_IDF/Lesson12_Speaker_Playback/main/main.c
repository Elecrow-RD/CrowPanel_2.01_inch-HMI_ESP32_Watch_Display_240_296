/*
 * ESP32-S3 Watch course project.
 *
 * All source comments are written in English for code-reading practice.
 * The tutorial documents are written in Chinese for classroom delivery.
 */
#include <math.h>                                   /* Provides sinf() for tone generation. */
#include "driver/gpio.h"                            /* Provides GPIO output control for amplifier power. */
#include "driver/i2s_std.h"                         /* Provides ESP-IDF standard I2S transmit driver. */
#include "esp_log.h"                                /* Provides serial log output for playback state. */
#include "freertos/FreeRTOS.h"                      /* Provides FreeRTOS timing conversion macros. */
#include "freertos/task.h"                          /* Provides vTaskDelay() for playback spacing. */

/** Tag used by ESP-IDF logs from this lesson. */
static const char *TAG = "Lesson12";
/** Speaker amplifier enable GPIO; low enables the amplifier on this board. */
#define AUDIO_ENABLE_GPIO GPIO_NUM_48
/** Speaker I2S data output GPIO. */
#define I2S_DATA_GPIO GPIO_NUM_41
/** Speaker I2S bit-clock GPIO. */
#define I2S_BCLK_GPIO GPIO_NUM_42
/** Speaker I2S word-select GPIO. */
#define I2S_LRCLK_GPIO GPIO_NUM_46
/** Shared peripheral power GPIO. */
#define PERIPHERAL_POWER_GPIO GPIO_NUM_47
/** Audio sample rate in Hz. */
#define SAMPLE_RATE_HZ 16000
/** Number of samples per transmit buffer. */
#define SAMPLES_PER_BUFFER 256

/** I2S transmit channel handle. */
static i2s_chan_handle_t tx_channel = NULL;
/** Sine-wave sample buffer. */
static int16_t sample_buffer[SAMPLES_PER_BUFFER] = {0};
/** Sine-wave phase that is preserved between buffers. */
static float tone_phase = 0.0f;

/**
 * Function: fill_tone_buffer
 * Description: Fills the transmit buffer with a 440 Hz sine wave.
 * Parameters: None.
 * Return Value: None.
 */
static void fill_tone_buffer(void)
{
    float phase_step = (2.0f * 3.1415926f * 440.0f) / (float)SAMPLE_RATE_HZ; /* Calculate the phase advance for one sample. */

    for (size_t index = 0; index < SAMPLES_PER_BUFFER; index++) { /* Fill every sample slot. */
        sample_buffer[index] = (int16_t)(sinf(tone_phase) * 9000.0f); /* Convert the sine value into 16-bit PCM. */
        tone_phase += phase_step;                  /* Advance the waveform phase. */
        if (tone_phase >= (2.0f * 3.1415926f)) {    /* Check whether the phase completed one full cycle. */
            tone_phase -= (2.0f * 3.1415926f);      /* Wrap the phase to keep the value bounded. */
        }                                           /* End the phase wrap check. */
    }                                               /* End the sample-fill loop. */
}

/**
 * Function: configure_i2s_speaker
 * Description: Configures standard I2S output and amplifier control.
 * Parameters: None.
 * Return Value: None.
 */
static void configure_i2s_speaker(void)
{
    gpio_config_t gpio_config_data = {0};            /* Store the amplifier and power GPIO configuration. */
    i2s_chan_config_t channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER); /* Create the default I2S channel configuration. */
    i2s_std_config_t std_config = {                  /* Store standard I2S mode configuration. */
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ), /* Set 16 kHz sample clock. */
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO), /* Use 16-bit mono MSB-first data. */
        .gpio_cfg = {                                /* Store all speaker I2S GPIO assignments. */
            .mclk = I2S_GPIO_UNUSED,                 /* Leave MCLK unused because the speaker circuit does not require it. */
            .bclk = I2S_BCLK_GPIO,                   /* Assign the bit-clock pin. */
            .ws = I2S_LRCLK_GPIO,                    /* Assign the word-select pin. */
            .dout = I2S_DATA_GPIO,                   /* Assign the data-output pin. */
            .din = I2S_GPIO_UNUSED,                  /* Leave data input unused for playback. */
            .invert_flags = {                        /* Store optional signal inversion flags. */
                .mclk_inv = false,                   /* Keep MCLK polarity normal. */
                .bclk_inv = false,                   /* Keep BCLK polarity normal. */
                .ws_inv = false                      /* Keep WS polarity normal. */
            }                                        /* Finish inversion flag configuration. */
        }                                            /* Finish speaker GPIO configuration. */
    };                                               /* Finish standard I2S mode configuration. */

    gpio_config_data.pin_bit_mask = (1ULL << AUDIO_ENABLE_GPIO) | (1ULL << PERIPHERAL_POWER_GPIO); /* Select output power-control pins. */
    gpio_config_data.mode = GPIO_MODE_OUTPUT;        /* Configure power-control pins as outputs. */
    gpio_config_data.pull_up_en = GPIO_PULLUP_DISABLE; /* Disable pull-up on driven outputs. */
    gpio_config_data.pull_down_en = GPIO_PULLDOWN_DISABLE; /* Disable pull-down on driven outputs. */
    gpio_config_data.intr_type = GPIO_INTR_DISABLE;  /* Disable interrupts for output pins. */
    ESP_ERROR_CHECK(gpio_config(&gpio_config_data)); /* Apply GPIO configuration. */
    ESP_ERROR_CHECK(gpio_set_level(PERIPHERAL_POWER_GPIO, 1)); /* Enable the shared peripheral power rail. */
    ESP_ERROR_CHECK(gpio_set_level(AUDIO_ENABLE_GPIO, 1)); /* Keep the amplifier muted while I2S is configured. */

    ESP_ERROR_CHECK(i2s_new_channel(&channel_config, &tx_channel, NULL)); /* Create an I2S transmit channel. */
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_channel, &std_config)); /* Initialize standard I2S mode. */
    ESP_ERROR_CHECK(i2s_channel_enable(tx_channel)); /* Enable the I2S transmitter. */
}

/**
 * Function: app_main
 * Description: Plays a generated 440 Hz tone through the I2S speaker.
 * Parameters: None.
 * Return Value: None.
 */
void app_main(void)
{
    configure_i2s_speaker();                        /* Prepare I2S and amplifier hardware. */
    ESP_LOGI(TAG, "I2S speaker playback is ready."); /* Print the lesson start message. */

    while (true) {                                  /* Repeat the tone playback demonstration. */
        size_t bytes_written = 0;                   /* Store the number of bytes accepted by the I2S driver. */
        ESP_ERROR_CHECK(gpio_set_level(AUDIO_ENABLE_GPIO, 0)); /* Enable the amplifier before audio starts. */
        for (int block = 0; block < 120; block++) { /* Generate about two seconds of audio blocks. */
            fill_tone_buffer();                     /* Fill the next PCM block. */
            ESP_ERROR_CHECK(i2s_channel_write(tx_channel, sample_buffer, sizeof(sample_buffer), &bytes_written, pdMS_TO_TICKS(1000))); /* Send PCM samples to I2S. */
        }                                           /* End the playback block loop. */
        ESP_ERROR_CHECK(gpio_set_level(AUDIO_ENABLE_GPIO, 1)); /* Mute the amplifier after playback. */
        ESP_LOGI(TAG, "SPEAKER: tone finished");   /* Print that one playback cycle completed. */
        vTaskDelay(pdMS_TO_TICKS(3000));            /* Wait three seconds before the next tone. */
    }                                               /* End the playback loop. */
}