#include "TermDisplay.h"
#include <SPI.h>
#include <Adafruit_GFX.h>
#ifdef TFT_ST7796S
  #include <Adafruit_ST7796S_kbv.h>
  typedef Adafruit_ST7796S_kbv Panel;
#else
  #include <Adafruit_ST7789.h>
  typedef Adafruit_ST7789 Panel;
#endif

#ifndef TFT_W
  #define TFT_W 240
#endif
#ifndef TFT_H
  #define TFT_H 240
#endif
#ifndef TFT_SPI_MODE
  #define TFT_SPI_MODE SPI_MODE3
#endif
#ifndef TFT_SPI_HZ
  #define TFT_SPI_HZ 24000000
#endif

// Optional: size of the area the UI uses, and where it sits inside the controller's coordinates.
// The ST7796S library is fixed at 320x480, so the visible area has to be given explicitly (-D TFT_VIS_W/TFT_VIS_H).
#ifndef TFT_X_OFF
  #define TFT_X_OFF 0
#endif
#ifndef TFT_Y_OFF
  #define TFT_Y_OFF 0
#endif

#define BAR_BG 0x033B   // status bar blue

// The colour slots the shared MeshCore UI code refers to (defined once per display driver).
ColorVal UIColor::window_bkg    = 0x0000;
ColorVal UIColor::title_bkg     = BAR_BG;
ColorVal UIColor::title_txt     = 0xFFFF;
ColorVal UIColor::primary_txt   = 0xFFFF;
ColorVal UIColor::secondary_txt = 0xBDF7;
ColorVal UIColor::warning_txt   = 0xFD20;
ColorVal UIColor::popup_bkg     = 0x2104;
ColorVal UIColor::popup_txt     = 0xFFFF;
ColorVal UIColor::corp_blue     = BAR_BG;

static Panel* tft = NULL;
static GFXcanvas16* rowc = NULL;     // one text row of pixels, pushed to the panel per changed row

bool TermDisplay::begin() {
  if (ready) return true;
  memset(page, 0, sizeof(page));
  footer[0] = 0;

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);                    // backlight off while the panel initialises

  SPI.setSCK(TFT_SCK);
  SPI.setTX(TFT_MOSI);
#ifdef TFT_ST7796S
  tft = new Panel(&SPI, TFT_DC, TFT_CS, TFT_RST);   // this library takes dc before cs
  tft->begin(TFT_SPI_HZ);
  tft->invertDisplay(true); //Inversion for ST7796S Displays
#else
  tft = new Panel(&SPI, TFT_CS, TFT_DC, TFT_RST);
  tft->init(TFT_W, TFT_H, TFT_SPI_MODE);
  tft->setSPISpeed(TFT_SPI_HZ);
#endif

  setRotation(rot);
  if (!ready) return false;
  on = true;
  applyBacklight();
  return true;
}

void TermDisplay::setRotation(uint8_t r) {
  rot = r & 3;
  if (!tft) return;
  tft->setRotation(rot);
#ifdef TFT_VIS_W
  pw = TFT_VIS_W;
  ph = TFT_VIS_H;
#else
  pw = tft->width();
  ph = tft->height();
#endif
  cw = 6 * TERM_FONT_SCALE;
  rh = 8 * TERM_FONT_SCALE;
  cols = pw / cw;  if (cols > TERM_COLS_MAX) cols = TERM_COLS_MAX;
  rows = ph / rh;  if (rows > ROWS_MAX) rows = ROWS_MAX;
  trows = rows - 2; if (trows < 1) trows = 1;
  margin = (pw - cols * cw) / 2;
  yoff = (ph - rows * rh) / 2;

  if (rowc && rowc->width() != pw) { delete rowc; rowc = NULL; }
  if (!rowc) {
    rowc = new GFXcanvas16(pw, rh);
    if (rowc && rowc->getBuffer()) {
      rowc->setTextSize(TERM_FONT_SCALE);
      rowc->setTextWrap(false);
      rowc->cp437(true);
    } else {
      delete rowc;
      rowc = NULL;
    }
  }
  ready = (rowc != NULL);
  tft->fillScreen(0x0000);
  for (int i = 0; i < ROWS_MAX; i++) shown_ok[i] = false;
  dirty = true;
}

void TermDisplay::applyBacklight() {
#ifdef TFT_BL
  analogWrite(TFT_BL, on ? (level * 255) / 100 : 0);
#endif
}

void TermDisplay::setBrightness(uint8_t pct) {
  if (pct < 5) pct = 5;
  if (pct > 100) pct = 100;
  level = pct;
  if (on) applyBacklight();
}

void TermDisplay::turnOn()  { on = true;  applyBacklight(); }
void TermDisplay::turnOff() { on = false; applyBacklight(); }

void TermDisplay::drawTextCentered(int mid_x, int y, const char* str) {
  char b[TERM_COLS_MAX + 1];
  int pad = (cols - (int) strlen(str)) / 2;
  if (pad < 0) pad = 0;
  snprintf(b, sizeof(b), "%*s%s", pad, "", str);
  pageLine(b, TERM_GREEN);
}

void TermDisplay::setStatus(const char* left, const char* right) {
  strncpy(bar_left, left, TERM_COLS_MAX);
  bar_left[TERM_COLS_MAX] = 0;
  strncpy(bar_right, right, sizeof(bar_right) - 1);
  bar_right[sizeof(bar_right) - 1] = 0;
  dirty = true;
}

void TermDisplay::pageClear() {
  page_n = 0;
  footer[0] = 0;
  footer_fg = TERM_GREY;
  dirty = true;
}

void TermDisplay::pageLine(const char* s, uint16_t fg, bool selected) {
  if (page_n >= ROWS_MAX) return;
  strncpy(page[page_n].text, s, TERM_COLS_MAX);
  page[page_n].text[TERM_COLS_MAX] = 0;
  page[page_n].fg = fg;
  page[page_n].sel = selected;
  page_n++;
  dirty = true;
}

void TermDisplay::pageFooter(const char* s, uint16_t fg) {
  strncpy(footer, s, TERM_COLS_MAX);
  footer[TERM_COLS_MAX] = 0;
  footer_fg = fg;
  dirty = true;
}

void TermDisplay::pageShow() {
  dirty = true;
  refresh();
}

void TermDisplay::drawRow(int r, const char* text, uint16_t fg, uint16_t bg) {
  if (!ready || r < 0 || r >= ROWS_MAX) return;
  if (shown_ok[r] && shown_fg[r] == fg && shown_bg[r] == bg && strcmp(shown[r], text) == 0) return;
  rowc->fillScreen(bg);
  rowc->setTextColor(fg);
  rowc->setCursor(margin, 0);
  rowc->print(text);
  tft->drawRGBBitmap(TFT_X_OFF, TFT_Y_OFF + yoff + r * rh, rowc->getBuffer(), pw, rh);
  strncpy(shown[r], text, TERM_COLS_MAX);
  shown[r][TERM_COLS_MAX] = 0;
  shown_fg[r] = fg;
  shown_bg[r] = bg;
  shown_ok[r] = true;
}

void TermDisplay::refresh() {
  if (!ready || !dirty) return;
  dirty = false;
  char buf[TERM_COLS_MAX + 4];

  // status bar: name on the left, time / unread on the right
  int rl = strlen(bar_right);
  int lw = cols - rl - (rl ? 1 : 0);
  if (lw < 1) lw = 1;
  snprintf(buf, sizeof(buf), "%-*.*s%s%s", lw, lw, bar_left, rl ? " " : "", bar_right);
  drawRow(0, buf, TERM_WHITE, BAR_BG);

  for (int i = 0; i < trows; i++) {
    if (i < page_n) {
      snprintf(buf, sizeof(buf), "%.*s", cols, page[i].text);
      if (page[i].sel) drawRow(1 + i, buf, 0x0000, TERM_SEL_BG);
      else drawRow(1 + i, buf, page[i].fg, 0x0000);
    } else {
      drawRow(1 + i, "", TERM_WHITE, 0x0000);
    }
  }

  snprintf(buf, sizeof(buf), "%.*s", cols, footer);
  drawRow(rows - 1, buf, footer_fg, 0x0000);
}
