/*
 * ESP32-S3 Watch course project.
 *
 * All source comments are written in English for code-reading practice.
 * The tutorial documents are written in Chinese for classroom delivery.
 */
#include <string.h>                                  /* Provides string helpers used by WiFi station configuration. */
#include "esp_event.h"                              /* Provides the event loop used by the WiFi driver. */
#include "esp_log.h"                                /* Provides serial log output for WiFi status. */
#include "esp_netif.h"                              /* Provides network interface initialization for station mode. */
#include "esp_wifi.h"                               /* Provides ESP-IDF WiFi station APIs. */
#include "freertos/FreeRTOS.h"                      /* Provides FreeRTOS timing conversion macros. */
#include "freertos/event_groups.h"                  /* Provides the event group used to wait for connection status. */
#include "freertos/task.h"                          /* Provides vTaskDelay() for the status loop. */
#include "nvs_flash.h"                              /* Provides NVS initialization required by the WiFi driver. */

/** Tag used by ESP-IDF logs from this lesson. */
static const char *TAG = "Lesson06";
/** WiFi SSID placeholder copied from the verified Arduino lesson for classroom editing. */
static const char *WIFI_SSID = "elecrow888";
/** WiFi password placeholder copied from the verified Arduino lesson for classroom editing. */
static const char *WIFI_PASSWORD = "elecrow2014";
/** Event bit set when the station receives an IPv4 address. */
#define WIFI_CONNECTED_BIT BIT0
/** Event bit set when the station connection fails. */
#define WIFI_FAILED_BIT BIT1

/** Event group used to synchronize WiFi connection attempts. */
static EventGroupHandle_t wifi_event_group = NULL;

/**
 * Function: wifi_event_handler
 * Description: Handles WiFi and IP events so the lesson can print connection state.
 * Parameters: arg - Unused user pointer; event_base - Event category; event_id - Event number; event_data - Event payload.
 * Return Value: None.
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;                                      /* Mark the unused argument pointer as intentionally unused. */
    (void)event_data;                               /* Mark the optional event payload as intentionally unused. */

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) { /* Check whether station mode just started. */
        ESP_ERROR_CHECK(esp_wifi_connect());        /* Start connecting to the configured access point. */
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) { /* Check whether the station disconnected. */
        ESP_LOGW(TAG, "WiFi disconnected");        /* Print the disconnection event. */
        xEventGroupSetBits(wifi_event_group, WIFI_FAILED_BIT); /* Signal the connection wait path that the attempt failed. */
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) { /* Check whether DHCP assigned an IP address. */
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data; /* Cast the event payload to the IP event type. */
        ESP_LOGI(TAG, "IP address: " IPSTR, IP2STR(&event->ip_info.ip)); /* Print the assigned IPv4 address. */
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT); /* Signal the connection wait path that WiFi is ready. */
    }                                                           /* End the event classification. */
}

/**
 * Function: initialize_wifi
 * Description: Initializes WiFi station mode and directly connects to the configured access point.
 * Parameters: None.
 * Return Value: None.
 */
static void initialize_wifi(void)
{
    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT(); /* Create the default WiFi driver configuration. */
    wifi_config_t station_config = {0};             /* Store the station SSID and password configuration. */

    ESP_ERROR_CHECK(nvs_flash_init());              /* Initialize NVS because WiFi stores calibration data there. */
    ESP_ERROR_CHECK(esp_netif_init());              /* Initialize TCP/IP network interfaces. */
    ESP_ERROR_CHECK(esp_event_loop_create_default()); /* Create the default event loop used by WiFi and IP events. */
    wifi_event_group = xEventGroupCreate();         /* Create the event group used to wait for connection results. */
    ESP_ERROR_CHECK(wifi_event_group == NULL ? ESP_ERR_NO_MEM : ESP_OK); /* Stop early if the event group allocation failed. */
    esp_netif_create_default_wifi_sta();            /* Create the default station network interface. */
    ESP_ERROR_CHECK(esp_wifi_init(&init_config));   /* Start the WiFi driver. */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL)); /* Register WiFi event handling. */
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL)); /* Register IP event handling. */
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); /* Put the radio into station mode. */
    strncpy((char *)station_config.sta.ssid, WIFI_SSID, sizeof(station_config.sta.ssid)); /* Copy the classroom SSID into the station config. */
    strncpy((char *)station_config.sta.password, WIFI_PASSWORD, sizeof(station_config.sta.password)); /* Copy the classroom password into the station config. */
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &station_config)); /* Apply the station credentials. */
    ESP_LOGI(TAG, "Connecting directly to WiFi SSID: %s", WIFI_SSID); /* Print the target SSID before the connection starts. */
    ESP_ERROR_CHECK(esp_wifi_start());              /* Start WiFi; the STA_START event handler will call esp_wifi_connect(). */
}

/**
 * Function: app_main
 * Description: Demonstrates direct WiFi station connection status.
 * Parameters: None.
 * Return Value: None.
 */
void app_main(void)
{
    initialize_wifi();                              /* Initialize WiFi and connect directly to the configured access point. */

    while (true) {                                  /* Keep the task alive so WiFi background work continues. */
        EventBits_t bits = xEventGroupGetBits(wifi_event_group); /* Read the current WiFi connection status bits. */
        ESP_LOGI(TAG, "WiFi status: connected=%s failed=%s", (bits & WIFI_CONNECTED_BIT) ? "YES" : "NO", (bits & WIFI_FAILED_BIT) ? "YES" : "NO"); /* Print a compact WiFi status line. */
        vTaskDelay(pdMS_TO_TICKS(5000));            /* Wait five seconds before printing the next status line. */
    }                                               /* End the WiFi status loop. */
}
