#include <Arduino.h>    // Load the Arduino core API for Serial, delay(), and GPIO definitions.
#include <OneButton.h>  // Load OneButton so the BOOT key can be debounced and classified.

namespace {                                              // Keep lesson constants and callbacks private to this file.
constexpr uint8_t kBootButtonPin = 0;                    // Select GPIO0, the active-low BOOT button on the ESP32-S3 module.
constexpr uint32_t kSerialBaud = 115200;                 // Use 115200 baud for the Arduino Serial Monitor.

OneButton bootButton(kBootButtonPin, true, true);        // Create a debounced button object; true means active-low and internal pull-up.

void onBootPressed() {                                   // Run this callback as soon as OneButton detects a press.
  Serial.println("BOOT_BUTTON: PRESSED");                // Print that the BOOT key is currently pressed.
}                                                        // End the press callback.

void onBootClicked() {                                   // Run this callback when one short click is complete.
  Serial.println("BOOT_BUTTON: CLICK");                  // Print a single-click event for the BOOT key.
}                                                        // End the click callback.

void onBootDoubleClicked() {                             // Run this callback when two short clicks are detected.
  Serial.println("BOOT_BUTTON: DOUBLE_CLICK");           // Print a double-click event for the BOOT key.
}                                                        // End the double-click callback.

void onBootLongPressStarted() {                          // Run this callback when the press duration passes the long-press threshold.
  Serial.println("BOOT_BUTTON: LONG_PRESS_START");       // Print that a BOOT long press has started.
}                                                        // End the long-press-start callback.

void onBootLongPressStopped() {                          // Run this callback when the long press is released.
  Serial.println("BOOT_BUTTON: LONG_PRESS_STOP");        // Print that the BOOT long press has ended.
}                                                        // End the long-press-stop callback.
}                                                        // End the private namespace.

void setup() {                                           // Run this function once after reset or upload.
  Serial.begin(kSerialBaud);                             // Start the USB serial port.
  delay(800);                                            // Give the Serial Monitor time to connect after reset.

  bootButton.setDebounceMs(30);                          // Ignore contact bounce shorter than 30 ms.
  bootButton.setClickMs(400);                            // Treat a release within 400 ms as a click candidate.
  bootButton.setPressMs(1000);                           // Treat a press longer than 1000 ms as a long press.
  bootButton.attachPress(onBootPressed);                 // Register the immediate press callback.
  bootButton.attachClick(onBootClicked);                 // Register the single-click callback.
  bootButton.attachDoubleClick(onBootDoubleClicked);     // Register the double-click callback.
  bootButton.attachLongPressStart(onBootLongPressStarted); // Register the long-press-start callback.
  bootButton.attachLongPressStop(onBootLongPressStopped);  // Register the long-press-stop callback.

  Serial.println();                                      // Print a blank line before the lesson title.
  Serial.println("=== Lesson02: Boot Button ===");       // Print the lesson title in the Serial Monitor.
  Serial.println("Only the BOOT button is monitored in this lesson."); // Explain that Power and Reset are intentionally not printed.
}                                                        // End setup().

void loop() {                                            // Run this function repeatedly after setup() finishes.
  bootButton.tick();                                     // Let OneButton sample GPIO0 and dispatch any pending button events.
  delay(5);                                              // Pause briefly so the CPU is not busy-spinning.
}                                                        // End loop().
