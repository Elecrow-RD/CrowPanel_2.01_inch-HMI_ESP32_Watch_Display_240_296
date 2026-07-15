/*
 * ESP32-S3 Watch course project.
 *
 * All source comments are written in English for code-reading practice.
 * The tutorial documents are written in Chinese for classroom delivery.
 */
#include <string.h>                                  /* Provides string copying for WiFi configuration. */
#include <time.h>                                    /* Provides time_t and struct tm for NTP conversion. */
#include "driver/i2c_master.h"                       /* Provides the ESP-IDF v5.5 I2C master driver. */
#include "esp_event.h"                               /* Provides the event loop used by WiFi. */
#include "esp_log.h"                                 /* Provides serial log output for sync status. */
#include "esp_netif.h"                               /* Provides network interface initialization. */
#include "esp_sntp.h"                                /* Provides SNTP time synchronization APIs. */
#include "esp_wifi.h"                                /* Provides WiFi station APIs. */
#include "freertos/FreeRTOS.h"                       /* Provides FreeRTOS timing conversion macros. */
#include "freertos/event_groups.h"                   /* Provides event bits for WiFi connection waiting. */
#include "freertos/task.h"                           /* Provides vTaskDelay() for loops. */
#include "nvs_flash.h"                               /* Provides NVS initialization required by WiFi. */

/** Tag used by ESP-IDF logs from this lesson. */
static const char *TAG = "Lesson07";
/** Classroom WiFi SSID placeholder. */
static const char *WIFI_SSID = "elecrow888";
/** Classroom WiFi password placeholder. */
static const char *WIFI_PASSWORD = "elecrow2014";
/** I2C SDA GPIO for PCF8563. */
#define WATCH_I2C_SDA GPIO_NUM_4
/** I2C SCL GPIO for PCF8563. */
#define WATCH_I2C_SCL GPIO_NUM_3
/** PCF8563 seven-bit I2C address. */
#define PCF8563_ADDRESS 0x51
/** Event bit set after WiFi gets an IP address. */
#define WIFI_CONNECTED_BIT BIT0

/** Event group used to wait until WiFi has an IP address. */
static EventGroupHandle_t wifi_event_group = NULL;
/** I2C device handle for the PCF8563 RTC. */
static i2c_master_dev_handle_t rtc_device = NULL;

/**
 * Function: decimal_to_bcd
 * Description: Converts a decimal value into the packed-BCD format used by PCF8563.
 * Parameters: value - Decimal value from the system clock.
 * Return Value: Packed-BCD value for the RTC register.
 */
static uint8_t decimal_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4) | (value % 10U)); /* Pack tens into the high nibble and ones into the low nibble. */
}

/**
 * Function: wifi_event_handler
 * Description: Starts WiFi connection and signals when an IP address is available.
 * Parameters: arg - Unused user pointer; event_base - Event category; event_id - Event number; event_data - Event payload.
 * Return Value: None.
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;                                       /* Mark the unused argument pointer as intentionally unused. */
    (void)event_data;                                /* Mark the optional event payload as intentionally unused. */

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) { /* Check whether station mode started. */
        ESP_ERROR_CHECK(esp_wifi_connect());         /* Begin the station connection. */
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) { /* Check whether the station disconnected. */
        ESP_LOGW(TAG, "WiFi disconnected, reconnecting"); /* Print the reconnect event. */
        ESP_ERROR_CHECK(esp_wifi_connect());         /* Try to reconnect so SNTP can continue later. */
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) { /* Check whether DHCP succeeded. */
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT); /* Signal that network time can be requested. */
    }                                                    /* End the event classification. */
}

/**
 * Function: configure_rtc_bus
 * Description: Creates the I2C bus and attaches the PCF8563 device.
 * Parameters: None.
 * Return Value: None.
 */
static void configure_rtc_bus(void)
{
    i2c_master_bus_config_t bus_config = {0};         /* Store the I2C bus configuration. */
    i2c_device_config_t device_config = {0};          /* Store the RTC device configuration. */
    i2c_master_bus_handle_t bus_handle = NULL;        /* Store the created I2C bus handle. */

    bus_config.i2c_port = I2C_NUM_0;                  /* Use the first I2C controller. */
    bus_config.sda_io_num = WATCH_I2C_SDA;            /* Assign the board SDA pin. */
    bus_config.scl_io_num = WATCH_I2C_SCL;            /* Assign the board SCL pin. */
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;      /* Use the default I2C clock source. */
    bus_config.glitch_ignore_cnt = 7;                 /* Ignore short line glitches. */
    bus_config.flags.enable_internal_pullup = true;   /* Enable internal pull-ups for the onboard I2C bus. */
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle)); /* Create the I2C bus. */

    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7; /* Use seven-bit device addressing. */
    device_config.device_address = PCF8563_ADDRESS;   /* Set the PCF8563 address. */
    device_config.scl_speed_hz = 400000;              /* Use 400 kHz I2C speed. */
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &device_config, &rtc_device)); /* Attach the RTC device. */
}

/**
 * Function: write_rtc_from_system_time
 * Description: Writes the SNTP-synchronized system time into PCF8563 registers.
 * Parameters: None.
 * Return Value: None.
 */
static void write_rtc_from_system_time(void)
{
    time_t now = 0;                                  /* Store the current Unix time. */
    struct tm local_time = {0};                      /* Store the broken-out calendar time. */
    uint8_t packet[8] = {0};                         /* Store the register address followed by seven time registers. */

    time(&now);                                      /* Read the current system time. */
    localtime_r(&now, &local_time);                  /* Convert Unix time into calendar fields. */
    packet[0] = 0x02;                                /* Select the PCF8563 seconds register as the write start. */
    packet[1] = decimal_to_bcd((uint8_t)local_time.tm_sec); /* Encode seconds. */
    packet[2] = decimal_to_bcd((uint8_t)local_time.tm_min); /* Encode minutes. */
    packet[3] = decimal_to_bcd((uint8_t)local_time.tm_hour); /* Encode hours. */
    packet[4] = decimal_to_bcd((uint8_t)local_time.tm_mday); /* Encode day of month. */
    packet[5] = decimal_to_bcd((uint8_t)local_time.tm_wday); /* Encode weekday. */
    packet[6] = decimal_to_bcd((uint8_t)(local_time.tm_mon + 1)); /* Encode month in 1 to 12 format. */
    packet[7] = decimal_to_bcd((uint8_t)(local_time.tm_year % 100)); /* Encode two-digit year. */
    ESP_ERROR_CHECK(i2c_master_transmit(rtc_device, packet, sizeof(packet), pdMS_TO_TICKS(100))); /* Write the complete time block to RTC. */
    ESP_LOGI(TAG, "RTC synced to %04d-%02d-%02d %02d:%02d:%02d", local_time.tm_year + 1900, local_time.tm_mon + 1, local_time.tm_mday, local_time.tm_hour, local_time.tm_min, local_time.tm_sec); /* Print the synchronized time. */
}

/**
 * Function: initialize_wifi
 * Description: Starts station mode and waits for IP connectivity.
 * Parameters: None.
 * Return Value: None.
 */
static void initialize_wifi(void)
{
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT(); /* Create the default WiFi driver configuration. */
    wifi_config_t station_config = {0};              /* Store the station credentials. */

    ESP_ERROR_CHECK(nvs_flash_init());               /* Initialize NVS for WiFi calibration storage. */
    ESP_ERROR_CHECK(esp_netif_init());               /* Initialize ESP-IDF networking. */
    ESP_ERROR_CHECK(esp_event_loop_create_default()); /* Create the default event loop. */
    wifi_event_group = xEventGroupCreate();          /* Allocate the event group used for IP waiting. */
    ESP_ERROR_CHECK(wifi_event_group == NULL ? ESP_ERR_NO_MEM : ESP_OK); /* Stop if the event group allocation failed. */
    esp_netif_create_default_wifi_sta();             /* Create the default WiFi station interface. */
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));    /* Initialize the WiFi driver. */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL)); /* Register WiFi events. */
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL)); /* Register IP events. */
    strncpy((char *)station_config.sta.ssid, WIFI_SSID, sizeof(station_config.sta.ssid)); /* Copy the WiFi SSID. */
    strncpy((char *)station_config.sta.password, WIFI_PASSWORD, sizeof(station_config.sta.password)); /* Copy the WiFi password. */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); /* Select station mode. */
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &station_config)); /* Apply station credentials. */
    ESP_ERROR_CHECK(esp_wifi_start());               /* Start WiFi and trigger the start event. */
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(30000)); /* Wait up to 30 seconds for an IP address. */
}

/**
 * Function: app_main
 * Description: Connects to WiFi, synchronizes time through NTP, and writes the result to PCF8563.
 * Parameters: None.
 * Return Value: None.
 */
void app_main(void)
{
    configure_rtc_bus();                             /* Prepare the RTC I2C bus first. */
    initialize_wifi();                               /* Connect to WiFi so SNTP can reach the network. */
    setenv("TZ", "CST-8", 1);                        /* Set China Standard Time for classroom-readable output. */
    tzset();                                         /* Apply the timezone environment variable. */
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);     /* Configure SNTP for polling mode. */
    esp_sntp_setservername(0, "pool.ntp.org");       /* Select a public NTP server. */
    esp_sntp_init();                                 /* Start SNTP synchronization. */
    vTaskDelay(pdMS_TO_TICKS(5000));                 /* Wait a few seconds for the first SNTP response. */
    write_rtc_from_system_time();                    /* Copy the synchronized system time into PCF8563. */

    while (true) {                                   /* Keep the program alive after synchronization. */
        vTaskDelay(pdMS_TO_TICKS(60000));            /* Wait one minute between status messages. */
        write_rtc_from_system_time();                /* Refresh the RTC periodically during long demonstrations. */
    }                                                /* End the RTC sync loop. */
}