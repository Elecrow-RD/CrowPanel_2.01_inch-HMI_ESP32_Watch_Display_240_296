#include <Arduino.h>     // Load the Arduino core API for Serial, GPIO, delay(), millis(), and analogWrite().
#include <Wire.h>        // Load the I2C API so the board bus can be initialized before sleep tests.
#include <driver/gpio.h> // Load ESP-IDF GPIO wakeup functions.
#include <esp_sleep.h>   // Load ESP32 sleep and wakeup APIs.

namespace {                                                           // Keep lesson constants, state, and helpers private to this file.
constexpr uint8_t kTouchInterruptPin = 2;                             // Use GPIO2 as the AXS5106L touch interrupt wake source.
constexpr uint8_t kTouchResetPin = 5;                                 // Use GPIO5 as the AXS5106L reset pin.
constexpr uint8_t kI2cSdaPin = 4;                                     // Use GPIO4 as the watch I2C SDA pin.
constexpr uint8_t kI2cSclPin = 3;                                     // Use GPIO3 as the watch I2C SCL pin.
constexpr uint8_t kDisplayPowerPin = 40;                              // Use GPIO40 to control the LCD power rail.
constexpr uint8_t kDisplayPowerStatePin = 14;                         // Use GPIO14 to observe the display power-state signal.
constexpr uint8_t kBacklightPin = 13;                                 // Use GPIO13 as the LCD backlight PWM pin.
constexpr uint8_t kPeripheralPowerPin = 47;                           // Use GPIO47 to control the shared peripheral power rail.
constexpr uint8_t kUsbPowerDetectPin = 17;                            // Use GPIO17 to detect USB power insertion.
constexpr uint32_t kSerialBaud = 115200;                              // Use 115200 baud for the Arduino Serial Monitor.
constexpr uint64_t kTimerWakeupMicroseconds = 10ULL * 1000ULL * 1000ULL; // Wake automatically after 10 seconds.
constexpr uint32_t kAwakeTimeMs = 5000;                               // Stay awake for five seconds before entering light sleep.

int touchWakeLevel = -1;                                              // Store the armed wake level for the touch interrupt pin.
int displayPowerStateWakeLevel = -1;                                  // Store the armed wake level for the display power-state pin.
int usbWakeLevel = -1;                                                // Store the armed wake level for the USB detect pin.

const char* wakeupCauseName(esp_sleep_wakeup_cause_t cause) {         // Convert the ESP32 wakeup cause enum into readable text.
  switch (cause) {                                                    // Choose a name based on the wakeup cause.
    case ESP_SLEEP_WAKEUP_TIMER:                                      // Handle timer wakeup.
      return "TIMER";                                                 // Return the timer wakeup label.
    case ESP_SLEEP_WAKEUP_GPIO:                                       // Handle GPIO wakeup.
      return "GPIO";                                                  // Return the GPIO wakeup label.
    default:                                                          // Handle all other wakeup causes.
      return "UNDEFINED";                                             // Return a generic label for reset or unsupported causes.
  }                                                                   // End the wakeup-cause switch.
}                                                                     // End wakeupCauseName().

void resetTouchController() {                                         // Reset the touch controller before using its interrupt pin.
  pinMode(kTouchResetPin, OUTPUT);                                    // Configure the touch reset pin as an output.
  digitalWrite(kTouchResetPin, LOW);                                  // Pull reset low to restart the touch controller.
  delay(200);                                                         // Hold reset low long enough for the chip to reset.
  digitalWrite(kTouchResetPin, HIGH);                                 // Release reset so the touch controller can boot.
  delay(300);                                                         // Wait for the touch controller to become stable.
}                                                                     // End resetTouchController().

void restorePower() {                                                 // Restore board power rails after waking from sleep.
  pinMode(kPeripheralPowerPin, OUTPUT);                               // Configure the shared peripheral rail pin as an output.
  digitalWrite(kPeripheralPowerPin, HIGH);                            // Enable the shared peripheral rail.
  pinMode(kDisplayPowerPin, OUTPUT);                                  // Configure the LCD power pin as an output.
  digitalWrite(kDisplayPowerPin, LOW);                                // Enable the LCD rail because the board uses active-low LCD power.
  analogWrite(kBacklightPin, 0);                                      // Keep the backlight off in this serial-only sleep lesson.
}                                                                     // End restorePower().

bool enableOppositeLevelWake(uint8_t pin, const char* label, int& configuredWakeLevel) { // Arm a GPIO wake on the opposite level.
  pinMode(pin, INPUT);                                                // Configure the candidate wake pin as an input.
  const int currentLevel = digitalRead(pin);                          // Read the current stable logic level.
  configuredWakeLevel = currentLevel == HIGH ? LOW : HIGH;            // Store the level that will indicate a change.
  const gpio_int_type_t wakeLevel =                                   // Choose the ESP-IDF level interrupt type.
      currentLevel == HIGH ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL; // Wake when the pin moves away from its current state.
  const esp_err_t result =                                            // Store the result of enabling GPIO wakeup.
      gpio_wakeup_enable(static_cast<gpio_num_t>(pin), wakeLevel);    // Arm this GPIO as a wakeup source.

  Serial.printf("WAKE_CONFIG: %s GPIO%u current=%d wake_level=%s result=%d\n", // Print the wake configuration.
                label, static_cast<unsigned int>(pin), currentLevel, // Include the signal name, pin number, and current level.
                currentLevel == HIGH ? "LOW" : "HIGH", static_cast<int>(result)); // Include the wake level and ESP-IDF result code.
  return result == ESP_OK;                                           // Report true only when the wake source was accepted.
}                                                                     // End enableOppositeLevelWake().

bool enableUsbInsertionWake() {                                       // Arm USB insertion as a wake source when USB is currently absent.
  pinMode(kUsbPowerDetectPin, INPUT);                                 // Configure USB detect as an input.
  const int currentLevel = digitalRead(kUsbPowerDetectPin);           // Read whether USB power is already present.
  if (currentLevel == HIGH) {                                         // Check whether USB is already plugged in.
    usbWakeLevel = -1;                                                // Mark USB wake as inactive because insertion cannot be detected now.
    Serial.println("WAKE_CONFIG: USB is already connected; insertion wake is disabled"); // Explain why this wake source is skipped.
    return false;                                                     // Report that USB wake was not armed.
  }                                                                   // End the already-connected check.

  usbWakeLevel = HIGH;                                                // Store HIGH as the wake target for USB insertion.
  const esp_err_t result = gpio_wakeup_enable(                        // Store the result of enabling USB GPIO wake.
      static_cast<gpio_num_t>(kUsbPowerDetectPin), GPIO_INTR_HIGH_LEVEL); // Wake when GPIO17 becomes HIGH.
  Serial.printf("WAKE_CONFIG: USB GPIO%u current=LOW wake_level=HIGH result=%d\n", // Print the USB wake configuration.
                static_cast<unsigned int>(kUsbPowerDetectPin), static_cast<int>(result)); // Include the pin number and result code.
  return result == ESP_OK;                                           // Report true only when the USB wake source was accepted.
}                                                                     // End enableUsbInsertionWake().

void prepareWakeupSources() {                                         // Clear old wake sources and configure the next sleep interval.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);              // Disable every previous ESP sleep wakeup source.
  gpio_wakeup_disable(static_cast<gpio_num_t>(kTouchInterruptPin));   // Disable any stale touch GPIO wake setting.
  gpio_wakeup_disable(static_cast<gpio_num_t>(kDisplayPowerStatePin)); // Disable any stale display-state GPIO wake setting.
  gpio_wakeup_disable(static_cast<gpio_num_t>(kUsbPowerDetectPin));   // Disable any stale USB GPIO wake setting.

  esp_sleep_enable_timer_wakeup(kTimerWakeupMicroseconds);            // Arm the 10-second timer wake source.
  bool gpioWakeEnabled = enableOppositeLevelWake(                     // Arm touch interrupt wake and store whether it succeeded.
      kTouchInterruptPin, "TOUCH_INT", touchWakeLevel);               // Use the current touch interrupt level as the baseline.
  gpioWakeEnabled |= enableOppositeLevelWake(                         // Arm display power-state wake and combine the result.
      kDisplayPowerStatePin, "POWER_STATE", displayPowerStateWakeLevel); // Use the current display power-state level as the baseline.
  gpioWakeEnabled |= enableUsbInsertionWake();                        // Arm USB insertion wake when possible.
  if (gpioWakeEnabled) {                                              // Check whether at least one GPIO wake source was accepted.
    esp_sleep_enable_gpio_wakeup();                                   // Enable GPIO wakeup globally for light sleep.
  }                                                                   // End the global GPIO wake enable check.
}                                                                     // End prepareWakeupSources().

void printGpioWakeState(const char* label, uint8_t pin, int targetLevel) { // Print whether one GPIO matches its armed wake level.
  const int currentLevel = digitalRead(pin);                          // Read the pin level after wakeup.
  Serial.printf("WAKE_GPIO_STATE: %s GPIO%u level=%d target=%d active=%s\n", // Print a wake-state diagnostic line.
                label, static_cast<unsigned int>(pin), currentLevel, targetLevel, // Include label, pin, current level, and target level.
                targetLevel >= 0 && currentLevel == targetLevel ? "YES" : "NO"); // Mark whether this pin is likely the wake reason.
}                                                                     // End printGpioWakeState().

void printGpioWakeStates() {                                          // Print wake diagnostics for every configured GPIO source.
  printGpioWakeState("TOUCH_INT", kTouchInterruptPin, touchWakeLevel); // Print the touch interrupt wake state.
  printGpioWakeState("POWER_STATE", kDisplayPowerStatePin,            // Print the display power-state wake state.
                     displayPowerStateWakeLevel);                     // Pass the display-state target level.
  printGpioWakeState("USB_POWER_DETECT", kUsbPowerDetectPin, usbWakeLevel); // Print the USB detect wake state.
}                                                                     // End printGpioWakeStates().

void enterLightSleep() {                                              // Shut down visible loads and enter ESP32 light sleep.
  Serial.println("SLEEP: PREPARING");                                 // Print that the board is preparing for sleep.
  Serial.flush();                                                     // Wait for the serial message to leave the USB buffer.

  analogWrite(kBacklightPin, 0);                                      // Turn off the LCD backlight before sleep.
  digitalWrite(kDisplayPowerPin, HIGH);                               // Disable the LCD rail because the board uses active-low LCD power.
  digitalWrite(kPeripheralPowerPin, LOW);                             // Disable the shared peripheral rail to reduce current.
  prepareWakeupSources();                                             // Configure timer and GPIO wake sources.

  Serial.println("SLEEP: ENTER_LIGHT_SLEEP");                         // Print the final message before sleeping.
  Serial.flush();                                                     // Flush serial output before the CPU enters light sleep.
  esp_light_sleep_start();                                            // Enter ESP32 light sleep until one wake source fires.

  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause(); // Read why the ESP32 woke up.
  restorePower();                                                     // Restore power rails before continuing normal code.
  Serial.printf("SLEEP: WAKE cause=%s(%d)\n", wakeupCauseName(cause), static_cast<int>(cause)); // Print the wakeup cause.
  if (cause == ESP_SLEEP_WAKEUP_GPIO) {                               // Check whether a GPIO source woke the board.
    printGpioWakeStates();                                            // Print GPIO levels to identify the active wake source.
  }                                                                   // End the GPIO wake diagnostic check.
}                                                                     // End enterLightSleep().
}                                                                     // End the private namespace.

void setup() {                                                        // Run this function once after reset or upload.
  Serial.begin(kSerialBaud);                                          // Start the USB serial port.
  delay(800);                                                         // Give the Serial Monitor time to reconnect.
  Serial.println();                                                   // Print a blank line before the lesson title.
  Serial.println("=== Lesson15: Power and Light Sleep ===");          // Print the lesson title.

  Wire.begin(kI2cSdaPin, kI2cSclPin);                                 // Initialize the watch I2C bus before resetting touch hardware.
  pinMode(kTouchInterruptPin, INPUT);                                 // Configure the touch interrupt pin as an input.
  pinMode(kDisplayPowerStatePin, INPUT);                              // Configure the display power-state pin as an input.
  resetTouchController();                                             // Reset the touch controller so its interrupt level is known.
  restorePower();                                                     // Ensure the board starts from the awake power state.
  Serial.println("The board will enter light sleep after five seconds."); // Explain the timing of the sleep demo.
}                                                                     // End setup().

void loop() {                                                         // Run this function repeatedly after setup() finishes.
  static uint32_t awakeStartedMs = millis();                          // Store when the current awake window started.
  if (millis() - awakeStartedMs >= kAwakeTimeMs) {                    // Check whether the board has been awake for five seconds.
    enterLightSleep();                                                // Enter light sleep and return here after wakeup.
    awakeStartedMs = millis();                                        // Restart the awake timer after waking.
  }                                                                   // End the awake-window check.
  delay(20);                                                          // Give background tasks a small time slice.
}                                                                     // End loop().
