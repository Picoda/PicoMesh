#pragma once

// Text-terminal renderer for an ST7789 / ST7796S TFT, exposed as a MeshCore DisplayDriver so the
// companion firmware's boot code can use it. The terminal UI draws pages made of text lines:
//   row 0 = status bar (name left, time/unread right), body rows, last row = footer / edit line.
// Only rows whose text changed are redrawn, one row at a time through a small canvas, so panel
// traffic and RAM use stay low.
//
// Display strings use one byte per character (bytes 0x80.. are drawn from the built-in cp437 font).

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

#define TERM_WHITE   0xFFFF      // "normal text": replaced by the theme's text colour when drawn
#define TERM_GREY    0xBDF7      // hints: replaced by the theme's dim colour
#define TERM_GREEN   0x07E0      // titles: replaced by the theme's accent colour
#define TERM_CYAN    0x07FF
#define TERM_YELLOW  0xFFE0
#define TERM_ORANGE  0xFD20
#define TERM_RED     0xF800
#define TERM_SEL_BG  0x07FF

enum TermFont { FONT_PIXEL = 0, FONT_MONO, FONT_MONO_BOLD, FONT_MONO_ITALIC, FONT_MONO_BOLD_ITALIC, FONT_SERIF, FONT_SERIF_BOLD, FONT_SERIF_ITALIC, FONT_SANS, FONT_SANS_BOLD, FONT_SANS_ITALIC, FONT_TYPES };
enum TermTheme { THEME_WHITE = 0, THEME_GREEN, THEME_AMBER, THEME_CYAN, THEME_COUNT };

class TermDisplay : public DisplayDriver {
  static const int ROWS_MAX = 32;
  struct PageLine { char text[TERM_COLS_MAX + 1]; uint16_t fg; bool sel; };

  PageLine page[ROWS_MAX];
  int page_n = 0;
  int page_indent = 0;                      // characters of left indent for the body (centres a narrow layout)
  char footer[TERM_COLS_MAX + 1] = {0};
  uint16_t footer_fg = TERM_GREY;
  char bar_left[TERM_COLS_MAX + 1] = {0};
  char bar_right[16] = {0};

  int cols = 20, rows = 15, trows = 13;
  int pw = 240, ph = 240, cw = 12, rh = 16, margin = 0, yoff = 0;
  int x_off = 0, y_off = 0;                 // where the text area sits inside the controller's pixel area
  int ty = 0;                               // text y inside a row (0 for the pixel font, baseline for GFX fonts)
  uint8_t rot = TERM_ROTATION;
  uint8_t font_type = FONT_PIXEL;
  uint8_t font_size = TERM_FONT_SCALE - 1;  // 0..2
  uint8_t theme = THEME_WHITE;
  uint8_t level = 100;                      // backlight, percent
  bool on = false;
  bool ready = false;
  bool dirty = true;

  char shown[ROWS_MAX][TERM_COLS_MAX + 1];
  uint16_t shown_fg[ROWS_MAX], shown_bg[ROWS_MAX];
  bool shown_sel[ROWS_MAX];
  bool shown_ok[ROWS_MAX];

  void drawRow(int r, const char* text, uint16_t fg, uint16_t bg, bool sel = false);
  void applyFont();
  void applyBacklight();
  uint16_t themed(uint16_t c) const;

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
  void setFont(uint8_t type, uint8_t size);
  void setTheme(uint8_t t);
  int columns() const { return cols; }
  int bodyRows() const { return trows; }
  int textColumns() const { return cols - 2 * page_indent; }   // width available inside the indent
  int cellWidth() const { return cw; }

  static const char* fontName(uint8_t type);
  static const char* sizeName(uint8_t type, uint8_t size);
  static const char* themeName(uint8_t t);

  void pageClear();
  void pageIndent(int n) { page_indent = n < 0 ? 0 : n; dirty = true; }
  void pageLine(const char* s, uint16_t fg = TERM_WHITE, bool selected = false);
  void pageFooter(const char* s, uint16_t fg = TERM_GREY);
  void pageShow();                          // mark for redraw and draw now
  void refresh();                           // draw changed rows (cheap when nothing changed)
};
