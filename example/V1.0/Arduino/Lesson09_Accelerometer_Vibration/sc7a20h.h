#pragma once                                                        // Compile this header only once per translation unit.

#include <Arduino.h>                                                // Load Arduino types such as uint8_t and int16_t.
#include <Wire.h>                                                   // Load the TwoWire I2C class used by the accelerometer.

class Sc7a20h {                                                     // Define a small local driver for the SC7A20H accelerometer.
 public:                                                           // Expose the functions used by the lesson sketch.
  bool begin(TwoWire& wire, uint8_t address = 0x19);                // Initialize the sensor on the selected I2C bus and address.
  bool readAcceleration(float& xMetersPerSecondSquared,             // Read acceleration on the X axis in m/s^2.
                        float& yMetersPerSecondSquared,             // Read acceleration on the Y axis in m/s^2.
                        float& zMetersPerSecondSquared);            // Read acceleration on the Z axis in m/s^2.

 private:                                                          // Keep raw register helpers hidden from the lesson sketch.
  bool writeRegister(uint8_t registerAddress, uint8_t value);       // Write one byte to one sensor register.
  bool readRegisters(uint8_t registerAddress, uint8_t* data, size_t length); // Read a block of bytes from sensor registers.
  static int16_t decode12Bit(uint8_t lowByte, uint8_t highByte);    // Convert two output bytes into one signed 12-bit sample.

  TwoWire* wire_ = nullptr;                                         // Store the I2C bus pointer after begin() succeeds.
  uint8_t address_ = 0;                                             // Store the active SC7A20H I2C address.
};                                                                  // End the Sc7a20h class.
