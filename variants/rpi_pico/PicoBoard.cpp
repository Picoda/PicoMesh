#include "PicoBoard.h"
#include <Wire.h>

void PicoBoard::begin() {
  startup_reason = BD_STARTUP_NORMAL;

#ifdef P_LORA_TX_LED
  pinMode(P_LORA_TX_LED, OUTPUT);
#endif
#ifdef PIN_VBAT_READ
  pinMode(PIN_VBAT_READ, INPUT);
#endif

#if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
  Wire.setSDA(PIN_BOARD_SDA);
  Wire.setSCL(PIN_BOARD_SCL);
#endif
  Wire.begin();

  delay(10);   // give the SX1262 time to power up
}

bool PicoBoard::startOTAUpdate(const char* id, char reply[]) {
  return false;
}
