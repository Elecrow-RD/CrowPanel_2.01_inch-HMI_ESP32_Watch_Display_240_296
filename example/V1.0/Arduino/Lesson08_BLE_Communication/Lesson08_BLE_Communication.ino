#include <Arduino.h>    // Load the Arduino core API for Serial, delay(), String, and millis().
#include <BLEDevice.h>  // Load the ESP32 BLE device API.
#include <BLEServer.h>  // Load the ESP32 BLE server API.
#include <BLEUtils.h>   // Load BLE utility classes used by services and characteristics.

namespace {                                                            // Keep lesson constants, objects, and callbacks private to this file.
constexpr uint32_t kSerialBaud = 115200;                               // Use 115200 baud for the Arduino Serial Monitor.
const char* kDeviceName = "ESP32-Watch";                               // Set the BLE name shown in phone scanner apps.
const char* kServiceUuid = "4fafc201-1fb5-459e-8fcc-c5c9c331914b";     // Define the custom BLE service UUID.
const char* kCharacteristicUuid = "beb5483e-36e1-4688-b7f5-ea07361b26a8"; // Define the custom data characteristic UUID.

BLECharacteristic* dataCharacteristic = nullptr;                       // Store the characteristic pointer so loop() can send notifications.
volatile bool clientConnected = false;                                 // Track whether a BLE central device is connected.

class ServerCallbacks final : public BLEServerCallbacks {              // Define BLE server connection callbacks.
  void onConnect(BLEServer* server) override {                         // Run this method when a BLE client connects.
    (void)server;                                                      // Mark the server parameter as intentionally unused in this callback.
    clientConnected = true;                                            // Record that notifications can now be sent.
    Serial.println("BLE_CLIENT: CONNECTED");                           // Print the connection event.
  }                                                                    // End onConnect().

  void onDisconnect(BLEServer* server) override {                      // Run this method when a BLE client disconnects.
    clientConnected = false;                                           // Record that notifications should stop.
    Serial.println("BLE_CLIENT: DISCONNECTED");                        // Print the disconnect event.
    server->getAdvertising()->start();                                 // Restart advertising so another phone can reconnect.
    Serial.println("BLE advertising restarted");                       // Print that advertising is active again.
  }                                                                    // End onDisconnect().
};                                                                     // End the server callback class.

class CharacteristicCallbacks final : public BLECharacteristicCallbacks { // Define BLE characteristic write callbacks.
  void onWrite(BLECharacteristic* characteristic) override {            // Run this method when a phone writes data to the characteristic.
    const String value = characteristic->getValue();                    // Read the written bytes as an Arduino String.
    Serial.printf("BLE_RX_TEXT: %s\n", value.c_str());                 // Print the received data as text.
    Serial.print("BLE_RX_HEX:");                                       // Start a hexadecimal byte dump.
    for (size_t index = 0; index < value.length(); ++index) {          // Visit every byte in the received value.
      Serial.printf(" %02X", static_cast<uint8_t>(value[index]));      // Print the byte as two uppercase hexadecimal digits.
    }                                                                  // End the received-byte loop.
    Serial.println();                                                  // Finish the hexadecimal byte dump line.
  }                                                                    // End onWrite().
};                                                                     // End the characteristic callback class.
}                                                                      // End the private namespace.

void setup() {                                                         // Run this function once after reset or upload.
  Serial.begin(kSerialBaud);                                           // Start the USB serial port.
  delay(800);                                                          // Give the Serial Monitor time to reconnect.
  Serial.println();                                                    // Print a blank line before the lesson title.
  Serial.println("=== Lesson08: BLE Server Communication ===");        // Print the lesson title.

  BLEDevice::init(kDeviceName);                                        // Initialize the BLE stack and advertise this device name.
  BLEServer* server = BLEDevice::createServer();                       // Create one BLE GATT server.
  server->setCallbacks(new ServerCallbacks());                         // Attach connection and disconnection callbacks.

  BLEService* service = server->createService(kServiceUuid);           // Create the custom BLE service.
  dataCharacteristic = service->createCharacteristic(                  // Create the custom data characteristic.
      kCharacteristicUuid,                                             // Use the UUID that phone apps will read/write.
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | // Allow phones to read and write this characteristic.
          BLECharacteristic::PROPERTY_NOTIFY);                         // Allow the watch to send notifications to a connected phone.
  dataCharacteristic->setCallbacks(new CharacteristicCallbacks());     // Attach the write callback to the characteristic.
  dataCharacteristic->setValue("BLE ready");                           // Set the initial readable characteristic value.

  service->start();                                                    // Start the custom BLE service.
  BLEAdvertising* advertising = BLEDevice::getAdvertising();           // Get the BLE advertising object.
  advertising->addServiceUUID(kServiceUuid);                           // Include the service UUID in advertising data.
  advertising->start();                                                // Start advertising so a phone can discover the watch.

  Serial.printf("BLE_DEVICE_NAME: %s\n", kDeviceName);                 // Print the BLE device name.
  Serial.println("BLE advertising started");                           // Print that BLE advertising is active.
}                                                                      // End setup().

void loop() {                                                          // Run this function repeatedly after setup() finishes.
  static uint32_t lastNotifyMs = 0;                                    // Store the last notification time.
  static uint32_t sequence = 0;                                        // Store a counter so each notification is unique.

  if (clientConnected && millis() - lastNotifyMs >= 5000) {            // Send a notification every five seconds while connected.
    lastNotifyMs = millis();                                           // Save the current notification time.
    const String message = "notify:" + String(sequence++);             // Build the notification text and increment the counter.
    dataCharacteristic->setValue(message.c_str());                     // Copy the message into the BLE characteristic.
    dataCharacteristic->notify();                                      // Send the message to the connected BLE client.
    Serial.printf("BLE_TX_NOTIFY: %s\n", message.c_str());             // Print the notification text.
  }                                                                    // End the notification interval check.

  delay(20);                                                           // Give BLE background tasks time to run.
}                                                                      // End loop().
