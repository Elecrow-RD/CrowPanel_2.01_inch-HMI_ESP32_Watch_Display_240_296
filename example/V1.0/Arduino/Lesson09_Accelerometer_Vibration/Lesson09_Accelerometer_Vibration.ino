#include <Arduino.h>  // Load the Arduino core API for Serial, delay(), and millis().
#include <Wire.h>     // Load the I2C API used by the SC7A20H accelerometer.
#include <math.h>     // Load sqrtf() and fabsf() for motion calculation.

#include "sc7a20h.h"  // Load the local SC7A20H register driver.

namespace {                                           // Keep lesson constants and state private to this file.
constexpr uint8_t kI2cSdaPin = 4;                     // Use GPIO4 as the watch I2C SDA pin.
constexpr uint8_t kI2cSclPin = 3;                     // Use GPIO3 as the watch I2C SCL pin.
constexpr uint8_t kSensorAddress = 0x19;              // Use the validated SC7A20H I2C address.
constexpr uint32_t kSerialBaud = 115200;              // Use 115200 baud for the Arduino Serial Monitor.
constexpr float kMotionDeltaThreshold = 1.5f;         // Treat magnitude changes above this value as motion.

Sc7a20h accelerometer;                                // Create the local accelerometer driver object.
float previousMagnitude = 0.0f;                       // Store the previous acceleration vector magnitude.
bool firstSample = true;                              // Mark that no previous sample exists yet.
}                                                     // End the private namespace.

void setup() {                                        // Run this function once after reset or upload.
  Serial.begin(kSerialBaud);                          // Start the USB serial port.
  delay(800);                                         // Give the Serial Monitor time to reconnect.
  Serial.println();                                   // Print a blank line before the lesson title.
  Serial.println("=== Lesson09: SC7A20HTR Acceleration ==="); // Print the lesson title.

  Wire.begin(kI2cSdaPin, kI2cSclPin);                 // Start I2C on the watch schematic pins.
  if (!accelerometer.begin(Wire, kSensorAddress)) {   // Initialize the accelerometer at address 0x19.
    Serial.println("ERROR: SC7A20HTR initialization failed"); // Print a sensor initialization error.
    while (true) {                                    // Stop the program so the hardware fault remains visible.
      delay(1000);                                    // Sleep inside the error loop to avoid busy-spinning.
    }                                                 // End the error loop body.
  }                                                   // End the accelerometer initialization check.

  Serial.println("SC7A20HTR initialization passed");  // Print that the accelerometer is ready.
}                                                     // End setup().

void loop() {                                         // Run this function repeatedly after setup() finishes.
  float x = 0.0f;                                     // Prepare a variable for X-axis acceleration.
  float y = 0.0f;                                     // Prepare a variable for Y-axis acceleration.
  float z = 0.0f;                                     // Prepare a variable for Z-axis acceleration.
  if (!accelerometer.readAcceleration(x, y, z)) {     // Try to read one acceleration sample from the sensor.
    Serial.println("ERROR: SC7A20HTR sample read failed"); // Print a sample-read error.
    delay(500);                                       // Wait before trying another sample.
    return;                                           // Skip the rest of this loop iteration.
  }                                                   // End the sample-read check.

  const float magnitude = sqrtf(x * x + y * y + z * z); // Calculate the total acceleration vector magnitude.
  const float delta = firstSample ? 0.0f : fabsf(magnitude - previousMagnitude); // Compare the current magnitude with the previous one.
  const bool motionDetected = !firstSample && delta >= kMotionDeltaThreshold; // Decide whether the change is large enough to count as motion.

  Serial.printf("ACCEL: X=%.3f Y=%.3f Z=%.3f m/s2 magnitude=%.3f delta=%.3f event=%s\n", // Print one acceleration report line.
                x, y, z, magnitude, delta, motionDetected ? "MOTION" : "STABLE"); // Include axis values, magnitude, delta, and event label.

  previousMagnitude = magnitude;                         // Save the current magnitude for the next loop iteration.
  firstSample = false;                                   // Mark that future loop iterations have a previous sample.
  delay(500);                                            // Wait half a second between acceleration samples.
}                                                        // End loop().
