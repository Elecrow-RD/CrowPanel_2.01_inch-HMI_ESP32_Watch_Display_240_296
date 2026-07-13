#include <Arduino.h>  // Load the Arduino core API for Serial, GPIO, delay(), and millis().
#include <ESP_I2S.h>  // Load the ESP32 Arduino I2S helper class.
#include <math.h>     // Load sinf() and PI for sine-wave generation.

namespace {                                                // Keep lesson constants, buffers, and helpers private to this file.
constexpr uint8_t kAudioEnablePin = 48;                    // Use GPIO48 to enable or mute the speaker amplifier.
constexpr uint8_t kI2sDataPin = 41;                        // Use GPIO41 as the I2S speaker data output.
constexpr uint8_t kI2sBclkPin = 42;                        // Use GPIO42 as the I2S bit clock.
constexpr uint8_t kI2sLrclkPin = 46;                       // Use GPIO46 as the I2S left/right word clock.
constexpr uint8_t kPeripheralPowerPin = 47;                // Use GPIO47 to enable the shared peripheral power rail.
constexpr uint32_t kSerialBaud = 115200;                   // Use 115200 baud for the Arduino Serial Monitor.
constexpr uint32_t kSampleRate = 16000;                    // Generate audio at 16 kHz.
constexpr float kToneFrequency = 440.0f;                   // Generate an A4 test tone at 440 Hz.
constexpr size_t kSamplesPerBuffer = 256;                  // Write audio in 256-sample blocks.
constexpr uint32_t kToneDurationMs = 1500;                 // Play each tone for 1.5 seconds.

I2SClass speaker;                                          // Create the I2S speaker output object.
int16_t sampleBuffer[kSamplesPerBuffer] = {};              // Allocate one PCM sample buffer.
float phase = 0.0f;                                        // Store the sine-wave phase between buffer fills.

void fillToneBuffer() {                                    // Fill the PCM buffer with one block of sine-wave samples.
  const float phaseStep = 2.0f * PI * kToneFrequency / kSampleRate; // Calculate phase advance for one sample.
  for (size_t index = 0; index < kSamplesPerBuffer; ++index) { // Fill every sample slot in the buffer.
    sampleBuffer[index] = static_cast<int16_t>(sinf(phase) * 9000.0f); // Convert the sine value into a 16-bit PCM sample.
    phase += phaseStep;                                    // Advance the waveform phase for the next sample.
    if (phase >= 2.0f * PI) {                              // Check whether the phase passed one full cycle.
      phase -= 2.0f * PI;                                  // Wrap phase back into the 0 to 2*PI range.
    }                                                       // End the phase-wrap check.
  }                                                         // End the sample-fill loop.
}                                                           // End fillToneBuffer().

void playTestTone() {                                      // Play one 440 Hz test tone through the speaker.
  Serial.println("SPEAKER: PLAY_START frequency=440Hz duration=1500ms"); // Print that playback is starting.
  digitalWrite(kAudioEnablePin, LOW);                      // Enable the amplifier because this board uses active-low audio enable.
  delay(20);                                                // Wait briefly for the amplifier to turn on cleanly.

  const uint32_t startedAt = millis();                      // Remember when playback started.
  size_t totalBytes = 0;                                    // Count how many bytes are written to the I2S driver.
  while (millis() - startedAt < kToneDurationMs) {          // Keep writing audio until the requested duration expires.
    fillToneBuffer();                                       // Generate the next PCM block.
    totalBytes += speaker.write(reinterpret_cast<uint8_t*>(sampleBuffer), sizeof(sampleBuffer)); // Write the PCM block to I2S.
  }                                                         // End the playback loop.

  digitalWrite(kAudioEnablePin, HIGH);                      // Disable the amplifier after playback.
  Serial.printf("SPEAKER: PLAY_END bytes=%u\n", static_cast<unsigned>(totalBytes)); // Print how many bytes were written.
}                                                           // End playTestTone().
}                                                           // End the private namespace.

void setup() {                                              // Run this function once after reset or upload.
  Serial.begin(kSerialBaud);                                // Start the USB serial port.
  delay(800);                                               // Give the Serial Monitor time to reconnect.

  pinMode(kPeripheralPowerPin, OUTPUT);                     // Configure the shared peripheral power rail as an output.
  digitalWrite(kPeripheralPowerPin, HIGH);                  // Enable the rail used by the audio circuit.
  pinMode(kAudioEnablePin, OUTPUT);                         // Configure the amplifier enable pin as an output.
  digitalWrite(kAudioEnablePin, HIGH);                      // Keep the amplifier disabled before I2S is ready.

  speaker.setPins(kI2sBclkPin, kI2sLrclkPin, kI2sDataPin);  // Assign I2S pins in BCLK, LRCLK, DOUT order.
  if (!speaker.begin(I2S_MODE_STD, kSampleRate, I2S_DATA_BIT_WIDTH_16BIT, // Start standard I2S output at 16 kHz and 16 bits.
                     I2S_SLOT_MODE_MONO)) {                 // Use mono audio output.
    Serial.println("ERROR: I2S speaker initialization failed"); // Print an I2S initialization error.
    while (true) {                                          // Stop the program so the hardware fault remains visible.
      delay(1000);                                          // Sleep inside the error loop to avoid busy-spinning.
    }                                                       // End the error loop body.
  }                                                         // End the speaker initialization check.

  Serial.println();                                         // Print a blank line before the lesson title.
  Serial.println("=== Lesson12: I2S Speaker Playback ==="); // Print the lesson title.
}                                                           // End setup().

void loop() {                                               // Run this function repeatedly after setup() finishes.
  playTestTone();                                           // Play one generated tone.
  delay(3000);                                              // Wait three seconds before playing the next tone.
}                                                           // End loop().
