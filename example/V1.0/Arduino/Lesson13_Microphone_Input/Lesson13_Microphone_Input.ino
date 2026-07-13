#include <Arduino.h>  // Load the Arduino core API for Serial, GPIO, delay(), and memory helpers.
#include <ESP_I2S.h>  // Load the ESP32 Arduino I2S helper class used for PDM microphone capture.
#include <math.h>     // Load sqrt() so the sketch can calculate RMS level.

namespace {                                              // Keep lesson constants, objects, and helpers private to this file.
constexpr uint8_t kMicrophoneClockPin = 12;              // Use GPIO12 as the PDM microphone clock pin.
constexpr uint8_t kMicrophoneDataPin = 11;               // Use GPIO11 as the PDM microphone data pin.
constexpr uint8_t kPeripheralPowerPin = 47;              // Use GPIO47 to enable the shared peripheral power rail.
constexpr uint32_t kSerialBaud = 115200;                 // Use 115200 baud for the Arduino Serial Monitor.
constexpr uint32_t kSampleRate = 16000;                  // Record audio at 16 kHz.
constexpr uint32_t kRecordSeconds = 1;                   // Record one second per measurement.
constexpr size_t kWavHeaderBytes = 44;                   // Skip the standard 44-byte WAV header before analyzing PCM data.

I2SClass microphone;                                     // Create the I2S microphone input object.

void analyzeRecording() {                                // Record one WAV buffer and calculate simple signal statistics.
  size_t wavBytes = 0;                                   // Prepare a variable to receive the WAV buffer size.
  uint8_t* wavData = microphone.recordWAV(kRecordSeconds, &wavBytes); // Ask the I2S helper to capture one WAV buffer.
  if (wavData == nullptr || wavBytes <= kWavHeaderBytes) { // Check whether recording failed or returned no PCM data.
    Serial.println("ERROR: Microphone recording failed"); // Print a recording error.
    free(wavData);                                       // Free the buffer safely even when it is null.
    return;                                              // Leave the analysis function early.
  }                                                       // End the recording failure check.

  const int16_t* samples = reinterpret_cast<const int16_t*>(wavData + kWavHeaderBytes); // Point to the first PCM sample after the WAV header.
  const size_t sampleCount = (wavBytes - kWavHeaderBytes) / sizeof(int16_t); // Calculate how many 16-bit samples were recorded.
  int32_t peak = 0;                                      // Store the largest absolute sample value.
  double squareSum = 0.0;                                // Accumulate squared samples for RMS calculation.

  for (size_t index = 0; index < sampleCount; ++index) { // Visit every recorded PCM sample.
    const int32_t sample = samples[index];                // Read one signed 16-bit sample as a wider integer.
    const int32_t absoluteValue = sample < 0 ? -sample : sample; // Convert the sample to an absolute value.
    if (absoluteValue > peak) {                           // Check whether this sample is the largest so far.
      peak = absoluteValue;                               // Save the new peak value.
    }                                                     // End the peak update check.
    squareSum += static_cast<double>(sample) * sample;    // Add the squared sample to the RMS sum.
  }                                                       // End the sample-analysis loop.

  const float rms = sampleCount > 0 ? sqrt(squareSum / sampleCount) : 0.0f; // Convert the mean square value into RMS amplitude.
  Serial.printf("MICROPHONE: wav_bytes=%u samples=%u peak=%ld rms=%.1f\n", // Print one microphone report line.
                static_cast<unsigned>(wavBytes), static_cast<unsigned>(sampleCount), // Include WAV size and sample count.
                static_cast<long>(peak), rms);                    // Include peak and RMS signal levels.
  free(wavData);                                                  // Release the WAV buffer allocated by recordWAV().
}                                                                 // End analyzeRecording().
}                                                                 // End the private namespace.

void setup() {                                                    // Run this function once after reset or upload.
  Serial.begin(kSerialBaud);                                      // Start the USB serial port.
  delay(800);                                                     // Give the Serial Monitor time to reconnect.

  pinMode(kPeripheralPowerPin, OUTPUT);                           // Configure the shared peripheral power rail as an output.
  digitalWrite(kPeripheralPowerPin, HIGH);                        // Enable the rail used by the microphone circuit.
  microphone.setPinsPdmRx(kMicrophoneClockPin, kMicrophoneDataPin); // Assign the PDM microphone clock and data pins.
  if (!microphone.begin(I2S_MODE_PDM_RX, kSampleRate, I2S_DATA_BIT_WIDTH_16BIT, // Start PDM receive mode at 16 kHz and 16 bits.
                        I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) { // Use mono input on the left slot.
    Serial.println("ERROR: PDM microphone initialization failed"); // Print an I2S microphone initialization error.
    while (true) {                                                // Stop the program so the hardware fault remains visible.
      delay(1000);                                                // Sleep inside the error loop to avoid busy-spinning.
    }                                                             // End the error loop body.
  }                                                               // End the microphone initialization check.

  Serial.println();                                               // Print a blank line before the lesson title.
  Serial.println("=== Lesson13: PDM Microphone Input ===");       // Print the lesson title.
}                                                                 // End setup().

void loop() {                                                     // Run this function repeatedly after setup() finishes.
  Serial.println("MICROPHONE: RECORD_START duration=1s");         // Print that a one-second recording is starting.
  analyzeRecording();                                             // Capture and analyze one microphone recording.
  delay(1000);                                                    // Wait one second before starting the next recording.
}                                                                 // End loop().
