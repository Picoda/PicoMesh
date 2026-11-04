#pragma once

// Minimal driver for the M5Stack CardKB (I2C keyboard, address 0x5F).
// poll() returns one key code per read, or 0 when nothing is pressed / not attached.

#include <Arduino.h>
#include <Wire.h>

#ifndef CARDKB_ADDR
  #define CARDKB_ADDR 0x5F
#endif
#ifndef CARDKB_POLL_MS
  #define CARDKB_POLL_MS 25
#endif

class CardKB {
  TwoWire* _wire = NULL;
  unsigned long _next_poll = 0;
  bool _present = false;
public:
  bool begin(TwoWire& wire) {
    _wire = &wire;
    _wire->beginTransmission(CARDKB_ADDR);
    _present = (_wire->endTransmission() == 0);
    return _present;
  }
  bool isPresent() const { return _present; }
  char poll() {
    if (!_wire) return 0;
    unsigned long now = millis();
    if ((long)(now - _next_poll) < 0) return 0;
    _next_poll = now + CARDKB_POLL_MS;
    if (_wire->requestFrom((uint8_t)CARDKB_ADDR, (uint8_t)1) != 1) { _present = false; return 0; }
    _present = true;
    return (char)_wire->read();
  }
};
