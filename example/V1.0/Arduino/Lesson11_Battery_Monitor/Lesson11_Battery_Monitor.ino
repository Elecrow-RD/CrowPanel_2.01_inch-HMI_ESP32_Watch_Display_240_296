#include <Arduino.h>  // Load the Arduino core API for Serial, GPIO, ADC, delay(), and millis().

namespace {                                                     // Keep lesson constants and helpers private to this file.
constexpr uint8_t kBatteryAdcPin = 1;                           // Use GPIO1 as the battery-divider ADC input.
constexpr uint8_t kChargeStatusPin = 15;                        // Use GPIO15 to read the charger status signal.
constexpr uint8_t kPeripheralPowerPin = 47;                     // Use GPIO47 to enable the shared peripheral power rail.
constexpr uint32_t kSerialBaud = 115200;                        // Use 115200 baud for the Arduino Serial Monitor.
constexpr uint8_t kSampleCount = 20;                            // Average 20 ADC samples for a smoother voltage value.
constexpr uint16_t kSampleDelayMs = 5;                          // Wait 5 ms between ADC samples.
constexpr float kUpperResistorKiloOhms = 100.0f;                // Store the upper resistor value from the schematic divider.
constexpr float kLowerResistorKiloOhms = 137.0f;                // Store the lower resistor value from the schematic divider.
constexpr float kDividerRatio =                                 // Calculate the ADC voltage as a fraction of battery voltage.
    kLowerResistorKiloOhms / (kUpperResistorKiloOhms + kLowerResistorKiloOhms); // Use Rlower / (Rupper + Rlower).

float readBatteryVoltage(float& adcMillivolts) {                // Read the battery voltage and return the reconstructed pack voltage.
  uint32_t millivoltSum = 0;                                    // Accumulate calibrated ADC readings in millivolts.
  for (uint8_t sample = 0; sample < kSampleCount; ++sample) {   // Take the configured number of samples.
    millivoltSum += analogReadMilliVolts(kBatteryAdcPin);       // Add one calibrated ADC reading to the sum.
    delay(kSampleDelayMs);                                      // Wait briefly before the next sample.
  }                                                             // End the ADC averaging loop.

  adcMillivolts = static_cast<float>(millivoltSum) / kSampleCount; // Convert the summed readings into an average ADC millivolt value.
  return (adcMillivolts / 1000.0f) / kDividerRatio;             // Convert divider voltage back into estimated battery voltage.
}                                                               // End readBatteryVoltage().
}                                                               // End the private namespace.

void setup() {                                                  // Run this function once after reset or upload.
  Serial.begin(kSerialBaud);                                    // Start the USB serial port.
  delay(800);                                                   // Give the Serial Monitor time to reconnect.

  pinMode(kPeripheralPowerPin, OUTPUT);                         // Configure the shared peripheral power rail as an output.
  digitalWrite(kPeripheralPowerPin, HIGH);                      // Enable the rail used by the board peripheral circuits.
  pinMode(kChargeStatusPin, INPUT);                             // Configure the charger status pin as a digital input.
  analogReadResolution(12);                                     // Configure the ADC for 12-bit readings.
  analogSetAttenuation(ADC_11db);                               // Select the wide ADC range needed for the divided battery voltage.

  Serial.println();                                             // Print a blank line before the lesson title.
  Serial.println("=== Lesson11: Battery Voltage Monitor ===");  // Print the lesson title.
  Serial.println("CHRG is reported as a raw logic level only."); // Explain that the charger signal is printed directly.
}                                                               // End setup().

void loop() {                                                   // Run this function repeatedly after setup() finishes.
  float adcMillivolts = 0.0f;                                   // Prepare a variable for the measured divider voltage.
  const float batteryVoltage = readBatteryVoltage(adcMillivolts); // Read and calculate the battery voltage.
  const int chargeStatusRaw = digitalRead(kChargeStatusPin);    // Read the raw charger status logic level.

  Serial.printf("BATTERY: adc=%.0f mV voltage=%.3f V CHRG_RAW=%d\n", // Print one battery report line.
                adcMillivolts, batteryVoltage, chargeStatusRaw); // Include ADC voltage, reconstructed battery voltage, and charger status.
  delay(1000);                                                  // Wait one second between battery reports.
}                                                               // End loop().
