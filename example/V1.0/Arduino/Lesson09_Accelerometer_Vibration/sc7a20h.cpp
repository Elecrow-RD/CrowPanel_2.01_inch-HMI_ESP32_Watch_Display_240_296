#include "sc7a20h.h"                                      // Load the class declaration for the local accelerometer driver.

namespace {                                                // Keep register constants private to this source file.
constexpr uint8_t kWhoAmIRegister = 0x0F;                  // Select the sensor identity register.
constexpr uint8_t kExpectedChipId = 0x11;                  // Expect the SC7A20H WHO_AM_I value used by the reference hardware.
constexpr uint8_t kControlRegister1 = 0x20;                // Select CTRL_REG1 for output data rate and axis enables.
constexpr uint8_t kControlRegister2 = 0x21;                // Select CTRL_REG2 for high-pass filter settings.
constexpr uint8_t kControlRegister3 = 0x22;                // Select CTRL_REG3 for interrupt routing settings.
constexpr uint8_t kControlRegister4 = 0x23;                // Select CTRL_REG4 for range and high-resolution settings.
constexpr uint8_t kControlRegister5 = 0x24;                // Select CTRL_REG5 for reboot and latch settings.
constexpr uint8_t kOutputXLowRegister = 0x28;              // Select the first output register in the X/Y/Z data block.
constexpr float kMetersPerSecondSquaredPerDigit = 0.001f * 9.80665f; // Convert 1 mg digits to m/s^2.
}                                                          // End the private namespace.

bool Sc7a20h::writeRegister(uint8_t registerAddress, uint8_t value) { // Write one register over I2C.
  wire_->beginTransmission(address_);                       // Start an I2C write transaction to the sensor.
  wire_->write(registerAddress);                            // Send the target register address.
  wire_->write(value);                                      // Send the byte that should be stored in the register.
  return wire_->endTransmission() == 0;                     // Return true only when the sensor acknowledges the write.
}                                                          // End writeRegister().

bool Sc7a20h::readRegisters(uint8_t registerAddress, uint8_t* data, size_t length) { // Read a contiguous register block.
  wire_->beginTransmission(address_);                       // Start an I2C write transaction to choose the first register.
  wire_->write(registerAddress | 0x80U);                    // Set the auto-increment bit so multiple registers can be read.
  if (wire_->endTransmission(false) != 0) {                 // End the write phase with a repeated-start request.
    return false;                                           // Report failure if the sensor did not acknowledge.
  }                                                         // End the register-select check.

  if (wire_->requestFrom(address_, length) != length) {     // Request the required number of bytes from the sensor.
    return false;                                           // Report failure if the read was shorter than expected.
  }                                                         // End the read-length check.

  return wire_->readBytes(data, length) == length;          // Copy the bytes into the caller buffer and report success.
}                                                          // End readRegisters().

int16_t Sc7a20h::decode12Bit(uint8_t lowByte, uint8_t highByte) { // Convert the SC7A20H output format to a signed integer.
  return static_cast<int16_t>((static_cast<int16_t>(highByte) << 8) | lowByte) >> 4; // Combine bytes and shift the left-aligned 12-bit value.
}                                                          // End decode12Bit().

bool Sc7a20h::begin(TwoWire& wire, uint8_t address) {      // Initialize and configure the accelerometer.
  wire_ = &wire;                                           // Store the I2C bus object supplied by the sketch.
  address_ = address;                                      // Store the sensor address supplied by the sketch.

  if (!writeRegister(kControlRegister5, 0x80)) {           // Reboot the sensor memory content before configuration.
    return false;                                          // Report failure if the reboot command cannot be written.
  }                                                        // End the reboot write check.
  delay(10);                                               // Wait briefly for the reboot command to complete.

  uint8_t chipId = 0;                                      // Prepare a variable for the WHO_AM_I value.
  if (!readRegisters(kWhoAmIRegister, &chipId, 1) || chipId != kExpectedChipId) { // Read and verify the sensor identity.
    return false;                                          // Report failure if the sensor is missing or not the expected chip.
  }                                                        // End the identity check.

  return writeRegister(kControlRegister1, 0x47) &&         // Enable X/Y/Z axes at 50 Hz normal mode.
         writeRegister(kControlRegister2, 0x00) &&         // Disable the high-pass filter for simple raw acceleration readings.
         writeRegister(kControlRegister3, 0x00) &&         // Disable interrupt routing because this lesson polls the sensor.
         writeRegister(kControlRegister4, 0x88);           // Enable block-data update and high-resolution +/-2 g output.
}                                                          // End begin().

bool Sc7a20h::readAcceleration(float& xMetersPerSecondSquared,
                               float& yMetersPerSecondSquared,
                               float& zMetersPerSecondSquared) { // Read acceleration values in physical units.
  uint8_t raw[6] = {};                                      // Allocate a six-byte buffer for X/Y/Z low/high output registers.
  if (!readRegisters(kOutputXLowRegister, raw, sizeof(raw))) { // Read the full acceleration output block.
    return false;                                           // Report failure if the output registers cannot be read.
  }                                                         // End the output-read check.

  const int16_t xRaw = decode12Bit(raw[0], raw[1]);         // Decode the raw X-axis sample.
  const int16_t yRaw = decode12Bit(raw[2], raw[3]);         // Decode the raw Y-axis sample.
  const int16_t zRaw = decode12Bit(raw[4], raw[5]);         // Decode the raw Z-axis sample.

  xMetersPerSecondSquared = xRaw * kMetersPerSecondSquaredPerDigit; // Convert raw X digits to m/s^2.
  yMetersPerSecondSquared = yRaw * kMetersPerSecondSquaredPerDigit; // Convert raw Y digits to m/s^2.
  zMetersPerSecondSquared = zRaw * kMetersPerSecondSquaredPerDigit; // Convert raw Z digits to m/s^2.
  return true;                                             // Report that all three axes were read successfully.
}                                                          // End readAcceleration().
