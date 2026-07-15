/*
 * ESP32-S3 Watch course project.
 *
 * All source comments are written in English for code-reading practice.
 * The tutorial documents are written in Chinese for classroom delivery.
 */
#include <string.h>                                  /* Provides memset() and small buffer helpers. */
#include "driver/gpio.h"                             /* Provides LCD, backlight, and touch GPIO control. */
#include "driver/i2c_master.h"                       /* Provides ESP-IDF v5.5 I2C master driver for AXS5106L. */
#include "driver/spi_master.h"                       /* Provides SPI bus definitions used by esp_lcd. */
#include "esp_heap_caps.h"                           /* Provides DMA-capable draw-buffer allocation. */
#include "esp_lcd_panel_io.h"                        /* Provides LCD panel I/O transmit APIs. */
#include "esp_lcd_panel_ops.h"                       /* Provides LCD panel operation type definitions. */
#include "esp_lcd_panel_vendor.h"                    /* Provides vendor panel helpers when available. */
#include "esp_log.h"                                 /* Provides serial log output for display and touch status. */
#include "esp_timer.h"                               /* Provides periodic LVGL tick timer. */
#include "freertos/FreeRTOS.h"                       /* Provides FreeRTOS timing conversion macros. */
#include "freertos/task.h"                           /* Provides vTaskDelay() for the GUI task loop. */
#include "lvgl.h"                                    /* Provides LVGL 9.1 graphics and input APIs. */
#include "ui.h"                                      /* Provides the copied SquareLine Studio UI. */

/** Tag used by ESP-IDF logs from this lesson. */
static const char *TAG = "Lesson03";
/** LCD horizontal resolution in pixels. */
#define LCD_WIDTH 240
/** LCD vertical resolution in pixels. */
#define LCD_HEIGHT 296
/** LVGL partial draw-buffer line count. */
#define DRAW_BUFFER_LINES 40
/** Shared I2C SDA GPIO. */
#define WATCH_I2C_SDA GPIO_NUM_4
/** Shared I2C SCL GPIO. */
#define WATCH_I2C_SCL GPIO_NUM_3
/** AXS5106L touch interrupt GPIO. */
#define TOUCH_INTERRUPT_GPIO GPIO_NUM_2
/** AXS5106L touch reset GPIO. */
#define TOUCH_RESET_GPIO GPIO_NUM_5
/** GC9309 LCD reset GPIO. */
#define LCD_RESET_GPIO GPIO_NUM_6
/** GC9309 LCD SPI clock GPIO. */
#define LCD_SCLK_GPIO GPIO_NUM_7
/** GC9309 LCD SPI MOSI GPIO. */
#define LCD_MOSI_GPIO GPIO_NUM_8
/** GC9309 LCD chip-select GPIO. */
#define LCD_CS_GPIO GPIO_NUM_9
/** GC9309 LCD data-command GPIO. */
#define LCD_DC_GPIO GPIO_NUM_10
/** LCD backlight GPIO. */
#define LCD_BACKLIGHT_GPIO GPIO_NUM_13
/** LCD tearing-effect input GPIO. */
#define LCD_TE_GPIO GPIO_NUM_21
/** LCD power-enable GPIO; low enables the rail. */
#define LCD_POWER_GPIO GPIO_NUM_40
/** Shared peripheral power GPIO; high enables the rail. */
#define PERIPHERAL_POWER_GPIO GPIO_NUM_47
/** AXS5106L seven-bit I2C address. */
#define AXS5106L_ADDRESS 0x63

/** LCD panel I/O handle used to send commands and pixels. */
static esp_lcd_panel_io_handle_t lcd_io = NULL;
/** I2C device handle for the AXS5106L touch controller. */
static i2c_master_dev_handle_t touch_device = NULL;
/** LVGL display handle used by the flush callback. */
static lv_display_t *lvgl_display = NULL;
/** Last valid touch point returned to LVGL. */
static lv_point_t last_touch_point = {0, 0};

/** One GC9309 initialization command entry. */
typedef struct {
    uint8_t command;                                  /* LCD command byte. */
    uint8_t data[16];                                 /* Command payload bytes. */
    uint8_t data_length;                              /* Number of payload bytes. */
    uint16_t delay_ms;                                /* Delay after the command, in milliseconds. */
} lcd_init_cmd_t;

/** GC9309 command sequence copied from the validated Arduino_GFX driver behavior. */
static const lcd_init_cmd_t gc9309_init_commands[] = {
    {0xFE, {0}, 0, 0}, {0xEF, {0}, 0, 0}, {0x80, {0xC0}, 1, 0}, {0x81, {0x01}, 1, 0},
    {0x82, {0x07}, 1, 0}, {0x83, {0x38}, 1, 0}, {0x88, {0x64}, 1, 0}, {0x89, {0x86}, 1, 0},
    {0x8B, {0x3C}, 1, 0}, {0x8D, {0x51}, 1, 0}, {0x8E, {0x70}, 1, 0}, {0x35, {0x00}, 1, 0},
    {0x36, {0x48}, 1, 0}, {0x3A, {0x05}, 1, 0}, {0xBF, {0x1F}, 1, 0}, {0x7D, {0x45, 0x06}, 2, 0},
    {0xEE, {0x00, 0x06}, 2, 0}, {0xF4, {0x53}, 1, 0}, {0xF6, {0x17, 0x08}, 2, 0}, {0x70, {0x4F, 0x4F}, 2, 0},
    {0x71, {0x12, 0x20}, 2, 0}, {0x72, {0x12, 0x20}, 2, 0}, {0xB5, {0x50}, 1, 0}, {0xBA, {0x00}, 1, 0},
    {0xEC, {0x71}, 1, 0}, {0x7B, {0x00, 0x0D}, 2, 0}, {0x7C, {0x0D, 0x03}, 2, 0}, {0xF5, {0x02, 0x10, 0x12}, 3, 0},
    {0xF0, {0x0C, 0x11, 0x0B, 0x0A, 0x05, 0x32, 0x44, 0x8E, 0x9A, 0x29, 0x2E, 0x5F}, 12, 0},
    {0xF1, {0x0B, 0x11, 0x0B, 0x07, 0x07, 0x32, 0x45, 0xBD, 0x8D, 0x21, 0x28, 0xAF}, 12, 0},
    {0x2A, {0x00, 0x00, 0x00, 0xEF}, 4, 0}, {0x2B, {0x00, 0x00, 0x01, 0x27}, 4, 0},
    {0x66, {0x2C}, 1, 0}, {0x67, {0x18}, 1, 0}, {0x68, {0x3E}, 1, 0}, {0xCA, {0x0E}, 1, 0},
    {0xE8, {0xF0}, 1, 0}, {0xCB, {0x06}, 1, 0}, {0xB6, {0x5C, 0x40, 0x40}, 3, 0}, {0xCC, {0x33}, 1, 0},
    {0xCD, {0x33}, 1, 0}, {0x11, {0}, 0, 80}, {0xE8, {0xA0}, 1, 0}, {0xE8, {0xF0}, 1, 0},
    {0xFE, {0}, 0, 0}, {0xEE, {0}, 0, 0}, {0x29, {0}, 0, 0}, {0x2C, {0}, 0, 10},
};

/**
 * Function: lcd_color_done_callback
 * Description: Notifies LVGL when an asynchronous LCD color transfer has completed.
 * Parameters: panel_io - LCD I/O handle; edata - Transfer event data; user_ctx - LVGL display pointer.
 * Return Value: false because no higher-priority task is woken directly here.
 */
static bool lcd_color_done_callback(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    (void)panel_io;                                  /* Mark the unused LCD I/O handle as intentionally unused. */
    (void)edata;                                     /* Mark the unused event data as intentionally unused. */
    lv_display_flush_ready((lv_display_t *)user_ctx); /* Tell LVGL the current flush operation is complete. */
    return false;                                    /* Report that no immediate context switch is requested. */
}

/**
 * Function: send_gc9309_command
 * Description: Sends one command from the GC9309 initialization table.
 * Parameters: item - Initialization command entry.
 * Return Value: None.
 */
static void send_gc9309_command(const lcd_init_cmd_t *item)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(lcd_io, item->command, item->data_length ? item->data : NULL, item->data_length)); /* Send one command and its optional data bytes. */
    if (item->delay_ms > 0) {                         /* Check whether the command requires a delay. */
        vTaskDelay(pdMS_TO_TICKS(item->delay_ms));    /* Wait for the LCD controller to finish the command. */
    }                                                 /* End the command-delay check. */
}

/**
 * Function: lcd_flush_callback
 * Description: Sends LVGL's rendered pixel area to the GC9309 LCD.
 * Parameters: display - LVGL display; area - Dirty rectangle; pixel_map - RGB565 pixel buffer.
 * Return Value: None.
 */
static void lcd_flush_callback(lv_display_t *display, const lv_area_t *area, uint8_t *pixel_map)
{
    uint8_t column_data[4] = {(uint8_t)(area->x1 >> 8), (uint8_t)area->x1, (uint8_t)(area->x2 >> 8), (uint8_t)area->x2}; /* Build the LCD column-address payload. */
    uint8_t row_data[4] = {(uint8_t)(area->y1 >> 8), (uint8_t)area->y1, (uint8_t)(area->y2 >> 8), (uint8_t)area->y2}; /* Build the LCD row-address payload. */
    int pixel_count = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1); /* Calculate how many pixels LVGL rendered. */

    (void)display;                                    /* Mark the display pointer as unused because the callback uses the global I/O handle. */
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(lcd_io, 0x2A, column_data, sizeof(column_data))); /* Select the LCD column range. */
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(lcd_io, 0x2B, row_data, sizeof(row_data))); /* Select the LCD row range. */
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(lcd_io, 0x2C, pixel_map, pixel_count * 2)); /* Send the RGB565 pixel bytes to display memory. */
}

/**
 * Function: touch_read_register
 * Description: Reads an AXS5106L register block through ESP-IDF I2C.
 * Parameters: reg - Register address; data - Destination buffer; length - Number of bytes to read.
 * Return Value: ESP-IDF error code from the I2C transaction.
 */
static esp_err_t touch_read_register(uint8_t reg, uint8_t *data, size_t length)
{
    return i2c_master_transmit_receive(touch_device, &reg, 1, data, length, pdMS_TO_TICKS(100)); /* Read the requested AXS5106L register block. */
}

/**
 * Function: touch_read_callback
 * Description: Lets LVGL query the latest AXS5106L touch point.
 * Parameters: input - LVGL input device; data - LVGL input data output.
 * Return Value: None.
 */
static void touch_read_callback(lv_indev_t *input, lv_indev_data_t *data)
{
    uint8_t packet[14] = {0};                         /* Store the touch packet returned by the controller. */
    uint16_t x = 0;                                   /* Store decoded touch X coordinate. */
    uint16_t y = 0;                                   /* Store decoded touch Y coordinate. */

    (void)input;                                      /* Mark the input device pointer as intentionally unused. */
    if (gpio_get_level(TOUCH_INTERRUPT_GPIO) == 0 && touch_read_register(0x01, packet, sizeof(packet)) == ESP_OK && packet[1] > 0) { /* Read a point only when the active-low interrupt is asserted. */
        x = (uint16_t)(((packet[2] & 0x0FU) << 8) | packet[3]); /* Decode the raw X coordinate. */
        y = (uint16_t)(((packet[4] & 0x0FU) << 8) | packet[5]); /* Decode the raw Y coordinate. */
        if (x < LCD_WIDTH && y < LCD_HEIGHT) {        /* Accept only points inside the screen bounds. */
            last_touch_point.x = (lv_coord_t)x;       /* Save X as the latest valid LVGL point. */
            last_touch_point.y = (lv_coord_t)y;       /* Save Y as the latest valid LVGL point. */
            data->point = last_touch_point;           /* Return the pressed coordinate to LVGL. */
            data->state = LV_INDEV_STATE_PRESSED;     /* Tell LVGL the screen is currently pressed. */
            ESP_LOGI(TAG, "TOUCH: x=%u y=%u", x, y);  /* Print the touch coordinate for debugging. */
            return;                                   /* Leave early because a valid touch point was reported. */
        }                                             /* End the bounds check. */
    }                                                 /* End the active-touch read path. */
    data->point = last_touch_point;                   /* Keep the last coordinate for LVGL release processing. */
    data->state = LV_INDEV_STATE_RELEASED;            /* Tell LVGL the pointer is released. */
}

/**
 * Function: lv_tick_callback
 * Description: Advances the LVGL millisecond tick from an ESP timer.
 * Parameters: arg - Unused timer argument.
 * Return Value: None.
 */
static void lv_tick_callback(void *arg)
{
    (void)arg;                                        /* Mark the unused timer argument as intentionally unused. */
    lv_tick_inc(1);                                   /* Advance LVGL time by one millisecond. */
}

/**
 * Function: configure_power_and_gpio
 * Description: Enables the board rails in the verified order and prepares GPIOs used by display and touch.
 * Parameters: None.
 * Return Value: None.
 */
static void configure_power_and_gpio(void)
{
    gpio_config_t output_config = {0};                /* Store output GPIO configuration for power and reset pins. */
    gpio_config_t input_config = {0};                 /* Store input GPIO configuration for TE and touch interrupt pins. */

    output_config.pin_bit_mask = (1ULL << PERIPHERAL_POWER_GPIO) | (1ULL << LCD_POWER_GPIO) | (1ULL << LCD_BACKLIGHT_GPIO) | (1ULL << LCD_RESET_GPIO) | (1ULL << TOUCH_RESET_GPIO); /* Select all output pins. */
    output_config.mode = GPIO_MODE_OUTPUT;            /* Configure selected pins as outputs. */
    output_config.pull_up_en = GPIO_PULLUP_DISABLE;   /* Disable pull-up on driven outputs. */
    output_config.pull_down_en = GPIO_PULLDOWN_DISABLE; /* Disable pull-down on driven outputs. */
    output_config.intr_type = GPIO_INTR_DISABLE;      /* Disable interrupts on output pins. */
    ESP_ERROR_CHECK(gpio_config(&output_config));     /* Apply output GPIO configuration. */

    input_config.pin_bit_mask = (1ULL << LCD_TE_GPIO) | (1ULL << TOUCH_INTERRUPT_GPIO); /* Select TE and touch interrupt inputs. */
    input_config.mode = GPIO_MODE_INPUT;              /* Configure selected pins as inputs. */
    input_config.pull_up_en = GPIO_PULLUP_ENABLE;     /* Enable pull-ups for active-low style interrupt signals. */
    input_config.pull_down_en = GPIO_PULLDOWN_DISABLE; /* Disable pull-downs. */
    input_config.intr_type = GPIO_INTR_DISABLE;       /* Disable normal interrupts for this polling-style lesson. */
    ESP_ERROR_CHECK(gpio_config(&input_config));      /* Apply input GPIO configuration. */

    ESP_ERROR_CHECK(gpio_set_level(PERIPHERAL_POWER_GPIO, 1)); /* Enable the shared peripheral rail first. */
    vTaskDelay(pdMS_TO_TICKS(30));                    /* Wait for the shared rail to stabilize. */
    ESP_ERROR_CHECK(gpio_set_level(LCD_POWER_GPIO, 0)); /* Enable LCD power because GPIO40 is active low. */
    ESP_ERROR_CHECK(gpio_set_level(LCD_BACKLIGHT_GPIO, 0)); /* Keep the backlight off while the LCD initializes. */
    vTaskDelay(pdMS_TO_TICKS(30));                    /* Wait for the LCD power rail to settle. */
    ESP_ERROR_CHECK(gpio_set_level(TOUCH_RESET_GPIO, 0)); /* Reset the touch controller. */
    vTaskDelay(pdMS_TO_TICKS(200));                   /* Hold touch reset low long enough for a clean reset. */
    ESP_ERROR_CHECK(gpio_set_level(TOUCH_RESET_GPIO, 1)); /* Release touch reset. */
    vTaskDelay(pdMS_TO_TICKS(300));                   /* Wait for touch firmware to boot. */
}

/**
 * Function: configure_lcd
 * Description: Configures SPI LCD I/O and runs the GC9309 initialization sequence.
 * Parameters: None.
 * Return Value: None.
 */
static void configure_lcd(void)
{
    spi_bus_config_t bus_config = {0};                /* Store SPI bus configuration. */
    esp_lcd_panel_io_spi_config_t io_config = {0};    /* Store LCD SPI I/O configuration. */
    esp_lcd_panel_io_callbacks_t callbacks = {0};     /* Store LCD transfer callbacks. */

    bus_config.sclk_io_num = LCD_SCLK_GPIO;           /* Assign LCD SPI clock pin. */
    bus_config.mosi_io_num = LCD_MOSI_GPIO;           /* Assign LCD SPI MOSI pin. */
    bus_config.miso_io_num = GPIO_NUM_NC;             /* Leave MISO unused because the LCD write path is one-way. */
    bus_config.quadwp_io_num = GPIO_NUM_NC;           /* Leave quad WP unused. */
    bus_config.quadhd_io_num = GPIO_NUM_NC;           /* Leave quad HD unused. */
    bus_config.max_transfer_sz = LCD_WIDTH * DRAW_BUFFER_LINES * 2; /* Allow one LVGL draw-buffer transfer. */
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO)); /* Initialize the SPI bus with automatic DMA channel selection. */

    io_config.dc_gpio_num = LCD_DC_GPIO;              /* Assign the LCD D/C pin. */
    io_config.cs_gpio_num = LCD_CS_GPIO;              /* Assign the LCD chip-select pin. */
    io_config.pclk_hz = 80000000;                     /* Use the 80 MHz SPI clock from the verified Arduino project. */
    io_config.lcd_cmd_bits = 8;                       /* Send LCD commands as eight-bit values. */
    io_config.lcd_param_bits = 8;                     /* Send LCD parameters as eight-bit values. */
    io_config.spi_mode = 0;                           /* Use SPI mode 0 as required by GC9309. */
    io_config.trans_queue_depth = 10;                 /* Allow several queued color transactions. */
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &lcd_io)); /* Create the LCD SPI I/O handle. */

    callbacks.on_color_trans_done = lcd_color_done_callback; /* Register the LVGL flush-complete callback. */
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(lcd_io, &callbacks, lvgl_display)); /* Attach callbacks to the LCD I/O handle. */
    ESP_ERROR_CHECK(gpio_set_level(LCD_RESET_GPIO, 1)); /* Drive LCD reset high before the reset pulse. */
    vTaskDelay(pdMS_TO_TICKS(10));                    /* Wait briefly before pulling reset low. */
    ESP_ERROR_CHECK(gpio_set_level(LCD_RESET_GPIO, 0)); /* Pull LCD reset low. */
    vTaskDelay(pdMS_TO_TICKS(10));                    /* Hold reset low briefly. */
    ESP_ERROR_CHECK(gpio_set_level(LCD_RESET_GPIO, 1)); /* Release LCD reset. */
    vTaskDelay(pdMS_TO_TICKS(120));                   /* Wait for the LCD controller to boot. */

    for (size_t index = 0; index < sizeof(gc9309_init_commands) / sizeof(gc9309_init_commands[0]); index++) { /* Walk through every GC9309 init command. */
        send_gc9309_command(&gc9309_init_commands[index]); /* Send the current initialization command. */
    }                                                   /* End the LCD init loop. */
}

/**
 * Function: configure_touch
 * Description: Creates the I2C bus and verifies the AXS5106L controller ID.
 * Parameters: None.
 * Return Value: None.
 */
static void configure_touch(void)
{
    i2c_master_bus_config_t bus_config = {0};          /* Store I2C bus configuration. */
    i2c_device_config_t device_config = {0};           /* Store touch device configuration. */
    i2c_master_bus_handle_t bus_handle = NULL;         /* Store the created I2C bus handle. */
    uint8_t id[3] = {0};                               /* Store the AXS5106L ID bytes. */

    bus_config.i2c_port = I2C_NUM_0;                   /* Use the first I2C controller. */
    bus_config.sda_io_num = WATCH_I2C_SDA;             /* Assign board SDA pin. */
    bus_config.scl_io_num = WATCH_I2C_SCL;             /* Assign board SCL pin. */
    bus_config.clk_source = I2C_CLK_SRC_DEFAULT;       /* Use the default clock source. */
    bus_config.glitch_ignore_cnt = 7;                  /* Filter short I2C glitches. */
    bus_config.flags.enable_internal_pullup = true;    /* Enable internal pull-ups for the onboard bus. */
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle)); /* Create the I2C bus. */

    device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7; /* Use seven-bit addressing. */
    device_config.device_address = AXS5106L_ADDRESS;   /* Set the touch-controller address. */
    device_config.scl_speed_hz = 400000;               /* Use 400 kHz I2C speed. */
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &device_config, &touch_device)); /* Attach the touch controller. */
    ESP_ERROR_CHECK(touch_read_register(0x08, id, sizeof(id))); /* Read the touch-controller ID register. */
    ESP_LOGI(TAG, "AXS5106L ID bytes: %02X %02X %02X", id[0], id[1], id[2]); /* Print the ID bytes expected from the verified board. */
}

/**
 * Function: configure_lvgl
 * Description: Creates LVGL display, draw buffer, input device, tick timer, and copied UI.
 * Parameters: None.
 * Return Value: None.
 */
static void configure_lvgl(void)
{
    size_t draw_buffer_size = LCD_WIDTH * DRAW_BUFFER_LINES * sizeof(lv_color_t); /* Calculate the LVGL partial buffer size. */
    void *draw_buffer = heap_caps_malloc(draw_buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL); /* Allocate a DMA-capable internal draw buffer. */
    lv_indev_t *touch_input = NULL;                  /* Store the LVGL touch input device. */
    esp_timer_handle_t tick_timer = NULL;            /* Store the ESP timer handle for LVGL ticks. */
    esp_timer_create_args_t timer_args = {0};        /* Store the periodic timer configuration. */

    ESP_ERROR_CHECK(draw_buffer == NULL ? ESP_ERR_NO_MEM : ESP_OK); /* Stop if the draw buffer allocation failed. */
    lv_init();                                       /* Initialize LVGL before creating display or UI objects. */
    lvgl_display = lv_display_create(LCD_WIDTH, LCD_HEIGHT); /* Create the LVGL display object for the watch screen. */
    ESP_ERROR_CHECK(lvgl_display == NULL ? ESP_ERR_NO_MEM : ESP_OK); /* Stop if LVGL could not create the display. */
    lv_display_set_color_format(lvgl_display, LV_COLOR_FORMAT_RGB565); /* Match LVGL output to the LCD RGB565 format. */
    lv_display_set_flush_cb(lvgl_display, lcd_flush_callback); /* Register the LCD flush callback. */
    lv_display_set_buffers(lvgl_display, draw_buffer, NULL, draw_buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL); /* Attach the partial draw buffer. */
    touch_input = lv_indev_create();                 /* Create one LVGL input device. */
    ESP_ERROR_CHECK(touch_input == NULL ? ESP_ERR_NO_MEM : ESP_OK); /* Stop if LVGL could not create the input device. */
    lv_indev_set_type(touch_input, LV_INDEV_TYPE_POINTER); /* Mark the input device as a touchscreen pointer. */
    lv_indev_set_read_cb(touch_input, touch_read_callback); /* Register the AXS5106L read callback. */

    timer_args.callback = lv_tick_callback;          /* Use the callback that increments LVGL time. */
    timer_args.name = "lv_tick";                     /* Name the timer for debugging. */
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &tick_timer)); /* Create the periodic LVGL tick timer. */
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer, 1000)); /* Run the tick callback every one millisecond. */
    ui_init();                                       /* Build the copied SquareLine Studio UI. */
}

/**
 * Function: app_main
 * Description: Initializes LCD, touch, LVGL, and runs the UI loop.
 * Parameters: None.
 * Return Value: None.
 */
void app_main(void)
{
    configure_power_and_gpio();                      /* Enable board power rails and prepare reset pins. */
    configure_lvgl();                                /* Create the LVGL display before registering LCD transfer callbacks. */
    configure_lcd();                                 /* Initialize SPI and GC9309 display controller. */
    configure_touch();                               /* Initialize AXS5106L touch over I2C. */
    ESP_ERROR_CHECK(gpio_set_level(LCD_BACKLIGHT_GPIO, 1)); /* Turn on the backlight after UI initialization is complete. */
    ESP_LOGI(TAG, "Display, LVGL UI, and touch are ready."); /* Print that the hardware UI is running. */

    while (true) {                                   /* Run the graphical user interface forever. */
        lv_timer_handler();                          /* Let LVGL process input, animations, and redraw work. */
        vTaskDelay(pdMS_TO_TICKS(5));                /* Give other FreeRTOS tasks time between UI iterations. */
    }                                                /* End the GUI loop. */
}