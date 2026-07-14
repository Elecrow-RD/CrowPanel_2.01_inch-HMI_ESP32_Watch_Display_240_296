#pragma once

/*********************** 引脚定义 ***********************/
//I2C:RTC、touch、加速度计
#define PIN_SCL           (3)
#define PIN_SDA           (4)     

//touch
#define PIN_TOUCH_INT     (2)
#define PIN_TOUCH_RST     (5)

//屏幕、背光
#define PIN_LCD_EN        (40)  //显示、触摸、背光供电使能,低电平使能
#define PIN_LCD_POWER_DET (14)  //屏幕背光状态设置引脚
#define PIN_LCD_BL        (13)  //背光
#define PIN_LCD_TE        (21)
#define PIN_LCD_RST       (6 )
#define PIN_LCD_DC        (10)
#define PIN_LCD_CS        (9 )
#define PIN_LCD_CLK       (7 )
#define PIN_LCD_MOSI      (8 )
#define PIN_LCD_MISO      (-1)

//电源管理
#define PIN_POWER_CTL     (47)  //其它芯片的电源使能

//ADC & BATTERY
#define PIN_BAT_CHAR      (15)  //充电芯片CHAR引脚
#define PIN_BAT_ADC       ( 1)  //电池电压检测ADC

// LED灯
#define PIN_LED_EN        (16)  //LED灯高电平点亮

//motor
#define PIN_VIB_EN        (45)  //线性马达使能，高电平使能

//按键
#define PIN_BOOT_KEY      ( 0)  //BOOT按键检测引脚，需要上拉
#define PIN_EC_BTN        (18)  //编码器按键
#define PIN_EC_A          (20)  //编码器A相
#define PIN_EC_B          (19)  //编码器B相

//microphone
#define PIN_MIC_MCLK      (12)
#define PIN_MIC_SD        (11)
//loudspeaker
#define PIN_I2S_AUDIO_EN  (48)  //音频供电使能,低电平使能
#define PIN_I2S_OUT_SD    (41)
#define PIN_I2S_OUT_SCLK  (42)
#define PIN_I2S_OUT_LRCK  (46)

//外部串口
#define PIN_EXTERNAL_UART_TX  (38)  //外部串口TX
#define PIN_EXTERNAL_UART_RX  (39)  //外部串口RX

/*********************** 引脚定义 ***********************/

/*********************** 屏幕定义 ***********************/
#define DISPLAY_WIDTH    240
#define DISPLAY_HEIGHT   296

#define DISPLAY_SPI_FREQ (80 * 1000 * 1000)  //屏幕SPI时钟频率，单位Hz

// #define DIRECT_RENDER_MODE
/*********************** 屏幕定义 ***********************/

/*********************** 上位机命令 ***********************/
#define CMD_SEPARATOR           "\n"  //命令分隔符

#define CMD_ENTRY_TEST          "Entry test"
#define CMD_EXIT_TEST           "Exit test"

#define CMD_PRESS_START         "P"
#define CMD_PRESS_STOP          "p"
#define CMD_EXIT_PRESS_TEST     "exit press test"

#define CMD_AUTO_START          "U"
#define CMD_AUTO_STOP           "u"
#define CMD_EXIT_AUTO_TEST      "exit auto test"

#define CMD_BACKLIGHT_ON        "L"
#define CMD_BACKLIGHT_OFF       "l"
#define CMD_BACKLIGHT_1         "1"
#define CMD_BACKLIGHT_2         "2"
#define CMD_BACKLIGHT_3         "3"
#define CMD_BACKLIGHT_4         "4"
#define CMD_BACKLIGHT_5         "5"
#define CMD_EXIT_BL_TEST        "exit backlight test"

#define CMD_DISPLAY_START       "D"
#define CMD_DISPLAY_STOP        "d"
#define CMD_EXIT_DISPLAY_TEST   "exit display test"

#define CMD_TOUCH_START         "T"
#define CMD_TOUCH_STOP          "t"
#define CMD_EXIT_TOUCH_TEST     "exit touch test"

#define CMD_TOUCH_DOT_START     "E"
#define CMD_TOUCH_DOT_STOP      "e"
#define CMD_EXIT_TOUCH_DOT_TEST   "exit touch dot test"

#define CMD_BAT_ADC_START       "A"
#define CMD_BAT_ADC_STOP        "a"
#define CMD_EXIT_BAT_TEST       "exit battery test"

#define CMD_KEY_ENCODER_START   "K"
#define CMD_KEY_ENCODER_STOP    "k"
#define CMD_EXIT_KEY_ENCODER    "exit key test"

#define CMD_MOTOR_START         "M"
#define CMD_MOTOR_STOP          "m"
#define CMD_EXIT_MOTOR_TEST     "exit motor test"

#define CMD_GYRO_START          "Y"
#define CMD_GYRO_STOP           "y"
#define CMD_EXIT_GYRO_TEST      "exit gyro test"

#define CMD_RTC_START           "R"
#define CMD_RTC_STOP            "r"
#define CMD_RTC_TIMESTAMP       "Calibrating_time:"
#define CMD_EXIT_RTC_TEST       "exit rtc test"

#define CMD_SPK_START           "C"
#define CMD_SPK_STOP            "c"
#define CMD_EXIT_SPK_TEST       "exit loudspeaker test"

#define CMD_MIC_START           "I"
#define CMD_MIC_STOP            "i"
#define CMD_EXIT_MIC_TEST       "exit microphone test"

#define CMD_WIFI_START          "W"
#define CMD_WIFI_STOP           "w"
#define CMD_WIFI_SSID           "wifi_ssid:"
#define CMD_WIFI_PASSWORD       "wifi_password:"
#define CMD_START_WIFI_TEST     "start wifi test"
#define CMD_EXIT_WIFI_TEST      "exit wifi test"

#define CMD_BLE_START           "B"
#define CMD_BLE_STOP            "b"
#define CMD_EXIT_BLE_TEST       "exit ble test"

/*********************** 上位机命令 ***********************/
