#include <Arduino.h>              // Load the Arduino core API for Serial, GPIO, delay(), millis(), and analogWrite().
#include <Arduino_GFX_Library.h>  // Load Arduino_GFX so the sketch can drive the GC9309 SPI LCD.
#include <Wire.h>                 // Load the I2C API used by the AXS5106L touch controller.
#include <esp_heap_caps.h>        // Load heap_caps_malloc() so the LVGL draw buffer can be placed in internal RAM.
#include <lvgl.h>                 // Load LVGL 9.1 graphics and input APIs.

#include "config.h"               // Load the board pin map and display constants for this watch.
#include "src/touch/axs5106l_touch.h" // Load the local AXS5106L touch driver.
#include "src/ui/ui.h"            // Load the SquareLine/LVGL UI files placed in the src/ui folder.

namespace {                                                       // Keep lesson objects and callbacks private to this file.
constexpr uint32_t kSerialBaud = 115200;                          // Use 115200 baud for the Arduino Serial Monitor.
constexpr uint16_t kDrawBufferLines = 40;                         // Render 40 display lines per LVGL partial-refresh buffer.

Arduino_DataBus* displayBus = nullptr;                            // Store the SPI DMA bus object used by Arduino_GFX.
Arduino_GFX* displayGfx = nullptr;                                // Store the LCD driver object used to draw pixels.
Axs5106lTouch touchController;                                    // Create the local touch-controller driver.
SemaphoreHandle_t teSemaphore = nullptr;                          // Use a semaphore to wait for the LCD tearing-effect signal.
lv_display_t* lvDisplay = nullptr;                                // Store the LVGL display object.
uint8_t* drawBuffer = nullptr;                                    // Store the LVGL draw buffer allocated from internal RAM.
uint32_t lastTickMs = 0;                                          // Store the last time value passed into lv_tick_inc().

void IRAM_ATTR onTearingEffect() {                                // Run this interrupt when the LCD TE pin toggles.
  BaseType_t taskWoken = pdFALSE;                                 // Track whether the ISR wakes a higher-priority task.
  xSemaphoreGiveFromISR(teSemaphore, &taskWoken);                 // Release the flush callback that is waiting for a safe refresh window.
  if (taskWoken == pdTRUE) {                                      // Check whether FreeRTOS should switch tasks immediately.
    portYIELD_FROM_ISR();                                         // Yield from the ISR so the waiting task can continue quickly.
  }                                                               // End the ISR-yield check.
}                                                                 // End the TE interrupt callback.

void displayFlush(lv_display_t* display, const lv_area_t* area, uint8_t* pixelMap) { // Let LVGL send one rendered area to the LCD.
  const uint32_t width = area->x2 - area->x1 + 1;                 // Calculate the width of the dirty rectangle.
  const uint32_t height = area->y2 - area->y1 + 1;                // Calculate the height of the dirty rectangle.

  xSemaphoreTake(teSemaphore, 0);                                 // Clear any stale TE event before waiting for a fresh one.
  xSemaphoreTake(teSemaphore, pdMS_TO_TICKS(100));                // Wait briefly for the LCD to reach a good refresh moment.
  displayGfx->draw16bitRGBBitmap(area->x1, area->y1,              // Start drawing at the dirty rectangle's top-left X/Y coordinate.
                                reinterpret_cast<uint16_t*>(pixelMap), width, height); // Send RGB565 pixels from LVGL to the LCD.
  lv_display_flush_ready(display);                                // Tell LVGL that this flush operation has completed.
}                                                                 // End the LVGL flush callback.

void touchRead(lv_indev_t* inputDevice, lv_indev_data_t* data) {  // Let LVGL ask the touch controller for the current pointer state.
  static lv_point_t lastPoint = {0, 0};                           // Keep the last valid point so LVGL has stable release coordinates.
  uint16_t x = 0;                                                 // Prepare a variable for the touch X coordinate.
  uint16_t y = 0;                                                 // Prepare a variable for the touch Y coordinate.

  (void)inputDevice;                                              // Mark the unused LVGL input-device pointer as intentionally unused.
  if (touchController.readPoint(x, y)) {                          // Check whether the AXS5106L driver has a valid point.
    lastPoint.x = x;                                              // Save the latest X coordinate.
    lastPoint.y = y;                                              // Save the latest Y coordinate.
    data->point = lastPoint;                                      // Pass the point to LVGL.
    data->state = LV_INDEV_STATE_PRESSED;                         // Tell LVGL that the screen is being touched.

    static uint32_t lastPrintMs = 0;                              // Limit touch logging so the Serial Monitor remains readable.
    if (millis() - lastPrintMs >= 200) {                          // Print at most five touch reports per second.
      lastPrintMs = millis();                                     // Remember when the report was printed.
      Serial.printf("TOUCH: x=%u y=%u\n", x, y);                  // Print the current touch coordinate.
    }                                                             // End the touch-log rate limit.
  } else {                                                        // Handle the case where no valid touch point is active.
    data->point = lastPoint;                                      // Keep the final pressed point for LVGL release processing.
    data->state = LV_INDEV_STATE_RELEASED;                        // Tell LVGL that the pointer is released.
  }                                                               // End the pressed/released state update.
}                                                                 // End the LVGL touch read callback.

void sliderChanged(lv_event_t* event) {                           // Run this callback when the UI slider value changes.
  lv_obj_t* slider = static_cast<lv_obj_t*>(lv_event_get_target(event)); // Get the LVGL object that triggered the event.
  Serial.printf("SLIDER_VALUE: %ld\n", static_cast<long>(lv_slider_get_value(slider))); // Print the slider value for the lesson.
}                                                                 // End the slider event callback.

bool initializeDisplayHardware() {                                // Power and initialize the physical LCD hardware.
  pinMode(PIN_PERIPHERAL_POWER, OUTPUT);                          // Configure the shared peripheral power rail as an output.
  digitalWrite(PIN_PERIPHERAL_POWER, HIGH);                       // Enable the peripheral rail required by the LCD and touch hardware.
  delay(30);                                                      // Wait for the rail to become stable before enabling the LCD.

  pinMode(PIN_LCD_POWER, OUTPUT);                                 // Configure the LCD power-control pin as an output.
  digitalWrite(PIN_LCD_POWER, LOW);                               // Enable the LCD power rail because this board uses active-low LCD power.
  analogWrite(PIN_LCD_BACKLIGHT, 0);                              // Keep the backlight off during LCD initialization.
  delay(30);                                                      // Wait briefly for the LCD power rail to settle.

  teSemaphore = xSemaphoreCreateBinary();                         // Create the semaphore used by the tearing-effect interrupt.
  if (teSemaphore == nullptr) {                                   // Check whether FreeRTOS could allocate the semaphore.
    Serial.println("ERROR: TE semaphore allocation failed");      // Print the allocation failure.
    return false;                                                 // Stop display initialization because flush synchronization cannot work.
  }                                                               // End the semaphore allocation check.

  pinMode(PIN_LCD_TE, INPUT_PULLUP);                              // Configure the LCD TE pin as an input with pull-up.
  attachInterrupt(digitalPinToInterrupt(PIN_LCD_TE), onTearingEffect, FALLING); // Attach the TE interrupt on the falling edge.

  displayBus = new Arduino_ESP32SPIDMA(PIN_LCD_DC, PIN_LCD_CS, PIN_LCD_SCK, // Allocate the SPI DMA bus with DC, CS, and SCK pins.
                                       PIN_LCD_MOSI, PIN_LCD_MISO, HSPI, true); // Add MOSI, no-MISO, HSPI, and DMA settings.
  displayGfx = new Arduino_GC9309(displayBus, PIN_LCD_RST, 0, false, // Allocate the GC9309 panel driver with reset and orientation settings.
                                  DISPLAY_WIDTH, DISPLAY_HEIGHT, 0, 0, 0, 0); // Provide panel size and zero display offsets.
  if (!displayGfx->begin(DISPLAY_SPI_FREQUENCY)) {                  // Start the LCD driver at the configured SPI frequency.
    Serial.println("ERROR: GC9309 initialization failed");          // Print an LCD initialization failure.
    return false;                                                   // Stop setup because LVGL cannot draw without the LCD.
  }                                                                 // End the LCD begin check.

  displayGfx->setRotation(0);                                       // Match the touch transform and UI layout to rotation 0.
  displayGfx->fillScreen(RGB565_BLACK);                             // Clear the LCD to black before LVGL starts drawing.
  return true;                                                      // Report that the LCD hardware is ready.
}                                                                   // End initializeDisplayHardware().

bool initializeLvgl() {                                             // Create LVGL display, input, buffers, and UI objects.
  lv_init();                                                        // Initialize the LVGL core before any LVGL object is created.

  lvDisplay = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);     // Create one LVGL display with the watch LCD resolution.
  if (lvDisplay == nullptr) {                                       // Check whether LVGL could allocate the display object.
    Serial.println("ERROR: LVGL display allocation failed");        // Print the LVGL display allocation failure.
    return false;                                                   // Stop setup because LVGL has no display target.
  }                                                                 // End the display allocation check.

  lv_display_set_color_format(lvDisplay, LV_COLOR_FORMAT_RGB565);   // Match LVGL's output format to the 16-bit LCD pixel format.
  lv_display_set_flush_cb(lvDisplay, displayFlush);                 // Register the callback that copies LVGL pixels to the LCD.

  const size_t drawBufferBytes = DISPLAY_WIDTH * kDrawBufferLines * sizeof(uint16_t); // Calculate the byte size of the partial draw buffer.
  drawBuffer = static_cast<uint8_t*>(heap_caps_malloc(drawBufferBytes, MALLOC_CAP_INTERNAL)); // Allocate fast internal RAM for drawing.
  if (drawBuffer == nullptr) {                                      // Check whether the draw buffer allocation succeeded.
    Serial.println("ERROR: LVGL draw buffer allocation failed");    // Print the buffer allocation failure.
    return false;                                                   // Stop setup because LVGL cannot render without a buffer.
  }                                                                 // End the draw-buffer allocation check.

  lv_display_set_buffers(lvDisplay, drawBuffer, nullptr, drawBufferBytes, // Attach the draw buffer to the LVGL display.
                         LV_DISPLAY_RENDER_MODE_PARTIAL);           // Use partial rendering so a small buffer can refresh the full screen.

  lv_indev_t* pointer = lv_indev_create();                          // Create one LVGL input device for touch.
  lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);                // Mark the input device as a pointer/touchscreen.
  lv_indev_set_read_cb(pointer, touchRead);                         // Register the touch-read callback.

  ui_init();                                                        // Build the UI objects from the files in src/ui.
  lv_obj_add_event_cb(ui_Slider2, sliderChanged, LV_EVENT_VALUE_CHANGED, nullptr); // Print slider changes from the generated UI.
  return true;                                                      // Report that LVGL and the UI are ready.
}                                                                   // End initializeLvgl().
}                                                                   // End the private namespace.

void setup() {                                                      // Run this function once after reset or upload.
  Serial.begin(kSerialBaud);                                        // Start the USB serial port.
  delay(800);                                                       // Give the Serial Monitor time to reconnect.
  Serial.println();                                                 // Print a blank line before the lesson title.
  Serial.println("=== Lesson03: Display, LVGL 9.1 and Touch ===");  // Print the lesson title.

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);                             // Start I2C on the watch schematic pins for the touch controller.

  if (!initializeDisplayHardware()) {                               // Initialize the LCD and stop if it fails.
    while (true) {                                                  // Stay in an error loop so the failure remains visible on serial.
      delay(1000);                                                  // Sleep inside the error loop to avoid busy-spinning.
    }                                                               // End the error loop body.
  }                                                                 // End the LCD initialization check.

  if (!touchController.begin(Wire, PIN_TOUCH_RST, PIN_TOUCH_INT,    // Reset and identify the AXS5106L touch controller.
                             DISPLAY_WIDTH, DISPLAY_HEIGHT, 0)) {   // Pass the display geometry and rotation to the touch driver.
    Serial.println("ERROR: AXS5106L initialization failed");        // Print a warning while still allowing the display lesson to continue.
  }                                                                 // End the touch initialization check.

  if (!initializeLvgl()) {                                          // Initialize LVGL and stop if it fails.
    while (true) {                                                  // Stay in an error loop so the failure remains visible on serial.
      delay(1000);                                                  // Sleep inside the error loop to avoid busy-spinning.
    }                                                               // End the error loop body.
  }                                                                 // End the LVGL initialization check.

  lastTickMs = millis();                                            // Initialize the LVGL tick timestamp.
  analogWrite(PIN_LCD_BACKLIGHT, 255);                              // Turn the LCD backlight fully on after the UI is ready.
  Serial.println("Display and touch are ready");                    // Print that the display lesson has reached the running state.
}                                                                   // End setup().

void loop() {                                                       // Run this function repeatedly after setup() finishes.
  const uint32_t now = millis();                                    // Read the current Arduino millisecond counter.
  lv_tick_inc(now - lastTickMs);                                    // Tell LVGL how many milliseconds have elapsed since the last loop.
  lastTickMs = now;                                                 // Store the current time for the next tick calculation.
  lv_timer_handler();                                               // Let LVGL process animations, input events, and redraw work.
  delay(5);                                                        // Give background tasks a short time slice.
}                                                                   // End loop().
