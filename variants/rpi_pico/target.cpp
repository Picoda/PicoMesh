#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

PicoBoard board;

RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI1);

WRAPPER_CLASS radio_driver(radio, board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
SensorManager sensors;

#ifdef PICO_TERM_UI
  TermDisplay display;
#endif

bool radio_init() {
  rtc_clock.begin(Wire);

  // Explicit pin assignment (do not rely on the core's default SPI1 pin group).
  SPI1.setSCK(P_LORA_SCLK);
  SPI1.setTX(P_LORA_MOSI);
  SPI1.setRX(P_LORA_MISO);
  pinMode(P_LORA_NSS, OUTPUT);
  digitalWrite(P_LORA_NSS, HIGH);
  SPI1.begin(false);

  bool ok = radio.std_init(NULL);   // SPI1 is already started above
  if (!ok) {
    // main() just halts when this returns false: say so on the screen and blink the LED.
#ifdef PICO_TERM_UI
    display.pageClear();
    display.pageLine("", TERM_WHITE);
    display.pageLine("RADIO INIT FAILED", TERM_RED);
    display.pageLine("", TERM_WHITE);
    display.pageLine("Check the SX1262", TERM_WHITE);
    display.pageLine("wiring and power,", TERM_WHITE);
    display.pageLine("then replug USB.", TERM_WHITE);
    display.pageShow();
#endif
#ifdef P_LORA_TX_LED
    while (1) {
      digitalWrite(P_LORA_TX_LED, HIGH); delay(100);
      digitalWrite(P_LORA_TX_LED, LOW);  delay(100);
    }
#endif
  }
  return ok;
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);  // create new random identity
}
