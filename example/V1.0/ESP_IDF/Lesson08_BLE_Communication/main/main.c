/*
 * ESP32-S3 Watch course project.
 *
 * All source comments are written in English for code-reading practice.
 * The tutorial documents are written in Chinese for classroom delivery.
 */
#include <string.h>                                  /* Provides strlen() for BLE characteristic responses. */
#include "esp_log.h"                                /* Provides serial log output for BLE events. */
#include "nvs_flash.h"                              /* Provides NVS initialization required by the Bluetooth stack. */
#include "host/ble_hs.h"                            /* Provides NimBLE host APIs and event definitions. */
#include "host/ble_uuid.h"                          /* Provides UUID helper macros for GATT definitions. */
#include "nimble/nimble_port.h"                     /* Provides NimBLE port initialization APIs. */
#include "nimble/nimble_port_freertos.h"            /* Provides the FreeRTOS host-task helper. */
#include "services/gap/ble_svc_gap.h"               /* Provides GAP device-name service helpers. */
#include "services/gatt/ble_svc_gatt.h"             /* Provides GATT server registration helpers. */

/** Tag used by ESP-IDF logs from this lesson. */
static const char *TAG = "Lesson08";
/** BLE device name shown in phone scanner applications. */
static const char *DEVICE_NAME = "ESP32-Watch";
/** Current text exposed by the custom characteristic. */
static char characteristic_value[64] = "BLE ready";
/** NimBLE address type selected during startup. */
static uint8_t own_addr_type = 0;

/**
 * Function: gatt_access_callback
 * Description: Handles phone reads and writes to the custom characteristic.
 * Parameters: conn_handle - Connection handle; attr_handle - Attribute handle; ctxt - Access context; arg - Unused user pointer.
 * Return Value: 0 on success or a BLE ATT error code.
 */
static int gatt_access_callback(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;                               /* Mark the connection handle as intentionally unused. */
    (void)attr_handle;                               /* Mark the attribute handle as intentionally unused. */
    (void)arg;                                       /* Mark the optional user argument as intentionally unused. */

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {  /* Check whether the phone is reading the characteristic. */
        return os_mbuf_append(ctxt->om, characteristic_value, strlen(characteristic_value)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES; /* Send the current text value to the phone. */
    }                                                /* End the read operation path. */

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) { /* Check whether the phone wrote new data. */
        uint16_t length = OS_MBUF_PKTLEN(ctxt->om); /* Read the number of received bytes. */
        if (length >= sizeof(characteristic_value)) { /* Check whether the received value fits the teaching buffer. */
            length = sizeof(characteristic_value) - 1U; /* Leave room for a terminating null byte. */
        }                                            /* End the length clamp. */
        os_mbuf_copydata(ctxt->om, 0, length, characteristic_value); /* Copy the received bytes into the local buffer. */
        characteristic_value[length] = '\0';          /* Null-terminate the received text. */
        ESP_LOGI(TAG, "BLE_RX_TEXT: %s", characteristic_value); /* Print the received text to the serial monitor. */
        return 0;                                     /* Report that the write was accepted. */
    }                                                /* End the write operation path. */

    return BLE_ATT_ERR_UNLIKELY;                     /* Report an error for unsupported access operations. */
}

/** Custom GATT service and characteristic table used by this lesson. */
static const struct ble_gatt_svc_def gatt_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,           /* Declare a primary custom service. */
        .uuid = BLE_UUID16_DECLARE(0xFFF0),          /* Use a simple 16-bit classroom UUID for the service. */
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0xFFF1), /* Use a simple 16-bit classroom UUID for the data characteristic. */
                .access_cb = gatt_access_callback,  /* Route reads and writes to the lesson callback. */
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE, /* Allow phone apps to read and write the value. */
            },
            {0},                                     /* Terminate the characteristic list. */
        },
    },
    {0},                                             /* Terminate the service list. */
};

/**
 * Function: start_advertising
 * Description: Starts BLE advertising so a phone can discover and connect to the watch.
 * Parameters: None.
 * Return Value: None.
 */
static void start_advertising(void)
{
    struct ble_gap_adv_params adv_params = {0};      /* Store BLE advertising parameters. */
    struct ble_hs_adv_fields fields = {0};           /* Store BLE advertising payload fields. */

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP; /* Advertise as a general BLE-only device. */
    fields.name = (uint8_t *)DEVICE_NAME;            /* Put the device name in the advertising payload. */
    fields.name_len = (uint8_t)strlen(DEVICE_NAME);  /* Store the advertising-name length. */
    fields.name_is_complete = 1;                     /* Mark the advertised name as complete. */
    ESP_ERROR_CHECK(ble_gap_adv_set_fields(&fields)); /* Apply the advertising payload. */

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;    /* Allow undirected connections from phone apps. */
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;    /* Make the device generally discoverable. */
    ESP_ERROR_CHECK(ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, NULL, NULL)); /* Start advertising forever. */
    ESP_LOGI(TAG, "BLE advertising started as %s", DEVICE_NAME); /* Print the advertised device name. */
}

/**
 * Function: ble_on_sync
 * Description: Runs when the NimBLE host is ready and starts advertising.
 * Parameters: None.
 * Return Value: None.
 */
static void ble_on_sync(void)
{
    ESP_ERROR_CHECK(ble_hs_id_infer_auto(0, &own_addr_type)); /* Select the BLE address type automatically. */
    start_advertising();                             /* Start advertising after the host stack is synchronized. */
}

/**
 * Function: ble_host_task
 * Description: Runs the NimBLE host loop on a FreeRTOS task.
 * Parameters: param - Unused task parameter.
 * Return Value: None.
 */
static void ble_host_task(void *param)
{
    (void)param;                                     /* Mark the unused task parameter as intentionally unused. */
    nimble_port_run();                               /* Run the NimBLE host event loop until the stack is stopped. */
    nimble_port_freertos_deinit();                   /* Clean up the host task if NimBLE ever stops. */
}

/**
 * Function: app_main
 * Description: Creates a BLE peripheral with one readable and writable characteristic.
 * Parameters: None.
 * Return Value: None.
 */
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());               /* Initialize NVS because Bluetooth stores configuration data there. */
    ESP_ERROR_CHECK(nimble_port_init());             /* Initialize the NimBLE controller and host port. */
    ble_hs_cfg.sync_cb = ble_on_sync;                /* Register the callback that runs when BLE is ready. */
    ble_svc_gap_init();                              /* Initialize the standard GAP service. */
    ble_svc_gatt_init();                             /* Initialize the standard GATT service support. */
    ESP_ERROR_CHECK(ble_svc_gap_device_name_set(DEVICE_NAME)); /* Set the BLE device name. */
    ESP_ERROR_CHECK(ble_gatts_count_cfg(gatt_services)); /* Count attribute handles for the custom service table. */
    ESP_ERROR_CHECK(ble_gatts_add_svcs(gatt_services)); /* Register the custom GATT service. */
    nimble_port_freertos_init(ble_host_task);        /* Start the NimBLE host task. */
}