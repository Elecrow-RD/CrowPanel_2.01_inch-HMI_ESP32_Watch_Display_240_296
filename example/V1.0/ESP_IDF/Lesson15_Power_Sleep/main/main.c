/*
 * ESP32-S3 Watch course project.
 *
 * All source comments are written in English for code-reading practice.
 * The tutorial documents are written in Chinese for classroom delivery.
 */
#include "driver/i2c_master.h"                     /* Provides the ESP-IDF v5.5 I2C master driver for the touch controller. */
#include "driver/gpio.h"                           /* Provides GPIO configuration and wakeup APIs. */
#include "esp_err.h"                               /* Provides esp_err_to_name() for readable error messages. */
#include "esp_log.h"                               /* Provides serial log output for sleep state changes. */
#include "esp_sleep.h"                             /* Provides light-sleep and wakeup-cause APIs. */
#include "freertos/FreeRTOS.h"                     /* Provides FreeRTOS timing conversion macros. */
#include "freertos/task.h"                         /* Provides vTaskDelay() before entering sleep. */

/** Tag used by ESP-IDF logs from this lesson. */
static const char *TAG = "Lesson15";
/** Shared I2C SDA GPIO used by the touch controller. */
#define WATCH_I2C_SDA GPIO_NUM_4
/** Shared I2C SCL GPIO used by the touch controller. */
#define WATCH_I2C_SCL GPIO_NUM_3
/** AXS5106L touch interrupt GPIO used as the wake source. */
#define TOUCH_INTERRUPT_GPIO GPIO_NUM_2
/** Touch reset GPIO used to keep the touch controller alive before sleep. */
#define TOUCH_RESET_GPIO GPIO_NUM_5
/** Shared peripheral power GPIO; high enables the rail used by the touch controller. */
#define PERIPHERAL_POWER_GPIO GPIO_NUM_47
/** AXS5106L seven-bit I2C address copied from the display and touch lesson. */
#define AXS5106L_ADDRESS 0x63
/** AXS5106L touch data register read by the display lesson. */
#define AXS5106L_TOUCH_DATA_REGISTER 0x01
/** Number of bytes read from the AXS5106L touch packet to clear pending INT state. */
#define AXS5106L_TOUCH_PACKET_BYTES 14
/** Time to stay awake before entering light sleep again. */
#define AWAKE_TIME_MS 3000
/** Timer wakeup in microseconds; if no touch happens, the board wakes and sleeps again. */
#define TIMER_WAKEUP_US 3000000ULL

/** I2C device handle for the AXS5106L touch controller. */
static i2c_master_dev_handle_t touch_device = NULL;

/**
 * Function: touch_read_register
 * Description: Reads one register block from the AXS5106L touch controller.
 * Parameters: reg - Register address; data - Destination buffer; length - Number of bytes to read.
 * Return Value: ESP-IDF error code from the I2C transaction.
 */
static esp_err_t touch_read_register(uint8_t reg, uint8_t *data, size_t length)
{
    return i2c_master_transmit_receive(touch_device, &reg, 1, data, length, pdMS_TO_TICKS(100)); /* Read the requested AXS5106L register block. */
}

/**
 * Function: clear_touch_interrupt
 * Description: Reads the AXS5106L touch packet so a pending active-low INT can be released before sleep.
 * Parameters: None.
 * Return Value: ESP-IDF error code from the touch read operation.
 */
static esp_err_t clear_touch_interrupt(void)
{
    uint8_t packet[AXS5106L_TOUCH_PACKET_BYTES] = {0}; /* Store one touch status packet from the controller. */
    esp_err_t result = touch_read_register(AXS5106L_TOUCH_DATA_REGISTER, packet, sizeof(packet)); /* Read the same touch packet used by Lesson03. */

    if (result == ESP_OK) {                         /* Check whether the touch status read succeeded. */
        ESP_LOGI(TAG, "TOUCH: clear INT packet_status=%u points=%u", packet[0], packet[1]); /* Print status bytes that may explain why INT was low. */
    } else {                                        /* Handle an I2C read failure. */
        ESP_LOGW(TAG, "TOUCH: clear INT read failed: %s", esp_err_to_name(result)); /* Print the readable I2C error name. */
    }                                               /* End clear result handling. */
    return result;                                  /* Return the I2C result to the caller. */
}

/**
 * Function: configure_touch_wakeup_hardware
 * Description: Powers, resets, and attaches the AXS5106L touch controller used as the wake source.
 * Parameters: None.
 * Return Value: None.
 */
static void configure_touch_wakeup_hardware(void)
{
    gpio_config_t input_config = {0};                /* Store the GPIO input configuration for the touch interrupt. */
    gpio_config_t output_config = {0};               /* Store the GPIO output configuration for touch power and reset. */
    i2c_master_bus_config_t bus_config = {0};        /* Store the I2C bus configuration. */
    i2c_device_config_t device_config = {0};         /* Store the AXS5106L device configuration. */
    i2c_master_bus_handle_t bus_handle = NULL;       /* Store the created I2C bus handle. */
    uint8_t id[3] = {0};                             /* Store optional touch-controller ID bytes. */

    output_config.pin_bit_mask = (1ULL << PERIPHERAL_POWER_GPIO) | (1ULL << TOUCH_RESET_GPIO); /* Select touch power and reset pins. */
    output_config.mode = GPIO_MODE_OUTPUT;           /* Configure selected pins as outputs. */
    output_config.pull_up_en = GPIO_PULLUP_DISABLE;  /* Disable pull-up because these pins are actively driven. */
    output_config.pull_down_en = GPIO_PULLDOWN_DISABLE; /* Disable pull-down because these pins are actively driven. */
    output_config.intr_type = GPIO_INTR_DISABLE;     /* Disable interrupts on output pins. */
    ESP_ERROR_CHECK(gpio_config(&output_config));    /* Apply output pin configuration. */
    ESP_ERROR_CHECK(gpio_set_level(PERIPHERAL_POWER_GPIO, 1)); /* Enable the shared peripheral rail used by the touch controller. */
    ESP_ERROR_CHECK(gpio_set_level(TOUCH_RESET_GPIO, 0)); /* Hold the touch controller in reset. */
    vTaskDelay(pdMS_TO_TICKS(200));                  /* Match the Lesson03 reset low time for a clean touch restart. */
    ESP_ERROR_CHECK(gpio_set_level(TOUCH_RESET_GPIO, 1)); /* Release the touch controller from reset. */
    vTaskDelay(pdMS_TO_TICKS(300));                  /* Wait for the touch firmware to boot and release INT. */

    input_config.pin_bit_mask = 1ULL << TOUCH_INTERRUPT_GPIO; /* Select the touch interrupt pin. */
    input_config.mode = GPIO_MODE_INPUT;              /* Configure the touch interrupt as an input. */
    input_config.pull_up_en = GPIO_PULLUP_ENABLE;     /* Enable a pull-up because the interrupt is active low. */
    input_config.pull_down_en = GPIO_PULLDOWN_DISABLE; /* Disable pull-down to avoid loading the interrupt line. */
    input_config.intr_type = GPIO_INTR_DISABLE;       /* Disable normal interrupts because sleep wake handles the level. */
    ESP_ERROR_CHECK(gpio_config(&input_config));      /* Apply the touch interrupt configuration. */

    bus_config.i2c_port = I2C_NUM_0;                  /* Use the same I2C controller as the display and touch lesson. */
    bus_config.sda_io_num = WATCH_I2C_SDA;            /* Assign the board SDA pin. */
    bus_config.scl_io_num = WATCH_I2C_SCL;            /* Assign the board SCL pin. */
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;      /* Use the default I2C clock source. */
    bus_config.glitch_ignore_cnt = 7;                 /* Filter short I2C glitches. */
    bus_config.flags.enable_internal_pullup = true;   /* Enable internal pull-ups for the onboard bus. */
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle)); /* Create the I2C bus. */

    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7; /* Use seven-bit I2C addressing. */
    device_config.device_address = AXS5106L_ADDRESS; /* Set the verified AXS5106L address. */
    device_config.scl_speed_hz = 400000;             /* Use the same 400 kHz speed as Lesson03. */
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &device_config, &touch_device)); /* Attach the touch controller. */
    esp_err_t id_result = touch_read_register(0x08, id, sizeof(id)); /* Read ID bytes to confirm I2C communication. */
    if (id_result == ESP_OK) {                       /* Check whether the ID read succeeded. */
        ESP_LOGI(TAG, "AXS5106L ID bytes: %02X %02X %02X", id[0], id[1], id[2]); /* Print ID bytes for troubleshooting. */
    } else {                                        /* Handle an ID read failure without hiding the real sleep logs. */
        ESP_LOGW(TAG, "AXS5106L ID read failed: %s", esp_err_to_name(id_result)); /* Print the I2C failure name. */
    }                                               /* End ID read handling. */
}

/**
 * Function: wakeup_cause_name
 * Description: Converts one ESP-IDF wakeup cause into classroom-readable text.
 * Parameters: cause - Wakeup cause returned by esp_sleep_get_wakeup_cause().
 * Return Value: Readable wakeup-cause string.
 */
static const char *wakeup_cause_name(esp_sleep_wakeup_cause_t cause)
{
    switch (cause) {                                /* Select a readable name for the wake cause. */
    case ESP_SLEEP_WAKEUP_GPIO:                     /* Handle touch interrupt wake through light-sleep GPIO wake. */
        return "GPIO";                             /* Return the GPIO wake label. */
    case ESP_SLEEP_WAKEUP_TIMER:                    /* Handle automatic timer wake. */
        return "TIMER";                            /* Return the timer wake label. */
    default:                                        /* Handle reset or any unsupported cause. */
        return "UNDEFINED";                        /* Return a generic label. */
    }                                               /* End wake-cause switch. */
}

/**
 * Function: wake_level_name
 * Description: Converts the configured GPIO wake level into readable text.
 * Parameters: wake_level - GPIO low-level or high-level wake setting.
 * Return Value: Readable wake-level string.
 */
static const char *wake_level_name(gpio_int_type_t wake_level)
{
    return wake_level == GPIO_INTR_LOW_LEVEL ? "LOW" : "HIGH"; /* Return the level name used in the serial log. */
}

/**
 * Function: arm_touch_gpio_wakeup
 * Description: Reads GPIO2 and arms wakeup on the opposite level, matching the verified Arduino lesson behavior.
 * Parameters: None.
 * Return Value: ESP-IDF error code from gpio_wakeup_enable().
 */
static esp_err_t arm_touch_gpio_wakeup(void)
{
    int current_level = gpio_get_level(TOUCH_INTERRUPT_GPIO); /* Read the current AXS5106L INT level before sleep. */
    gpio_int_type_t wake_level = current_level == 1 ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL; /* Wake when GPIO2 changes away from its current level. */
    esp_err_t result = gpio_wakeup_enable(TOUCH_INTERRUPT_GPIO, wake_level); /* Configure GPIO2 as a light-sleep wake source. */

    ESP_LOGI(TAG, "WAKE_CONFIG: TOUCH_INT GPIO2 current=%d wake_level=%s result=%s", current_level, wake_level_name(wake_level), esp_err_to_name(result)); /* Print the exact dynamic wake setting. */
    return result;                                  /* Return whether GPIO2 wakeup was accepted. */
}

/**
 * Function: enter_light_sleep
 * Description: Arms dynamic GPIO2 and timer wake sources, enters light sleep, and prints the wake result.
 * Parameters: None.
 * Return Value: None.
 */
static void enter_light_sleep(void)
{
    esp_err_t touch_wakeup_result;                  /* Store whether GPIO2 wakeup was configured successfully. */

    ESP_ERROR_CHECK(esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)); /* Clear every previous wake source before arming this cycle. */
    (void)gpio_wakeup_disable(TOUCH_INTERRUPT_GPIO); /* Clear any previous GPIO2 wake level before choosing the next one. */
    touch_wakeup_result = arm_touch_gpio_wakeup();  /* Configure GPIO2 to wake on the opposite level from its current state. */
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup(TIMER_WAKEUP_US)); /* Wake automatically after three seconds if no touch happens. */
    if (touch_wakeup_result == ESP_OK) {            /* Check whether the touch GPIO wake source was accepted. */
        ESP_ERROR_CHECK(esp_sleep_enable_gpio_wakeup()); /* Enable light-sleep GPIO wakeup after GPIO2 is configured. */
    } else {                                        /* Handle the unlikely case that GPIO2 wakeup cannot be armed. */
        ESP_LOGW(TAG, "WAKE_CONFIG: GPIO wake disabled, timer wake remains active"); /* Keep the lesson running with timer wake only. */
    }                                               /* End GPIO wake enable handling. */

    ESP_LOGI(TAG, "SLEEP: entering light sleep; touch GPIO2 to wake, timer=%u us", (unsigned)TIMER_WAKEUP_US); /* Print the armed wake behavior. */
    esp_err_t result = esp_light_sleep_start();      /* Enter light sleep and resume here after GPIO or timer wake. */
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause(); /* Read the wake reason after light sleep returns. */
    int gpio2_level_after_wake = gpio_get_level(TOUCH_INTERRUPT_GPIO); /* Read GPIO2 after wake for visible touch diagnostics. */

    ESP_LOGI(TAG, "SLEEP: wake result=%s cause=%s(%d) GPIO2=%d", esp_err_to_name(result), wakeup_cause_name(cause), (int)cause, gpio2_level_after_wake); /* Print the result. */
    (void)clear_touch_interrupt();                   /* Clear any touch packet generated by a touch wake. */
}

/**
 * Function: app_main
 * Description: Repeatedly enters light sleep and exits when the screen is touched or the three-second timer fires.
 * Parameters: None.
 * Return Value: None.
 */
void app_main(void)
{
    configure_touch_wakeup_hardware();              /* Prepare touch power, reset, I2C, and interrupt input. */
    ESP_LOGI(TAG, "Lesson15 is ready: touch the screen to exit light sleep."); /* Explain the intended hardware action. */

    while (true) {                                  /* Repeat the simple sleep demonstration forever. */
        ESP_LOGI(TAG, "AWAKE: waiting %d ms before sleep", AWAKE_TIME_MS); /* Show the three-second awake window. */
        vTaskDelay(pdMS_TO_TICKS(AWAKE_TIME_MS));   /* Stay awake for three seconds before sleeping again. */
        enter_light_sleep();                        /* Enter light sleep and return after touch or timer wake. */
    }                                               /* End repeating sleep loop. */
}
