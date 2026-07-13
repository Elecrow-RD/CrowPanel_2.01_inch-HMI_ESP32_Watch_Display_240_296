#include <Arduino.h>  // Load the Arduino core API for Serial, delay(), String, and millis().
#include <RTClib.h>   // Load RTClib so the PCF8563 RTC can be read and adjusted.
#include <WiFi.h>     // Load the ESP32 WiFi API used to reach NTP servers.
#include <Wire.h>     // Load the I2C API used by the RTC.
#include <time.h>     // Load time functions such as configTzTime() and getLocalTime().

namespace {                                                   // Keep lesson constants and helpers private to this file.
constexpr uint8_t kI2cSdaPin = 4;                             // Use GPIO4 as the watch I2C SDA pin.
constexpr uint8_t kI2cSclPin = 3;                             // Use GPIO3 as the watch I2C SCL pin.
constexpr uint32_t kSerialBaud = 115200;                      // Use 115200 baud for the Arduino Serial Monitor.
constexpr uint32_t kWifiTimeoutMs = 30000;                    // Limit WiFi connection attempts to 30 seconds.
constexpr uint32_t kNtpTimeoutMs = 15000;                     // Limit NTP waiting time to 15 seconds.

const char* kWifiSsid = "elecrow888";                         // Store the WiFi SSID used for the network time demo.
const char* kWifiPassword = "elecrow2014";                    // Store the WiFi password used for the network time demo.
const char* kTimezone = "EST5EDT,M3.2.0,M11.1.0";             // Select the U.S. Eastern Time zone with automatic daylight saving time.
const char* kNtpServer1 = "pool.ntp.org";                     // Use the public NTP pool as the first time source.
const char* kNtpServer2 = "time.nist.gov";                    // Use a second NTP server as a fallback time source.

RTC_PCF8563 rtc;                                              // Create the RTClib PCF8563 driver object.

void printDateTime(const char* label, const DateTime& value) { // Print one labeled DateTime value.
  Serial.printf("%s: %04u-%02u-%02u %02u:%02u:%02u unix=%lu\n", // Format a readable time line.
                label, value.year(), value.month(), value.day(), value.hour(), value.minute(), // Print label and date/time fields.
                value.second(), static_cast<unsigned long>(value.unixtime())); // Print seconds and Unix timestamp.
}                                                             // End printDateTime().

bool connectWifi() {                                          // Connect the ESP32 to WiFi before requesting NTP time.
  if (String(kWifiSsid) == "YOUR_WIFI_SSID") {                // Check whether the SSID was left as a placeholder.
    Serial.println("ERROR: Configure WiFi credentials before running this lesson"); // Print a clear configuration error.
    return false;                                             // Report that WiFi cannot be used.
  }                                                           // End the placeholder check.

  WiFi.mode(WIFI_STA);                                        // Put the ESP32 radio into station mode.
  WiFi.begin(kWifiSsid, kWifiPassword);                       // Start connecting to the configured access point.
  const uint32_t startedAt = millis();                        // Remember the start time for the timeout.
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < kWifiTimeoutMs) { // Wait until connected or timed out.
    Serial.print('.');                                        // Print progress while the connection is pending.
    delay(500);                                               // Wait half a second before checking the status again.
  }                                                           // End the WiFi wait loop.
  Serial.println();                                           // Move to a new line after progress dots.

  if (WiFi.status() != WL_CONNECTED) {                        // Check whether the timeout expired before connection.
    Serial.println("ERROR: WiFi connection timed out");       // Print a WiFi timeout message.
    return false;                                             // Report that WiFi is not ready.
  }                                                           // End the connection failure check.

  Serial.printf("WiFi connected: IP=%s RSSI=%d dBm\n",        // Print connection details.
                WiFi.localIP().toString().c_str(), WiFi.RSSI()); // Print IP address and signal strength.
  return true;                                                // Report that WiFi is ready.
}                                                             // End connectWifi().

bool getNetworkTime(DateTime& networkTime) {                  // Request local time from NTP and convert it into a DateTime object.
  configTzTime(kTimezone, kNtpServer1, kNtpServer2);          // Configure timezone and NTP server addresses.

  struct tm timeInfo = {};                                    // Create a C time structure for the returned local time.
  if (!getLocalTime(&timeInfo, kNtpTimeoutMs)) {              // Wait for an NTP-synchronized local time.
    Serial.println("ERROR: NTP time was not received");       // Print a timeout message when NTP fails.
    return false;                                             // Report that no network time is available.
  }                                                           // End the NTP failure check.

  networkTime = DateTime(timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday, // Convert tm year/month/day to RTClib format.
                         timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec); // Copy hour, minute, and second into DateTime.
  return true;                                                // Report that network time was obtained.
}                                                             // End getNetworkTime().
}                                                             // End the private namespace.

void setup() {                                                // Run this function once after reset or upload.
  Serial.begin(kSerialBaud);                                  // Start the USB serial port.
  delay(800);                                                 // Give the Serial Monitor time to reconnect.
  Serial.println();                                           // Print a blank line before the lesson title.
  Serial.println("=== Lesson07: WiFi NTP to PCF8563 ===");    // Print the lesson title.

  Wire.begin(kI2cSdaPin, kI2cSclPin);                         // Start I2C on the watch schematic pins for the RTC.
  if (!rtc.begin(&Wire)) {                                    // Try to initialize the PCF8563 through RTClib.
    Serial.println("ERROR: PCF8563 initialization failed");   // Print an RTC initialization error.
    return;                                                   // Leave setup because the RTC is required for this lesson.
  }                                                           // End the RTC initialization check.

  printDateTime("RTC before sync", rtc.now());                // Print the RTC value before network synchronization.
  if (!connectWifi()) {                                       // Try to connect to WiFi.
    return;                                                   // Leave setup if WiFi cannot be connected.
  }                                                           // End the WiFi connection check.

  DateTime networkTime;                                       // Create a variable to receive the NTP time.
  if (!getNetworkTime(networkTime)) {                         // Try to obtain network time from NTP.
    return;                                                   // Leave setup if NTP time is unavailable.
  }                                                           // End the NTP check.

  printDateTime("Network time", networkTime);                 // Print the time received from NTP.
  rtc.adjust(networkTime);                                    // Write the NTP time into the external PCF8563 RTC.
  delay(100);                                                 // Wait briefly so the RTC write can settle.
  printDateTime("RTC after sync", rtc.now());                 // Read back and print the updated RTC time.
  Serial.println("RTC synchronization passed");               // Print the synchronization result.

  WiFi.disconnect(true);                                      // Disconnect from WiFi after synchronization is complete.
  WiFi.mode(WIFI_OFF);                                        // Turn off the WiFi radio to reduce power use.
}                                                             // End setup().

void loop() {                                                 // Run this function repeatedly after setup() finishes.
  static uint32_t lastPrintMs = 0;                            // Store the last time an RTC line was printed.
  if (millis() - lastPrintMs >= 1000) {                       // Print the RTC value once per second.
    lastPrintMs = millis();                                   // Save the current print time.
    printDateTime("RTC", rtc.now());                          // Print the current RTC time.
  }                                                           // End the once-per-second check.
  delay(10);                                                  // Give background tasks a small time slice.
}                                                             // End loop().
