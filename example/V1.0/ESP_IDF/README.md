# ESP32-S3 Watch Course Code for ESP-IDF

This directory contains the code for a 15-lesson ESP32-S3 watch development course built on ESP-IDF. Each lesson is a standalone ESP-IDF project that can be opened, built, and flashed independently.

This course is intended for users who have already set up their development environment and want to learn ESP32-S3 peripheral development using native ESP-IDF APIs. Topics include buttons, an LCD, touch input, LVGL, a rotary encoder, an RTC, Wi-Fi, BLE, an accelerometer, a vibration motor, battery monitoring, I2S audio, a PDM microphone, audio playback, and touch wake-up from Light-sleep mode.

## Recommended Development Environment

Use the following configuration when opening and building the lesson projects:

| Item | Configuration |
|---|---|
| Development framework | ESP-IDF |
| ESP-IDF version | v5.5.4 |
| Recommended IDE | VS Code with the ESP-IDF extension |
| Target chip | ESP32-S3 |
| Flash size | 16 MB |
| PSRAM | Octal-mode PSRAM (`CONFIG_SPIRAM_MODE_OCT=y`) |
| Serial baud rate | 115200 |

Each project includes an `sdkconfig.defaults` file that defines the target chip, flash memory, PSRAM, and other baseline settings. The first time you open a project, ESP-IDF uses this file to generate a local `sdkconfig` file.

On Windows, place the projects in a path that contains only ASCII characters. Some parts of the ESP-IDF toolchain may not reliably support paths containing Chinese or other non-ASCII characters.

## Directory Structure

Each lesson is a standalone ESP-IDF project containing a `CMakeLists.txt` file, an `sdkconfig.defaults` file, and a `main` source directory.

```text
Code/
├─ Lesson01_Environment_Setup/
│  ├─ CMakeLists.txt
│  ├─ sdkconfig.defaults
│  └─ main/
├─ Lesson02_Boot_Button/
├─ Lesson03_Display_Touch_LVGL/
├─ ...
└─ Lesson15_Power_Sleep/
```

Lesson 3 also includes the following components:

```text
Lesson03_Display_Touch_LVGL/
└─ components/
   ├─ lvgl/
   └─ ui/
```

The `lvgl` directory contains LVGL 9.1.0. The `ui` directory contains UI files copied from Lesson 3 of the Arduino course and adapted for ESP-IDF. When copying or uploading Lesson 3, be sure to include the entire `components` directory.

## Course Overview

| Lesson | Topic | Description |
|---|---|---|
| Lesson01_Environment_Setup | ESP-IDF setup and serial-port verification | Verifies that ESP-IDF v5.5.4, the ESP32-S3 target, QIO flash mode, 16 MB of flash, octal-mode PSRAM, and the basic project settings are configured correctly. |
| Lesson02_Boot_Button | BOOT button events over serial | Uses the ESP-IDF GPIO API to read the onboard BOOT button and reports press, release, single-click, double-click, and long-press events. The Reset and Power buttons are not covered in this lesson. |
| Lesson03_Display_Touch_LVGL | GC9309 display, AXS5106L touch controller, and LVGL 9.1 | Uses native ESP-IDF SPI and I2C APIs to initialize the GC9309, read touch input from the AXS5106L, and render a UI with LVGL 9.1.0. |
| Lesson04_Rotary_Encoder | Rotary encoder and push button | Reads the A/B phase signals from an EC04 encoder to determine rotation direction and monitors the encoder's push-button state. |
| Lesson05_RTC_Basics | Reading the PCF8563 RTC | Uses the ESP-IDF I2C master API to read time and date information from the PCF8563 RTC. |
| Lesson06_WiFi_Communication | Wi-Fi scanning and Station mode | Uses the ESP-IDF Wi-Fi component to scan for nearby networks and connect to a specified router. |
| Lesson07_WiFi_RTC_Sync | Synchronizing the PCF8563 RTC over Wi-Fi | Connects to Wi-Fi, retrieves the current time via SNTP, and writes it to the PCF8563 RTC. |
| Lesson08_BLE_Communication | BLE services and characteristic communication | Uses NimBLE to create a BLE GATT service with characteristic read, write, and notification support. |
| Lesson09_Accelerometer_Vibration | SC7A20HTR accelerometer and motion detection | Reads three-axis data from the SC7A20HTR over I2C and detects device movement based on changes in acceleration. |
| Lesson10_Vibration_Motor | Vibration motor control | Uses a GPIO to turn the vibration motor on and off in a fixed feedback pattern. |
| Lesson11_Battery_Monitor | Battery voltage and charging status | Uses the ADC oneshot and calibration APIs to measure a divided battery voltage, estimate the actual battery voltage, and read the charging-status GPIO. |
| Lesson12_Speaker_Playback | Playing a test tone over I2S | Uses ESP-IDF's I2S standard TX mode to generate 440 Hz PCM audio in real time and play it through the speaker. |
| Lesson13_Microphone_Input | PDM microphone input and volume analysis | Uses I2S PDM RX to capture microphone data and calculate peak and RMS volume levels. |
| Lesson14_Audio_Loopback | Microphone recording and speaker playback | Combines PDM microphone input with I2S speaker output to capture and play back audio. |
| Lesson15_Power_Sleep | Light-sleep mode and touch wake-up | Demonstrates low-power Light-sleep mode with the AXS5106L touch interrupt as the primary wake-up source and a timer as a backup wake-up source for classroom use. |

## Getting Started

1. Install ESP-IDF v5.5.4.
2. Open VS Code and launch the ESP-IDF extension.
3. Open a lesson's project directory, such as `Lesson03_Display_Touch_LVGL`.
4. Set the target chip to `esp32s3`.
5. Confirm that the flash SPI mode is set to QIO and the flash size is set to 16 MB. The project defaults configure PSRAM for octal mode.
6. Select the correct serial port.
7. Build the project.
8. Flash the project to the device.
9. Open the serial monitor to view logs, or observe the display, motor, speaker, microphone, or other hardware behavior covered by the lesson.

You can also run the following commands in an ESP-IDF terminal:

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with the device's actual serial port.

## Dependencies

- Most lessons use only official ESP-IDF components and do not require the Arduino framework.
- Lesson 3 includes LVGL 9.1.0 and the required UI component.
- Lesson 8 uses the ESP-IDF NimBLE Bluetooth stack.
- The audio lessons use the ESP-IDF I2S driver.
- The battery-monitoring lesson uses the ESP-IDF ADC oneshot driver.

## Important Notes

- Each lesson is a standalone project. Copying only the `main.c` file is not recommended.
- Lesson 3 requires both `components/lvgl` and `components/ui`.
- Lessons 6 and 7 require you to update the Wi-Fi network name and password for your environment.
- Lesson 8 requires a BLE debugging tool or mobile app to test connections, reads, writes, and notifications.
- Lessons 12, 13, and 14 use audio input and output. Make sure the development board has a stable power supply.
- Lesson 15 does not use the Power button. It demonstrates only touch wake-up and timer-based backup wake-up.
