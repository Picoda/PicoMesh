#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/KeyValueStore.h>

// Plain Raspberry Pi Pico (RP2040, no wireless chip) + external Waveshare SX1262 HAT.
// Deliberately does not use LED_BUILTIN (on the Pico W that is routed through the wireless chip):
// the TX LED is the explicit GPIO given by P_LORA_TX_LED.

class PicoBoard : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

public:
  void begin();
  uint8_t getStartupReason() const override { return startup_reason; }

  void attachDynamicPrefs(KeyValueStore* prefs) { }  // no-op, used by the repeater/room examples

#ifdef P_LORA_TX_LED
  void onBeforeTransmit() override { digitalWrite(P_LORA_TX_LED, HIGH); }
  void onAfterTransmit() override { digitalWrite(P_LORA_TX_LED, LOW); }
#endif

  // Battery: the Waveshare HAT routes its battery sense to GP26. Enable with
  //   -D PIN_VBAT_READ=26 -D ADC_MULTIPLIER=10230
  // (plain numbers only: PlatformIO passes parentheses in -D values to the shell unquoted).
  // 10230 is the Pico W variant's value for this circuit and is not verified for the HAT.
  uint16_t getBattMilliVolts() override {
#if defined(PIN_VBAT_READ) && defined(ADC_MULTIPLIER)
    analogReadResolution(12);
    uint32_t raw = 0;
    const int BATTERY_SAMPLES = 8;
    for (int i = 0; i < BATTERY_SAMPLES; i++) raw += analogRead(PIN_VBAT_READ);
    raw = raw / BATTERY_SAMPLES;
    return (uint16_t) ((ADC_MULTIPLIER * raw) / 4096);
#else
    return 0;   // no battery sense configured
#endif
  }

  float getMCUTemperature() override { return analogReadTemp(); }   // RP2040 on-chip sensor

  const char* getManufacturerName() const override { return "Raspberry Pi Pico"; }

  void reboot() override { rp2040.reboot(); }

  bool startOTAUpdate(const char* id, char reply[]) override;
};
