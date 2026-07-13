#include <Arduino.h>  // Load the Arduino core API for Serial, delay(), and basic types.
#include <RTClib.h>   // Load RTClib so the PCF8563 real-time clock can be read.
#include <Wire.h>     // Load the I2C API used by the RTC chip.

namespace {                                                   // Keep lesson constants and helpers private to this file.
constexpr uint8_t kI2cSdaPin = 4;                             // Use GPIO4 as the watch I2C SDA pin.
constexpr uint8_t kI2cSclPin = 3;                             // Use GPIO3 as the watch I2C SCL pin.
constexpr uint8_t kRtcAddress = 0x51;                         // Use the PCF8563 I2C address.
constexpr uint8_t kRtcSecondsRegister = 0x02;                 // Use the seconds register because it contains the voltage-low flag.
constexpr uint32_t kSerialBaud = 115200;                      // Use 115200 baud for the Arduino Serial Monitor.

RTC_PCF8563 rtc;                                              // Create the RTClib PCF8563 driver object.

bool readVoltageLowFlag(bool& voltageLow) {                   // Read the PCF8563 voltage-low flag into the output reference.
  Wire.beginTransmission(kRtcAddress);                        // Start an I2C transaction with the RTC.
  Wire.write(kRtcSecondsRegister);                            // Select the seconds register before reading it.
  if (Wire.endTransmission(false) != 0) {                      // End the write phase while keeping the bus active for a repeated start.
    return false;                                             // Report failure if the RTC did not acknowledge.
  }                                                           // End the register-select check.

  if (Wire.requestFrom(kRtcAddress, static_cast<uint8_t>(1)) != 1) { // Request exactly one byte from the seconds register.
    return false;                                             // Report failure if one byte was not returned.
  }                                                           // End the read-length check.

  voltageLow = (Wire.read() & 0x80U) != 0;                     // Extract bit 7, which is the PCF8563 voltage-low flag.
  return true;                                                // Report that the flag was read successfully.
}                                                             // End readVoltageLowFlag().

void printRtcTime() {                                         // Read and print the current RTC time.
  const DateTime now = rtc.now();                             // Ask RTClib for the current date and time.
  bool voltageLow = false;                                    // Prepare a variable for the raw voltage-low flag.
  const bool flagRead = readVoltageLowFlag(voltageLow);       // Read the voltage-low flag directly from the RTC register.

  Serial.printf("RTC: %04u-%02u-%02u %02u:%02u:%02u weekday=%u unix=%lu VL=%s\n", // Format one complete RTC report line.
                now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second(), // Print calendar and clock fields.
                now.dayOfTheWeek(), static_cast<unsigned long>(now.unixtime()), // Print weekday and Unix timestamp.
                flagRead ? (voltageLow ? "1" : "0") : "READ_ERROR"); // Print the voltage-low flag or a read-error label.
}                                                             // End printRtcTime().
}                                                             // End the private namespace.

void setup() {                                                // Run this function once after reset or upload.
  Serial.begin(kSerialBaud);                                  // Start the USB serial port.
  delay(800);                                                 // Give the Serial Monitor time to reconnect.
  Serial.println();                                           // Print a blank line before the lesson title.
  Serial.println("=== Lesson05: PCF8563 RTC Basics ===");     // Print the lesson title.

  Wire.begin(kI2cSdaPin, kI2cSclPin);                         // Start I2C on the watch schematic pins instead of Arduino defaults.
  if (!rtc.begin(&Wire)) {                                    // Try to initialize the PCF8563 through RTClib.
    Serial.println("ERROR: PCF8563 was not detected at address 0x51"); // Print an RTC detection error.
    while (true) {                                            // Stop the program so the hardware fault is obvious.
      delay(1000);                                            // Sleep inside the error loop to avoid busy-spinning.
    }                                                         // End the error loop body.
  }                                                           // End the RTC initialization check.

  Serial.println("PCF8563 initialization passed");            // Print that the RTC is ready.
}                                                             // End setup().

void loop() {                                                 // Run this function repeatedly after setup() finishes.
  printRtcTime();                                             // Print the current RTC time and voltage-low status.
  delay(1000);                                                // Wait one second between RTC reports.
}                                                             // End loop().
