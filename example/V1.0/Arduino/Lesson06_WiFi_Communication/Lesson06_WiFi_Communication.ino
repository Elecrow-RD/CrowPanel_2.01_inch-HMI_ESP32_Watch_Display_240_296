#include <Arduino.h>  // Load the Arduino core API for Serial, delay(), String, and millis().
#include <WiFi.h>     // Load the ESP32 WiFi API for scanning and station-mode connection.

namespace {                                          // Keep lesson constants and helpers private to this file.
constexpr uint32_t kSerialBaud = 115200;             // Use 115200 baud for the Arduino Serial Monitor.
constexpr uint32_t kConnectionTimeoutMs = 30000;     // Limit WiFi connection attempts to 30 seconds.

const char* kWifiSsid = "elecrow888";            // Store the WiFi SSID placeholder that the student can edit.
const char* kWifiPassword = "elecrow2014";    // Store the WiFi password placeholder that the student can edit.

bool credentialsConfigured() {                       // Check whether the WiFi placeholders have been replaced.
  return String(kWifiSsid) != "YOUR_WIFI_SSID";      // Return true only when the SSID is not the default placeholder.
}                                                     // End credentialsConfigured().

void scanNetworks() {                                // Scan nearby access points and print their basic information.
  Serial.println("Starting WiFi scan...");           // Print that the scan is starting.
  const int networkCount = WiFi.scanNetworks();      // Ask the ESP32 radio to scan for nearby WiFi networks.
  if (networkCount <= 0) {                           // Check whether the scan found zero networks or returned an error.
    Serial.println("No WiFi networks found");        // Print that no networks were found.
    WiFi.scanDelete();                               // Release memory used by scan results.
    return;                                          // Leave the scan function early.
  }                                                   // End the empty-scan check.

  Serial.printf("Found %d network(s)\n", networkCount); // Print the number of scanned networks.
  for (int index = 0; index < networkCount; ++index) { // Visit each scanned network by index.
    Serial.printf("%02d | SSID=%s | RSSI=%ld dBm | channel=%ld | encrypted=%s\n", // Print one formatted network row.
                  index + 1, WiFi.SSID(index).c_str(), WiFi.RSSI(index), // Print the list number, SSID, and signal strength.
                  WiFi.channel(index),                                   // Print the WiFi channel.
                  WiFi.encryptionType(index) == WIFI_AUTH_OPEN ? "NO" : "YES"); // Print whether the network is encrypted.
  }                                                   // End the scan-results loop.
  WiFi.scanDelete();                                 // Release memory used by scan results.
}                                                     // End scanNetworks().

void connectToNetwork() {                            // Connect to the configured access point when credentials are available.
  if (!credentialsConfigured()) {                     // Check whether the student has replaced the placeholder SSID.
    Serial.println("WiFi credentials are not configured. Scan-only mode is active."); // Explain why the sketch will not connect.
    return;                                          // Leave the function because there is no real network to join.
  }                                                   // End the credentials check.

  Serial.printf("Connecting to %s", kWifiSsid);       // Print the target SSID before starting the connection.
  WiFi.begin(kWifiSsid, kWifiPassword);              // Start station-mode connection with the configured credentials.
  const uint32_t startedAt = millis();               // Remember the start time so the loop can enforce a timeout.

  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < kConnectionTimeoutMs) { // Wait until connected or timed out.
    Serial.print('.');                               // Print progress while the connection attempt is running.
    delay(500);                                      // Wait half a second before checking WiFi status again.
  }                                                   // End the connection wait loop.
  Serial.println();                                  // Move to a new line after the progress dots.

  if (WiFi.status() == WL_CONNECTED) {               // Check whether the connection succeeded.
    Serial.println("WiFi connection passed");        // Print a success message.
    Serial.printf("IP address: %s\n", WiFi.localIP().toString().c_str()); // Print the IP address assigned by the router.
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());    // Print the connected signal strength.
  } else {                                           // Handle the timeout or connection failure path.
    Serial.printf("WiFi connection failed, status=%d\n", static_cast<int>(WiFi.status())); // Print the final WiFi status code.
  }                                                   // End the connection result check.
}                                                     // End connectToNetwork().
}                                                     // End the private namespace.

void setup() {                                        // Run this function once after reset or upload.
  Serial.begin(kSerialBaud);                          // Start the USB serial port.
  delay(800);                                         // Give the Serial Monitor time to reconnect.
  Serial.println();                                   // Print a blank line before the lesson title.
  Serial.println("=== Lesson06: WiFi Scan and Station Mode ==="); // Print the lesson title.

  WiFi.mode(WIFI_STA);                                // Put the ESP32 radio into station mode.
  WiFi.disconnect(true);                              // Clear any previous connection state before scanning.
  delay(200);                                         // Give the radio a short time to settle after disconnecting.
  scanNetworks();                                     // Scan and print nearby WiFi networks.
  connectToNetwork();                                 // Try to connect if the credentials have been configured.
}                                                     // End setup().

void loop() {                                         // Run this function repeatedly after setup() finishes.
  static wl_status_t previousStatus = WiFi.status();  // Store the last WiFi status seen by the loop.
  const wl_status_t currentStatus = WiFi.status();    // Read the current WiFi connection status.
  if (currentStatus != previousStatus) {              // Check whether the WiFi state changed.
    Serial.printf("WiFi status changed: %d -> %d\n",  // Print the transition when it changes.
                  static_cast<int>(previousStatus), static_cast<int>(currentStatus)); // Convert enum values to integers for printing.
    previousStatus = currentStatus;                   // Save the new status for the next comparison.
  }                                                   // End the status-change check.
  delay(500);                                         // Check the WiFi state twice per second.
}                                                     // End loop().
