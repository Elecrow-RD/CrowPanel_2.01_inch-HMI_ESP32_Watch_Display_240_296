/*
 * ESP32-S3 Watch course project.
 *
 * All source comments are written in English for code-reading practice.
 * The tutorial documents are written in Chinese for classroom delivery.
 */
#include <string.h>                                  /* Provides memcpy() for moving recorded PCM blocks. */
#include "driver/gpio.h"                             /* Provides GPIO control for amplifier and peripheral power. */
#include "driver/i2s_pdm.h"                          /* Provides ESP-IDF PDM microphone receive driver. */
#include "driver/i2s_std.h"                          /* Provides ESP-IDF standard I2S speaker transmit driver. */
#include "esp_heap_caps.h"                           /* Provides PSRAM allocation for the five-second recording buffer. */
#include "esp_log.h"                                 /* Provides serial log output for loopback status. */
#include "freertos/FreeRTOS.h"                       /* Provides FreeRTOS timing conversion macros. */
#include "freertos/task.h"                           /* Provides vTaskDelay() for loopback pacing. */

/** Tag used by ESP-IDF logs from this lesson. */
static const char *TAG = "Lesson14";
/** PDM microphone clock GPIO. */
#define PDM_CLOCK_GPIO GPIO_NUM_12
/** PDM microphone data GPIO. */
#define PDM_DATA_GPIO GPIO_NUM_11
/** Speaker data output GPIO. */
#define I2S_DATA_GPIO GPIO_NUM_41
/** Speaker bit-clock GPIO. */
#define I2S_BCLK_GPIO GPIO_NUM_42
/** Speaker word-select GPIO. */
#define I2S_LRCLK_GPIO GPIO_NUM_46
/** Speaker amplifier enable GPIO; low enables the amplifier. */
#define AUDIO_ENABLE_GPIO GPIO_NUM_48
/** Shared peripheral power GPIO. */
#define PERIPHERAL_POWER_GPIO GPIO_NUM_47
/** Shared microphone and speaker sample rate. */
#define SAMPLE_RATE_HZ 16000
/** Number of PCM samples per loopback block. */
#define SAMPLE_COUNT 512
/** Number of seconds recorded before playback starts. */
#define RECORD_SECONDS 5
/** Number of PCM samples stored for one complete recording. */
#define RECORD_SAMPLE_COUNT (SAMPLE_RATE_HZ * RECORD_SECONDS)
/** Target peak used to normalize the recorded clip before playback. */
#define TARGET_PLAYBACK_PEAK 30000.0f
/** Maximum automatic gain used to avoid amplifying background noise too much. */
#define MAXIMUM_GAIN 8.0f
/** Delay after playback so the next recording cycle is easy to observe. */
#define PLAYBACK_PAUSE_MS 5000

/** I2S receive channel for the PDM microphone. */
static i2s_chan_handle_t rx_channel = NULL;
/** I2S transmit channel for the speaker. */
static i2s_chan_handle_t tx_channel = NULL;
/** Small internal PCM block used for I2S read and write calls. */
static int16_t audio_chunk[SAMPLE_COUNT] = {0};
/** Full five-second PCM recording stored in PSRAM. */
static int16_t *record_buffer = NULL;

/**
 * Function: clamp_sample
 * Description: Limits one processed sample to signed 16-bit PCM range.
 * Parameters: value - Floating-point sample after gain processing.
 * Return Value: Clamped signed 16-bit PCM sample.
 */
static int16_t clamp_sample(float value)
{
    if (value > 32767.0f) {                          /* Check whether the processed sample exceeds the positive PCM limit. */
        return 32767;                                /* Return the maximum signed 16-bit value. */
    }                                                /* End positive clipping check. */
    if (value < -32768.0f) {                         /* Check whether the processed sample exceeds the negative PCM limit. */
        return -32768;                               /* Return the minimum signed 16-bit value. */
    }                                                /* End negative clipping check. */
    return (int16_t)value;                           /* Convert the safe floating-point value back to PCM. */
}

/**
 * Function: configure_audio_loopback
 * Description: Configures PDM microphone input and standard I2S speaker output.
 * Parameters: None.
 * Return Value: None.
 */
static void configure_audio_loopback(void)
{
    gpio_config_t gpio_config_data = {0};             /* Store GPIO output configuration. */
    i2s_chan_config_t rx_channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER); /* Create microphone channel configuration. */
    i2s_chan_config_t tx_channel_config = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER); /* Create speaker channel configuration. */
    i2s_pdm_rx_config_t pdm_config = {                /* Store PDM receive configuration. */
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ), /* Set microphone sample rate. */
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO), /* Use 16-bit mono microphone output. */
        .gpio_cfg = {                                 /* Store microphone GPIO assignments. */
            .clk = PDM_CLOCK_GPIO,                    /* Assign PDM microphone clock pin. */
            .din = PDM_DATA_GPIO,                     /* Assign PDM microphone data pin. */
            .invert_flags = {                         /* Store optional microphone inversion flags. */
                .clk_inv = false                      /* Keep PDM clock polarity normal. */
            }                                         /* Finish microphone inversion flag configuration. */
        }                                             /* Finish microphone GPIO configuration. */
    };                                                /* Finish PDM receive configuration. */
    i2s_std_config_t std_config = {                   /* Store standard I2S transmit configuration. */
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE_HZ), /* Set speaker sample rate. */
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO), /* Use 16-bit mono speaker output. */
        .gpio_cfg = {                                 /* Store speaker GPIO assignments. */
            .mclk = I2S_GPIO_UNUSED,                  /* Leave MCLK unused. */
            .bclk = I2S_BCLK_GPIO,                    /* Assign speaker bit clock. */
            .ws = I2S_LRCLK_GPIO,                     /* Assign speaker word select. */
            .dout = I2S_DATA_GPIO,                    /* Assign speaker data output. */
            .din = I2S_GPIO_UNUSED,                   /* Leave standard I2S input unused. */
            .invert_flags = {                         /* Store optional speaker inversion flags. */
                .mclk_inv = false,                    /* Keep MCLK polarity normal. */
                .bclk_inv = false,                    /* Keep BCLK polarity normal. */
                .ws_inv = false                       /* Keep WS polarity normal. */
            }                                         /* Finish speaker inversion flag configuration. */
        }                                             /* Finish speaker GPIO configuration. */
    };                                                /* Finish standard I2S transmit configuration. */

    gpio_config_data.pin_bit_mask = (1ULL << PERIPHERAL_POWER_GPIO) | (1ULL << AUDIO_ENABLE_GPIO); /* Select output power pins. */
    gpio_config_data.mode = GPIO_MODE_OUTPUT;         /* Configure selected pins as outputs. */
    gpio_config_data.pull_up_en = GPIO_PULLUP_DISABLE; /* Disable pull-ups on driven outputs. */
    gpio_config_data.pull_down_en = GPIO_PULLDOWN_DISABLE; /* Disable pull-downs on driven outputs. */
    gpio_config_data.intr_type = GPIO_INTR_DISABLE;   /* Disable output interrupts. */
    ESP_ERROR_CHECK(gpio_config(&gpio_config_data));  /* Apply GPIO configuration. */
    ESP_ERROR_CHECK(gpio_set_level(PERIPHERAL_POWER_GPIO, 1)); /* Enable shared peripheral power. */
    ESP_ERROR_CHECK(gpio_set_level(AUDIO_ENABLE_GPIO, 1)); /* Keep amplifier muted until channels are ready. */

    ESP_ERROR_CHECK(i2s_new_channel(&rx_channel_config, NULL, &rx_channel)); /* Create the microphone receive channel. */
    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(rx_channel, &pdm_config)); /* Initialize microphone PDM receive mode. */
    ESP_ERROR_CHECK(i2s_channel_enable(rx_channel));  /* Enable microphone input. */

    ESP_ERROR_CHECK(i2s_new_channel(&tx_channel_config, &tx_channel, NULL)); /* Create the speaker transmit channel. */
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_channel, &std_config)); /* Initialize speaker standard I2S mode. */
    ESP_ERROR_CHECK(i2s_channel_enable(tx_channel));  /* Enable speaker output. */
}

/**
 * Function: record_audio_clip
 * Description: Records five seconds of microphone PCM into PSRAM while the speaker amplifier stays muted.
 * Parameters: None.
 * Return Value: Largest absolute sample value found in the recording.
 */
static int32_t record_audio_clip(void)
{
    size_t samples_recorded = 0;                      /* Count how many PCM samples have been stored. */
    int32_t peak = 0;                                 /* Store the largest absolute sample value. */

    ESP_ERROR_CHECK(gpio_set_level(AUDIO_ENABLE_GPIO, 1)); /* Keep the amplifier muted during recording to prevent acoustic feedback. */
    ESP_LOGI(TAG, "AUDIO_LOOPBACK: RECORD_START duration=%d s", RECORD_SECONDS); /* Print the recording start message. */

    while (samples_recorded < RECORD_SAMPLE_COUNT) {  /* Continue until the full recording buffer is filled. */
        size_t samples_remaining = RECORD_SAMPLE_COUNT - samples_recorded; /* Calculate how many samples are still needed. */
        size_t samples_to_read = samples_remaining > SAMPLE_COUNT ? SAMPLE_COUNT : samples_remaining; /* Limit each read to the chunk buffer size. */
        size_t bytes_to_read = samples_to_read * sizeof(audio_chunk[0]); /* Convert sample count to byte count for I2S. */
        size_t bytes_read = 0;                        /* Store how many bytes the microphone driver returned. */

        ESP_ERROR_CHECK(i2s_channel_read(rx_channel, audio_chunk, bytes_to_read, &bytes_read, pdMS_TO_TICKS(1000))); /* Capture one microphone PCM block. */
        size_t samples_read = bytes_read / sizeof(audio_chunk[0]); /* Convert returned bytes into sample count. */
        if (samples_read == 0) {                      /* Check whether the read timed out without data. */
            ESP_LOGW(TAG, "AUDIO_LOOPBACK: microphone returned no samples"); /* Print a warning but keep the lesson running. */
            continue;                                /* Try to read the next block. */
        }                                            /* End empty-read handling. */

        memcpy(&record_buffer[samples_recorded], audio_chunk, samples_read * sizeof(audio_chunk[0])); /* Copy the block into the five-second recording. */
        for (size_t index = 0; index < samples_read; index++) { /* Visit every sample for peak detection. */
            int32_t sample = audio_chunk[index];      /* Read one signed PCM sample. */
            int32_t absolute_value = sample < 0 ? -sample : sample; /* Convert it to absolute amplitude. */
            if (absolute_value > peak) {             /* Check whether this is the largest sample so far. */
                peak = absolute_value;               /* Store the new peak. */
            }                                        /* End peak update check. */
        }                                            /* End peak detection loop. */
        samples_recorded += samples_read;            /* Advance the recording position. */
    }                                                /* End recording loop. */

    ESP_LOGI(TAG, "AUDIO_LOOPBACK: RECORD_END samples=%u peak=%ld", (unsigned)samples_recorded, (long)peak); /* Print recording statistics. */
    return peak;                                     /* Return the peak value for gain processing. */
}

/**
 * Function: normalize_recording
 * Description: Applies safe gain to the recorded clip so playback is audible without excessive noise amplification.
 * Parameters: peak - Largest absolute sample value from the recording.
 * Return Value: Applied gain value.
 */
static float normalize_recording(int32_t peak)
{
    float gain = 1.0f;                                /* Start with unity gain. */

    if (peak > 0) {                                   /* Check whether the recording contains non-zero audio. */
        gain = TARGET_PLAYBACK_PEAK / (float)peak;    /* Calculate gain that moves the peak near the target. */
        if (gain > MAXIMUM_GAIN) {                    /* Check whether quiet audio would be amplified too much. */
            gain = MAXIMUM_GAIN;                      /* Limit gain to reduce background-noise amplification. */
        }                                            /* End gain-limit check. */
    }                                                /* End gain calculation. */

    for (size_t index = 0; index < RECORD_SAMPLE_COUNT; index++) { /* Visit the full recorded clip. */
        record_buffer[index] = clamp_sample((float)record_buffer[index] * gain); /* Apply gain and clamp the PCM sample. */
    }                                                /* End normalization loop. */

    ESP_LOGI(TAG, "AUDIO_LOOPBACK: PROCESS gain=%.2f", gain); /* Print the applied gain. */
    return gain;                                     /* Return the gain for completeness. */
}

/**
 * Function: play_recorded_clip
 * Description: Plays the stored recording through the speaker, then mutes the amplifier.
 * Parameters: None.
 * Return Value: Number of bytes written to the speaker I2S channel.
 */
static size_t play_recorded_clip(void)
{
    size_t samples_played = 0;                        /* Count how many recorded samples have been played. */
    size_t total_bytes_written = 0;                   /* Count how many bytes the speaker driver accepted. */

    ESP_LOGI(TAG, "AUDIO_LOOPBACK: PLAY_START");     /* Print that playback is starting. */
    ESP_ERROR_CHECK(gpio_set_level(AUDIO_ENABLE_GPIO, 0)); /* Enable the active-low speaker amplifier only during playback. */
    vTaskDelay(pdMS_TO_TICKS(20));                    /* Wait briefly so the amplifier turns on cleanly. */

    while (samples_played < RECORD_SAMPLE_COUNT) {    /* Continue until the full recording has been played. */
        size_t samples_remaining = RECORD_SAMPLE_COUNT - samples_played; /* Calculate samples left to play. */
        size_t samples_to_write = samples_remaining > SAMPLE_COUNT ? SAMPLE_COUNT : samples_remaining; /* Limit each write to the chunk buffer size. */
        size_t bytes_to_write = samples_to_write * sizeof(audio_chunk[0]); /* Convert sample count to byte count. */
        size_t bytes_written = 0;                     /* Store how many bytes this I2S write accepted. */

        memcpy(audio_chunk, &record_buffer[samples_played], bytes_to_write); /* Copy one playback block into the internal chunk buffer. */
        ESP_ERROR_CHECK(i2s_channel_write(tx_channel, audio_chunk, bytes_to_write, &bytes_written, pdMS_TO_TICKS(1000))); /* Play one block through I2S. */
        total_bytes_written += bytes_written;         /* Accumulate the total playback byte count. */
        samples_played += bytes_written / sizeof(audio_chunk[0]); /* Advance by the number of accepted samples. */
    }                                                /* End playback loop. */

    ESP_ERROR_CHECK(gpio_set_level(AUDIO_ENABLE_GPIO, 1)); /* Mute the amplifier immediately after playback. */
    ESP_LOGI(TAG, "AUDIO_LOOPBACK: PLAY_END bytes=%u", (unsigned)total_bytes_written); /* Print playback statistics. */
    return total_bytes_written;                      /* Return the playback byte count. */
}

/**
 * Function: app_main
 * Description: Records one audio clip first, then plays it back to avoid real-time acoustic feedback.
 * Parameters: None.
 * Return Value: None.
 */
void app_main(void)
{
    configure_audio_loopback();                       /* Prepare microphone, speaker, and amplifier hardware. */
    record_buffer = (int16_t *)heap_caps_malloc(RECORD_SAMPLE_COUNT * sizeof(record_buffer[0]), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); /* Allocate the full clip buffer in PSRAM. */
    ESP_ERROR_CHECK(record_buffer == NULL ? ESP_ERR_NO_MEM : ESP_OK); /* Stop clearly if the recording buffer cannot be allocated. */
    ESP_LOGI(TAG, "Audio record-then-play loopback is running."); /* Print the lesson start message. */

    while (true) {                                    /* Repeat the record-then-play demonstration forever. */
        int32_t peak = record_audio_clip();           /* Record one five-second microphone clip while muted. */
        (void)normalize_recording(peak);              /* Normalize the clip before playback. */
        (void)play_recorded_clip();                   /* Play the recorded clip through the speaker. */
        vTaskDelay(pdMS_TO_TICKS(PLAYBACK_PAUSE_MS)); /* Wait before starting the next recording cycle. */
    }                                                 /* End repeating record-then-play loop. */
}
