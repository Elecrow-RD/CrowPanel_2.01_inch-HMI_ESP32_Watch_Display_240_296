#pragma once                                                   // Compile this header only once per translation unit.

#include <Arduino.h>                                           // Load fixed-width Arduino types such as uint8_t and uint16_t.

constexpr uint8_t PIN_I2C_SCL = 3;                             // Use GPIO3 as the shared I2C clock pin from the reference project.
constexpr uint8_t PIN_I2C_SDA = 4;                             // Use GPIO4 as the shared I2C data pin from the reference project.
constexpr uint8_t PIN_TOUCH_INT = 2;                           // Use GPIO2 for the AXS5106L interrupt signal.
constexpr uint8_t PIN_TOUCH_RST = 5;                           // Use GPIO5 for the AXS5106L reset signal.

constexpr uint8_t PIN_LCD_POWER = 40;                          // Use GPIO40 to control the LCD power rail.
constexpr uint8_t PIN_PERIPHERAL_POWER = 47;                   // Use GPIO47 to enable the shared peripheral power rail.
constexpr uint8_t PIN_LCD_BACKLIGHT = 13;                      // Use GPIO13 as the LCD backlight PWM output.
constexpr uint8_t PIN_LCD_TE = 21;                             // Use GPIO21 for the LCD tearing-effect synchronization signal.
constexpr uint8_t PIN_LCD_RST = 6;                             // Use GPIO6 for the GC9309 LCD reset signal.
constexpr uint8_t PIN_LCD_DC = 10;                             // Use GPIO10 for the LCD data/command signal.
constexpr uint8_t PIN_LCD_CS = 9;                              // Use GPIO9 for the LCD chip-select signal.
constexpr uint8_t PIN_LCD_SCK = 7;                             // Use GPIO7 for the LCD SPI clock signal.
constexpr uint8_t PIN_LCD_MOSI = 8;                            // Use GPIO8 for the LCD SPI MOSI data signal.
constexpr int8_t PIN_LCD_MISO = -1;                            // Set MISO to -1 because the LCD write path does not use MISO.

constexpr uint16_t DISPLAY_WIDTH = 240;                        // Define the LCD horizontal resolution in pixels.
constexpr uint16_t DISPLAY_HEIGHT = 296;                       // Define the LCD vertical resolution in pixels.
constexpr uint32_t DISPLAY_SPI_FREQUENCY = 80UL * 1000UL * 1000UL; // Drive the LCD SPI bus at 80 MHz like the reference project.
