#include <Arduino.h>      // Load the Arduino core API for Serial, delay(), and GPIO constants.
#include <EncoderTool.h>  // Load EncoderTool so the EC04 rotary signals can be decoded.
#include <OneButton.h>    // Load OneButton so the encoder push button can be debounced.

namespace {                                            // Keep lesson constants and callbacks private to this file.
constexpr uint8_t kEncoderPinA = 20;                   // Use GPIO20 for encoder phase A.
constexpr uint8_t kEncoderPinB = 19;                   // Use GPIO19 for encoder phase B.
constexpr uint8_t kEncoderButtonPin = 18;              // Use GPIO18 for the active-low encoder push button.
constexpr uint32_t kSerialBaud = 115200;               // Use 115200 baud for the Arduino Serial Monitor.

EncoderTool::Encoder encoder;                          // Create the encoder decoder object.
OneButton encoderButton(kEncoderButtonPin, true, true); // Create a debounced button object with active-low input and pull-up.

void onButtonPressed() {                               // Run this callback when the encoder button is first pressed.
  Serial.println("ENCODER_BUTTON: PRESSED");           // Print the immediate encoder-button press event.
}                                                      // End the press callback.

void onButtonClicked() {                               // Run this callback when one short encoder-button click is complete.
  Serial.println("ENCODER_BUTTON: CLICK");             // Print the encoder-button click event.
}                                                      // End the click callback.

void onButtonDoubleClicked() {                         // Run this callback when two short clicks are detected.
  Serial.println("ENCODER_BUTTON: DOUBLE_CLICK");      // Print the encoder-button double-click event.
}                                                      // End the double-click callback.

void onButtonLongPressed() {                           // Run this callback when the encoder button is held long enough.
  Serial.println("ENCODER_BUTTON: LONG_PRESS_START");  // Print the encoder-button long-press-start event.
}                                                      // End the long-press callback.
}                                                      // End the private namespace.

void setup() {                                         // Run this function once after reset or upload.
  Serial.begin(kSerialBaud);                           // Start the USB serial port.
  delay(800);                                          // Give the Serial Monitor time to connect.

  encoder.begin(kEncoderPinA, kEncoderPinB);           // Start decoding the encoder A/B phase pins.
  encoderButton.setDebounceMs(30);                     // Ignore button contact bounce shorter than 30 ms.
  encoderButton.attachPress(onButtonPressed);          // Register the immediate press callback.
  encoderButton.attachClick(onButtonClicked);          // Register the single-click callback.
  encoderButton.attachDoubleClick(onButtonDoubleClicked); // Register the double-click callback.
  encoderButton.attachLongPressStart(onButtonLongPressed); // Register the long-press-start callback.

  Serial.println();                                    // Print a blank line before the lesson title.
  Serial.println("=== Lesson04: Rotary Encoder ===");  // Print the lesson title.
  Serial.println("Rotate the encoder or press its button."); // Explain the two interactions used in this lesson.
}                                                      // End setup().

void loop() {                                          // Run this function repeatedly after setup() finishes.
  encoderButton.tick();                                // Let OneButton sample GPIO18 and dispatch button events.

  if (encoder.valueChanged()) {                        // Check whether the encoder count changed since the previous loop.
    const long value = encoder.getValue();             // Read the current accumulated encoder count.
    static long previousValue = 0;                     // Remember the previous value so direction can be inferred.
    const char* direction = value > previousValue ? "CLOCKWISE" : "COUNTERCLOCKWISE"; // Choose a readable direction label.
    Serial.printf("ENCODER: direction=%s value=%ld\n", direction, value); // Print the direction and count.
    previousValue = value;                             // Save the current value for the next comparison.
  }                                                    // End the encoder-change check.

  delay(5);                                            // Pause briefly so the loop does not busy-spin.
}                                                      // End loop().
