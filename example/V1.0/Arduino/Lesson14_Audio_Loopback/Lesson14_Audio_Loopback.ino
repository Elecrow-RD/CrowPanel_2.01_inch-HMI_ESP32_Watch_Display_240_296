#include <Arduino.h>       // Load the Arduino core API for Serial, GPIO, delay(), and memory helpers.
#include <ESP_I2S.h>       // Load the ESP32 Arduino I2S helper class for microphone and speaker streams.
#include <esp32-hal-psram.h> // Load ps_malloc() and psramFound() for OPI PSRAM buffering.

namespace {                                                // Keep lesson constants, objects, and helpers private to this file.
constexpr uint8_t kMicrophoneClockPin = 12;                // Use GPIO12 as the PDM microphone clock pin.
constexpr uint8_t kMicrophoneDataPin = 11;                 // Use GPIO11 as the PDM microphone data pin.
constexpr uint8_t kAudioEnablePin = 48;                    // Use GPIO48 to enable or mute the speaker amplifier.
constexpr uint8_t kI2sDataPin = 41;                        // Use GPIO41 as the I2S speaker data output.
constexpr uint8_t kI2sBclkPin = 42;                        // Use GPIO42 as the I2S bit clock.
constexpr uint8_t kI2sLrclkPin = 46;                       // Use GPIO46 as the I2S left/right word clock.
constexpr uint8_t kPeripheralPowerPin = 47;                // Use GPIO47 to enable the shared peripheral power rail.
constexpr uint32_t kSerialBaud = 115200;                   // Use 115200 baud for the Arduino Serial Monitor.
constexpr uint32_t kSampleRate = 16000;                    // Record and play audio at 16 kHz.
constexpr uint32_t kRecordSeconds = 5;                     // Record five seconds before playback.
constexpr size_t kWavHeaderBytes = 44;                     // Skip the standard 44-byte WAV header before PCM processing.
constexpr float kMaximumGain = 8.0f;                       // Limit automatic gain so quiet recordings are not over-amplified.

I2SClass microphone;                                       // Create the I2S microphone input object.
I2SClass speaker;                                          // Create the I2S speaker output object.

int16_t clampSample(float value) {                         // Clamp a floating-point sample into signed 16-bit PCM range.
  if (value > 32767.0f) {                                  // Check whether the amplified value exceeds the positive PCM limit.
    return 32767;                                          // Return the maximum signed 16-bit value.
  }                                                        // End the positive-limit check.
  if (value < -32768.0f) {                                 // Check whether the amplified value exceeds the negative PCM limit.
    return -32768;                                         // Return the minimum signed 16-bit value.
  }                                                        // End the negative-limit check.
  return static_cast<int16_t>(value);                      // Return the value converted to a signed 16-bit sample.
}                                                          // End clampSample().

void recordAndPlay() {                                     // Record audio, normalize it in PSRAM, and play it back.
  size_t wavBytes = 0;                                     // Prepare a variable to receive the recorded WAV byte count.
  Serial.println("AUDIO_LOOPBACK: RECORD_START duration=5s"); // Print that recording is starting.
  uint8_t* wavData = microphone.recordWAV(kRecordSeconds, &wavBytes); // Record a WAV buffer from the PDM microphone.
  if (wavData == nullptr || wavBytes <= kWavHeaderBytes) { // Check whether recording failed or returned no PCM data.
    Serial.println("ERROR: Recording failed");             // Print a recording error.
    free(wavData);                                         // Free the buffer safely even when it is null.
    return;                                                // Leave the function because no audio can be processed.
  }                                                        // End the recording failure check.

  const size_t pcmBytes = wavBytes - kWavHeaderBytes;      // Calculate the number of bytes that contain PCM samples.
  const size_t sampleCount = pcmBytes / sizeof(int16_t);   // Calculate the number of 16-bit PCM samples.
  const int16_t* inputSamples = reinterpret_cast<const int16_t*>(wavData + kWavHeaderBytes); // Point to the recorded PCM data.
  int16_t* outputSamples = static_cast<int16_t*>(ps_malloc(pcmBytes)); // Allocate a processed copy in OPI PSRAM.
  if (outputSamples == nullptr) {                          // Check whether PSRAM allocation succeeded.
    Serial.println("ERROR: PSRAM output buffer allocation failed"); // Print a PSRAM allocation error.
    free(wavData);                                         // Release the original WAV buffer.
    return;                                                // Leave the function because playback data cannot be stored.
  }                                                        // End the PSRAM allocation check.

  int32_t peak = 0;                                        // Store the largest absolute sample in the recording.
  for (size_t index = 0; index < sampleCount; ++index) {   // Visit every recorded sample.
    const int32_t sample = inputSamples[index];             // Read one signed sample as a wider integer.
    const int32_t absoluteValue = sample < 0 ? -sample : sample; // Convert the sample to an absolute value.
    if (absoluteValue > peak) {                             // Check whether this sample is the largest so far.
      peak = absoluteValue;                                 // Save the new peak value.
    }                                                       // End the peak update check.
  }                                                         // End the peak-scan loop.

  float gain = 1.0f;                                        // Start with unity gain.
  if (peak > 0) {                                           // Only calculate normalization gain when the recording is not silent.
    gain = 30000.0f / peak;                                 // Choose a gain that moves the peak near 30000 counts.
    if (gain > kMaximumGain) {                              // Check whether the calculated gain is too large.
      gain = kMaximumGain;                                  // Limit gain to avoid excessive noise amplification.
    }                                                       // End the maximum-gain check.
  }                                                         // End the gain calculation.

  for (size_t index = 0; index < sampleCount; ++index) {    // Visit every sample again for gain processing.
    outputSamples[index] = clampSample(inputSamples[index] * gain); // Apply gain and clamp to valid PCM range.
  }                                                         // End the gain-processing loop.

  Serial.printf("AUDIO_LOOPBACK: wav_bytes=%u pcm_bytes=%u peak=%ld gain=%.2f\n", // Print recording and processing statistics.
                static_cast<unsigned>(wavBytes), static_cast<unsigned>(pcmBytes), // Include WAV and PCM byte counts.
                static_cast<long>(peak), gain);                   // Include peak level and applied gain.
  Serial.println("AUDIO_LOOPBACK: PLAY_START");                   // Print that playback is starting.

  digitalWrite(kAudioEnablePin, LOW);                             // Enable the amplifier because this board uses active-low audio enable.
  delay(20);                                                       // Wait briefly for the amplifier to turn on cleanly.
  const size_t writtenBytes =                                     // Store how many bytes the I2S driver accepted.
      speaker.write(reinterpret_cast<uint8_t*>(outputSamples), pcmBytes); // Play the processed PCM buffer through I2S.
  digitalWrite(kAudioEnablePin, HIGH);                            // Disable the amplifier after playback.
  Serial.printf("AUDIO_LOOPBACK: PLAY_END bytes=%u\n", static_cast<unsigned>(writtenBytes)); // Print the playback byte count.

  free(outputSamples);                                            // Release the PSRAM output buffer.
  free(wavData);                                                  // Release the WAV buffer allocated by recordWAV().
}                                                                 // End recordAndPlay().
}                                                                 // End the private namespace.

void setup() {                                                    // Run this function once after reset or upload.
  Serial.begin(kSerialBaud);                                      // Start the USB serial port.
  delay(800);                                                     // Give the Serial Monitor time to reconnect.

  if (!psramFound()) {                                            // Check whether OPI PSRAM is enabled in the board menu.
    Serial.println("ERROR: OPI PSRAM was not detected");          // Print a PSRAM configuration error.
    while (true) {                                                // Stop the program because this lesson needs PSRAM.
      delay(1000);                                                // Sleep inside the error loop to avoid busy-spinning.
    }                                                             // End the error loop body.
  }                                                               // End the PSRAM check.

  pinMode(kPeripheralPowerPin, OUTPUT);                           // Configure the shared peripheral power rail as an output.
  digitalWrite(kPeripheralPowerPin, HIGH);                        // Enable the rail used by the audio hardware.
  pinMode(kAudioEnablePin, OUTPUT);                               // Configure the amplifier enable pin as an output.
  digitalWrite(kAudioEnablePin, HIGH);                            // Keep the amplifier disabled before playback.

  microphone.setPinsPdmRx(kMicrophoneClockPin, kMicrophoneDataPin); // Assign the PDM microphone clock and data pins.
  if (!microphone.begin(I2S_MODE_PDM_RX, kSampleRate, I2S_DATA_BIT_WIDTH_16BIT, // Start PDM receive mode at 16 kHz and 16 bits.
                        I2S_SLOT_MODE_MONO, I2S_STD_SLOT_LEFT)) { // Use mono input on the left slot.
    Serial.println("ERROR: PDM microphone initialization failed"); // Print a microphone initialization error.
    while (true) {                                                // Stop the program so the hardware fault remains visible.
      delay(1000);                                                // Sleep inside the error loop to avoid busy-spinning.
    }                                                             // End the error loop body.
  }                                                               // End the microphone initialization check.

  speaker.setPins(kI2sBclkPin, kI2sLrclkPin, kI2sDataPin);        // Assign speaker pins in BCLK, LRCLK, DOUT order.
  if (!speaker.begin(I2S_MODE_STD, kSampleRate, I2S_DATA_BIT_WIDTH_16BIT, // Start standard I2S output at 16 kHz and 16 bits.
                     I2S_SLOT_MODE_MONO)) {                       // Use mono playback.
    Serial.println("ERROR: I2S speaker initialization failed");    // Print a speaker initialization error.
    while (true) {                                                // Stop the program so the hardware fault remains visible.
      delay(1000);                                                // Sleep inside the error loop to avoid busy-spinning.
    }                                                             // End the error loop body.
  }                                                               // End the speaker initialization check.

  Serial.println();                                               // Print a blank line before the lesson title.
  Serial.println("=== Lesson14: Five-Second Audio Loopback ===");  // Print the lesson title.
  Serial.printf("PSRAM size: %u bytes\n", ESP.getPsramSize());    // Print the detected PSRAM size.
}                                                                 // End setup().

void loop() {                                                     // Run this function repeatedly after setup() finishes.
  recordAndPlay();                                                // Record, process, and play one audio clip.
  delay(5000);                                                    // Wait five seconds before the next loopback cycle.
}                                                                 // End loop().
