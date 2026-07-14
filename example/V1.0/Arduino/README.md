# ESP32-S3 Watch Arduino IDE Course Code

This directory contains code for 15 ESP32-S3 watch lessons developed on the Arduino IDE platform. The course starts with development environment checks and gradually covers buttons, display touch, rotary encoder, RTC, WiFi, BLE, accelerometer, vibration motor, battery detection, speaker, microphone, audio playback, and low-power sleep features.

These examples are suitable for classroom teaching, hardware function verification, secondary development reference, and for beginners to progressively understand the main peripherals of the watch development board by following the course order.

## Basic Platform Configuration

Please use the following environment configuration first when opening and compiling the lesson code in this directory:

| Item | Configuration |
|---|---|
| Development Tool | Arduino IDE 2.3.6 |
| Board Package | esp32 by Espressif Systems 3.3.3 |
| Board | ESP32S3 Dev Module |
| Flash Size | 16MB (128Mb) |
| PSRAM | OPI PSRAM |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) |
| Serial Baud Rate | 115200 |

This project provides a unified `libraries` folder. It is recommended to use the libraries in this folder as the shared dependency source for the whole course, avoiding different compilation results caused by inconsistent library versions installed on different computers.

## Arduino Library Dependencies

The main third-party libraries used in this Arduino course are listed below:

| Library Folder | Library Name | Version |
|---|---|---|
| `OneButton` | OneButton | 2.6.1 |
| `GFX_Library_for_Arduino` | GFX Library for Arduino | 1.6.2 |
| `lvgl` | LVGL | 9.1.0 |
| `RTClib` | RTClib | 2.1.4 |
| `Adafruit_BusIO` | Adafruit BusIO | 1.17.4 |
| `EncoderTool` | EncoderTool | 3.2.2 |
| `Bounce2` | Bounce2 | 2.71 |

Not every lesson uses all of these libraries. To make it easy for users to compile the code immediately after downloading it, it is recommended to keep the complete `libraries` folder.

## Directory Structure

Each lesson is an independent Arduino project. When opening a lesson, enter the corresponding lesson folder and open the `.ino` file with the same name as the folder.

```text
Code/
├─ Lesson01_Environment_Setup/
├─ Lesson02_Boot_Button/
├─ Lesson03_Display_Touch_LVGL/
├─ ...
└─ Lesson15_Power_Sleep/
```

Lesson 3 contains dependency files related to the display, touch, and LVGL UI. When moving or copying the code, keep `config.h`, `src/touch`, and `src/ui` in the same lesson project directory as the `.ino` file.

## Overview of the 15 Lessons

| Lesson | Topic | Brief Description |
|---|---|---|
| Lesson01_Environment_Setup | Arduino IDE environment installation and serial self-check | Checks whether the Arduino IDE, ESP32-S3 board package, Flash, OPI PSRAM, and partition scheme are configured correctly. This is the basic self-check for the following lessons. |
| Lesson02_Boot_Button | BOOT button serial output | Reads the onboard BOOT button and prints button events through the serial port. Reset is used for program reset, and the Power key is used for screen on/off and shutdown, so they are not printed in this lesson. |
| Lesson03_Display_Touch_LVGL | GC9309 display, AXS5106L touch, and LVGL 9.1 | Initializes the LCD, touch chip, and LVGL UI. This is the lesson in the course where you need to see interface output on the screen. |
| Lesson04_Rotary_Encoder | Rotary encoder and encoder button | Reads the A/B phase signals of the EC04 encoder, determines the rotation direction, and reads the encoder button while outputting the result through the serial port. |
| Lesson05_RTC_Basics | Basic PCF8563 RTC reading | Reads the PCF8563 real-time clock through I2C to obtain the date, time, weekday, and basic status information. |
| Lesson06_WiFi_Communication | WiFi scan and Station connection | Scans nearby WiFi networks and tries to connect to a router using the SSID and password in the code, introducing the basic WiFi workflow. |
| Lesson07_WiFi_RTC_Sync | WiFi NTP synchronization with PCF8563 RTC | Connects to WiFi, obtains network time through NTP, and writes it to the external RTC to implement network time synchronization. |
| Lesson08_BLE_Communication | BLE service and characteristic communication | Makes the ESP32-S3 advertise as a BLE peripheral and supports phone or computer connection, reading, writing, and Notify. |
| Lesson09_Accelerometer_Vibration | SC7A20HTR accelerometer and motion detection | Reads X, Y, and Z data from the three-axis accelerometer and detects device motion based on acceleration changes. |
| Lesson10_Vibration_Motor | Vibration motor control | Uses GPIO to control the vibration motor in rhythmic patterns, serving as a basis for message alerts, alarms, or interaction feedback. |
| Lesson11_Battery_Monitor | Battery voltage and charging status reading | Reads the battery voltage divider through ADC and calculates the battery voltage, while also reading the charging status pin. |
| Lesson12_Speaker_Playback | I2S speaker test tone playback | Uses I2S to generate a 440Hz PCM sine wave in real time and plays it through the onboard amplifier and speaker. |
| Lesson13_Microphone_Input | PDM microphone input and volume analysis | Uses a PDM microphone to capture audio, calculates peak value and RMS, and observes sound intensity through the serial port. |
| Lesson14_Audio_Loopback | Microphone recording and speaker playback | Combines the microphone and speaker to complete the workflow of recording, audio buffering, and playback. |
| Lesson15_Power_Sleep | Light Sleep and wake-up source test | Demonstrates turning off some loads before entering Light Sleep, and configures wake-up sources such as touch, display power status, USB insertion, and timer. |

## Usage

1. Install Arduino IDE 2.3.6.
2. Install the `esp32 by Espressif Systems` board package and select version 3.3.3.
3. Configure the board, Flash, PSRAM, and partition scheme according to the platform configuration above.
4. Prepare the `libraries` folder provided by this project.
5. Open the `.ino` file in one of the lesson folders.
6. Select the correct serial port, click Verify to compile first, then click Upload to flash the program.
7. Most lessons show results through the Serial Monitor; Lesson 3 requires observing the LCD screen interface.

## Notes

- Lesson 3 has many dependency files. Do not copy only the `.ino` file.
- Lessons 6 and 7 require modifying the WiFi name and password according to your actual router.
- Lesson 8 requires a phone app or computer tool that supports BLE for connection testing.
- Lessons 12, 13, and 14 involve audio input and output. It is recommended to keep the development board power supply stable during testing.
- Lesson 15 involves low power consumption and wake-up. The serial port may briefly stop outputting during sleep, which is normal.
