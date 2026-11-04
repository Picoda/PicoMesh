#pragma once

// Text-terminal renderer for an ST7789 TFT, exposed as a MeshCore DisplayDriver so the companion
// firmware's boot code can use it. The terminal UI draws pages made of text lines:
//   row 0 = status bar (name left, time/unread right), body rows, last row = footer / edit line.
// Only rows whose text changed are redrawn, one row at a time through a small canvas, so the
// panel traffic and RAM use stay low (a row buffer of 240 x 16 pixels instead of a full frame).

#include <helpers/ui/DisplayDriver.h>
#include <Arduino.h>

#ifndef TERM_COLS_MAX
  #define TERM_COLS_MAX 40
#endif
#ifndef TERM_ROTATION
  #define TERM_ROTATION 3
#endif
#ifndef TERM_FONT_SCALE
  #define TERM_FONT_SCALE 2
#endif

#define TERM_WHITE   0xFFFF
#define TERM_GREY    0xBDF7
#define TERM_GREEN   0x07E0
#define TERM_CYAN    0x07FF
#define TERM_YELLOW  0xFFE0
#define TERM_ORANGE  0xFD20
#define TERM_RED     0xF800
#define TERM_SEL_BG  0x07FF      // highlighted line: black text on cyan

class TermDisplay : public DisplayDriver {
  static const int ROWS_MAX = 32;
  struct PageLine { char text[TERM_COLS_MAX + 1]; uint16_t fg; bool sel; };

  PageLine page[ROWS_MAX];
  int page_n = 0;
  char footer[TERM_COLS_MAX + 1] = {0};
  uint16_t footer_fg = TERM_GREY;
  char bar_left[TERM_COLS_MAX + 1] = {0};
  char bar_right[16] = {0};

  int cols = 20, rows = 15, trows = 13;
  int pw = 240, ph = 240, cw = 12, rh = 16, margin = 0, yoff = 0;
  uint8_t rot = TERM_ROTATION;
  uint8_t level = 100;                      // backlight, percent
  bool on = false;
  bool ready = false;
  bool dirty = true;

  char shown[ROWS_MAX][TERM_COLS_MAX + 1];
  uint16_t shown_fg[ROWS_MAX], shown_bg[ROWS_MAX];
  bool shown_ok[ROWS_MAX];

  void drawRow(int r, const char* text, uint16_t fg, uint16_t bg);
  void applyBacklight();

public:
  TermDisplay() : DisplayDriver(240, 240) {}
  bool begin();

  // --- DisplayDriver (only what the companion boot code needs) ---
  bool isOn() override { return on; }
  void turnOn() override;
  void turnOff() override;
  void clear() override { pageClear(); dirty = true; }
  void startFrame(ColorVal bkg = UIColor::window_bkg) override { pageClear(); }
  void setTextSize(int sz) override {}
  void setColor(ColorVal c) override {}
  void setCursor(int x, int y) override {}
  void print(const char* str) override { pageLine(str, TERM_WHITE); }
  void fillRect(int x, int y, int w, int h) override {}
  void drawRect(int x, int y, int w, int h) override {}
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override {}
  uint16_t getTextWidth(const char* str) override { return strlen(str) * 6; }
  void drawTextCentered(int mid_x, int y, const char* str) override;
  void endFrame() override { pageShow(); }

  // --- terminal API ---
  void setStatus(const char* left, const char* right = "");
  void setRotation(uint8_t r);
  uint8_t getRotation() const { return rot; }
  void setBrightness(uint8_t pct);
  uint8_t getBrightness() const { return level; }
  int columns() const { return cols; }
  int bodyRows() const { return trows; }

  void pageClear();
  void pageLine(const char* s, uint16_t fg = TERM_WHITE, bool selected = false);
  void pageFooter(const char* s, uint16_t fg = TERM_GREY);
  void pageShow();                          // mark for redraw and draw now
  void refresh();                           // draw changed rows (cheap when nothing changed)
};
