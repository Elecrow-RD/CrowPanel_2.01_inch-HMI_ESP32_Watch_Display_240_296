#include <Arduino.h>    // Arduino核心库  // 必须放在最顶部，确保所有函数能识别Arduino API

#include <vector>
#include <string>

#include <BLEDevice.h>  //BLE驱动库
#include <BLEServer.h>  //BLE蓝牙服务器库
#include <BLEUtils.h>   //BLE实用程序库
#include <BLE2901.h>
#include <BLE2902.h>  //特征添加描述符库

#include <WiFi.h>     // 添加WiFi库
#include <esp_wifi.h>

// #include <esp_sleep.h>
#include <esp_pm.h>

#include <esp_adc_cal.h>  //引入 ESP32 官方 ADC 校准库，提供 esp_adc_cal_characterize （校准初始化）和 esp_adc_cal_raw_to_voltage （原始值转电压）核心函数
#include <driver/adc.h>   // 底层ADC驱动（包含adc1_get_raw等函数）

#include <Wire.h>    // I2C通信库
#include <RTClib.h>  // 替换原来的#include <PCF8563.h>

#include <Preferences.h>  // 封装了底层的 NVS API

#include <EncoderTool.h>  // 引用EncoderTool库
#include <OneButton.h>    // 引用按键库

#include <lvgl.h>
#include <demos/lv_demos.h>  // LVGL演示库
#include <Arduino_GFX_Library.h>    // LCD显示库

#include "ESP_I2S.h"  // ESP32 I2S库

#include "config.h"
#include "esp_lcd_touch_axs5106l.h"  // 电容触摸屏AXS5106L驱动库
#include "src/bsp_sc7a20htr/bsp_sc7a20htr.h"   // 陀螺仪SC7A20HTR驱动库
#include "src\audio.h"

#include "src\ui\ui.h"

bool screen_change_flag = false;

Preferences preferences;

/*********************** DISPLAY ***********************/
Arduino_DataBus *display_bus;
Arduino_GC9309 *gfx;

lv_disp_draw_buf_t draw_buf;
lv_color_t *disp_draw_buf;
lv_disp_drv_t disp_drv;
lv_obj_t *label;
SemaphoreHandle_t te_sync_semaphore;  // 二值信号量
SemaphoreHandle_t lvgl_mutex;   // 递归互斥锁
SemaphoreHandle_t touch_mutex;  // 互斥锁
volatile bool touch_print_enable_flag = false;

long position = 0;
long current_encodervalue = 0;
long pre_encodervalue = 0;
long position_tmp = 0;
bool switchPressed = false;
float angle = 0;

bool anim_loaded_end = false;
//Alarm switch sign
int fal = 0;
//Indicates whether the alarm has gone off
int fal1 = 0;
uint32_t hourValue = 255;
uint32_t minuteValue = 255;
bool autosleep = false;
unsigned long lastSwitchTime = 0;  // 上次切换的时间
unsigned long switchInterval = 500; // 设置屏幕切换间隔，500ms
bool switchState_pre = false;

unsigned long last_active_time = 0;
bool volage_low_flag = false;
static bool test_flag = false;
static char alarm_cnt = 0;
bool alarm_run = true;
int originalHour, originalMin;
#define INACTIVITY_TIMEOUT_MS (10 * 1000)

void normal_operation();

#if LV_USE_LOG != 0
/* Serial debugging */
void my_print(const char *buf)
{
  Serial.printf(buf);
  Serial.flush();
}
#endif

// 加锁回调
bool lvgl_port_lock(uint32_t timeout_ms) {
  const TickType_t timeout_ticks = (timeout_ms == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
  return xSemaphoreTakeRecursive(lvgl_mutex, timeout_ticks)  == pdTRUE;  // 等待获取锁，直到成功
}

// 解锁回调
void lvgl_port_unlock(void) {
  xSemaphoreGiveRecursive(lvgl_mutex);  // 释放锁
}

// ----------------------------------------------------
// TE 上升沿中断服务程序 (ISR)
// ----------------------------------------------------
void IRAM_ATTR te_isr_handler() {
    // 声明一个变量，用于检查高优先级任务是否被唤醒
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    // 释放信号量。这是在 ISR 中使用的 "Give" 版本。
    // 如果信号量释放成功，并且唤醒了某个正在等待的更高优先级任务，
    // xHigherPriorityTaskWoken 将被设置为 pdTRUE。

    xSemaphoreGiveFromISR(te_sync_semaphore, &xHigherPriorityTaskWoken);

    // 如果 xHigherPriorityTaskWoken 为 pdTRUE，请求 FreeRTOS 进行上下文切换。
    if (xHigherPriorityTaskWoken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
    // 注意：ISR 必须短且快，不应有 Serial.print 或耗时操作。
}

/* Display flushing */
void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
  if (!gfx) {
    lv_disp_flush_ready(disp_drv);
    return;
  }
#ifndef DIRECT_RENDER_MODE
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  xSemaphoreTake(te_sync_semaphore, 0);
  if (xSemaphoreTake(te_sync_semaphore, 100) == pdTRUE) {
    ;
  }
#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif
#endif // #ifndef DIRECT_RENDER_MODE

  lv_disp_flush_ready(disp_drv);
}

/*Read the touchpad*/
void touchpad_read_cb(lv_indev_drv_t *indev_drv, lv_indev_data_t *data)
{
  touch_data_t touch_data;
  uint8_t touchpad_cnt = 0;

  /* Read touch controller data */
  xSemaphoreTake(touch_mutex, portMAX_DELAY);
  bsp_touch_read();
  xSemaphoreGive(touch_mutex);
  /* Get coordinates */
  bool touchpad_pressed = bsp_touch_get_coordinates(&touch_data);

  if (touchpad_pressed) {
    data->point.x = touch_data.coords[0].x;
    data->point.y = touch_data.coords[0].y;
    data->state = LV_INDEV_STATE_PRESSED;

    static uint32_t last_print_time = 0;
    uint32_t current_time = millis();
    last_active_time = millis();
    if (current_time - last_print_time > 200) { // 最短每200ms打印一次触摸数据
      if (touch_print_enable_flag) {
        Serial.printf("x:%03d, y:%03d\r\n", data->point.x, data->point.y);
      }
      current_time = last_print_time;
    }
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

TaskHandle_t lvgl_task_handle = NULL;

void lvgl_task(void *pvParameters)
{
  uint32_t time_till_next;
  while (1)
  {
    if (lvgl_port_lock(0)) {
      time_till_next = lv_timer_handler(); /* let the GUI do its work */

      lvgl_port_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(time_till_next));
  }
}

TaskHandle_t display_task_handle = NULL;
volatile bool display_task_delete_flag = false;

void display_task(void *pvParameters)
{
  static const int color_array[] = {
    0xFF0000, // 红色
    0x00FF00, // 绿色
    0x0000FF, // 蓝色
    0xFFFFFF, // 白色
    0x000000, // 黑色
    0x808080, // 灰色
  };

  lv_obj_t *obj_color;
  if (lvgl_port_lock(0)) {
    obj_color = lv_obj_create(lv_scr_act());
    lv_obj_set_size(obj_color, gfx->width(), gfx->height());
    lv_obj_set_style_bg_color(obj_color, lv_color_hex(color_array[0]), LV_PART_MAIN); // 设置背景色
    lv_obj_set_style_border_width(obj_color, 0, LV_PART_MAIN);                  // 去除边框

    lvgl_port_unlock();
  }
  

  while (1)
  {
    for (int i=0; i<sizeof(color_array)/sizeof(color_array[0]); i++) {
      if (lvgl_port_lock(0)) {
        lv_obj_set_style_bg_color(obj_color, lv_color_hex(color_array[i]), LV_PART_MAIN); // 设置背景色

        lvgl_port_unlock();
      }
      if (true == display_task_delete_flag)  {  //收到退出指令
        display_task_handle = NULL;
        display_task_delete_flag = false;
        vTaskDelete(NULL);
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
  }
}

lv_obj_t *backlight_screen_obj = NULL;

void backlight_screen_generate()
{
  if (lvgl_port_lock(0)) {
    backlight_screen_obj = lv_obj_create(lv_scr_act());
    lv_obj_set_size(backlight_screen_obj, gfx->width(), gfx->height());
    lv_obj_set_style_bg_color(backlight_screen_obj, lv_color_hex(0xffffff), LV_PART_MAIN); // 设置背景色
    lv_obj_set_style_border_width(backlight_screen_obj, 0, LV_PART_MAIN);                  // 去除边框

    lvgl_port_unlock();
  }
}


void backlight_screen_delete()
{
  if (lvgl_port_lock(0)) {
    if (NULL != backlight_screen_obj) {
      lv_obj_del(backlight_screen_obj);
      backlight_screen_obj = NULL;
    }
    lvgl_port_unlock();
  }
}

#define DEF_POINT_NUM_X     (4)
#define DEF_POINT_NUM_Y     (5)
#define DEF_POINT_NUM       (DEF_POINT_NUM_X * DEF_POINT_NUM_Y)
#define DEF_POINT_OFFSET    (10)
#define DEF_POINT_DIAMETER  (40)

lv_obj_t *circle_obj[DEF_POINT_NUM] = {};


// --- 建议修改宏定义或增加实际点数变量 ---
#define ACTUAL_POINT_COUNT (3 + 4 + 3 + 4 + 3) // 17个点

// 如果你不想改宏，可以在 generate 函数里统计出实际点数赋给这个全局变量
int total_created_points = ACTUAL_POINT_COUNT; 
int clicked_point_num = 0;

void circle_touch_callback(lv_event_t *event)
{
    lv_obj_t * obj = lv_event_get_current_target(event);

    if(!lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
        clicked_point_num++;
    }

    lv_indev_t * indev = lv_indev_get_act();
    if(indev) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);
        Serial.printf("Clicked Count: %d | Pos -> x:%03d, y:%03d\r\n", clicked_point_num, point.x, point.y);
    }


    if (clicked_point_num >= ACTUAL_POINT_COUNT) {
        Serial.println("All points clicked! Resetting...");
        
        clicked_point_num = 0; 
        
        for (int i = 0; i < ACTUAL_POINT_COUNT; i++) {
            if (circle_obj[i] != NULL) {
                lv_obj_clear_flag(circle_obj[i], LV_OBJ_FLAG_HIDDEN); // 重新显示所有点
            }
        }
    }
}
lv_obj_t *touch_screen_obj = NULL;


static lv_style_t style_circle;
static bool style_inited = false;

void touch_screen_generate()
{
    if (lvgl_port_lock(0)) {
        // 1. 创建背景容器
        touch_screen_obj = lv_obj_create(lv_scr_act());
        lv_obj_set_size(touch_screen_obj, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        lv_obj_align(touch_screen_obj, LV_ALIGN_CENTER, 0, 0);
        
        // 容器样式设置
        lv_obj_set_style_bg_color(touch_screen_obj, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(touch_screen_obj, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(touch_screen_obj, 0, LV_PART_MAIN);
        lv_obj_set_style_outline_width(touch_screen_obj, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(touch_screen_obj, 0, LV_PART_MAIN);
        lv_obj_clear_flag(touch_screen_obj, LV_OBJ_FLAG_SCROLLABLE);


        if (!style_inited) {
            lv_style_init(&style_circle);
            lv_style_set_bg_color(&style_circle, lv_color_hex(0xff0000)); 
            lv_style_set_radius(&style_circle, LV_RADIUS_CIRCLE);        
            lv_style_set_border_width(&style_circle, 0);                 
            lv_style_set_outline_width(&style_circle, 0);                
            style_inited = true;
        }

        // 3. 布局配置
        const int row_config[] = {3, 4, 3, 4, 3}; // 1、3行3个点，2、4行4个点
        const int num_rows = sizeof(row_config) / sizeof(row_config[0]);
        
        int side_margin = 34; 
        int top_bottom_margin = 20; 
        int current_idx = 0;
        int y_usable = DISPLAY_HEIGHT - 2 * DEF_POINT_OFFSET - DEF_POINT_DIAMETER;
        int x_usable = DISPLAY_WIDTH - 2 * DEF_POINT_OFFSET - DEF_POINT_DIAMETER;

        // 4. 交错生成圆形点
        for (int i = 0; i < num_rows; i++) {
            int points_in_this_row = row_config[i];

            int restricted_y_usable = y_usable - (2 * top_bottom_margin);
            lv_coord_t y_pos = (restricted_y_usable * i / (num_rows - 1)) + DEF_POINT_OFFSET + top_bottom_margin;

            // 根据当前行点数决定 X 轴的计算逻辑
            for (int j = 0; j < points_in_this_row; j++) {
                if (current_idx >= DEF_POINT_NUM) break;

                lv_coord_t x_pos;
                
                if (points_in_this_row == 3) {
                    // --- 3个点行的特殊逻辑：向中间移动 ---
                    // 计算方式：在总宽度减去两倍 side_margin 的空间内平分，再加上 side_margin
                    int restricted_x_usable = x_usable - (2 * side_margin);
                    x_pos = (restricted_x_usable * j / (points_in_this_row - 1)) + DEF_POINT_OFFSET + side_margin;
                    
                } else {
                    // --- 4个点行的标准逻辑：撑满宽度 ---
                    x_pos = (x_usable * j / (points_in_this_row - 1)) + DEF_POINT_OFFSET;
                }

                // 创建与设置位置
                circle_obj[current_idx] = lv_obj_create(touch_screen_obj);
                lv_obj_set_size(circle_obj[current_idx], DEF_POINT_DIAMETER, DEF_POINT_DIAMETER);
                lv_obj_add_style(circle_obj[current_idx], &style_circle, LV_PART_MAIN);
                lv_obj_align(circle_obj[current_idx], LV_ALIGN_TOP_LEFT, x_pos, y_pos);
                
                // 交互与回调
                lv_obj_add_flag(circle_obj[current_idx], LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(circle_obj[current_idx], circle_touch_callback, LV_EVENT_CLICKED, (void*)(uintptr_t)current_idx);

                current_idx++;
            }
        }
        lvgl_port_unlock();
    }
}

void touch_screen_delete()
{
  if (lvgl_port_lock(0)) {
    if (NULL != touch_screen_obj) {
      lv_obj_del(touch_screen_obj);
      touch_screen_obj = NULL;
    }

    lvgl_port_unlock();
  }
}

static lv_obj_t *touch_dot_screen_obj = NULL;

void display_touch_dot_callback(lv_event_t *event)
{
    lv_indev_t * indev = lv_indev_get_act();

    if(indev) {
        lv_point_t point;
        lv_indev_get_point(indev, &point);

        if (point.x < 0)
            point.x = 0;
        else if (point.x > DISPLAY_WIDTH - 1)
            point.x = DISPLAY_WIDTH - 1;

        if (point.y < 0)
            point.y = 0;
        else if (point.y > DISPLAY_HEIGHT - 1)
            point.y = DISPLAY_HEIGHT - 1;

        gfx->fillRect(point.x - 1, point.y - 1, 3, 3, 0x001F);
    }
}

void touch_dot_screen_generate()
{
  // touch_print_enable_flag = true;
  if (lvgl_port_lock(0)) {
    if (NULL == touch_dot_screen_obj) {
      // 创建基础对象作为容器（替代画布）
      touch_dot_screen_obj = lv_obj_create(lv_scr_act());
      lv_obj_set_size(touch_dot_screen_obj, DISPLAY_WIDTH, DISPLAY_HEIGHT);
      lv_obj_align(touch_dot_screen_obj, LV_ALIGN_CENTER, 0, 0);
      lv_obj_set_style_bg_color(touch_dot_screen_obj, lv_color_hex(0xFFFFFF), LV_PART_MAIN); // 设置背景色
      lv_obj_set_style_bg_opa(touch_dot_screen_obj, LV_OPA_COVER, LV_PART_MAIN);     // 设置背景不透明度
      lv_obj_set_style_border_width(touch_dot_screen_obj, 0, LV_PART_MAIN);                  // 去除边框
      
      // lv_obj_add_event_cb(touch_dot_screen_obj, display_touch_dot_callback, 
      //                    LV_EVENT_PRESSED, NULL);
      lv_obj_add_event_cb(touch_dot_screen_obj, display_touch_dot_callback, 
                        LV_EVENT_PRESSING, NULL);
    }
    lvgl_port_unlock();
  }
}

void touch_dot_screen_delete()
{
  // touch_print_enable_flag = false;
  if (lvgl_port_lock(0)) {
    if (NULL != touch_dot_screen_obj) {
      lv_obj_del(touch_dot_screen_obj);
      touch_dot_screen_obj = NULL;
    }

    lvgl_port_unlock();
  }
}

void display_init()
{
  // 二值信号量在创建后默认为“空”（不可获取）状态。
  te_sync_semaphore = xSemaphoreCreateBinary();
  if (te_sync_semaphore == NULL) {
      Serial.println("FATAL: Semaphore creation failed!");
      while(1);
  }
  
  // 配置 TE 引脚并附加中断
  pinMode(PIN_LCD_TE, INPUT_PULLUP); 
  // RISING: 当电平从低到高变化时触发
  attachInterrupt(digitalPinToInterrupt(PIN_LCD_TE), te_isr_handler, FALLING);

  display_bus = new Arduino_ESP32SPIDMA(PIN_LCD_DC /* DC */, PIN_LCD_CS /* CS */, PIN_LCD_CLK /* SCK */, PIN_LCD_MOSI /* MOSI */, PIN_LCD_MISO /* MISO */, HSPI, true);
  gfx = new Arduino_GC9309(display_bus, PIN_LCD_RST /* RST */, 0 /* rotation */, false /* IPS */, DISPLAY_WIDTH /* width */, DISPLAY_HEIGHT /* height */, 0 /* col offset 1 */, 0 /* row off set 1 */, 0 /* col offset 2 */, 0 /* row offset 2 */);

  // Init Display
  if (!gfx->begin(DISPLAY_SPI_FREQ)) {
    Serial.println("gfx->begin() failed!");
  }
  gfx->setRotation(0);
  gfx->fillScreen(RGB565_BLACK);

  // display_bus->writeC8D8(0x35, 0x00); // TE ON
}

void touch_init()
{
  // Init touch device
  touch_mutex = xSemaphoreCreateMutex();
  bsp_touch_init(&Wire, PIN_TOUCH_RST, PIN_TOUCH_INT, gfx->getRotation(), gfx->width(), gfx->height());
  printf("Clock:%d\n", Wire.getClock());
}

void lvgl_init()
{
  Serial.println("Arduino_GFX LVGL_Arduino_v8 example ");
  String LVGL_Arduino = String('V') + lv_version_major() + "." + lv_version_minor() + "." + lv_version_patch();
  Serial.println(LVGL_Arduino);

  lvgl_mutex = xSemaphoreCreateRecursiveMutex();

  lv_init();

#if LV_USE_LOG != 0
  lv_log_register_print_cb(my_print); /* register print function for debugging */
#endif

  uint32_t screenWidth = gfx->width();
  uint32_t screenHeight = gfx->height();

#ifdef DIRECT_RENDER_MODE
  uint32_t bufSize = screenWidth * screenHeight;
#else
  uint32_t bufSize = screenWidth * screenHeight;
#endif

  disp_draw_buf = (lv_color_t *)malloc(bufSize * sizeof(lv_color_t));

  if (!disp_draw_buf)
  {
    Serial.println("LVGL disp_draw_buf malloc failed!");
  }
  else
  {
    lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, bufSize);

    /* Initialize the display */
    lv_disp_drv_init(&disp_drv);
    /* Change the following line to your display resolution */
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    // disp_drv.full_refresh = 1;
    disp_drv.direct_mode = true;
#ifdef DIRECT_RENDER_MODE
    disp_drv.direct_mode = true;
#endif
    lv_disp_drv_register(&disp_drv);

    /* Initialize the (dummy) input device driver */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read_cb;
    lv_indev_drv_register(&indev_drv);

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), LV_PART_MAIN); // 设置背景色
    /* Option 2: Or try out a demo. Don't forget to enable the demos in lv_conf.h. E.g. LV_USE_DEMOS_WIDGETS*/
    // lv_demo_widgets();
    // lv_demo_benchmark();
    // lv_demo_keypad_encoder();
    // lv_demo_music();
    // lv_demo_stress();

    // touch_dot_screen_generate();
    // touch_screen_generate();
    ui_init();
    //xTaskCreate(lvgl_task, "lvgl_task", 4096, NULL, 15, &lvgl_task_handle);
  }
}


// 亮度调节函数（按百分比设置，直观易用）
// 参数：brightness_percent - 亮度百分比（0~100，0=全暗，100=最亮）
void set_backlight_brightness(uint8_t brightness_percent) {
  // 边界保护：避免亮度值超出0~100
  if (100 < brightness_percent) {
    brightness_percent = 100;
  }
  // 计算对应占空比（分辨率8位：0~255）
  uint32_t duty = brightness_percent * 255 / 100;  // 255 = (1<<8)-1

  analogWrite(PIN_LCD_BL, duty);
}
/*********************** DISPLAY ***********************/

/*********************** BAT & ADC ***********************/
// ADC配置参数（使用官方校准库）
#define ADC_WIDTH_BIT     ADC_WIDTH_BIT_12 // 分辨率：12位（0~4095）
#define ADC_ATTEN_DB      (ADC_11db)  // 衰减：11dB

// 分压电阻参数
#define R1_KOHM           (100.0f)    // 上拉电阻
#define R2_KOHM           (137.0f)    // 下拉电阻
#define VOLTAGE_DIVIDER   (R2_KOHM / (R1_KOHM + R2_KOHM))  // 分压系数≈0.578

// 滤波参数
#define SAMPLE_COUNT      (50)        // 采样次数
#define SAMPLE_DELAY_MS   (2)         // 采样间隔（ms）

// 官方校准结构体（存储ADC校准数据）
esp_adc_cal_characteristics_t adc_chars;
volatile bool bat_adc_task_delete_flag = false;

float battery_get_voltage() {
    uint32_t sum_mv = 0;
    const int count = 20;
    vTaskDelay(pdMS_TO_TICKS(10));
    for (int i = 0; i < count; i++) {
        // 3.x 官方推荐函数：自动处理内部 eFuse 校准
        sum_mv += analogReadMilliVolts(PIN_BAT_ADC);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    // 计算校准后的引脚毫伏值
    float avg_pin_mv = (float)sum_mv / count;

    // 反推电池电压
    float battery_voltage = avg_pin_mv/ 1000.0f / VOLTAGE_DIVIDER;
    if(bat_adc_task_delete_flag)
    {
      Serial.printf("PIN_BAT_CHAR:%d\n", digitalRead(PIN_BAT_CHAR));
      Serial.printf("[ADC] 原始采样: %.0fmV,  电池: %.3fV\n", 
                    avg_pin_mv, battery_voltage);
    }


    return battery_voltage;
}

void bat_adc_init() {
  
  pinMode(PIN_BAT_CHAR, INPUT);

  analogReadResolution(ADC_WIDTH_BIT);
  analogSetAttenuation(ADC_ATTEN_DB);
}

TaskHandle_t bat_adc_task_handle = NULL;

      
// 电池ADC任务（不变）
void bat_adc_task(void *pvParameters) {
  const float LOW_BATTERY_THRESHOLD = 3.4;
  while (1) {
    float voltage = battery_get_voltage();
    

    if (voltage < LOW_BATTERY_THRESHOLD) {
      Serial.println("Low battery! Shutting down...");
      
      // 执行闪烁提醒（例如闪烁 5 次）
      for (int i = 0; i < 5; i++) {
        digitalWrite(PIN_LED_EN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(1000));
        digitalWrite(PIN_LED_EN, LOW);
        vTaskDelay(pdMS_TO_TICKS(1000));
      }

      // 执行关机操作（需根据硬件设计调用具体关机函数，如进入 Deep Sleep 或控制电源引脚）
      // machine_power_off(); 
      esp_sleep_enable_ext0_wakeup(GPIO_NUM_17, 1);
      esp_deep_sleep_start(); // 示例：进入深度睡眠实现关机
    }

    if(volage_low_flag)
    {
        volage_low_flag = false;
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_15, 0);
        esp_deep_sleep_start();
    }

    // if (true == bat_adc_task_delete_flag)  {  //收到退出指令
    //     bat_adc_task_handle = NULL;
    //     bat_adc_task_delete_flag = false;
    //     vTaskDelete(NULL);
    //   }
    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

/*********************** BAT & ADC ***********************/

/*********************** ble ***********************/
// BLE配置参数
#define BLE_DEVICE_NAME     "ESP32-Watch"  // BLE设备名称（英文避免乱码）
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"  // 服务UUID
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"  // 特征值UUID

// 前置声明：解决“先声明指针，后定义类”的顺序问题
class MyServerCallbacks;
class MyCharacteristicCallbacks;

// 全局变量：保存所有动态创建的BLE对象指针
BLEServer* pServer = nullptr;
BLEService* pService = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
BLE2902* p2902Descriptor = nullptr;  // 2902描述符指针
MyServerCallbacks* pServerCallbacks = nullptr;  // 服务回调指针
MyCharacteristicCallbacks* pCharCallbacks = nullptr;  // 特征值回调指针
volatile bool isBLERunning = false;  // 蓝牙运行状态标记


// BLE回调类定义（在指针声明之后）
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) { Serial.println("[BLE] Device connected"); }
  void onDisconnect(BLEServer* pServer) { 
    Serial.println("[BLE] Device disconnected"); 
    pServer->getAdvertising()->start();  // 断开后自动重启广播
  }
};

class MyCharacteristicCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pChar) {
    String arduinoStr = pChar->getValue();
    std::string value(arduinoStr.c_str());
    if (!value.empty()) {
      Serial.print("[BLE] Received data: ");
      Serial.println(value.c_str());
      for (char c : value) {
        unsigned char byte = static_cast<unsigned char>(c);
        if (byte < 0x10) Serial.print('0');
        Serial.print(byte, HEX);
        Serial.print(' ');
      }
      Serial.println();
    }
  }
};


// 启动蓝牙函数
void startBLE() {
  if (isBLERunning) {
    Serial.println("[BLE] Already running, no need to restart");
    return;
  }

  // 1. 初始化BLE设备
  BLEDevice::init(BLE_DEVICE_NAME);
  Serial.printf("[BLE] Device name: %s\n", BLE_DEVICE_NAME);

  // 2. 创建服务器+绑定回调
  pServer = BLEDevice::createServer();
  pServerCallbacks = new MyServerCallbacks();
  pServer->setCallbacks(pServerCallbacks);

  // 3. 创建服务
  pService = pServer->createService(SERVICE_UUID);
  
  // 4. 创建特征值+绑定回调
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_WRITE |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharCallbacks = new MyCharacteristicCallbacks();
  pCharacteristic->setCallbacks(pCharCallbacks);

  // 5. 创建2902描述符（支持Notify功能）
  p2902Descriptor = new BLE2902();
  pCharacteristic->addDescriptor(p2902Descriptor);

  // 6. 启动服务和广播
  pCharacteristic->setValue("BLE started");
  pService->start();
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();

  isBLERunning = true;
  Serial.println("[BLE] Started successfully, advertising...");
}


// 关闭蓝牙函数（最终修正：遍历断开所有连接，适配库参数要求）
void stopBLE() {
  if (!isBLERunning) {
    Serial.println("[BLE] Not running, no need to stop");
    return;
  }

  // 步骤1：遍历断开所有连接（核心修正：适配disconnect参数要求）
  if (pServer) {
    uint16_t connCount = pServer->getConnectedCount();  // 获取当前连接数
    if (connCount > 0) {
      Serial.printf("[BLE] Disconnecting %d device(s)...\n", connCount);
      // 连接ID从1开始计数，遍历所有连接并断开
      for (uint16_t connId = 1; connId <= connCount; connId++) {
        pServer->disconnect(connId);  // 传入连接ID，断开指定连接
      }
      vTaskDelay(pdMS_TO_TICKS(150));  // 延长延时，确保底层断开流程完成（适配3.3.3库）
      Serial.println("[BLE] All devices disconnected");
    } else {
      Serial.println("[BLE] No connected devices");
    }
  }

  // 步骤2：停止广播
  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  if (pAdvertising) {
    pAdvertising->stop();
    Serial.println("[BLE] Advertising stopped");
  }

  // 步骤3：停止服务
  if (pService) {
    pService->stop();
    Serial.println("[BLE] Service stopped");
  }

  // 步骤4：释放所有动态对象（避免堆泄漏）
  if (p2902Descriptor) {
    delete p2902Descriptor;
    p2902Descriptor = nullptr;
  }
  if (pCharCallbacks) {
    delete pCharCallbacks;
    pCharCallbacks = nullptr;
  }
  if (pServerCallbacks) {
    delete pServerCallbacks;
    pServerCallbacks = nullptr;
  }

  // 步骤5：清空指针+释放底层资源
  pCharacteristic = nullptr;
  pService = nullptr;
  pServer = nullptr;
  BLEDevice::deinit();
  isBLERunning = false;
  Serial.println("[BLE] Stopped successfully");
}
/*********************** ble ***********************/

/*********************** wifi ***********************/
// ==================== WiFi AP模式配置参数（可根据需求修改）====================
const char* AP_SSID = "ESP32-WATCH-AP";    // AP热点名称（英文，避免乱码）
const char* AP_PASSWORD = "12345678";   // AP密码（至少8位，低于8位会启动失败）
const int AP_MAX_CONN = 4;              // AP最大支持连接设备数（1-10）
const int AP_CHANNEL = 6;               // AP使用的信道（1-14，建议选1/6/11避免干扰）
const bool AP_HIDDEN = false;           // AP是否隐藏（false=可见，true=隐藏）

// 自定义AP的IP配置（可选，默认IP为 192.168.4.1）
IPAddress AP_LOCAL_IP(192, 168, 5, 1);  // AP自身的IP地址
IPAddress AP_GATEWAY(192, 168, 5, 1);   // 网关（AP模式下网关=AP自身IP）
IPAddress AP_SUBNET(255, 255, 255, 0);  // 子网掩码

// 全局状态标记：记录当前是否处于AP模式
bool isAPRunning = false;


// ==================== 进入WiFi AP模式函数 ====================
/**
 * @brief 启动WiFi AP模式，创建热点供其他设备连接
 * @return bool：true=启动成功，false=启动失败
 */
bool enterAPMode() {
  // 先判断是否已在AP模式，避免重复启动
  if (isAPRunning) {
    Serial.println("AP Mode: Already running, no need to restart");
    return true;
  }

  // 步骤1：配置AP的IP（可选，跳过则使用默认IP 192.168.4.1）
  if (!WiFi.softAPConfig(AP_LOCAL_IP, AP_GATEWAY, AP_SUBNET)) {
    Serial.println("AP Mode: Custom IP config failed, use default IP (192.168.4.1)");
  }

  // 步骤2：启动AP模式（核心函数）
  // 参数依次：SSID、密码、信道、是否隐藏、最大连接数
  bool apStarted = WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, AP_HIDDEN, AP_MAX_CONN);
  
  if (apStarted) {
    // 启动成功：更新状态标记并打印AP信息
    isAPRunning = true;
    Serial.println("AP Mode: Started successfully");
    Serial.printf("AP Mode: SSID = %s\n", AP_SSID);
    Serial.printf("AP Mode: Password = %s\n", AP_PASSWORD);
    Serial.printf("AP Mode: Local IP = %s\n", WiFi.softAPIP().toString().c_str());
    Serial.printf("AP Mode: Max Connections = %d\n", AP_MAX_CONN);
  } else {
    // 启动失败：通常是密码长度不足8位
    Serial.println("AP Mode: Start failed! Check password (min 8 characters)");
  }

  return apStarted;
}


// ==================== 退出WiFi AP模式函数 ====================
/**
 * @brief 关闭WiFi AP模式，断开所有连接并释放WiFi资源
 */
void exitAPMode() {
  // 先判断是否处于AP模式，避免重复关闭
  if (!isAPRunning) {
    Serial.println("AP Mode: Not running, no need to stop");
    return;
  }

  // 核心函数：关闭AP模式（参数true=关闭后清除AP配置，避免残留）
  WiFi.softAPdisconnect(true);
  
  // 更新状态标记并打印信息
  isAPRunning = false;
  Serial.println("AP Mode: Stopped successfully");
  Serial.println("AP Mode: All connections disconnected");
}

// ==================== 监测AP模式连接状态（可选） ====================
/**
 * @brief 实时监测AP的连接设备数量，数量变化时打印信息
 */
void monitorAPConnections() {
  static uint8_t lastConnCount = 0;
  uint8_t currentConnCount = WiFi.softAPgetStationNum();
  if (currentConnCount != lastConnCount) {
    Serial.printf("AP Mode: Current Connections = %d\n", currentConnCount);
    lastConnCount = currentConnCount;
  }
}

void wifi_scan() {
  int n = WiFi.scanNetworks();
  Serial.println("Scan done");
  if (0 == n) {
    Serial.println("no networks found");
  } else {
    Serial.print(n);
    Serial.println(" networks found");
    Serial.println("Nr | SSID                             | RSSI | CH | Encryption");
    for (int i = 0; i < n; ++i) {
      // Print SSID and RSSI for each network found
      Serial.printf("%2d", i + 1);
      Serial.print(" | ");
      Serial.printf("%-32.32s", WiFi.SSID(i).c_str());
      Serial.print(" | ");
      Serial.printf("%4ld", WiFi.RSSI(i));
      Serial.print(" | ");
      Serial.printf("%2ld", WiFi.channel(i));
      Serial.print(" | ");
      switch (WiFi.encryptionType(i)) {
        case WIFI_AUTH_OPEN: Serial.print("open"); break;
        case WIFI_AUTH_WEP: Serial.print("WEP"); break;
        case WIFI_AUTH_WPA_PSK: Serial.print("WPA"); break;
        case WIFI_AUTH_WPA2_PSK: Serial.print("WPA2"); break;
        case WIFI_AUTH_WPA_WPA2_PSK: Serial.print("WPA+WPA2"); break;
        case WIFI_AUTH_WPA2_ENTERPRISE: Serial.print("WPA2-EAP"); break;
        case WIFI_AUTH_WPA3_PSK: Serial.print("WPA3"); break;
        case WIFI_AUTH_WPA2_WPA3_PSK: Serial.print("WPA2+WPA3"); break;
        case WIFI_AUTH_WAPI_PSK: Serial.print("WAPI"); break;
        default: Serial.print("unknown");
      }
      Serial.println();
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
  WiFi.scanDelete();
}

/*********************** wifi ***********************/

/*********************** key & encoder ***********************/

/*********************** key & encoder ***********************/
// 初始化编码器对象（支持多实例）
EncoderTool::Encoder encoder;  // 第一个编码器

void encoder_init()
{
  // 配置编码器：A相引脚=PIN_EC_A，B相引脚=PIN_EC_B，精度=4X，启用转速计算
  encoder.begin(PIN_EC_A, PIN_EC_B);  // 绑定引脚
}

void encoder_print()
{
  if (encoder.valueChanged()) // do we have a new value?
  {
    Serial.print("encoder:");
    Serial.println(encoder.getValue());
  }
}


// 按键配置（用OneButton库初始化，引脚GPIO0，上拉电阻）
OneButton encoderKey(PIN_EC_BTN, true);  // 第二个参数true=启用内部上拉电阻
OneButton boot_key(PIN_BOOT_KEY, true);  // 第二个参数true=启用内部上拉电阻
long lastCount = 0;
TaskHandle_t key_task_handle = NULL;
volatile bool key_task_delete_flag = false;

void key_task(void *pvParameters)
{
    while(1) {
      encoder_print();
      encoderKey.tick();
      boot_key.tick();

      if (true == key_task_delete_flag)  //收到退出指令
      {
        key_task_handle = NULL;
        key_task_delete_flag = false;
        vTaskDelete(NULL);
      }

      vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void key_init()
{
  // ------------ 绑定按键回调函数 ------------
  // 初始化按键回调
  encoderKey.attachClick([](){ Serial.println("encoderKey:短按"); if(test_flag == false) {switchPressed = !switchPressed;}});
  encoderKey.attachLongPressStart([](){ Serial.println("encoderKey:长按开始"); });
  encoderKey.attachDoubleClick([](){ Serial.println("encoderKey:双击"); });
  boot_key.attachClick([](){ Serial.println("boot_key:短按"); });
  boot_key.attachLongPressStart([](){ Serial.println("boot_key:长按开始"); });
  boot_key.attachDoubleClick([](){ Serial.println("boot_key:双击"); });
}

/*********************** key & encoder ***********************/

/*********************** RTC PCF8563 ***********************/
uint32_t timeToTimestamp(DateTime time);
DateTime timestampToTime(uint32_t timestamp);
DateTime readRTC();
void printTime();
void setRTC(DateTime time);
void rtc_task(void *pvParameters);
void RTC_PCF8563_Init();

// 1. 正确初始化PCF8563：类名是RTC_PCF8563（Adafruit RTClib规定）
RTC_PCF8563 rtc;

// 读取RTC时间（用库的now()方法）
DateTime readRTC() {
  return rtc.now();  // 直接返回当前时间，无需手动拼接
}

// DateTime转时间戳（适配Adafruit RTClib）
uint32_t timeToTimestamp(DateTime time) {
  // 从1970-01-01 00:00:00 UTC开始计算，直接用库的TimeSpan偏移
  return time.unixtime();
}

// 时间戳转DateTime（适配Adafruit RTClib）
DateTime timestampToTime(uint32_t timestamp) {
  // 从1970-01-01 00:00:00 UTC开始计算，直接用库的TimeSpan偏移
  return DateTime(timestamp);
}

// 辅助函数：打印时间（格式化中文星期）
void printTime() {
  DateTime time = readRTC();
  const char *weekdays[] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
  // 用库的成员函数获取时间（year()/month()/day()等，避免错误）
  Serial.printf("[RTC] unixtime = %d\n", timeToTimestamp(time));
  time = time + TimeSpan(8 * 3600);  // 转为北京时间（UTC+8）
  Serial.printf("[RTC] %04d-%02d-%02d %02d:%02d:%02d %s\n",
                time.year(),
                time.month(),
                time.day(),
                time.hour(),
                time.minute(),
                time.second(),
                weekdays[time.dayOfTheWeek()]);  // 库自带星期获取
}

TaskHandle_t rtc_task_handle = NULL;
volatile bool rtc_task_delete_flag = false;

void rtc_task(void *pvParameters)
{
  while (1)
  {
    printTime();
    if (true == rtc_task_delete_flag) {
      rtc_task_handle = NULL;
      rtc_task_delete_flag = false;
      vTaskDelete(NULL);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// 无需自定义TimeStruct！用库自带的DateTime类（更简洁，无类型错误）
DateTime currentTime;

// 设置RTC时间（用库自带的adjust()方法）
void setRTC(DateTime time) {
  rtc.adjust(time);  // 一句话完成时间写入，无需逐个设置年/月/日
  Serial.print("RTC已设置时间：");
  Serial.printf("unixtime = %d\n", timeToTimestamp(time));
  printTime();
}

void RTC_PCF8563_Init()
{
  // 初始化RTC（用Adafruit RTClib的begin()方法）
  if (!rtc.begin(&Wire)) {
    Serial.println("RTC初始化失败!检查接线或芯片");
    while(1);
  }

  // 关键：关闭PCF8563的电源故障检测（避免断电后时间重置）
  // rtc.disable32K();
  // rtc.clearAlarm();

  Serial.println("RTC初始化成功");

  // 用时间戳设置RTC（示例：2025-11-01 00:00:00 UTC）
  // uint32_t timestamp = 1761926400;
  // DateTime timeFromTs = timestampToTime(timestamp);
  // setRTC(timeFromTs);

  // Serial.println("RTC时间已设置，开始读取并打印：");
}
/*********************** RTC PCF8563 ***********************/

/*********************** gyro SC7A20HTR ***********************/
BSP_SC7A20 bsp_sc7a20;
TaskHandle_t gyro_task_handle = NULL;
volatile bool gyro_task_delete_flag = false;

void gyro_task(void *pvParameters)
{
  float accel_X_ms2;
  float accel_Y_ms2;
  float accel_Z_ms2;
  static const float SENSITIVITY_2G = 1.0 / 1000.0;  // ±2g: 1 mg/digit
  static const float SENSITIVITY_4G = 2.0 / 1000.0;  // ±4g: 2 mg/digit  
  static const float SENSITIVITY_8G = 4.0 / 1000.0;  // ±8g: 4 mg/digit
  static const float GRAVITY = 9.80665;              // m/s²
  while (1)
  {
    bsp_sc7a20.measure();
    accel_X_ms2 = bsp_sc7a20.accel_X * SENSITIVITY_2G * GRAVITY;
    accel_Y_ms2 = bsp_sc7a20.accel_Y * SENSITIVITY_2G * GRAVITY;
    accel_Z_ms2 = bsp_sc7a20.accel_Z * SENSITIVITY_2G * GRAVITY;
    Serial.printf("X:%.2f m/s2 Y:%.2f m/s2 Z:%.2f m/s2\r\n", accel_X_ms2, accel_Y_ms2, accel_Z_ms2);

    if (true == gyro_task_delete_flag) {
      gyro_task_handle = NULL;
      gyro_task_delete_flag = false;
      vTaskDelete(NULL);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void gyro_sc7a20_init()
{
  delay(10);
  // 先执行 BOOT 复位，重新加载 OTP 校准数据
  Wire.beginTransmission(0x19);
  Wire.write(0x24);   // CTRL_REG5
  Wire.write(0x80);   // BOOT bit7=1
  Wire.endTransmission();
  
  delay(10);  // 等待 BOOT 完成
  
  if (!bsp_sc7a20.begin(0x19)) {
    Serial.printf("SC7A20 init error!!!");
  } else {
    Serial.println("SC7A20 init success");
  }
}
/*********************** gyro SC7A20HTR ***********************/

/*********************** microphone & loudspeaker ***********************/
/*microphone*/
static I2SClass i2s_mic; // 创建一个 I2SClass 麦克风实例
/*loudspeaker*/
static I2SClass i2s_spk; // 创建一个 I2SClass 扬声器实例

static TaskHandle_t mic_task_handle = NULL;
static volatile bool mic_task_delete_flag = false;

void play_audio(void)
{
  digitalWrite(PIN_I2S_AUDIO_EN, LOW);
  i2s_spk.write(Audio, sizeof(Audio));
  digitalWrite(PIN_I2S_AUDIO_EN, HIGH);
}

void mic_task(void *pvParameters)
{
  uint8_t *origin_buffer;
  int16_t *read_buffer;
  size_t read_bytes;
  int16_t *out_buffer;
  out_buffer = (int16_t*)heap_caps_malloc((16000 * 2 * 5), MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA | MALLOC_CAP_32BIT);
  // out_buffer = (uint16_t*)malloc((read_bytes));
  if (out_buffer==NULL) {
    Serial.println("Memory allocation failure!");
  }
  while (1) {
    
    Serial.println("Recording 5 seconds of audio data...");
    origin_buffer = i2s_mic.recordWAV(5, &read_bytes);
    read_bytes -= 44;
    read_buffer = (int16_t*)(44 + origin_buffer);
    size_t num_samples = read_bytes / 2; // 样本数
    // i2s_mic.read();
    // int available_bytes = i2s_mic.available();
    // if(available_bytes < 16000 * 5) {
    //   read_bytes = i2s_mic.readBytes((char*)origin_buffer, available_bytes);
    // } else {
    //   read_bytes = i2s_mic.readBytes((char*)origin_buffer, 16000 * 5);
    // }
    // read_bytes = i2s_mic.readBytes((char*)origin_buffer, 16000 * 5);
    // read_buffer = (int16_t*)(origin_buffer);
    
    Serial.printf("read_bytes = %d\n", read_bytes);
    Serial.println("Recording end");

    // 查找原始数据中的最大绝对值（峰值）
    int32_t max_val = 0;
    for (size_t i = 0; i < num_samples; i++) {
      // 使用 abs() 函数确保取绝对值
      int32_t current_val = abs(read_buffer[i]); 
      if (current_val > max_val) {
          max_val = current_val;
      }
    }
    
    float safe_gain = 1.0f;
    if (max_val > 0) {
      safe_gain = 32767.0f / max_val;
    }

    float desired_amplification = 20.0f;  // 最大放大倍数
    float final_gain = desired_amplification;
    if (final_gain > safe_gain) {
      final_gain = safe_gain * 1.5f;
      Serial.printf("Warning: Clipping prevented. Max safe gain used: %.2f\n", final_gain);
    } else {
      Serial.printf("Applying desired gain: %.2f\n", final_gain);
    }

    for (size_t i=0; i<num_samples; i+=1) {
      if (read_buffer[i] < 0) {
        if (-32768 / final_gain < read_buffer[i]) {
          out_buffer[i] = (read_buffer[i]) * final_gain;
        } else {
          out_buffer[i] = -32768;
        }
      } else {
        if (read_buffer[i]< 32767 / final_gain) {
          out_buffer[i] = (read_buffer[i]) * final_gain;
        } else {
          out_buffer[i] = 32767;
        }
      }
      // Serial.printf("%6d\r\n", out_buffer[i]);
    }
    Serial.println("Playing the recorded audio...");
    digitalWrite(PIN_I2S_AUDIO_EN, LOW);
    i2s_spk.write((uint8_t*)out_buffer, read_bytes);
    digitalWrite(PIN_I2S_AUDIO_EN, HIGH);

    free(origin_buffer);
    
    if (true == mic_task_delete_flag) {
      mic_task_handle = NULL;
      mic_task_delete_flag = false;
      free(out_buffer);
      vTaskDelete(NULL);
    }
  }
}

void mic_loudspeaker_init()
{
  pinMode(PIN_I2S_AUDIO_EN, OUTPUT);
  digitalWrite(PIN_I2S_AUDIO_EN, HIGH);
  
  i2s_mic.setPinsPdmRx(PIN_MIC_MCLK, PIN_MIC_SD); // 设置用于音频输入的引脚
  // 以 16 kHz 频率及 16 位深度单声道启动 I2S
  if (!i2s_mic.begin(I2S_MODE_PDM_RX, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) {
    Serial.println("初始化I2S输入失败!");
    while (1);  // 不执行任何操作
  }
  
  i2s_spk.setPins(PIN_I2S_OUT_SCLK, PIN_I2S_OUT_LRCK, PIN_I2S_OUT_SD);  // BCLK, LRCLK, DOUT
  // 以相同的参数启动I2S，但改为输出模式，使用立体声（右声道播放）
  if (!i2s_spk.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
    Serial.println("初始化I2S输出模式失败!");
    while (1);
  }
}

/*********************** microphone & loudspeaker ***********************/

/*********************** press test ***********************/
TaskHandle_t press_task_handle = NULL;
volatile bool press_task_delete_flag = false;

void press_task(void *pvParameters)
{
  float accel_X_ms2;
  float accel_Y_ms2;
  float accel_Z_ms2;
  static const float SENSITIVITY_2G = 1.0 / 1000.0;  // ±2g: 1 mg/digit
  static const float SENSITIVITY_4G = 2.0 / 1000.0;  // ±4g: 2 mg/digit  
  static const float SENSITIVITY_8G = 4.0 / 1000.0;  // ±8g: 4 mg/digit
  static const float GRAVITY = 9.80665;              // m/s²
  while (1)
  {
    bsp_sc7a20.measure();
    accel_X_ms2 = bsp_sc7a20.accel_X * SENSITIVITY_2G * GRAVITY;
    accel_Y_ms2 = bsp_sc7a20.accel_Y * SENSITIVITY_2G * GRAVITY;
    accel_Z_ms2 = bsp_sc7a20.accel_Z * SENSITIVITY_2G * GRAVITY;
    Serial.printf("X:%.2f m/s2 Y:%.2f m/s2 Z:%.2f m/s2\r\n", accel_X_ms2, accel_Y_ms2, accel_Z_ms2);

    if (true == press_task_delete_flag) {
      press_task_handle = NULL;
      press_task_delete_flag = false;
      vTaskDelete(NULL);
    }
    vTaskDelay(pdMS_TO_TICKS(2000));

    printTime();

    if (true == press_task_delete_flag) {
      press_task_handle = NULL;
      press_task_delete_flag = false;
      vTaskDelete(NULL);
    }
    vTaskDelay(pdMS_TO_TICKS(2000));

    battery_get_voltage();
    Serial.printf("PIN_BAT_CHAR:%d\n", digitalRead(PIN_BAT_CHAR));

    if (true == press_task_delete_flag) {
      press_task_handle = NULL;
      press_task_delete_flag = false;
      vTaskDelete(NULL);
    }
    vTaskDelay(pdMS_TO_TICKS(2000));

    digitalWrite(PIN_VIB_EN, HIGH);
    vTaskDelay(pdMS_TO_TICKS(500));
    digitalWrite(PIN_VIB_EN, LOW);

    monitorAPConnections();

    if (true == press_task_delete_flag) {
      press_task_handle = NULL;
      press_task_delete_flag = false;
      vTaskDelete(NULL);
    }
  }
}
/*********************** press test ***********************/

void Preferences_read()
{
  Preferences prefs; // Declaring Preferences objects
  prefs.begin("alarm_clock"); // Open the namespace mynamespace
  hourValue = prefs.getUInt("hourValue", 0); // Gets the value of the key named "hourValue" in the current namespace
  // If this element is not present, the default value 0 is returned
  minuteValue = prefs.getUInt("minuteValue", 0); // Gets the value of the key name "minuteValue" in the current namespace
  // If this element is not present, the default value 0 is returned
  lv_dropdown_set_selected(ui_HourAlarm, hourValue + 1);
  lv_dropdown_set_selected(ui_MinuteAlarm, minuteValue + 1);
  prefs.end(); // Close the current namespace
}

void Preferences_init()
{
  //Initializes the read alarm
  Preferences prefs; // Declaring Preferences objects
  prefs.begin("alarm_clock"); // Open the namespace mynamespace
  hourValue = prefs.getUInt("hourValue", 0); // Gets the value of the key named "hourValue" in the current namespace
  // If this element is not present, the default value 0 is returned
  minuteValue = prefs.getUInt("minuteValue", 0); // Gets the value of the key name "minuteValue" in the current namespace
  // If this element is not present, the default value 0 is returned
  hourValue = atoi(AlarmClockhour); // Record the alarm time
  minuteValue = atoi(AlarmClockmimu);
  Serial.printf("Set an alarm： %u:%u \n", hourValue, minuteValue);
  prefs.putUInt("hourValue", hourValue); // Saves the data to the "hourValue" key of the current namespace
  prefs.putUInt("minuteValue", minuteValue); // Save the data to the "minuteValue" key of the current namespace键中
  prefs.end(); // Close the current namespace
}

void Alarm_task()
{
  int i = 0;
  int toneCount = 0;
  unsigned long previousMillis = 0;
  const long interval = 500;  // The time interval for each drop is 0.5 seconds
  unsigned long delayStart = 0;
  bool delayActive = false;
  unsigned long alarm_enable_tiem = millis();
  if(alarm_cnt == 0)
  {
    originalHour = hourValue;
    originalMin = minuteValue;
  }

  while (1)
  {
    unsigned long currentMillis = millis();  
    if (currentMillis - previousMillis >= interval && !delayActive)
    {
      previousMillis = currentMillis;
      i = !i;
      
      if (i)
      {
        // 启动线性马达
        digitalWrite(PIN_VIB_EN, HIGH); 
      }
      else
      {
        // 停止线性马达
        digitalWrite(PIN_VIB_EN, LOW); 
        toneCount++; 
      }
    }

    if (toneCount == 3 && !delayActive)
    {
      delayStart = currentMillis;  
      delayActive = true; 
    }

    if (delayActive && currentMillis - delayStart >= 1000)
    {
      delayActive = false;  
      toneCount = 0;  
    }
    
    lv_timer_handler();

    if(currentMillis - alarm_enable_tiem > 60000) //60000
    {
      digitalWrite(PIN_VIB_EN, LOW); 
      
      if(alarm_cnt < 2)
      {
        hourValue = hour;
        minuteValue = minute + 11;
        if(minuteValue >= 60)
        {
          minuteValue = minuteValue - 60;
          hourValue = hourValue + 1;
          if(hourValue >= 24)
          {
            hourValue = hourValue - 24;
          }
        }
        alarm_cnt++;

      }
      else{

        digitalWrite(PIN_VIB_EN, LOW); 
        lv_obj_clear_state(ui_AlarmClockSwitch, LV_STATE_CHECKED);
        hourValue = originalHour; 
        minuteValue = originalMin;
        alarm_cnt = 0;  
        switchState = false;
      }
      
      fal1 = 0;
      if (lvgl_port_lock(0)) {
        lv_label_set_text(ui_Label1,"Please swipe \nthe screen");
        _ui_screen_change(&ui_HomeScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 400, 0, &ui_HomeScreen_screen_init);
        lvgl_port_unlock();
      }
      break;
    }
    else if (currentMillis - alarm_enable_tiem > 55000) // 55000
    { 
      if(alarm_cnt < 2) {
        if (lvgl_port_lock(0)) {
          lv_label_set_text(ui_Label1,"The alarm will ring\nagain in ten minutes.");
          lvgl_port_unlock();
        }
      }
    }

    if (!switchState)
    {
      digitalWrite(PIN_VIB_EN, LOW);  // 关闭线性马达
      break;
    }
  }
}



void serial_send_cmd(char* cmd)
{
  Serial.printf("%s"CMD_SEPARATOR, cmd);
}

void uart_test()
{
  static std::string str(1024, '\0');
  char c;
  int pos = -1, len = 0;
  
  while (true) {
    lv_timer_handler();
    if(test_flag == false)
    {
        normal_operation();
    }

    if (Serial.available()) {
      c = Serial.read();
      // Serial.printf("%c", c);
    } else {
      //vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    
    str.push_back(c);
    
    pos = str.find(CMD_SEPARATOR);
    if (pos < 0) {
      continue;
    } 

    if (false == test_flag) { 
      pos = str.find(CMD_ENTRY_TEST);
      if (0 <= pos) {

        test_flag = true;
        serial_send_cmd(CMD_ENTRY_TEST);
        
        str.clear();
        
        if (lvgl_port_lock(0)) {
          _ui_screen_change(&ui_testscreen, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_testscreen_screen_init);

          lvgl_port_unlock();
        }
        
        autosleep = false;
        // str.erase(pos, strlen(CMD_ENTRY_TEST));
        // vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      } else {
        continue;
      }
    } else {
      pos = str.find(CMD_EXIT_TEST);
      if (0 <= pos) {

        test_flag = false;
        serial_send_cmd(CMD_EXIT_TEST);
        
        str.clear();
        if (lvgl_port_lock(0)) {
          
          _ui_screen_change(&ui_HomeScreen, LV_SCR_LOAD_ANIM_NONE, 0, 0, &ui_HomeScreen_screen_init);
          ui_testscreen_screen_destroy();
          lvgl_port_unlock();
        }
        last_active_time = millis();
        autosleep = true;
        set_backlight_brightness(100);
        // str.erase(pos, strlen(CMD_EXIT_TEST));
        // vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
    }

    /*********************** PRESS ***********************/
    static bool press_test_flag = false;
    pos = str.find(CMD_PRESS_START);
    if (0 <= pos) {
      
      press_test_flag = true;
      enterAPMode();
      press_task_delete_flag = false;
      xTaskCreate(press_task, "press_task", 4096, NULL, 5, &press_task_handle);
      key_task_delete_flag = false;
      xTaskCreate(key_task, "key_task", 4096, NULL, 5, &key_task_handle);
      mic_task_delete_flag = false;
      xTaskCreate(mic_task, "mic_task", 4096, NULL, 5, &mic_task_handle);
      backlight_screen_generate();
      set_backlight_brightness(100);
      touch_print_enable_flag = true;

      str.erase(pos, strlen(CMD_PRESS_START));
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (true == press_test_flag) {
      pos = str.find(CMD_PRESS_STOP);
      if (0 <= pos) {

        press_test_flag = false;
        press_task_delete_flag = true;
        key_task_delete_flag = true;
        mic_task_delete_flag = true;
        exitAPMode();
        while (true == press_task_delete_flag) 
        {};
        while (true == key_task_delete_flag) 
        {};
        while (true == mic_task_delete_flag) 
        {};
        backlight_screen_delete();
        set_backlight_brightness(0);
        touch_print_enable_flag = false;
        serial_send_cmd(CMD_EXIT_PRESS_TEST);

        str.erase(pos, strlen(CMD_PRESS_STOP));
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
    /*********************** PRESS ***********************/

    /*********************** BACKLIGHT ***********************/
    static bool bl_test_flag = false;
    pos = str.find(CMD_BACKLIGHT_ON);
    if (0 <= pos) {
      Serial.printf("backlight GPIO = %d\n", PIN_LCD_BL);
      
      bl_test_flag = true;
      touch_dot_screen_delete();
      backlight_screen_generate();
      vTaskDelay(pdMS_TO_TICKS(200));
      set_backlight_brightness(100);
        
      str.erase(pos, strlen(CMD_BACKLIGHT_ON));
      // vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    if (1 == bl_test_flag) {
      pos = str.find(CMD_BACKLIGHT_1);
      if (0 <= pos) {
        
        set_backlight_brightness(0);

        str.erase(pos, strlen(CMD_BACKLIGHT_1));
        continue;
      }

      pos = str.find(CMD_BACKLIGHT_2);
      if (0 <= pos) {
        
        set_backlight_brightness(25);

        str.erase(pos, strlen(CMD_BACKLIGHT_2));
        continue;
      }

      pos = str.find(CMD_BACKLIGHT_3);
      if (0 <= pos) {
        
        set_backlight_brightness(50);

        str.erase(pos, strlen(CMD_BACKLIGHT_3));
        continue;
      }

      pos = str.find(CMD_BACKLIGHT_4);
      if (0 <= pos) {
        
        set_backlight_brightness(75);

        str.erase(pos, strlen(CMD_BACKLIGHT_4));
        continue;
      }

      pos = str.find(CMD_BACKLIGHT_5);
      if (0 <= pos) {
        
        set_backlight_brightness(100);

        str.erase(pos, strlen(CMD_BACKLIGHT_5));
        continue;
      }
    }

    if (true == bl_test_flag) {
      pos = str.find(CMD_BACKLIGHT_OFF);
      if (0 <= pos) {
        serial_send_cmd(CMD_EXIT_BL_TEST);

        bl_test_flag = false;
        set_backlight_brightness(0);
        backlight_screen_delete();
        
        str.erase(pos, strlen(CMD_BACKLIGHT_OFF));
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }
    }
    /*********************** BACKLIGHT ***********************/

    /*********************** DISPLAY ***********************/
    static bool display_test_flag = false;
    pos = str.find(CMD_DISPLAY_START);
    if (0 <= pos) {
      Serial.printf("LCD_EN GPIO = %d\n", PIN_LCD_EN);
      Serial.printf("LCD_POWER_DET GPIO = %d\n", PIN_LCD_POWER_DET);
      Serial.printf("LCD_TE GPIO = %d\n", PIN_LCD_TE);
      Serial.printf("LCD_BL GPIO = %d\n", PIN_LCD_BL);
      Serial.printf("LCD_RST GPIO = %d\n", PIN_LCD_RST);
      Serial.printf("LCD_DC GPIO = %d\n", PIN_LCD_DC);
      Serial.printf("LCD_CS GPIO = %d\n", PIN_LCD_CS);
      Serial.printf("LCD_CLK GPIO = %d\n", PIN_LCD_CLK);
      Serial.printf("LCD_MOSI GPIO = %d\n", PIN_LCD_MOSI);

      display_test_flag = true;
      display_task_delete_flag = false;
      xTaskCreate(display_task, "display_task", 4096, NULL, 5, &display_task_handle);
      vTaskDelay(pdMS_TO_TICKS(200));
      set_backlight_brightness(100);

      str.erase(pos, strlen(CMD_DISPLAY_START));
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (true == display_test_flag) {
      pos = str.find(CMD_DISPLAY_STOP);
      if (0 <= pos) {

        display_test_flag = false;
        display_task_delete_flag = true;
        set_backlight_brightness(0);
        touch_dot_screen_delete();
        while (true == display_task_delete_flag)
        {};
        serial_send_cmd(CMD_EXIT_DISPLAY_TEST);

        str.erase(pos, strlen(CMD_DISPLAY_STOP));
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
    /*********************** DISPLAY ***********************/

    /*********************** TOUCH ***********************/
    static bool touch_test_flag = false;
    pos = str.find(CMD_TOUCH_START);
    if (0 <= pos) {
      Serial.printf("TOUCH_SCL GPIO = %d\n", PIN_SCL);
      Serial.printf("TOUCH_SDA GPIO = %d\n", PIN_SDA);
      Serial.printf("TOUCH_INT GPIO = %d\n", PIN_TOUCH_INT);
      Serial.printf("TOUCH_RST GPIO = %d\n", PIN_TOUCH_RST);

      touch_test_flag = true;
      touch_print_enable_flag = false;
      touch_dot_screen_delete();
      touch_screen_generate();
      vTaskDelay(pdMS_TO_TICKS(200));
      set_backlight_brightness(100);

      str.erase(pos, strlen(CMD_TOUCH_START));
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (true == touch_test_flag) {
      pos = str.find(CMD_TOUCH_STOP);
      if (0 <= pos) {

        touch_test_flag = false;
        touch_print_enable_flag = false;
        set_backlight_brightness(0);
        touch_screen_delete();
        serial_send_cmd(CMD_EXIT_TOUCH_TEST);

        str.erase(pos, strlen(CMD_TOUCH_STOP));
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
    /*********************** TOUCH ***********************/

    /*********************** TOUCH DOT ***********************/
    static bool touch_dot_test_flag = false;
    pos = str.find(CMD_TOUCH_DOT_START);
    if (0 <= pos) {
      Serial.printf("TOUCH_SCL GPIO = %d\n", PIN_SCL);
      Serial.printf("TOUCH_SDA GPIO = %d\n", PIN_SDA);
      Serial.printf("TOUCH_INT GPIO = %d\n", PIN_TOUCH_INT);
      Serial.printf("TOUCH_RST GPIO = %d\n", PIN_TOUCH_RST);

      touch_dot_test_flag = true;
      touch_dot_screen_generate();
      vTaskDelay(pdMS_TO_TICKS(200));
      set_backlight_brightness(100);

      str.erase(pos, strlen(CMD_TOUCH_DOT_START));
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (true == touch_dot_test_flag) {
      pos = str.find(CMD_TOUCH_DOT_STOP);
      if (0 <= pos) {

        touch_dot_test_flag = false;
        set_backlight_brightness(0);
        touch_dot_screen_delete();
        serial_send_cmd(CMD_EXIT_TOUCH_DOT_TEST);

        str.erase(pos, strlen(CMD_TOUCH_DOT_STOP));
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
    /*********************** TOUCH ***********************/

    /*********************** BAT & ADC ***********************/
    static bool bat_test_flag = false;
    pos = str.find(CMD_BAT_ADC_START);
    if (0 <= pos) {
      Serial.printf("battery_CHAR GPIO = %d\n", PIN_BAT_CHAR);
      Serial.printf("battery_ADC GPIO = %d\n", PIN_BAT_ADC);
        
      bat_test_flag = true;
      bat_adc_task_delete_flag = true;
      //bat_adc_task_delete_flag = false;
      //xTaskCreate(bat_adc_task, "bat_adc_task", 4096, NULL, 5, &bat_adc_task_handle);

      str.erase(pos, strlen(CMD_BAT_ADC_START));
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (true == bat_test_flag) {
      pos = str.find(CMD_BAT_ADC_STOP);
      if (0 <= pos) {
          bat_test_flag = false;
          // bat_adc_task_delete_flag = true;
          bat_adc_task_delete_flag = false;
          while (true == bat_adc_task_delete_flag)
          {};
          serial_send_cmd(CMD_EXIT_BAT_TEST);

          str.erase(pos, strlen(CMD_BAT_ADC_STOP));
          vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
    /*********************** BAT & ADC ***********************/

    /*********************** 震动马达 ***********************/
    static bool motor_test_flag = false;
    pos = str.find(CMD_MOTOR_START);
    if (0 <= pos) {
      Serial.printf("motor GPIO = %d\n", PIN_VIB_EN);
      
      motor_test_flag = true;
      digitalWrite(PIN_VIB_EN, HIGH);

      str.erase(pos, strlen(CMD_MOTOR_START));
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (true == motor_test_flag) {
      pos = str.find(CMD_MOTOR_STOP);
      if (0 <= pos) {

        motor_test_flag = false;
        digitalWrite(PIN_VIB_EN, LOW);

        serial_send_cmd(CMD_EXIT_MOTOR_TEST);
        str.erase(pos, strlen(CMD_MOTOR_STOP));
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
    /*********************** 震动马达 ***********************/

    /*********************** ble ***********************/
    static bool ble_test_flag = false;
    pos = str.find(CMD_BLE_START);
    if (0 <= pos) {
      
      ble_test_flag = true;
      startBLE();  // 启动蓝牙

      str.erase(pos, strlen(CMD_BLE_START));
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (true == ble_test_flag) {
      pos = str.find(CMD_BLE_STOP);
      if (0 <= pos) {

        ble_test_flag = false;
        stopBLE();  // 关闭蓝牙

        serial_send_cmd(CMD_EXIT_BLE_TEST);
        str.erase(pos, strlen(CMD_BLE_STOP));
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
    /*********************** ble ***********************/

    /*********************** wifi ***********************/
    static bool wifi_test_flag = false;
    pos = str.find(CMD_WIFI_START);
    if (0 <= pos) {
      wifi_test_flag = true;

      int cur_pos = 0;
      static char temp_data[64];
      static char wifi_ssid[32];
      static char wifi_password[64];

      while (1) {
        if (Serial.available()) {
          temp_data[cur_pos] = Serial.read();
          if ('\n' == temp_data[cur_pos]) {
              char *pos = strstr(temp_data, CMD_WIFI_SSID);
              temp_data[cur_pos] = '\0';
              strncpy(wifi_ssid, pos + strlen(CMD_WIFI_SSID), sizeof(wifi_ssid));
              break;
          }
          cur_pos += 1;
        } else {
          vTaskDelay(pdMS_TO_TICKS(10));
        }
      }
      cur_pos = 0;
      while (1) {
        if (Serial.available()) {
          temp_data[cur_pos] = Serial.read();
          if ('\n' == temp_data[cur_pos]) {
              char *pos = strstr(temp_data, CMD_WIFI_PASSWORD);
              temp_data[cur_pos] = '\0';
              strncpy(wifi_password, pos + strlen(CMD_WIFI_PASSWORD), sizeof(wifi_password));
              break;
          }
          cur_pos += 1;
        } else {
          vTaskDelay(pdMS_TO_TICKS(10));
        }
      }

      Serial.printf("wifi_ssid:%s\n", wifi_ssid);
      Serial.printf("wifi_password:%s\n", wifi_password);

      WiFi.begin(wifi_ssid, wifi_password);
      // 等待连接成功（超时30秒）
      uint8_t retry = 0;
      while (WiFi.status() != WL_CONNECTED && retry < 30) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        Serial.print(".");
        retry++;
      }

      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[WiFi] connected successfully");
        Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("[WiFi] : %d dBm\n", WiFi.RSSI());
      } else {
        Serial.println("\n[WiFi] connected fail");
      }

      serial_send_cmd(CMD_START_WIFI_TEST);

      str.erase(pos, strlen(CMD_WIFI_START));
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (true == wifi_test_flag) {
      pos = str.find(CMD_WIFI_STOP);
      if (0 <= pos) {
        
        wifi_test_flag = false;
        WiFi.disconnect();  // 打断连接
        while (WL_DISCONNECTED != WiFi.status()) {
          vTaskDelay(pdMS_TO_TICKS(100));
        }
        serial_send_cmd(CMD_EXIT_WIFI_TEST);

        str.erase(pos, strlen(CMD_WIFI_STOP));
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
    /*********************** wifi ***********************/

    /*********************** key & encoder ***********************/
    static bool key_test_flag = false;
    pos = str.find(CMD_KEY_ENCODER_START);
    if (0 <= pos) {
      Serial.printf("Encoder key GPIO = %d\n", PIN_EC_BTN);
      Serial.printf("Encoder A GPIO = %d\n", PIN_EC_A);
      Serial.printf("Encoder B GPIO = %d\n", PIN_EC_B);

      key_test_flag = true;
      key_task_delete_flag = false;
      xTaskCreate(key_task, "key_task", 4096, NULL, 5, &key_task_handle);

      str.erase(pos, strlen(CMD_KEY_ENCODER_START));
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (true == key_test_flag) {
      pos = str.find(CMD_KEY_ENCODER_STOP);
      if (0 <= pos) {
        key_test_flag = false;

        key_task_delete_flag = true;
        while (true == key_task_delete_flag)
        {};
        serial_send_cmd(CMD_EXIT_KEY_ENCODER);

        str.erase(pos, strlen(CMD_KEY_ENCODER_STOP));
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
    /*********************** key & encoder ***********************/

    /*********************** gyro SC7A20HTR ***********************/
    static bool gyro_test_flag = false;
    pos = str.find(CMD_GYRO_START);
    if (0 <= pos) {
      Serial.printf("I2C SCL GPIO = %d\n", PIN_SCL);
      Serial.printf("I2C SDA GPIO = %d\n", PIN_SDA);

      gyro_test_flag = true;
      gyro_task_delete_flag = false;
      xTaskCreate(gyro_task, "gyro_task", 4096, NULL, 5, &gyro_task_handle);

      str.erase(pos, strlen(CMD_GYRO_START));
      vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (true == gyro_test_flag) {
      pos = str.find(CMD_GYRO_STOP);
      if (0 <= pos) {
        
        gyro_test_flag = false;
        gyro_task_delete_flag = true;
        while (true == gyro_task_delete_flag) 
        {};
        serial_send_cmd(CMD_EXIT_GYRO_TEST);

        str.erase(pos, strlen(CMD_GYRO_STOP));
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
    /*********************** gyro SC7A20HTR ***********************/

    /*********************** RTC PCF8563 ***********************/
    static bool rtc_test_flag = false;
    pos = str.find(CMD_RTC_START);
    if (0 <= pos) {
      str.clear();
      int time = 0;
      while (1) {
        if (Serial.available()) {
          str += Serial.read();
          if (std::string::npos != str.find(CMD_SEPARATOR)) {
            pos = str.find(CMD_RTC_TIMESTAMP);
            if (std::string::npos != pos) {
              size_t start_pos = pos + strlen(CMD_RTC_TIMESTAMP);
              size_t end_pos = str.find(CMD_SEPARATOR, start_pos);

              // 截取时间戳字符串（从 start_pos 到 end_pos，不包含 '\n'）
              std::string timestamp_str = str.substr(start_pos, end_pos - start_pos);
              // 转换为数字类型（根据时间戳长度选 uint64_t 或 time_t）
              time_t timestamp = std::strtoull(timestamp_str.c_str(), nullptr, 10);  // 无符号64位
              // Serial.println(timestamp);
              DateTime timeFromTs = timestampToTime(timestamp);
              setRTC(timeFromTs);
            }
            str.clear();
            break;
          }
        } else {
          vTaskDelay(pdMS_TO_TICKS(20));
          time += 20;
          if (time >= 2000) {  // 超时2秒自动退出
            break;
          }
        }
      }

      Serial.printf("I2C SCL GPIO = %d\n", PIN_SCL);
      Serial.printf("I2C SDA GPIO = %d\n", PIN_SDA);

      rtc_test_flag = true;
      rtc_task_delete_flag = false;
      xTaskCreate(rtc_task, "rtc_task", 4096, NULL, 5, &rtc_task_handle);

      str.erase(pos, strlen(CMD_RTC_START));
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (true == rtc_test_flag) {
      pos = str.find(CMD_RTC_STOP);
      if (0 <= pos) {
        
        rtc_test_flag = false;
        rtc_task_delete_flag = true;
        while (true == rtc_task_delete_flag)
        {};
        serial_send_cmd(CMD_EXIT_RTC_TEST);

        str.erase(pos, strlen(CMD_RTC_STOP));
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }

    /*********************** RTC PCF8563 ***********************/

    /*********************** microphone ***********************/
    static bool mic_test_flag = false;
    pos = str.find(CMD_MIC_START);
    if (0 <= pos) {
      Serial.printf("MIC PIN_MIC_MCLK GPIO = %d\n", PIN_MIC_MCLK);
      Serial.printf("MIC PIN_MIC_SD GPIO = %d\n", PIN_MIC_SD);

      mic_test_flag = true;
      mic_task_delete_flag = false;
      xTaskCreate(mic_task, "mic_task", 4096, NULL, 10, &mic_task_handle);

      str.erase(pos, strlen(CMD_MIC_START));
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (true == mic_test_flag) {
      pos = str.find(CMD_MIC_STOP);
      if (0 <= pos) {
        
        mic_test_flag = false;
        mic_task_delete_flag = true;
        while (true == mic_task_delete_flag)
        {};
        serial_send_cmd(CMD_EXIT_MIC_TEST);

        str.erase(pos, strlen(CMD_MIC_STOP));
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
    /*********************** microphone ***********************/

    /*********************** loudspeaker ***********************/
    static bool spk_test_flag = false;
    pos = str.find(CMD_SPK_START);
    if (0 <= pos) {
      Serial.printf("SPK PIN_I2S_AUDIO_EN GPIO = %d\n", PIN_I2S_AUDIO_EN);
      Serial.printf("SPK PIN_I2S_OUT_SD GPIO = %d\n", PIN_I2S_OUT_SD);
      Serial.printf("SPK PIN_I2S_OUT_SCLK GPIO = %d\n", PIN_I2S_OUT_SCLK);
      Serial.printf("SPK PIN_I2S_OUT_LRCK GPIO = %d\n", PIN_I2S_OUT_LRCK);

      spk_test_flag = true;
      play_audio();

      str.erase(pos, strlen(CMD_SPK_START));
      vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (true == spk_test_flag) {
      pos = str.find(CMD_SPK_STOP);
      if (0 <= pos) {
        
        spk_test_flag = false;
        serial_send_cmd(CMD_EXIT_SPK_TEST);

        str.erase(pos, strlen(CMD_SPK_STOP));
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    }
    /*********************** loudspeaker ***********************/
    str.clear();

  }

}

/*********************** power ***********************/
void sleep_begin()
{
  digitalWrite(PIN_LCD_EN, HIGH); // 关闭屏幕供电使能
  digitalWrite(PIN_LED_EN, LOW); // 关闭LED灯
  gpio_set_direction((gpio_num_t)PIN_LCD_RST , GPIO_MODE_INPUT);
  gpio_set_direction((gpio_num_t)PIN_LCD_DC  , GPIO_MODE_INPUT);
  gpio_set_direction((gpio_num_t)PIN_LCD_CS  , GPIO_MODE_INPUT);
  // gpio_set_direction((gpio_num_t)PIN_LCD_CLK , GPIO_MODE_INPUT);
  // gpio_set_direction((gpio_num_t)PIN_LCD_MOSI, GPIO_MODE_INPUT);
  // gpio_set_direction((gpio_num_t)PIN_LCD_MISO, GPIO_MODE_INPUT);

  // gpio_set_direction((gpio_num_t)PIN_SCL, GPIO_MODE_INPUT);
  // gpio_set_direction((gpio_num_t)PIN_SDA, GPIO_MODE_INPUT);
  
  set_backlight_brightness(0);
  Serial.printf("backlight = 0\r\n");
  Serial.println("进入浅睡...");
  Serial.flush(); // 确保串口数据发送完毕
  digitalWrite(PIN_POWER_CTL, LOW);
  xSemaphoreTake(touch_mutex, portMAX_DELAY);
}

// void sleep_exit()
// {
//   esp_restart(); // 软件触发复位
//   xSemaphoreGive(touch_mutex);
//   digitalWrite(PIN_LCD_EN, LOW); // 打开屏幕供电使能
//   digitalWrite(PIN_LED_EN, HIGH); // 打开LED灯
//   ((Arduino_GC9309*)gfx)->tftInit();
//   gfx->setRotation(0); // apply the setting rotation to the display
//   gfx->setAddrWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
//   Serial.println("退出浅睡...");
// }
void sleep_exit()
{
    Serial.println("开始唤醒...");
    if(volage_low_flag)
    {
      volage_low_flag = false;
      esp_restart();
    }
    // 1. 恢复所有电源控制
    digitalWrite(PIN_POWER_CTL, HIGH); // 必须先打开总电源
    digitalWrite(PIN_LCD_EN, LOW);    // 打开 LCD 使能 (低电平有效)
    set_backlight_brightness(0);
    delay(30);                       // 给电容充电和电压稳定留够时间

    // 2. 必须手动恢复引脚模式
    // 因为 Light Sleep 唤醒后，GPIO 状态虽然维持，但你之前手动改成了 INPUT
    pinMode(PIN_LCD_RST, OUTPUT);
    pinMode(PIN_LCD_DC, OUTPUT);
    pinMode(PIN_LCD_CS, OUTPUT);
    digitalWrite(PIN_LCD_CS, HIGH);   // CS 先拉高，准备开始通信
    gpio_set_direction((gpio_num_t)PIN_VIB_EN  , GPIO_MODE_OUTPUT);

    // // 3. 硬件强制复位 (断电后的 GC9309 必须经历此过程)
    // digitalWrite(PIN_LCD_RST, HIGH);
    // delay(5);
    // digitalWrite(PIN_LCD_RST, LOW);
    // delay(100);                        // 拉低至少 10ms-50ms
    // digitalWrite(PIN_LCD_RST, HIGH);
    // delay(150);                       // 复位后必须等待 120ms 以上才能发指令

    // 4. 初始化屏幕芯片寄存器
    ((Arduino_GC9309*)gfx)->tftInit();
    Serial.printf("tftInit()\n");
    // 显式发送唤醒和显示开启指令
    gfx->displayOn(); // 注意：有些库在 displayOn 后会自动刷屏

    // 强制清屏（如果库支持直接填充颜色）
    gfx->fillScreen(0x0000); 

    // 5. 恢复 LVGL 并强制重绘
    lv_obj_invalidate(lv_scr_act()); 
    
    //手动触发一次 LVGL 句柄
    // lv_timer_handler(); 
    Serial.printf("rtc.now()\n");
    DateTime now = rtc.now();
    hour = now.hour();
    minute = now.minute();
    second = now.second();
    s = (uint32_t)second * 60;
    /*********************** RTC PCF8563 ***********************/
    lv_img_set_angle(ui_SecondHand,  second * 60);
    lv_img_set_angle(ui_SecondHand1,  second * 60);
    lv_img_set_angle(ui_MinuteHands2, minute * 60);
    lv_img_set_angle(ui_Hourshands2, (hour % 12) * 300 + minute / 12 * 60);
    lv_img_set_angle(ui_MinuteHands, minute * 60);
    lv_img_set_angle(ui_Hourshands, (hour % 12) * 300 + minute / 12 * 60);
    //lv_timer_create((void*)send_event, 50, NULL);
    // lv_timer_create(time_refresh, 100, NULL);
    Serial.printf("hours : %d\n", hour);
    if (hour >= 12 &&  hour <= 23)
    {
      lv_label_set_text(ui_AmPm, "PM");
      lv_label_set_text(ui_AmPm2, "PM");
    } else
    {
      lv_label_set_text(ui_AmPm, "AM");
      lv_label_set_text(ui_AmPm2, "AM");
    }

  if (hour == hourValue && minute == minuteValue && switchState && !fal1)
  {
    Serial.println("The alarm goes off.");
    //set_pin_io(0, true);
    //Switching interface 5
    digitalWrite(PIN_VIB_EN, HIGH);
    if(lvgl_port_lock(0)) {
      _ui_screen_change( &ui_Screen5, LV_SCR_LOAD_ANIM_FADE_ON, 0, 0, &ui_Screen5_screen_init);
      lvgl_port_unlock();
    }
    fal1 = 1;

  }
  lv_timer_handler(); 
    // 6. 最后开启背光
    // 此时 GRAM 里已经是正确的 UI 画面，开启背光不会看到花屏
    delay(50); // 给 SPI 传输留一点缓冲时间
    digitalWrite(PIN_LED_EN, HIGH); 

    Serial.println("退出浅睡，显示已重置");
    set_backlight_brightness(100);
    
    xSemaphoreGive(touch_mutex);

}

int64_t get_sleep_duration_to_alarm() {
    DateTime now = rtc.now(); 
    Serial.printf("时间戳: %lu\n", rtc.now().secondstime());
    // 将当前时间和闹钟时间统一转换为当天的总秒数
    uint32_t current_secs = now.hour() * 3600 + now.minute() * 60 + now.second();
    Serial.printf("Current time: %02u:%02u:%02u, total seconds today: %u\n", now.hour(), now.minute(), now.second(), current_secs);
    uint32_t alarm_secs = hourValue * 3600 + minuteValue * 60; // hourValue和minuteValue为Preferences中读取的值 [cite: 571, 572]

    int64_t sleep_duration_sec;
    if (alarm_secs > current_secs) {
        // 闹钟在今天晚些时候
        sleep_duration_sec = alarm_secs - current_secs;
    } else {
        // 闹钟在明天（24小时 - 已过去时间 + 目标时间）
        sleep_duration_sec = (24 * 3600) - current_secs + alarm_secs;
    }
    
    return sleep_duration_sec;
}

bool last_usb_connected = false; 
void configure_usb_wakeup() {
    bool current_usb_connected = (digitalRead(17) == HIGH);

    if (!current_usb_connected) {
        // --- 情况 1：当前没插 USB ---
        // 我们希望在“第一次插入”时醒来，所以开启低电平唤醒
        esp_sleep_enable_gpio_wakeup();
        gpio_wakeup_enable((gpio_num_t)17, GPIO_INTR_HIGH_LEVEL);
        //Serial.println("USB未连接，已配置插入唤醒");
    } 
    else {
        // --- 情况 2：当前已经插着 USB 了 ---
        // 为了防止它持续唤醒或重复唤醒，我们禁用该引脚的唤醒功能
        // 或者你可以配置为 GPIO_INTR_HIGH_LEVEL (拔出时唤醒)
        gpio_wakeup_disable((gpio_num_t)17);
        //Serial.println("USB已连接，禁用重复插入唤醒");
    }
    
    last_usb_connected = current_usb_connected;
}


void lcd_backlight_task(void *pvParameters)
{
  static uint8_t first_times = 1;
  static uint8_t cur_lcd_power_state;
  static uint8_t prev_lcd_power_state = 1;

  static uint8_t logic_power_state = 1; 
  static uint8_t last_phys_pin_level = 0;
  last_phys_pin_level = digitalRead(PIN_LCD_POWER_DET);
  while(1) {

        if(test_flag == false)
        {
          configure_usb_wakeup();
        }
        // 1. 读取当前真实的物理引脚电平
        uint8_t current_phys_pin_level = digitalRead(PIN_LCD_POWER_DET);

        // 2. 【边沿检测】判断物理引脚是否发生了切换（按下或拨动）
        if (current_phys_pin_level != last_phys_pin_level && autosleep) {
            logic_power_state = !logic_power_state; // 翻转逻辑状态
            last_phys_pin_level = current_phys_pin_level; // 同步物理记录
            last_active_time = millis(); // 只要有操作，就刷新活跃时间
            Serial.printf("Physical Toggle! New Logic State: %d\n", logic_power_state);
        }

        // 3. 【超时检测】处理自动睡眠
        if (autosleep && (logic_power_state == 1)) {
            if (millis() - last_active_time > INACTIVITY_TIMEOUT_MS) {
                logic_power_state = 0; // 自动进入灭屏逻辑
                Serial.println("Inactivity timeout: Auto-sleep triggered.");
            }
        }
        // 4. 【执行机构】根据逻辑状态决定亮灭
        // 使用一个静态变量记录硬件实际执行到的状态，避免重复调用执行函数
        static uint8_t current_hw_executed_state = 1; 

        if (logic_power_state != current_hw_executed_state) {
            if (logic_power_state == 1) {
                // 执行亮屏
                delay(500);
                digitalWrite(PIN_POWER_CTL, HIGH);

                set_backlight_brightness(100);
                Serial.println("Display Wake Up");
            } 
            else {
                // 执行休眠准备
                Serial.println("Display Going to Sleep...");
                // sleep_begin();

                  if(switchState)
                  {
                // 计算闹钟唤醒时间
                    
                    int64_t sleep_sec = get_sleep_duration_to_alarm();
                    Serial.printf("Alarm set for %02u:%02u, sleeping for %lld seconds\n", hourValue, minuteValue, sleep_sec);
                    delay(50);
                    // 1. 配置定时器唤醒 (单位是微秒，所以要乘以 1000000)
                    esp_sleep_enable_timer_wakeup(sleep_sec * 1000000ULL);
                    switchState_pre = true;
                  } else if (switchState_pre ){
                      // 如果闹钟关闭，可以调用此 API 显式禁用定时器唤醒（可选，因为不设置通常就不会触发）
                      esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
                      switchState_pre = false;
                  }
                sleep_begin();
                //configure_usb_wakeup();
                // esp_sleep_enable_gpio_wakeup();
                // gpio_wakeup_enable((gpio_num_t)17, GPIO_INTR_HIGH_LEVEL);
                esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_LCD_POWER_DET, current_phys_pin_level == HIGH ? 0 : 1);
                
                // 进入轻度睡眠
                esp_light_sleep_start();
                
                // --- 唤醒后执行 ---
                sleep_exit();
                
                // 唤醒后，强制将逻辑状态设为 1 (亮屏)
                logic_power_state = 1; 
                last_active_time = millis(); 
                
                // 重要：唤醒后瞬间重新同步物理引脚电平
                // 这样下一次循环时，current_phys_pin_level == last_phys_pin_level 成立
                // 不会因为唤醒动作本身触发上面的“边沿检测”导致又睡过去
                last_phys_pin_level = digitalRead(PIN_LCD_POWER_DET);
            }
            current_hw_executed_state = logic_power_state;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    
    
  }
}
/*********************** power ***********************/
void timer_cb(lv_timer_t * timer)
{
    _ui_screen_change( &ui_Screen5, LV_SCR_LOAD_ANIM_FADE_ON, 0, 0, &ui_Screen5_screen_init);
    screen_change_flag = false;
    lv_timer_del(timer);      // 删除自己，实现一次性定时器
}
/* ===================normal operation============== */
void normal_operation() {
  // put your main code here, to run repeatedly:
  if(anim_loaded_end)
  {
    anim_loaded_end = false;
    last_active_time = millis();
  }

  if (!digitalRead(PIN_BOOT_KEY))
  {
    delay(500);
    if (digitalRead(PIN_BOOT_KEY))
    {
      if (Screen_flag != 4)
      {
        if(lvgl_port_lock(0)) {
          _ui_screen_change(&ui_AlarmClockScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 0, 0, &ui_AlarmClockScreen_screen_init);
          lvgl_port_unlock();
        }
        Screen_flag = 4;
      }
    }
  }

  if(tmp1 == 1)
  {
    tmp1 = 0;
    DateTime now = rtc.now();
    hour = now.hour();
    minute = now.minute();

    lv_img_set_angle(ui_MinuteHands, minute * 60);
    lv_img_set_angle(ui_Hourshands, hour * 300 + minute / 12 * 60);
    lv_img_set_angle(ui_MinuteHands2, minute * 60);
    lv_img_set_angle(ui_Hourshands2, hour * 300 + minute / 12 * 60);
  }

  if (switchState && !fal)
  {
    // Turn on the alarm
    fal = 1;
    fal1 = 0;
    Serial.println(switchState ? "On" : "Off");
    Preferences_init();
  } else if (!switchState && fal)
  {
    // Turn off the alarm
    fal = 0;
    fal1 = 0;
    Serial.println(switchState ? "On" : "Off");
    //set_pin_io(0, false);

    Serial.println("The alarm clock is off.");
  }
  //Determine if the alarm is about to go off
  if (hour == hourValue && minute == minuteValue && switchState && !fal1)
  {
    Serial.println("The alarm goes off.");
    //set_pin_io(0, true);
    //Switching interface 5
    digitalWrite(PIN_VIB_EN, HIGH);
    if (lvgl_port_lock(0)) {
      
      if(screen_change_flag)
      {

        lv_timer_t * t = lv_timer_create(timer_cb, 400, ui_Screen5); 
      }
      else
      {

        _ui_screen_change( &ui_Screen5, LV_SCR_LOAD_ANIM_FADE_ON, 0, 0, &ui_Screen5_screen_init);
      }

      
      
      lvgl_port_unlock();
    }
    
    fal1 = 1;

  }
  if (fal1)
  {
    autosleep = false;
    alarm_run = false;
    Alarm_task();
    alarm_run = true;
    autosleep = true;
  }

  if(switchPressed && lv_scr_act() != ui_AlarmClockScreen)
  {
        autosleep = false;
        if (position_tmp == 1) //position_tmp<position
        {
          minute++;
          lv_img_set_angle(ui_MinuteHands, minute * 60);
          lv_img_set_angle(ui_Hourshands, hour * 300 + minute / 12 * 60);
          lv_img_set_angle(ui_MinuteHands2, minute * 60);
          lv_img_set_angle(ui_Hourshands2, hour * 300 + minute / 12 * 60);
          if (minute == 60  )
          {
            minute = 0;
            if (hour == 24)
              hour = 0;
            else
              hour++;
          }
          position_tmp = 2;
        }
        if (position_tmp == 0) //position_tmp>position
        {
          if (minute == 0)
          {
            minute = 60;
            if (hour == 0)
              hour = 23;
            else
              hour--;
          }
          minute--;
          lv_img_set_angle(ui_MinuteHands, minute * 60);
          lv_img_set_angle(ui_Hourshands, hour * 300 + minute / 12 * 60);
          lv_img_set_angle(ui_MinuteHands2, minute * 60);
          lv_img_set_angle(ui_Hourshands2, hour * 300 + minute / 12 * 60);

          //position_tmp=position;
          position_tmp = 2; 
        }
        angle = lv_img_get_angle(ui_SecondHand);
        int i = ((int)(angle / 3600 * 60) - 1 + 4) % 60;
        if (minute == 60)
          minute == 0;
        DateTime currentTime(2000, 1, 1, hour, minute, i);
        rtc.adjust(currentTime);
    // }
        if (hour >= 12 &&  hour <= 23)
        {
          lv_label_set_text(ui_AmPm, "PM");
          lv_label_set_text(ui_AmPm2, "PM");
        } else
        {
          lv_label_set_text(ui_AmPm, "AM");
          lv_label_set_text(ui_AmPm2, "AM");
        }
        lastSwitchTime = millis(); 
        last_active_time = millis();
  }
  else
  {
    autosleep = true;
  }

  if (position_tmp == 1)  // position_tmp < position
  {
    
    if (millis() - lastSwitchTime >= switchInterval) {
      Serial.print("++++ ");
      screen_change_flag = true;
      lv_obj_t *current_screen;
      current_screen = lv_scr_act();
      
      // 屏幕切换
      if (current_screen == ui_HomeScreen) {
          if (lvgl_port_lock(0)) {
              _ui_screen_change(&ui_HomeScreen2, LV_SCR_LOAD_ANIM_MOVE_LEFT, 390, 0, &ui_HomeScreen2_screen_init);

              lvgl_port_unlock();
          }
        
        Screen_flag = 3;
      }
      else if (current_screen == ui_HomeScreen2) {
        
          if (lvgl_port_lock(0)) {
              _ui_screen_change(&ui_AlarmClockScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 390, 0, &ui_AlarmClockScreen_screen_init);

              lvgl_port_unlock();
          }
        Screen_flag = 4;
      }
      else if (current_screen == ui_AlarmClockScreen) {
        if (lvgl_port_lock(0)) {
            _ui_screen_change(&ui_HomeScreen, LV_SCR_LOAD_ANIM_MOVE_LEFT, 390, 0, &ui_HomeScreen_screen_init);
            lvgl_port_unlock();
        }
        Screen_flag = 2;
      }


      lastSwitchTime = millis();  
      
    }
    
    position_tmp = 2; // 更新状态
    
  }
  // reversal
  if (position_tmp == 0)  // position_tmp > position
  {
    if (millis() - lastSwitchTime >= switchInterval) { 
      Serial.print("----");
      screen_change_flag = true;
      lv_obj_t *current_screen = lv_scr_act();

      // 屏幕切换
      if (current_screen == ui_HomeScreen) {
        if (lvgl_port_lock(0)) {
            _ui_screen_change(&ui_AlarmClockScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 390, 0, &ui_AlarmClockScreen_screen_init);
            lvgl_port_unlock();
        }
        Screen_flag = 4;
      }
      else if (current_screen == ui_HomeScreen2) {
        if (lvgl_port_lock(0)) {
            _ui_screen_change(&ui_HomeScreen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 390, 0, &ui_HomeScreen_screen_init);
            lvgl_port_unlock();
        }
        Screen_flag = 2;
      }
      else if (current_screen == ui_AlarmClockScreen) {
        if (lvgl_port_lock(0)) {
            _ui_screen_change(&ui_HomeScreen2, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 390, 0, &ui_HomeScreen2_screen_init);
            lvgl_port_unlock();
        }
        Screen_flag = 3;
      }

      
      lastSwitchTime = millis();  
    }
    position_tmp = 2;
  }

  encoderKey.tick();
  if (encoder.valueChanged() && alarm_run)
  {
      current_encodervalue = encoder.getValue();
      if(pre_encodervalue > current_encodervalue)
      {
          position_tmp = 0;
      }
      else
      {
          position_tmp = 1;
      }
      pre_encodervalue = current_encodervalue;
      last_active_time = millis();
  }
  //lv_timer_handler();
}

#define PCF8563_ADDR 0x51 // 芯片 I2C 地址

uint8_t readVL() {
  Wire.beginTransmission(PCF8563_ADDR);
  Wire.write(0x02);              // 发送要读取的寄存器地址 (秒寄存器)
  Wire.endTransmission(false);   // 发送重启信号而不停止总线

  Wire.requestFrom(PCF8563_ADDR, 1); // 请求读取 1 个字节
  if (Wire.available()) {
    uint8_t secondsReg = Wire.read();
    // 检查最高位 (Bit 7)
    return (secondsReg >> 7) & 0x01; 
  }
  return 0; // 读取失败默认返回0
}

/* ===================normal operation============== */

void setup() {
  // put your setup code here, to run once:

  Serial.begin(115200);  // Init Uart

  /*********************** power ***********************/
  preferences.begin("power", false);
  preferences.putInt("power_on", 1);
  preferences.end();

  pinMode(PIN_POWER_CTL, OUTPUT);
  digitalWrite(PIN_POWER_CTL, HIGH);

  pinMode(PIN_LED_EN, OUTPUT);
  digitalWrite(PIN_LED_EN, HIGH); // 打开LED灯
  delay(800);
  // // 开启自动浅睡配置
  // esp_pm_config_esp32s3_t pm_config = {
  //   .max_freq_mhz = 240,
  //   .min_freq_mhz = 80,
  //   .light_sleep_enable = true // 允许系统根据蓝牙空闲情况自动浅睡
  // };
  // ESP_ERROR_CHECK(esp_pm_configure(&pm_config));

  // WiFi.begin("Redmi_K60", "12345679");

  // esp_sleep_enable_ext0_wakeup 只能设置一个引脚检测，且功耗更高
  // esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_LCD_POWER_DET, 1); // 设置引脚高电平唤醒

  // esp_sleep_enable_ext1_wakeup 可以同时检测多个引脚，且功耗更低
 esp_sleep_enable_ext1_wakeup((1ULL << PIN_TOUCH_INT), ESP_EXT1_WAKEUP_ANY_HIGH);

  xTaskCreate(lcd_backlight_task, "lcd_backlight_task", 4096, NULL, 10, NULL);
  xTaskCreate(bat_adc_task, "bat_adc_task", 4096, NULL, 5, &bat_adc_task_handle);
  /*********************** power ***********************/

  /*********************** I2C ***********************/
  Wire.begin(PIN_SDA, PIN_SCL);  // 初始化 I2C（默认 100kHz）
  // Wire.setClock(400000);       // 修改频率为 400kHz（快速模式）
  /*********************** I2C ***********************/

  /*********************** DISPLAY ***********************/
  pinMode(PIN_LCD_EN, OUTPUT);
  digitalWrite(PIN_LCD_EN, LOW);  
  set_backlight_brightness(0);

  display_init();
  touch_init();
  lvgl_init();
  set_backlight_brightness(100);
  /*********************** DISPLAY ***********************/

  /*********************** BAT & ADC ***********************/
  bat_adc_init();
  /*********************** BAT & ADC ***********************/

  /*********************** 震动马达 ***********************/
  pinMode(PIN_VIB_EN, OUTPUT);
  digitalWrite(PIN_VIB_EN, LOW);
  /*********************** 震动马达 ***********************/

  /*********************** ble ***********************/
  
  /*********************** ble ***********************/

  /*********************** wifi ***********************/
  // wifi_scan();
  // esp_wifi_set_max_tx_power(80);
  /*********************** wifi ***********************/

  /*********************** key & encoder ***********************/
  encoder_init();
  key_init();
  /*********************** key & encoder ***********************/

  /*********************** gyro SC7A20HTR ***********************/
  gyro_sc7a20_init();
  /*********************** gyro SC7A20HTR ***********************/

  /*********************** RTC PCF8563 ***********************/
  RTC_PCF8563_Init();
  /*********************** RTC PCF8563 ***********************/

  /*********************** microphone & loudspeaker ***********************/
  mic_loudspeaker_init();
  /*********************** microphone & loudspeaker ***********************/
  Serial.printf("setup done\n");

  if (readVL()) {
    Serial.println("发现 VL=1: 电池掉电或首次上电，请设置时间！");
    DateTime currentTime(2000, 1, 1, 0, 0, 0);
    rtc.adjust(currentTime);
  }
  while(1)
  {
      float voltage = battery_get_voltage(); 
      Serial.printf("Battery Voltage: %.2fV, PIN_BAT_CHAR: %d\n", voltage, digitalRead(PIN_BAT_CHAR));

      // 2. 判定低电量逻辑
      if (voltage < 3.6) {
        Serial.println("Low battery! Shutting down...");
        for (int i = 0; i < 5; i++) {
          digitalWrite(PIN_LED_EN, HIGH);
          vTaskDelay(pdMS_TO_TICKS(1000));
          digitalWrite(PIN_LED_EN, LOW);
          vTaskDelay(pdMS_TO_TICKS(1000));
        }
        // esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_LCD_POWER_DET, current_phys_pin_level == HIGH ? 0 : 1);
        
        // // 进入轻度睡眠
        // esp_light_sleep_start();
        volage_low_flag = true;

      }
      else
      {
        volage_low_flag = false;
        break ;
      }
  }
  
  DateTime now = rtc.now();
  hour = now.hour();
  minute = now.minute();
  second = now.second();
  s = (uint32_t)second * 60;
  /*********************** RTC PCF8563 ***********************/
  lv_img_set_angle(ui_SecondHand,  second * 60);
  //lv_img_set_angle(ui_SecondHand,  second * 60);
  lv_img_set_angle(ui_MinuteHands2, minute * 60);
  lv_img_set_angle(ui_Hourshands2, (hour % 12) * 300 + minute / 12 * 60);
  lv_img_set_angle(ui_MinuteHands, minute * 60);
  lv_img_set_angle(ui_Hourshands, (hour % 12) * 300 + minute / 12 * 60);
  //lv_timer_create((void*)send_event, 50, NULL);
  lv_timer_create(time_refresh, 100, NULL);
  Serial.printf("hours : %d\n", hour);
  if (hour >= 12 &&  hour <= 23)
  {
    lv_label_set_text(ui_AmPm, "PM");
    lv_label_set_text(ui_AmPm2, "PM");
  } else
  {
    lv_label_set_text(ui_AmPm, "AM");
    lv_label_set_text(ui_AmPm2, "AM");
  }

  last_active_time = millis();
}

void loop() {
  // put your main code here, to run repeatedly:
  
  uart_test();

  
}
