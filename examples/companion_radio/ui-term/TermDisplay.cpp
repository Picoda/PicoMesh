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
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMono12pt7b.h>
#include <Fonts/FreeMono18pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeMonoOblique9pt7b.h>
#include <Fonts/FreeMonoOblique12pt7b.h>
#include <Fonts/FreeMonoOblique18pt7b.h>
#include <Fonts/FreeMonoBoldOblique9pt7b.h>
#include <Fonts/FreeMonoBoldOblique12pt7b.h>
#include <Fonts/FreeMonoBoldOblique18pt7b.h>
#include <Fonts/FreeSerif9pt7b.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeSerif18pt7b.h>
#include <Fonts/FreeSerifBold9pt7b.h>
#include <Fonts/FreeSerifBold12pt7b.h>
#include <Fonts/FreeSerifBold18pt7b.h>
#include <Fonts/FreeSerifItalic9pt7b.h>
#include <Fonts/FreeSerifItalic12pt7b.h>
#include <Fonts/FreeSerifItalic18pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansOblique9pt7b.h>
#include <Fonts/FreeSansOblique2pt7b.h>
#include <Fonts/FreeSansOblique18pt7b.h>

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

// Where the text area sits inside the controller's pixel area. Default is the top-left corner; the ST7796S
// library is fixed at 320x480, so -D TFT_VIS_W/TFT_VIS_H give the area that is really visible.
#ifndef TFT_X_OFF
  #define TFT_X_OFF 0
#endif
#ifndef TFT_Y_OFF
  #define TFT_Y_OFF 0
#endif

#define BAR_BG 0x033B   // default status bar blue

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

struct ThemeColors { uint16_t fg, acc, grey, bar, sel; };
static const ThemeColors THEMES[THEME_COUNT] = {
  { 0xFFFF, 0x07E0, 0xBDF7, 0x033B, 0x07FF },    // White
  { 0x07E0, 0xFFE0, 0x0400, 0x0200, 0x07E0 },    // Green (classic terminal)
  { 0xFD20, 0xFFE0, 0x9B00, 0x4200, 0xFD20 },    // Amber
  { 0x07FF, 0xFFFF, 0x0410, 0x0210, 0x07FF },    // Cyan
};

static const GFXfont* const FONT_TABLE[FONT_TYPES][3] = {
  { NULL, NULL, NULL },
  { &FreeMono9pt7b, &FreeMono12pt7b, &FreeMono18pt7b },
  { &FreeMonoBold9pt7b, &FreeMonoBold12pt7b, &FreeMonoBold18pt7b },
  { &FreeMonoOblique9pt7b, &FreeMonoOblique12pt7b, &FreeMonoOblique18pt7b },
  { &FreeMonoBoldOblique9pt7b, &FreeMonoBoldOblique12pt7b, &FreeMonoBoldOblique18pt7b },
  { &FreeSerif9pt7b, &FreeSerif12pt7b, &FreeSerif18pt7b },
  { &FreeSerifBold9pt7b, &FreeSerifBold12pt7b, &FreeSerifBold18pt7b },
  { &FreeSerifItalic9pt7b, &FreeSerifItalic12pt7b, &FreeSerifItalic18pt7b },
  { &FreeSans9pt7b, &FreeSans12pt7b, &FreeSans18pt7b },
  { &FreeSansBold9pt7b, &FreeSansBold12pt7b, &FreeSansBold18pt7b },
  { &FreeSansOblique9pt7b, &FreeSansOblique12pt7b, &FreeSansOblique18pt7b },
};

static Panel* tft = NULL;
static GFXcanvas16* rowc = NULL;     // one text row of pixels, pushed to the panel per changed row

const char* TermDisplay::fontName(uint8_t t) {
  static const char* const N[FONT_TYPES] = { "Pixel", "Mono", "Mono bold", "Mono italic", "Mono bold italic", "Serif", "Serif bold", "Serif italic", "Sans", "Sans bold", "Sans italic" };
  return N[t % FONT_TYPES];
}
const char* TermDisplay::sizeName(uint8_t type, uint8_t size) {
  static const char* const PX[3] = { "1x", "2x", "3x" };
  static const char* const MN[3] = { "9pt", "12pt", "18pt" };
  return (type == FONT_PIXEL ? PX : MN)[size % 3];
}
const char* TermDisplay::themeName(uint8_t t) {
  static const char* const N[THEME_COUNT] = { "White", "Green", "Amber", "Cyan" };
  return N[t % THEME_COUNT];
}

uint16_t TermDisplay::themed(uint16_t c) const {
  const ThemeColors& th = THEMES[theme];
  if (c == TERM_WHITE) return th.fg;
  if (c == TERM_GREEN) return th.acc;
  if (c == TERM_GREY) return th.grey;
  return c;
}

bool TermDisplay::begin() {
  if (ready) return true;
  memset(page, 0, sizeof(page));
  footer[0] = 0;

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);                    // backlight off while the panel initialises

  SPI.setSCK(TFT_SCK);
  SPI.setTX(TFT_MOSI);
#ifdef SD_MISO
  SPI.setRX(SD_MISO);                           // an SD card shares this bus (its own chip select)
#endif
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

void TermDisplay::setFont(uint8_t type, uint8_t size) {
  font_type = type % FONT_TYPES;
  font_size = size % 3;
  if (ready) setRotation(rot);                  // geometry depends on the font
}

void TermDisplay::setTheme(uint8_t t) {
  theme = t % THEME_COUNT;
  for (int i = 0; i < ROWS_MAX; i++) shown_ok[i] = false;
  dirty = true;
}

void TermDisplay::applyFont() {
  if (!rowc) return;
  const GFXfont* f = FONT_TABLE[font_type][font_size];
  rowc->setFont(f);                             // NULL = the built-in pixel font
  rowc->setTextSize(f ? 1 : font_size + 1);
  rowc->setTextWrap(false);
  rowc->cp437(true);
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
  const GFXfont* f = FONT_TABLE[font_type][font_size];
  if (f) {
    cw = f->glyph['M' - f->first].xAdvance;
    rh = f->yAdvance;
    ty = (rh * 3) / 4;                          // baseline
  } else {
    cw = 6 * (font_size + 1);
    rh = 8 * (font_size + 1);
    ty = 0;
  }
  cols = pw / cw;  if (cols > TERM_COLS_MAX) cols = TERM_COLS_MAX;
  rows = ph / rh;  if (rows > ROWS_MAX) rows = ROWS_MAX;
  trows = rows - 2; if (trows < 1) trows = 1;
  margin = (pw - cols * cw) / 2;
  yoff = (ph - rows * rh) / 2;

  x_off = TFT_X_OFF;
  y_off = TFT_Y_OFF;

  if (rowc && (rowc->width() != pw || rowc->height() != rh)) { delete rowc; rowc = NULL; }
  if (!rowc) {
    rowc = new GFXcanvas16(pw, rh);
    if (!(rowc && rowc->getBuffer())) { delete rowc; rowc = NULL; }
  }
  applyFont();
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
  page_indent = 0;
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

void TermDisplay::drawRow(int r, const char* text, uint16_t fg, uint16_t bg, bool sel) {
  if (!ready || r < 0 || r >= ROWS_MAX) return;
  if (shown_ok[r] && shown_fg[r] == fg && shown_bg[r] == bg && shown_sel[r] == sel && strcmp(shown[r], text) == 0) return;
  const ThemeColors& th = THEMES[theme];
  uint16_t pbg = (bg == BAR_BG) ? th.bar : bg;
  rowc->fillScreen(sel ? 0x0000 : pbg);
  uint16_t pfg = themed(fg);
  if (sel) {
    // a highlight box around the words only, not the whole row
    int i0 = 0, i1 = strlen(text);
    while (i0 < i1 && text[i0] == ' ') i0++;
    while (i1 > i0 && text[i1 - 1] == ' ') i1--;
    rowc->fillRect(margin + i0 * cw - 2, 0, (i1 - i0) * cw + 4, rh, th.sel);
    pfg = 0x0000;
  }
  rowc->setTextColor(pfg);

  rowc->setCursor(margin, ty);
  rowc->print(text);
  tft->drawRGBBitmap(x_off, y_off + yoff + r * rh, rowc->getBuffer(), pw, rh);
  strncpy(shown[r], text, TERM_COLS_MAX);
  shown[r][TERM_COLS_MAX] = 0;
  shown_fg[r] = fg;
  shown_bg[r] = bg;
  shown_sel[r] = sel;
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

  int ind = page_indent;
  for (int i = 0; i < trows; i++) {
    if (i < page_n) {
      snprintf(buf, sizeof(buf), "%*s%.*s", ind, "", cols - ind, page[i].text);
      drawRow(1 + i, buf, page[i].sel ? 0x0000 : page[i].fg, 0x0000, page[i].sel);
    } else {
      drawRow(1 + i, "", TERM_WHITE, 0x0000);
    }
  }

  // footer: hints are centred, an edit line ("> text") stays left aligned
  int fl = strlen(footer);
  int pad = (footer[0] == '>' || fl >= cols) ? 0 : (cols - fl) / 2;
  snprintf(buf, sizeof(buf), "%*s%.*s", pad, "", cols - pad, footer);
  drawRow(rows - 1, buf, footer_fg, 0x0000);
}
