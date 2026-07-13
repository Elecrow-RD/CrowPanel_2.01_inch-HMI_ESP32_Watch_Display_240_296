#include <Arduino.h>  // Load the Arduino core API for Serial, GPIO, delay(), and millis().

namespace {                                              // Keep lesson constants and state private to this file.
constexpr uint8_t kMotorEnablePin = 45;                  // Use GPIO45 to drive the vibration motor enable transistor.
constexpr uint8_t kPeripheralPowerPin = 47;              // Use GPIO47 to enable the shared peripheral power rail.
constexpr uint32_t kSerialBaud = 115200;                 // Use 115200 baud for the Arduino Serial Monitor.

struct MotorStep {                                       // Define one step in a vibration pattern.
  bool enabled;                                          // Store whether the motor should be on during this step.
  uint16_t durationMs;                                   // Store how long this step should last in milliseconds.
};                                                       // End the MotorStep structure.

const MotorStep kPattern[] = {                           // Define the repeating vibration pattern.
    {true, 200}, {false, 200}, {true, 200}, {false, 1000}, // Create two short pulses followed by a one-second pause.
    {true, 500}, {false, 2000},                           // Create one longer pulse followed by a two-second pause.
};                                                       // End the vibration pattern table.

size_t patternIndex = 0;                                 // Store the index of the current pattern step.
uint32_t stepStartedMs = 0;                              // Store the time when the current step started.

void applyStep(const MotorStep& step) {                  // Apply one motor pattern step to the hardware.
  digitalWrite(kMotorEnablePin, step.enabled ? HIGH : LOW); // Turn the motor transistor on or off.
  Serial.printf("MOTOR: %s duration=%u ms\n", step.enabled ? "ON" : "OFF", step.durationMs); // Print the active step.
  stepStartedMs = millis();                              // Record the start time for duration tracking.
}                                                        // End applyStep().
}                                                        // End the private namespace.

void setup() {                                           // Run this function once after reset or upload.
  Serial.begin(kSerialBaud);                             // Start the USB serial port.
  delay(800);                                            // Give the Serial Monitor time to reconnect.

  pinMode(kPeripheralPowerPin, OUTPUT);                  // Configure the shared peripheral power rail as an output.
  digitalWrite(kPeripheralPowerPin, HIGH);               // Enable the rail so the motor circuit has power.
  pinMode(kMotorEnablePin, OUTPUT);                      // Configure the motor enable pin as an output.
  digitalWrite(kMotorEnablePin, LOW);                    // Keep the motor off before the pattern starts.
  delay(50);                                             // Wait for the peripheral rail to stabilize.

  Serial.println();                                      // Print a blank line before the lesson title.
  Serial.println("=== Lesson10: Vibration Motor ===");   // Print the lesson title.
  applyStep(kPattern[patternIndex]);                     // Start the first vibration-pattern step.
}                                                        // End setup().

void loop() {                                            // Run this function repeatedly after setup() finishes.
  const MotorStep& currentStep = kPattern[patternIndex];  // Read the current pattern step.
  if (millis() - stepStartedMs >= currentStep.durationMs) { // Check whether the current step has run long enough.
    patternIndex = (patternIndex + 1) % (sizeof(kPattern) / sizeof(kPattern[0])); // Advance to the next step and wrap at the end.
    applyStep(kPattern[patternIndex]);                   // Apply the new pattern step.
  }                                                       // End the step-duration check.
  delay(5);                                              // Pause briefly so the loop does not busy-spin.
}                                                        // End loop().
