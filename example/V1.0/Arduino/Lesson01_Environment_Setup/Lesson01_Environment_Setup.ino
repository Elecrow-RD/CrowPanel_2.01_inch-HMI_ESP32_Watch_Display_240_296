#include <Arduino.h>          // Load the Arduino core API, including Serial, delay(), and ESP helper functions.
#include <esp_chip_info.h>    // Load the ESP-IDF chip information API so the sketch can read CPU details.

namespace {                                                     // Keep lesson constants private to this source file.
constexpr uint32_t kSerialBaud = 115200;                        // Use 115200 baud because it is the common Arduino Serial Monitor speed.
constexpr uint32_t kExpectedFlashBytes = 16UL * 1024UL * 1024UL; // Expect the board menu to be set to 16 MB flash.
}                                                               // End the private namespace.

void setup() {                                                  // Run this function once after reset or upload.
  Serial.begin(kSerialBaud);                                    // Start the USB serial port with the selected baud rate.
  delay(1200);                                                  // Give the host computer time to reopen the serial connection.

  esp_chip_info_t chipInfo = {};                                // Create a zero-initialized structure for chip information.
  esp_chip_info(&chipInfo);                                     // Fill the structure with CPU core count and feature data.

  const uint32_t flashBytes = ESP.getFlashChipSize();           // Read the flash size detected by the ESP32 Arduino core.
  const uint32_t psramBytes = ESP.getPsramSize();               // Read the PSRAM size detected by the ESP32 Arduino core.
  const bool flashMatches = flashBytes == kExpectedFlashBytes;  // Check whether the detected flash size matches the required 16 MB setting.
  const bool psramReady = psramFound();                         // Check whether OPI PSRAM is enabled and usable.

  Serial.println();                                             // Print a blank line to make the boot log easier to read.
  Serial.println("=== Lesson01: Environment Self-Test ===");    // Print the lesson title in the Serial Monitor.
  Serial.printf("Chip model: %s\n", ESP.getChipModel());        // Print the ESP32 chip model reported by the core.
  Serial.printf("CPU cores: %u\n", static_cast<unsigned int>(chipInfo.cores)); // Print the number of CPU cores.
  Serial.printf("CPU frequency: %lu MHz\n", static_cast<unsigned long>(ESP.getCpuFreqMHz())); // Print the configured CPU frequency.
  Serial.printf("Flash size: %lu bytes\n", static_cast<unsigned long>(flashBytes)); // Print the detected flash capacity.
  Serial.printf("PSRAM size: %lu bytes\n", static_cast<unsigned long>(psramBytes)); // Print the detected PSRAM capacity.
  Serial.printf("Flash check: %s\n", flashMatches ? "PASS" : "FAIL"); // Print PASS only when the flash menu setting is correct.
  Serial.printf("PSRAM check: %s\n", psramReady ? "PASS" : "FAIL");   // Print PASS only when OPI PSRAM is enabled.
  Serial.println(flashMatches && psramReady ? "Environment OK" : "Check board menu settings"); // Print the final environment result.
}                                                               // End setup().

void loop() {                                                   // Run this function repeatedly after setup() finishes.
  delay(1000);                                                  // Keep the sketch alive without printing repeated messages.
}                                                               // End loop().
