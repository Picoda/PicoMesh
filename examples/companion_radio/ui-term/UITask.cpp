#include "UITask.h"
#include <target.h>
#include <LittleFS.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/ui/MomentaryButton.h>
#include "CardKB.h"
#include "Ascii.h"
#include "Presets.h"

extern DataStore store;       // defined in main.cpp
extern StdRNG fast_rng;       // defined in main.cpp

#define UI_VERSION "picomesh ui v1"

// ---------------------------------------------------------------- base64 (channel keys)
// Not using <base64.hpp>: that header holds full function bodies and BaseChatMesh.cpp already includes it,
// so a second include here causes "multiple definition" errors at link time.
static const char B64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/,;-_=!@#$%^&*()[]{}.<>|':?`~";
static const char B64_CHARS[] = "'";
static const char B64_CHARS[] = '"';
static int b64Encode(const uint8_t* in, int n, char* out) {          // NUL-terminated, returns length
  int o = 0;
  for (int i = 0; i < n; i += 3) {
    uint32_t v = (uint32_t) in[i] << 16;
    if (i + 1 < n) v |= (uint32_t) in[i + 1] << 8;
    if (i + 2 < n) v |= in[i + 2];
    out[o++] = B64_CHARS[(v >> 18) & 63];
    out[o++] = B64_CHARS[(v >> 12) & 63];
    out[o++] = (i + 1 < n) ? B64_CHARS[(v >> 6) & 63] : '=';
    out[o++] = (i + 2 < n) ? B64_CHARS[v & 63] : '=';
  }
  out[o] = 0;
  return o;
}

static int b64Value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '+') return 62;
  if (c == '/') return 63;
  return -1;
}

static int b64Decode(const char* in, uint8_t* out, int max_out) {    // returns byte count or -1
  uint32_t acc = 0;
  int bits = 0, o = 0;
  for (; *in && *in != '='; in++) {
    if (*in == ' ' || *in == '\n' || *in == '\r') continue;
    int v = b64Value(*in);
    if (v < 0) return -1;
    acc = (acc << 6) | (uint32_t) v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (o >= max_out) return -1;
      out[o++] = (uint8_t) ((acc >> bits) & 0xFF);
    }
  }
  return o;
}

// ---------------------------------------------------------------- limits (all static memory)
#define CONV_MAX    20
#define MSG_MAX     70
#define MSG_TEXT    150
#define PEND_MAX    20
#define RETRY_MAX   5
#define LP          6               // list items per page (keys 1-6, 7 previous, 8 next)
#define EDIT_MAX    MSG_TEXT
#define NO_CONV     0xFF
#define SAVE_DELAY  300000UL        // flash is only written this long after the last change

enum TermKey {
  TK_BACKSPACE = 8, TK_ENTER = 13, TK_ESC = 27,
  TK_UP = 0x101, TK_DOWN = 0x102, TK_LEFT = 0x103, TK_RIGHT = 0x104,
  TK_NEXT = 0x105, TK_PREV = 0x106
};

enum Pg : uint8_t {
  PG_SPLASH, PG_ONB_WELCOME, PG_ONB_NAME, PG_ONB_REGION, PG_ONB_TIME, PG_ONB_DONE,
  PG_HOME,
  PG_RADIO, PG_PRESETS, PG_FREQ, PG_BW, PG_SF, PG_CR, PG_TX,
  PG_ADVERT,
  PG_MSGS, PG_THREAD, PG_THREAD_ACT, PG_QUICK,
  PG_CONTACTS, PG_CONTACT_LIST, PG_CONTACT, PG_CONTACT_DEL, PG_HEARD,
  PG_CHANNELS, PG_CHAN_ITEM, PG_CHAN_DEL, PG_CHAN_KEY_SHOW,
  PG_AIRTIME,
  PG_SETTINGS, PG_DISPLAY, PG_TIME, PG_TZ, PG_QR_LIST,
  PG_ABOUT, PG_REBOOT,
  PG_EDIT
};

enum EditKind : uint8_t { E_NAME, E_MSG, E_QR, E_CHAN_NEW, E_CHAN_JOIN_NAME, E_CHAN_JOIN_KEY, E_FREQ };

// ---------------------------------------------------------------- stored data
struct Conv {
  uint8_t kind;          // 0 free, 1 direct, 2 channel
  uint8_t chan;          // channel slot (kind 2)
  uint8_t key[6];        // contact public key prefix (kind 1)
  uint8_t unread;
  uint32_t last_ts;      // ordering
  char name[24];
};

struct Msg {
  uint32_t seq;          // running number, 0 = unused
  uint32_t ts;           // sender timestamp
  uint8_t conv;
  uint8_t dir;           // 0 incoming, 1 outgoing
  uint8_t state;         // outgoing: 0 sent, 1 retrying, 2 delivered, 3 failed
  uint8_t tries;
  uint8_t hops;          // incoming: path length, 0xFF unknown
  char text[MSG_TEXT + 1];
};

struct Pending {
  bool active;
  uint32_t seq;
  uint8_t pub[PUB_KEY_SIZE];
  uint32_t ack[3];
  uint8_t n_ack;
  unsigned long deadline;
};

struct Blob {              // what is saved to flash
  uint32_t magic;
  uint32_t seq_next;
  Conv convs[CONV_MAX];
  Msg msgs[MSG_MAX];
};
#define BLOB_MAGIC 0x4D534731UL

struct UiPrefs {
  uint8_t version;
  uint8_t onboarded;
  uint8_t rot;
  uint8_t bright;        // percent
  uint16_t sleep_s;      // 0 = never
};

struct Stage {             // radio settings being edited, applied explicitly
  float freq, bw;
  uint8_t sf, cr;
  int8_t tx;
};

static const float BW_LIST[8] = { 15.6f, 20.8f, 31.25f, 41.7f, 62.5f, 125.0f, 250.0f, 500.0f };
static const int8_t TX_LIST[8] = { 2, 5, 8, 10, 14, 17, 20, 22 };
static const uint16_t SLEEP_LIST[5] = { 0, 15, 30, 60, 300 };
static const uint8_t BRIGHT_LIST[4] = { 25, 50, 75, 100 };
static const char* const QR_DEFAULT[6] = { "OK", "Yes", "No", "On my way", "Thanks", "Call me" };

// ---------------------------------------------------------------- the UI object
struct Ui {
  NodePrefs* prefs = NULL;
  UITask* task = NULL;

  // input
  CardKB kbd;
  bool kb_present = false;
#ifdef TERM_BTN_PIN
  MomentaryButton btn = MomentaryButton(TERM_BTN_PIN, 1000, true, true);   // active low, pull-up
#endif

  // data
  Blob blob;
  Pending pend[PEND_MAX];
  UiPrefs up;
  char qr[6][41];
  bool dirty_blob = false;
  unsigned long dirty_since = 0;

  // navigation
  uint8_t pg = PG_SPLASH;
  uint8_t stk[10];
  int sp = 0;
  int sel = 0, list_pg = 0;
  unsigned long splash_until = 0, toast_until = 0, last_input = 0, next_status = 0;
  char toast_txt[28];
  bool asleep = false;

  // working state
  Stage st;
  uint8_t cur_conv = NO_CONV;
  int thr_off = 0;                 // messages scrolled up from the newest
  int cur_contact_filter = 0;      // 0 people, 1 repeaters, 2 rooms, 3 favourites
  uint8_t cidx[MAX_CONTACTS];      // contact list for the current filter (indexes into the mesh table)
  int cidx_n = 0;
  bool cidx_valid = false;
  uint8_t cur_pub[PUB_KEY_SIZE];   // contact being shown
  int cur_chan = 0;                // channel slot being shown
  int qr_edit = 0;
  char tmp_name[32];
  char tmp_key[48];
  int tm[5];                       // time editor: year, month, day, hour, minute
  int tm_field = 0;

  // text entry
  uint8_t edit_kind = E_NAME;
  char edit_title[24];
  char ebuf[EDIT_MAX + 1];
  int elen = 0, emax = 31;
  uint8_t edit_return = PG_HOME;
  int kp_cur = 0, kp_set = 0;

  // ================================================================ small helpers
  static const char* cstr(const char* s) { return s ? s : ""; }

  int utcOffsetSecs() const { return (int) prefs->tz_offset * 3600; }

  static void civilFromDays(int64_t z, int& y, int& m, int& d) {      // days since 1970-01-01
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned) (z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t yy = (int64_t) yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    d = (int) (doy - (153 * mp + 2) / 5 + 1);
    m = (int) (mp < 10 ? mp + 3 : mp - 9);
    y = (int) (yy + (m <= 2));
  }
  static int64_t daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned) (y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t) doe - 719468;
  }

  void localTime(int& y, int& mo, int& d, int& h, int& mi) {
    int64_t t = (int64_t) rtc_clock.getCurrentTime() + utcOffsetSecs();
    if (t < 0) t = 0;
    civilFromDays(t / 86400, y, mo, d);
    int s = (int) (t % 86400);
    h = s / 3600;
    mi = (s % 3600) / 60;
  }

  void fmtAgo(char* b, size_t n, uint32_t ts) {
    uint32_t now = rtc_clock.getCurrentTime();
    if (ts == 0 || ts > now) { snprintf(b, n, "-"); return; }
    uint32_t s = now - ts;
    if (s < 90) snprintf(b, n, "%lus", (unsigned long) s);
    else if (s < 5400) snprintf(b, n, "%lum", (unsigned long) (s / 60));
    else if (s < 172800) snprintf(b, n, "%luh", (unsigned long) (s / 3600));
    else snprintf(b, n, "%lud", (unsigned long) (s / 86400));
  }

  const char* fmtFreq(float f, char* b, size_t n) {
    int mhz = (int) f;
    int khz = (int) ((f - mhz) * 1000.0f + 0.5f);
    snprintf(b, n, "%d.%03d", mhz, khz);
    return b;
  }
  const char* fmtBw(float bw, char* b, size_t n) {
    int whole = (int) bw;
    int frac = (int) ((bw - whole) * 100.0f + 0.5f);
    if (frac == 0) snprintf(b, n, "%d", whole);
    else if (frac % 10 == 0) snprintf(b, n, "%d.%d", whole, frac / 10);
    else snprintf(b, n, "%d.%02d", whole, frac);
    return b;
  }

  int batteryPct() {                                     // -1 when no plausible reading
    uint16_t mv = board.getBattMilliVolts();
    if (mv < 2800 || mv > 4500) return -1;
    int p = ((int) mv - 3300) * 100 / 900;
    return p < 0 ? 0 : (p > 100 ? 100 : p);
  }

  void toast(const char* s) {
    strncpy(toast_txt, s, sizeof(toast_txt) - 1);
    toast_txt[sizeof(toast_txt) - 1] = 0;
    toast_until = millis() + 2500;
    render();
  }
  void foot(const char* s) {
    if (millis() < toast_until) display.pageFooter(toast_txt, TERM_ORANGE);
    else display.pageFooter(s, TERM_GREY);
  }

  // ================================================================ persistence
  void loadUiPrefs() {
    memset(&up, 0, sizeof(up));
    up.version = 1; up.rot = TERM_ROTATION; up.bright = 100; up.sleep_s = 0;
    if (LittleFS.exists("/ui_prefs")) {
      File f = LittleFS.open("/ui_prefs", "r");
      if (f) {
        UiPrefs t;
        if (f.read((uint8_t*) &t, sizeof(t)) == (int) sizeof(t) && t.version == 1) up = t;
        f.close();
      }
    }
    if (up.rot > 3) up.rot = TERM_ROTATION;
#ifdef TFT_ST7796S
    if (up.rot != 1 && up.rot != 3) up.rot = TERM_ROTATION;
#endif
    if (up.bright < 5 || up.bright > 100) up.bright = 100;
  }
  void saveUiPrefs() {
    File f = LittleFS.open("/ui_prefs", "w");
    if (f) { f.write((const uint8_t*) &up, sizeof(up)); f.close(); }
  }

  void loadQuick() {
    for (int i = 0; i < 6; i++) { strncpy(qr[i], QR_DEFAULT[i], 40); qr[i][40] = 0; }
    if (LittleFS.exists("/ui_qr")) {
      File f = LittleFS.open("/ui_qr", "r");
      if (f) {
        char t[6][41];
        if (f.read((uint8_t*) t, sizeof(t)) == (int) sizeof(t)) memcpy(qr, t, sizeof(qr));
        f.close();
      }
    }
  }
  void saveQuick() {
    File f = LittleFS.open("/ui_qr", "w");
    if (f) { f.write((const uint8_t*) qr, sizeof(qr)); f.close(); }
  }

  void loadBlob() {                      // read straight into the live buffer (no second 6 KB copy)
    bool ok = false;
    if (LittleFS.exists("/ui_msgs")) {
      File f = LittleFS.open("/ui_msgs", "r");
      if (f) {
        if (f.size() == (size_t) sizeof(Blob) && f.read((uint8_t*) &blob, sizeof(blob)) == (int) sizeof(blob) &&
            blob.magic == BLOB_MAGIC) ok = true;
        f.close();
      }
    }
    if (!ok) {
      memset(&blob, 0, sizeof(blob));
      blob.magic = BLOB_MAGIC;
      blob.seq_next = 1;
    }
  }
  void markDirty() {
    if (!dirty_blob) dirty_since = millis();
    dirty_blob = true;
  }
  void flushBlob(bool force) {
    if (!dirty_blob) return;
    if (!force && millis() - dirty_since < SAVE_DELAY) return;
    File f = LittleFS.open("/ui_msgs", "w");
    if (f) { f.write((const uint8_t*) &blob, sizeof(blob)); f.close(); }
    dirty_blob = false;
  }

  // ================================================================ conversations and messages
  int totalUnread() const {
    int u = 0;
    for (int i = 0; i < CONV_MAX; i++) { if (blob.convs[i].kind) u += blob.convs[i].unread; }
    return u;
  }

  int findConvDirect(const uint8_t* pub) {
    for (int i = 0; i < CONV_MAX; i++) {
      if (blob.convs[i].kind == 1 && memcmp(blob.convs[i].key, pub, 6) == 0) return i;
    }
    return -1;
  }
  int findConvChan(int slot) {
    for (int i = 0; i < CONV_MAX; i++) {
      if (blob.convs[i].kind == 2 && blob.convs[i].chan == slot) return i;
    }
    return -1;
  }

  void dropConvMessages(int c) {
    for (int i = 0; i < MSG_MAX; i++) {
      if (blob.msgs[i].seq && blob.msgs[i].conv == c) blob.msgs[i].seq = 0;
    }
  }

  int allocConv() {
    for (int i = 0; i < CONV_MAX; i++) { if (!blob.convs[i].kind) return i; }
    int victim = 0;                                       // evict the least recently active one
    for (int i = 1; i < CONV_MAX; i++) { if (blob.convs[i].last_ts < blob.convs[victim].last_ts) victim = i; }
    dropConvMessages(victim);
    memset(&blob.convs[victim], 0, sizeof(Conv));
    return victim;
  }

  int convForContact(const uint8_t* pub, const char* name) {
    int c = findConvDirect(pub);
    if (c < 0) {
      c = allocConv();
      Conv& v = blob.convs[c];
      memset(&v, 0, sizeof(v));
      v.kind = 1;
      memcpy(v.key, pub, 6);
      strncpy(v.name, name, sizeof(v.name) - 1);
    }
    return c;
  }
  int convForChannel(int slot, const char* name) {
    int c = findConvChan(slot);
    if (c < 0) {
      c = allocConv();
      Conv& v = blob.convs[c];
      memset(&v, 0, sizeof(v));
      v.kind = 2;
      v.chan = slot;
      strncpy(v.name, name, sizeof(v.name) - 1);
    }
    return c;
  }

  Msg* msgBySeq(uint32_t seq) {
    if (seq == 0) return NULL;
    Msg* m = &blob.msgs[seq % MSG_MAX];
    return (m->seq == seq) ? m : NULL;
  }

  Msg* addMsg(int conv, uint8_t dir, uint32_t ts, const char* text, uint8_t hops) {
    uint32_t seq = blob.seq_next++;
    if (seq == 0) seq = blob.seq_next++;
    Msg* m = &blob.msgs[seq % MSG_MAX];
    memset(m, 0, sizeof(Msg));
    m->seq = seq;
    m->ts = ts;
    m->conv = conv;
    m->dir = dir;
    m->hops = hops;
    strncpy(m->text, text, MSG_TEXT);
    m->text[MSG_TEXT] = 0;
    blob.convs[conv].last_ts = ts ? ts : rtc_clock.getCurrentTime();
    markDirty();
    return m;
  }

  // newest-first iteration helper: returns the n-th newest message of a conversation or NULL
  Msg* nthMsg(int conv, int n) {
    uint32_t s = blob.seq_next;
    uint32_t lo = (s > MSG_MAX) ? s - MSG_MAX : 1;
    while (s > lo) {
      s--;
      Msg* m = msgBySeq(s);
      if (m && m->conv == conv) { if (n == 0) return m; n--; }
    }
    return NULL;
  }
  int countMsgs(int conv) {
    int n = 0;
    for (int i = 0; i < MSG_MAX; i++) { if (blob.msgs[i].seq && blob.msgs[i].conv == conv) n++; }
    return n;
  }

  // order conversations by last activity (newest first)
  int orderConvs(uint8_t* out) {
    int n = 0;
    for (int i = 0; i < CONV_MAX; i++) {
      if (!blob.convs[i].kind) continue;
      int j = n++;
      while (j > 0 && blob.convs[out[j - 1]].last_ts < blob.convs[i].last_ts) { out[j] = out[j - 1]; j--; }
      out[j] = i;
    }
    return n;
  }

  // ================================================================ sending (with retries)
  Pending* findPending(uint32_t seq) {
    for (int i = 0; i < PEND_MAX; i++) { if (pend[i].active && pend[i].seq == seq) return &pend[i]; }
    return NULL;
  }

  void rememberAck(Pending* p, uint32_t ack) {
    if (p->n_ack < 3) p->ack[p->n_ack++] = ack;
    else { p->ack[0] = p->ack[1]; p->ack[1] = p->ack[2]; p->ack[2] = ack; }
  }

  bool startDirect(int conv, const char* text) {
    const Conv& cv = blob.convs[conv];
    ContactInfo* c = NULL;
    Pending* slot = NULL;
    for (int i = 0; i < PEND_MAX; i++) { if (!pend[i].active) { slot = &pend[i]; break; } }
    // find the contact by its stored prefix
    int total = the_mesh.getNumContacts();
    for (int i = 0; i < total && !c; i++) {
      ContactInfo ci;
      if (the_mesh.getContactByIdx(i, ci) && memcmp(ci.id.pub_key, cv.key, 6) == 0) {
        c = the_mesh.lookupContactByPubKey(ci.id.pub_key, PUB_KEY_SIZE);
      }
    }
    if (!c) return false;
    uint32_t ts = rtc_clock.getCurrentTimeUnique();
    uint32_t ack = 0, est = 0;
    int r = the_mesh.sendMessage(*c, ts, 0, text, ack, est);
    if (r == MSG_SEND_FAILED) return false;
    Msg* m = addMsg(conv, 1, ts, text, 0xFF);
    m->state = 0;
    if (slot) {
      memset(slot, 0, sizeof(Pending));
      slot->active = true;
      slot->seq = m->seq;
      memcpy(slot->pub, c->id.pub_key, PUB_KEY_SIZE);
      rememberAck(slot, ack);
      slot->deadline = millis() + est + 1500;
    }
    return true;
  }

  bool startChannel(int conv, const char* text) {
    const Conv& cv = blob.convs[conv];
    ChannelDetails cd;
    if (!the_mesh.getChannel(cv.chan, cd) || cd.name[0] == 0) return false;
    uint32_t ts = rtc_clock.getCurrentTimeUnique();
    if (!the_mesh.sendGroupMessage(ts, cd.channel, prefs->node_name, text, strlen(text))) return false;
    Msg* m = addMsg(conv, 1, ts, text, 0xFF);
    m->state = 0;                       // channels have no acknowledgement
    return true;
  }

  void tickRetries() {
    for (int i = 0; i < PEND_MAX; i++) {
      Pending& p = pend[i];
      if (!p.active || millis() < p.deadline) continue;
      Msg* m = msgBySeq(p.seq);
      if (!m) { p.active = false; continue; }
      ContactInfo* c = the_mesh.lookupContactByPubKey(p.pub, PUB_KEY_SIZE);
      if (!c || m->tries >= RETRY_MAX) { m->state = 3; p.active = false; markDirty(); if (inThread(m->conv)) render(); continue; }
      m->tries++;
      m->state = 1;
      if (m->tries >= 2) the_mesh.resetPathTo(*c);          // route probably stale: fall back to flooding
      uint32_t ack = 0, est = 0;
      int r = the_mesh.sendMessage(*c, m->ts, m->tries, m->text, ack, est);
      if (r == MSG_SEND_FAILED) { m->state = 3; p.active = false; }
      else { rememberAck(&p, ack); p.deadline = millis() + est + 1500; }
      markDirty();
      if (inThread(m->conv)) render();
    }
  }

  bool inThread(int conv) const { return (pg == PG_THREAD || pg == PG_THREAD_ACT) && cur_conv == conv; }

  void ackReceived(uint32_t crc) {
    for (int i = 0; i < PEND_MAX; i++) {
      Pending& p = pend[i];
      if (!p.active) continue;
      for (int k = 0; k < p.n_ack; k++) {
        if (p.ack[k] == crc) {
          Msg* m = msgBySeq(p.seq);
          if (m) { m->state = 2; markDirty(); if (inThread(m->conv)) render(); }
          p.active = false;
          return;
        }
      }
    }
  }

  // ================================================================ radio staging
  void stageLoad() {
    st.freq = prefs->freq; st.bw = prefs->bw; st.sf = prefs->sf; st.cr = prefs->cr; st.tx = prefs->tx_power_dbm;
  }
  bool stageDiffers() const {
    return fabsf(st.freq - prefs->freq) > 0.0005f || fabsf(st.bw - prefs->bw) > 0.001f ||
           st.sf != prefs->sf || st.cr != prefs->cr || st.tx != prefs->tx_power_dbm;
  }
  void stageApply() {
    prefs->freq = st.freq; prefs->bw = st.bw; prefs->sf = st.sf; prefs->cr = st.cr; prefs->tx_power_dbm = st.tx;
    the_mesh.savePrefs();
    radio_driver.setParams(prefs->freq, prefs->bw, prefs->sf, prefs->cr);
    radio_driver.setTxPower(prefs->tx_power_dbm);
  }
  int presetIndexOf(const Stage& s) const {
    for (int i = 0; i < PRESET_COUNT; i++) {
      if (fabsf(PRESETS[i].freq - s.freq) < 0.0005f && PRESETS[i].sf == s.sf &&
          fabsf(PRESETS[i].bw - s.bw) < 0.001f && PRESETS[i].cr == s.cr) return i;
    }
    return -1;
  }

  // ---- everything below is added in the following parts of the file

  // ================================================================ navigation helpers
  void go(uint8_t p) {
    if (pg != PG_SPLASH && pg != PG_EDIT && sp < 10) stk[sp++] = pg;
    pg = p; sel = 0; list_pg = 0;
  }
  void open(uint8_t p) { go(p); render(); }
  void back() {
    pg = (sp > 0) ? stk[--sp] : (uint8_t) PG_HOME;
    if (pg == PG_HOME) sp = 0;
    sel = 0; list_pg = 0;
    render();
  }
  void goHome() { sp = 0; pg = PG_HOME; sel = 0; list_pg = 0; render(); }

  // returns -1 for back, >= 0 for the chosen item, -2 when nothing was chosen
  int menuInput(int k, int n) {
    if (k == '0' || k == TK_ESC) return -1;
    if (k >= '1' && k <= '9' && (k - '1') < n) { sel = k - '1'; return sel; }
    if (k == TK_DOWN || k == TK_NEXT) { sel = (sel + 1) % n; render(); return -2; }
    if (k == TK_UP || k == TK_PREV) { sel = (sel + n - 1) % n; render(); return -2; }
    if (k == TK_ENTER) return sel;
    return -2;
  }
  int onPage(int total, int per_page) {
    int n = total - list_pg * per_page;
    return n > per_page ? per_page : (n < 0 ? 0 : n);
  }
  int listInput(int k, int on_page, int total, int per_page) {
    if (k == '0' || k == TK_ESC) return -1;
    if (total > per_page) {
      if (k == '7') { if (list_pg > 0) { list_pg--; sel = 0; render(); } return -2; }
      if (k == '8') { if ((list_pg + 1) * per_page < total) { list_pg++; sel = 0; render(); } return -2; }
    }
    if (k >= '1' && k <= '9' && (k - '1') < on_page) { sel = k - '1'; return sel; }
    if (k == TK_DOWN || k == TK_NEXT) { if (on_page) sel = (sel + 1) % on_page; render(); return -2; }
    if (k == TK_UP || k == TK_PREV) { if (on_page) sel = (sel + on_page - 1) % on_page; render(); return -2; }
    if (k == TK_ENTER && on_page) return sel;
    return -2;
  }

  void item(int n, const char* text) {
    char b[TERM_COLS_MAX + 1];
    bool s = (sel == n - 1);
    snprintf(b, sizeof(b), "%c%d %s", s ? '>' : ' ', n, text);
    display.pageLine(b, TERM_WHITE, s);
  }
  void title(const char* t) { display.pageLine(t, TERM_GREEN); }

  int wrapLines(const char* text, uint16_t colr, int maxlines) {
    int w = display.columns() - 1;
    if (w < 8) w = 8;
    int n = 0;
    const char* p = text;
    char line[TERM_COLS_MAX + 1];
    while (*p && n < maxlines) {
      while (*p == ' ') p++;
      if (!*p) break;
      int len = 0, brk = -1;
      while (p[len] && len < w) { if (p[len] == ' ') brk = len; len++; }
      if (p[len] && brk > 0) len = brk;
      memcpy(line, p, len);
      line[len] = 0;
      display.pageLine(line, colr);
      p += len;
      n++;
    }
    return n;
  }

  // ================================================================ channels
  bool chanUsed(int idx) {
    ChannelDetails cd;
    return the_mesh.getChannel(idx, cd) && cd.name[0] != 0;
  }
  int usedChanCount() {
    int n = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) { if (chanUsed(i)) n++; }
    return n;
  }
  int usedSlot(int k) {
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      if (chanUsed(i)) { if (k == 0) return i; k--; }
    }
    return -1;
  }
  const char* chanName(int slot) {
    static ChannelDetails cd;
    if (the_mesh.getChannel(slot, cd)) return cd.name;
    return "?";
  }
  bool addChannelKey(const char* name, const char* psk_b64) {
    if (strlen(psk_b64) > 44) return false;
    int slot = -1;
    for (int i = 1; i < MAX_GROUP_CHANNELS; i++) { if (!chanUsed(i)) { slot = i; break; } }
    if (slot < 0) return false;
    ChannelDetails cd;
    memset(&cd, 0, sizeof(cd));
    int len = b64Decode(psk_b64, cd.channel.secret, 32);
    if (len != 16 && len != 32) return false;
    strncpy(cd.name, name, sizeof(cd.name) - 1);
    if (!the_mesh.setChannel(slot, cd)) return false;
    store.saveChannels(&the_mesh);
    return true;
  }
  void deleteChannel(int slot) {
    ChannelDetails cd;
    memset(&cd, 0, sizeof(cd));
    the_mesh.setChannel(slot, cd);
    store.saveChannels(&the_mesh);
    int c = findConvChan(slot);
    if (c >= 0) { dropConvMessages(c); memset(&blob.convs[c], 0, sizeof(Conv)); markDirty(); }
  }

  // ================================================================ contacts
  static bool matchFilter(const ContactInfo& ci, int filter) {
    switch (filter) {
      case 0: return ci.type == ADV_TYPE_CHAT;
      case 1: return ci.type == ADV_TYPE_REPEATER;
      case 2: return ci.type == ADV_TYPE_ROOM;
      default: return ci.isFav();
    }
  }
  int countFilter(int filter) {
    int n = 0, total = the_mesh.getNumContacts();
    for (int i = 0; i < total; i++) {
      ContactInfo ci;
      if (the_mesh.getContactByIdx(i, ci) && matchFilter(ci, filter)) n++;
    }
    return n;
  }
  void buildContactIndex() {                 // newest first, cached until the table changes
    cidx_n = 0;
    int total = the_mesh.getNumContacts();
    for (int i = total - 1; i >= 0 && cidx_n < MAX_CONTACTS; i--) {
      ContactInfo ci;
      if (the_mesh.getContactByIdx(i, ci) && matchFilter(ci, cur_contact_filter)) cidx[cidx_n++] = (uint8_t) i;
    }
    cidx_valid = true;
  }
  ContactInfo* contactByPub(const uint8_t* pub) { return the_mesh.lookupContactByPubKey(pub, PUB_KEY_SIZE); }
  void saveContacts() { store.saveContacts(&the_mesh); cidx_valid = false; }

  void openContactChat(const ContactInfo& ci) {
    int c = convForContact(ci.id.pub_key, ci.name);
    cur_conv = c;
    thr_off = 0;
    blob.convs[c].unread = 0;
    open(PG_THREAD);
  }

  // ================================================================ text entry
  void startEdit(uint8_t kind, const char* ttl, const char* initial, int max, uint8_t ret) {
    edit_kind = kind;
    strncpy(edit_title, ttl, sizeof(edit_title) - 1);
    edit_title[sizeof(edit_title) - 1] = 0;
    strncpy(ebuf, initial, EDIT_MAX);
    ebuf[EDIT_MAX] = 0;
    elen = strlen(ebuf);
    emax = max > EDIT_MAX ? EDIT_MAX : max;
    edit_return = ret;
    kb_present = kbd.begin(Wire);              // re-probe: the keyboard may have been plugged in since boot
    kp_cur = 0; kp_set = 0;
    pg = PG_EDIT;
    render();
  }
  void finishEdit(bool accept) {
    pg = edit_return;
    sel = 0;
    if (accept && elen > 0) editAccepted();
    else render();
  }

  // on-screen keypad (used when no CardKB is attached)
  int kpCharCount() const { return 36; }
  const char* kpChars() const {
    static const char L[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    static const char U[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static const char S[] = ".,!?:;'\"-_+=/@#()&%*$<>~ \\|^[]{}`";
    return kp_set == 0 ? L : (kp_set == 1 ? U : S);
  }
  int kpCells() const { return kpCharCount() + 5; }       // + SPC DEL shift OK ESC
  void kpSelect() {
    int n = kpCharCount();
    if (kp_cur < n) { if (elen < emax) { ebuf[elen++] = kpChars()[kp_cur]; ebuf[elen] = 0; } }
    else switch (kp_cur - n) {
      case 0: if (elen < emax) { ebuf[elen++] = ' '; ebuf[elen] = 0; } break;
      case 1: if (elen > 0) ebuf[--elen] = 0; break;
      case 2: kp_set = (kp_set + 1) % 3; break;
      case 3: finishEdit(true); return;
      case 4: finishEdit(false); return;
    }
    render();
  }

  // ================================================================ status bar
  void updateStatus() {
    char r[16];
    int y, mo, d, h, mi;
    localTime(y, mo, d, h, mi);
    int u = totalUnread();
    int n = 0;
    if (u) n += snprintf(r + n, sizeof(r) - n, "*%d ", u > 99 ? 99 : u);
    n += snprintf(r + n, sizeof(r) - n, "%02d:%02d", h, mi);
    int p = batteryPct();
    if (p >= 0 && n + 5 <= (int) sizeof(r) - 1) snprintf(r + n, sizeof(r) - n, " %d%%", p);
    display.setStatus(prefs->node_name, r);
  }

  void fmtClock(char* b, size_t n, uint32_t ts) {
    int64_t t = (int64_t) ts + utcOffsetSecs();
    if (t < 0) t = 0;
    int s = (int) (t % 86400);
    snprintf(b, n, "%02d:%02d", s / 3600, (s % 3600) / 60);
  }

  // ================================================================ chat thread drawing
  int wrapInto(char out[][TERM_COLS_MAX + 1], int max_lines, const char* text) {
    int w = display.columns() - 1;
    if (w < 8) w = 8;
    int n = 0;
    const char* p = text;
    while (*p && n < max_lines) {
      while (*p == ' ') p++;
      if (!*p) break;
      int len = 0, brk = -1;
      while (p[len] && len < w) { if (p[len] == ' ') brk = len; len++; }
      if (p[len] && brk > 0) len = brk;
      memcpy(out[n], p, len);
      out[n][len] = 0;
      p += len;
      n++;
    }
    return n;
  }

  void renderThread() {
    static char L[32][TERM_COLS_MAX + 1];
    static uint16_t C[32];
    int tr = display.bodyRows();
    int cap = tr - 1;
    if (cap > 31) cap = 31;
    int filled = 0;                                  // lines filled from the bottom
    const Conv& cv = blob.convs[cur_conv];

    for (int n = 0; filled < cap; n++) {
      Msg* m = nthMsg(cur_conv, n + thr_off);
      if (!m) break;
      static char blk[10][TERM_COLS_MAX + 1];
      int bl = 0;
      char clk[8], hdr[TERM_COLS_MAX + 1];
      fmtClock(clk, sizeof(clk), m->ts);
      uint16_t hcol;
      if (m->dir == 0) {
        snprintf(hdr, sizeof(hdr), "<%s %s", cv.name, clk);
        hcol = TERM_CYAN;
      } else {
        const char* stt = "";
        hcol = TERM_GREEN;
        char rt[12];
        if (cv.kind == 1) {
          switch (m->state) {
            case 0: stt = " Sent"; break;
            case 1: snprintf(rt, sizeof(rt), " Retry %d/%d", m->tries, RETRY_MAX); stt = rt; hcol = TERM_ORANGE; break;
            case 2: stt = " Delivered"; break;
            default: stt = " FAILED"; hcol = TERM_RED; break;
          }
        } else stt = " Sent";
        snprintf(hdr, sizeof(hdr), ">Me %s%s", clk, stt);
      }
      strncpy(blk[bl], hdr, TERM_COLS_MAX); blk[bl][TERM_COLS_MAX] = 0; bl++;
      bl += wrapInto(&blk[bl], 9, m->text);
      int room = cap - filled;
      int take = bl > room ? room : bl;
      int start = bl - take;                          // keep the bottom part if the block does not fit
      for (int i = take - 1; i >= 0; i--) {
        int dst = cap - 1 - filled - (take - 1 - i);
        strncpy(L[dst], blk[start + i], TERM_COLS_MAX);
        L[dst][TERM_COLS_MAX] = 0;
        C[dst] = (start + i == 0) ? hcol : TERM_WHITE;
      }
      filled += take;
    }
    title(cv.name);
    for (int i = cap - filled; i < cap; i++) display.pageLine(L[i], C[i]);
    if (filled == 0) display.pageLine("(no messages)", TERM_GREY);
    foot("1 Rep 2 Quick 3 More");
  }

  // ================================================================ rendering of every page
  void render() {
    display.pageClear();
    char b[48], f[16];
    switch (pg) {

    case PG_SPLASH: {
      bool big = display.columns() >= 30;
      int n = big ? LOGO_L_N : LOGO_S_N;
      const char* const* lg = big ? LOGO_L : LOGO_S;
      display.pageLine("", TERM_WHITE);
      for (int i = 0; i < n; i++) display.pageLine(lg[i], i < n - 1 ? TERM_GREEN : TERM_WHITE);
      display.pageLine("", TERM_WHITE);
      display.pageLine(" RP_Pico pager", TERM_GREY);
      display.pageLine(UI_VERSION, TERM_GREY);
      foot("any key to start");
      break; }

    case PG_ONB_WELCOME: {
      bool big = display.columns() >= 30;
      int n = big ? LOGO_L_N : LOGO_S_N;
      const char* const* lg = big ? LOGO_L : LOGO_S;
      for (int i = 0; i < n; i++) display.pageLine(lg[i], i < n - 1 ? TERM_GREEN : TERM_WHITE);
      display.pageLine("", TERM_WHITE);
      display.pageLine("Welcome! Let's set", TERM_WHITE);
      display.pageLine("up your pager.", TERM_WHITE);
      item(1, "Start setup");
      item(2, "Skip");
      foot("1-2 or Up/Dn+Enter");
      break; }

    case PG_ONB_NAME:
      title("== 1/3 NAME ==");
      display.pageLine("Device name:", TERM_GREY);
      display.pageLine(prefs->node_name, TERM_YELLOW);
      item(1, "Edit name");
      item(2, "Next");
      foot("0-back");
      break;

    case PG_ONB_REGION: {
      title("== 2/3 REGION ==");
      int np = onPage(PRESET_COUNT, LP);
      if (sel >= np) sel = 0;
      for (int d = 0; d < np; d++) {
        snprintf(b, sizeof(b), "%c%d %s", sel == d ? '>' : ' ', d + 1, PRESETS[list_pg * LP + d].name);
        display.pageLine(b, TERM_WHITE, sel == d);
      }
      foot(PRESET_COUNT > LP ? "<7/8> 0-back" : "0-back");
      break; }

    case PG_ONB_TIME: {
      title("== 3/3 TIME ==");
      int y, mo, d, h, mi;
      localTime(y, mo, d, h, mi);
      snprintf(b, sizeof(b), "%04d-%02d-%02d %02d:%02d", y, mo, d, h, mi);
      display.pageLine(b, TERM_YELLOW);
      snprintf(b, sizeof(b), "UTC%+d", (int) prefs->tz_offset);
      display.pageLine(b, TERM_GREY);
      item(1, "Set time");
      item(2, "Time zone");
      item(3, "Next");
      foot("0-back");
      break; }

    case PG_ONB_DONE:
      display.pageLine("", TERM_WHITE);
      display.pageLine("  All set!", TERM_GREEN);
      display.pageLine("", TERM_WHITE);
      display.pageLine("Send an advert so", TERM_WHITE);
      display.pageLine("nearby nodes see you:", TERM_WHITE);
      display.pageLine("Home > 2 Advert", TERM_YELLOW);
      display.pageLine("", TERM_WHITE);
      item(1, "Go to home");
      foot("");
      break;

    case PG_HOME: {
      int u = totalUnread();
      title("== MESHCORE ==");
      item(1, "Set radio");
      item(2, "Advert");
      if (u) snprintf(b, sizeof(b), "Messages (%d new)", u); else snprintf(b, sizeof(b), "Messages");
      item(3, b);
      item(4, "Contacts");
      item(5, "Settings");
      item(6, "Channels");
      item(7, "Heard nearby");
      item(8, "Airtime");
      item(9, "About");
      foot("1-9 or Up/Dn+Enter");
      break; }

    case PG_RADIO: {
      title("== SET RADIO ==");
      int pi = presetIndexOf(st);
      snprintf(b, sizeof(b), "Region %s", pi >= 0 ? PRESETS[pi].name : "Custom");
      item(1, b);
      snprintf(b, sizeof(b), "Freq %s", fmtFreq(st.freq, f, sizeof(f)));      item(2, b);
      snprintf(b, sizeof(b), "BW   %s", fmtBw(st.bw, f, sizeof(f)));           item(3, b);
      snprintf(b, sizeof(b), "SF   %d", (int) st.sf);                          item(4, b);
      snprintf(b, sizeof(b), "CR   4/%d", (int) st.cr);                        item(5, b);
      snprintf(b, sizeof(b), "TX   %d dBm", (int) st.tx);                      item(6, b);
      if (stageDiffers()) item(7, "Apply changes");
      foot(stageDiffers() ? "not applied yet" : "0-back");
      break; }

    case PG_PRESETS: {
      title("== REGION ==");
      int np = onPage(PRESET_COUNT, LP);
      if (sel >= np) sel = 0;
      for (int d = 0; d < np; d++) {
        snprintf(b, sizeof(b), "%c%d %s", sel == d ? '>' : ' ', d + 1, PRESETS[list_pg * LP + d].name);
        display.pageLine(b, TERM_WHITE, sel == d);
      }
      foot(PRESET_COUNT > LP ? "<7/8> 0 back" : "0-back");
      break; }

    case PG_FREQ:
      title("== FREQUENCY ==");
      snprintf(b, sizeof(b), "  %s MHz", fmtFreq(st.freq, f, sizeof(f)));
      display.pageLine(b, TERM_YELLOW);
      item(1, "+1 MHz");
      item(2, "-1 MHz");
      item(3, "+25 kHz");
      item(4, "-25 kHz");
      item(5, "Type a value");
      foot("0-back");
      break;

    case PG_BW:
      title("== BANDWIDTH kHz ==");
      for (int i = 0; i < 8; i++) {
        snprintf(b, sizeof(b), "%s%s", fmtBw(BW_LIST[i], f, sizeof(f)), fabsf(BW_LIST[i] - st.bw) < 0.01f ? " *" : "");
        item(i + 1, b);
      }
      foot("0-back");
      break;
    case PG_SF:
      title("== SPREAD FACTOR ==");
      for (int i = 0; i < 8; i++) { snprintf(b, sizeof(b), "SF%d%s", 5 + i, (5 + i == st.sf) ? " *" : ""); item(i + 1, b); }
      foot("0-back");
      break;
    case PG_CR:
      title("== CODING RATE ==");
      for (int i = 0; i < 4; i++) { snprintf(b, sizeof(b), "4/%d%s", 5 + i, (5 + i == st.cr) ? " *" : ""); item(i + 1, b); }
      foot("0-back");
      break;
    case PG_TX:
      title("== TX POWER ==");
      for (int i = 0; i < 8; i++) { snprintf(b, sizeof(b), "%d dBm%s", (int) TX_LIST[i], TX_LIST[i] == st.tx ? " *" : ""); item(i + 1, b); }
      foot("0-back");
      break;

    case PG_ADVERT:
      title("== ADVERT ==");
      for (int i = 0; i < ICON_ADVERT_N && 4 + i < display.bodyRows() - 1; i++) display.pageLine(ICON_ADVERT[i], TERM_CYAN);
      item(1, "Send (flood)");
      item(2, "Send (local only)");
      foot("0-back");
      break;

    case PG_MSGS: {
      title("== MESSAGES ==");
      uint8_t ord[CONV_MAX];
      int total = orderConvs(ord);
      int np = onPage(total, LP);
      if (total == 0) display.pageLine("(no chats yet)", TERM_GREY);
      if (sel >= np) sel = 0;
      for (int d = 0; d < np; d++) {
        const Conv& cv = blob.convs[ord[list_pg * LP + d]];
        Msg* m = nthMsg(ord[list_pg * LP + d], 0);
        if (cv.unread) snprintf(b, sizeof(b), "%c%d*%s %s", sel == d ? '>' : ' ', d + 1, cv.name, m ? m->text : "");
        else snprintf(b, sizeof(b), "%c%d %s%s%s", sel == d ? '>' : ' ', d + 1, cv.name, m ? " " : "", m ? m->text : "");
        display.pageLine(b, cv.unread ? TERM_CYAN : TERM_WHITE, sel == d);
      }
      foot(total > LP ? "<7/8> 0-back" : "0-back");
      break; }

    case PG_THREAD:
      renderThread();
      break;

    case PG_THREAD_ACT: {
      title(blob.convs[cur_conv].name);
      item(1, "Reply (type)");
      item(2, "Quick reply");
      item(3, "Resend last failed");
      item(4, "Clear this chat");
      foot("0-back");
      break; }

    case PG_QUICK:
      title("== QUICK REPLY ==");
      for (int i = 0; i < 6; i++) item(i + 1, qr[i]);
      foot("0-back");
      break;

    case PG_CONTACTS:
      title("== CONTACTS ==");
      snprintf(b, sizeof(b), "People (%d)", countFilter(0));     item(1, b);
      snprintf(b, sizeof(b), "Repeaters (%d)", countFilter(1));  item(2, b);
      snprintf(b, sizeof(b), "Rooms (%d)", countFilter(2));      item(3, b);
      snprintf(b, sizeof(b), "Favourites (%d)", countFilter(3)); item(4, b);
      item(5, "Heard nearby");
      foot("0-back");
      break;

    case PG_CONTACT_LIST: {
      static const char* const T[4] = { "== PEOPLE ==", "== REPEATERS ==", "== ROOMS ==", "== FAVOURITES ==" };
      title(T[cur_contact_filter]);
      if (!cidx_valid) buildContactIndex();
      int np = onPage(cidx_n, LP);
      if (cidx_n == 0) display.pageLine("(none yet)", TERM_GREY);
      if (sel >= np) sel = 0;
      for (int d = 0; d < np; d++) {
        ContactInfo ci;
        if (the_mesh.getContactByIdx(cidx[list_pg * LP + d], ci)) {
          snprintf(b, sizeof(b), "%c%d %s%s", sel == d ? '>' : ' ', d + 1, ci.isFav() ? "+" : "", ci.name);
          display.pageLine(b, TERM_WHITE, sel == d);
        }
      }
      foot(cidx_n > LP ? "<7/8> 0-back" : "0-back");
      break; }

    case PG_CONTACT: {
      ContactInfo* c = contactByPub(cur_pub);
      if (!c) { display.pageLine("(contact gone)", TERM_ORANGE); foot("0-back"); break; }
      title(c->name);
      const char* tn = c->type == ADV_TYPE_CHAT ? "person" : c->type == ADV_TYPE_REPEATER ? "repeater" : c->type == ADV_TYPE_ROOM ? "room" : "other";
      char ago[12];
      fmtAgo(ago, sizeof(ago), c->last_advert_timestamp);
      snprintf(b, sizeof(b), "%s, heard %s", tn, ago);
      display.pageLine(b, TERM_GREY);
      if (c->out_path_len == OUT_PATH_UNKNOWN) display.pageLine("route: flood", TERM_GREY);
      else { snprintf(b, sizeof(b), "route: %d hops", (int) c->out_path_len); display.pageLine(b, TERM_GREY); }
      int n = 1;
      if (c->type == ADV_TYPE_CHAT || c->type == ADV_TYPE_ROOM) item(n++, "Message");
      item(n++, c->isFav() ? "Remove favourite" : "Make favourite");
      item(n++, "Reset route");
      item(n++, "Remove contact");
      foot("0-back");
      break; }

    case PG_CONTACT_DEL: {
      ContactInfo* c = contactByPub(cur_pub);
      display.pageLine("Remove contact?", TERM_ORANGE);
      display.pageLine(c ? c->name : "?", TERM_WHITE);
      display.pageLine("", TERM_WHITE);
      item(1, "Yes, remove");
      foot("0-cancel");
      break; }

    case PG_HEARD: {
      title("== HEARD NEARBY ==");
      static AdvertPath heard[8];
      int n = the_mesh.getRecentlyHeard(heard, 8);
      if (n == 0) display.pageLine("(nothing yet)", TERM_GREY);
      for (int i = 0; i < n && i < 8; i++) {
        char ago[12];
        fmtAgo(ago, sizeof(ago), heard[i].recv_timestamp);
        snprintf(b, sizeof(b), "%s %s", heard[i].name[0] ? heard[i].name : "?", ago);
        display.pageLine(b, TERM_WHITE);
      }
      foot("0-back");
      break; }

    case PG_CHANNELS: {
      title("== CHANNELS ==");
      int used = usedChanCount();
      int total = used + 2;
      int np = onPage(total, 8);
      if (sel >= np) sel = 0;
      for (int d = 0; d < np; d++) {
        int k = list_pg * 8 + d;
        if (k < used) snprintf(b, sizeof(b), "%c%d %s", sel == d ? '>' : ' ', d + 1, chanName(usedSlot(k)));
        else if (k == used) snprintf(b, sizeof(b), "%c%d + New private", sel == d ? '>' : ' ', d + 1);
        else snprintf(b, sizeof(b), "%c%d + Join with key", sel == d ? '>' : ' ', d + 1);
        display.pageLine(b, TERM_WHITE, sel == d);
      }
      foot("0-back");
      break; }

    case PG_CHAN_ITEM:
      snprintf(b, sizeof(b), "#%s", chanName(cur_chan));
      title(b);
      item(1, "Open chat");
      if (cur_chan != 0) item(2, "Delete channel");
      foot("0-back");
      break;

    case PG_CHAN_DEL:
      display.pageLine("Delete channel?", TERM_ORANGE);
      snprintf(b, sizeof(b), "#%s", chanName(cur_chan));
      display.pageLine(b, TERM_WHITE);
      display.pageLine("", TERM_WHITE);
      item(1, "Yes, delete");
      foot("0-cancel");
      break;

    case PG_CHAN_KEY_SHOW: {
      title("Channel created");
      snprintf(b, sizeof(b), "#%s", tmp_name);
      display.pageLine(b, TERM_WHITE);
      display.pageLine("Share this key:", TERM_GREY);
      int w = display.columns() - 1;
      char line[TERM_COLS_MAX + 1];
      for (int i = 0; tmp_key[i] && i < 48; i += w) {
        strncpy(line, &tmp_key[i], w);
        line[w] = 0;
        display.pageLine(line, TERM_YELLOW);
      }
      foot("0-back");
      break; }

    case PG_AIRTIME: {
      title("== AIRTIME ==");
      unsigned long up_s = millis() / 1000;
      unsigned long tx = the_mesh.getTotalAirTime();       // ms
      unsigned long rx = the_mesh.getReceiveAirTime();
      snprintf(b, sizeof(b), "up   %luh%02lum", up_s / 3600, (up_s % 3600) / 60);
      display.pageLine(b, TERM_WHITE);
      snprintf(b, sizeof(b), "TX   %lu.%lus", tx / 1000, (tx % 1000) / 100);
      display.pageLine(b, TERM_WHITE);
      unsigned long p100 = up_s ? (tx * 10UL) / up_s : 0;          // duty cycle in hundredths of a percent
      snprintf(b, sizeof(b), "duty %lu.%02lu%%", p100 / 100, p100 % 100);
      display.pageLine(b, TERM_WHITE);
      snprintf(b, sizeof(b), "RX   %lu.%lus", rx / 1000, (rx % 1000) / 100);
      display.pageLine(b, TERM_WHITE);
      snprintf(b, sizeof(b), "sent F%lu D%lu", (unsigned long) the_mesh.getNumSentFlood(), (unsigned long) the_mesh.getNumSentDirect());
      display.pageLine(b, TERM_WHITE);
      snprintf(b, sizeof(b), "recv F%lu D%lu", (unsigned long) the_mesh.getNumRecvFlood(), (unsigned long) the_mesh.getNumRecvDirect());
      display.pageLine(b, TERM_WHITE);
      foot("0-back");
      break; }

    case PG_SETTINGS:
      title("== SETTINGS ==");
      item(1, "Node name");
      item(2, "Radio");
      item(3, "Display");
      item(4, "Set time");
      item(5, "Time zone");
      item(6, "Quick replies");
      item(7, "Channels");
      item(8, "About");
      item(9, "Reboot");
      foot("0-back");
      break;

    case PG_DISPLAY:
      title("== DISPLAY ==");
      snprintf(b, sizeof(b), "Rotation %d", (int) up.rot);          item(1, b);
      snprintf(b, sizeof(b), "Brightness %d%%", (int) up.bright);   item(2, b);
      if (up.sleep_s == 0) snprintf(b, sizeof(b), "Sleep: never");
      else if (up.sleep_s < 60) snprintf(b, sizeof(b), "Sleep: %ds", (int) up.sleep_s);
      else snprintf(b, sizeof(b), "Sleep: %dmin", (int) (up.sleep_s / 60));
      item(3, b);
      foot("0-back");
      break;

    case PG_TIME: {
      title("== SET TIME ==");
      static const char* const names[5] = { "year", "month", "day", "hour", "minute" };
      char ln[24];
      snprintf(ln, sizeof(ln), "%04d-%02d-%02d %02d:%02d", tm[0], tm[1], tm[2], tm[3], tm[4]);
      display.pageLine(ln, TERM_YELLOW);
      // marker under the active field
      static const int pos[5] = { 0, 5, 8, 11, 14 };
      static const int wid[5] = { 4, 2, 2, 2, 2 };
      char mk[24];
      memset(mk, ' ', sizeof(mk));
      for (int i = 0; i < wid[tm_field]; i++) mk[pos[tm_field] + i] = '^';
      mk[19] = 0;
      display.pageLine(mk, TERM_YELLOW);
      snprintf(b, sizeof(b), "field: %s", names[tm_field]);
      display.pageLine(b, TERM_GREY);
      display.pageLine("Up/Dn or click: +/-", TERM_GREY);
      display.pageLine("Enter/long: next", TERM_GREY);
      foot("Esc/dbl-click: back");
      break; }

    case PG_TZ:
      title("== TIME ZONE ==");
      snprintf(b, sizeof(b), "  UTC%+d", (int) prefs->tz_offset);
      display.pageLine(b, TERM_YELLOW);
      item(1, "+1 hour");
      item(2, "-1 hour");
      foot("0-back");
      break;

    case PG_QR_LIST:
      title("== QUICK REPLIES ==");
      for (int i = 0; i < 6; i++) item(i + 1, qr[i]);
      foot("0-back");
      break;

    case PG_ABOUT: {
      title("== ABOUT ==");
      snprintf(b, sizeof(b), "Name %s", prefs->node_name);
      display.pageLine(b, TERM_WHITE);
      char hex[16];
      mesh::Utils::toHex(hex, the_mesh.self_id.pub_key, 6);
      snprintf(b, sizeof(b), "ID   %s", hex);
      display.pageLine(b, TERM_WHITE);
      snprintf(b, sizeof(b), "Freq %s", fmtFreq(prefs->freq, f, sizeof(f)));
      display.pageLine(b, TERM_WHITE);
      snprintf(b, sizeof(b), "BW %s SF%d CR4/%d", fmtBw(prefs->bw, f, sizeof(f)), (int) prefs->sf, (int) prefs->cr);
      display.pageLine(b, TERM_WHITE);
      snprintf(b, sizeof(b), "Heap %uKB free", (unsigned) (rp2040.getFreeHeap() / 1024));
      display.pageLine(b, TERM_WHITE);
      snprintf(b, sizeof(b), "Temp %dC Bat %umV", (int) analogReadTemp(), (unsigned) board.getBattMilliVolts());
      display.pageLine(b, TERM_WHITE);
      snprintf(b, sizeof(b), "Contacts %d", the_mesh.getNumContacts());
      display.pageLine(b, TERM_WHITE);
      display.pageLine(UI_VERSION, TERM_GREY);
      foot("0-back");
      break; }

    case PG_REBOOT:
      title("== REBOOT ==");
      for (int i = 0; i < ICON_POWER_N && 3 + i < display.bodyRows() - 3; i++) display.pageLine(ICON_POWER[i], TERM_RED);
      item(1, "Reboot now");
      foot("0-cancel");
      break;

    case PG_EDIT:
      renderEdit();
      break;
    }
    display.pageShow();
  }

  void renderEdit() {
    display.pageLine(edit_title, TERM_GREEN);
    int w = display.columns() - 3;
    if (w < 4) w = 4;
    const char* tail = ebuf + ((elen > w) ? (elen - w) : 0);
    char b[TERM_COLS_MAX + 4];
    if (kb_present) {
      snprintf(b, sizeof(b), "> %s_", tail);
      display.pageLine("Type, then Enter", TERM_GREY);
      display.pageLine("Esc = cancel", TERM_GREY);
      display.pageFooter(b, TERM_YELLOW);
      return;
    }
    // keypad grid: click = next, triple = previous, long = select, double = cancel
    snprintf(b, sizeof(b), "%s_", tail);
    display.pageLine(b, TERM_YELLOW);
    int per = display.columns() / 3;
    if (per < 3) per = 3;
    int nch = kpCharCount();
    for (int r0 = 0; r0 < nch; r0 += per) {
      char line[TERM_COLS_MAX + 1];
      int p = 0;
      bool has = false;
      for (int i = r0; i < r0 + per && i < nch && p + 3 <= TERM_COLS_MAX; i++) {
        bool cur = (i == kp_cur);
        line[p++] = cur ? '[' : ' ';
        line[p++] = kpChars()[i];
        line[p++] = cur ? ']' : ' ';
        if (cur) has = true;
      }
      line[p] = 0;
      display.pageLine(line, has ? TERM_YELLOW : TERM_WHITE);
    }
    {
      static const char* const SP[5] = { "SPC", "DEL", "Aa#", "OK", "ESC" };
      char line[TERM_COLS_MAX + 1];
      int p = 0;
      bool has = false;
      for (int i = 0; i < 5; i++) {
        bool cur = (kp_cur == nch + i);
        if (cur) has = true;
        p += snprintf(&line[p], sizeof(line) - p, "%s%s%s%s", cur ? "[" : "", SP[i], cur ? "]" : "", i < 4 ? " " : "");
      }
      line[TERM_COLS_MAX] = 0;
      display.pageLine(line, has ? TERM_YELLOW : TERM_WHITE);
    }
    display.pageFooter("click next long pick", TERM_GREY);
  }

  // ================================================================ actions
  void leaveSplash() {
    if (!up.onboarded && strcmp(prefs->node_name, "NONAME") != 0) { up.onboarded = 1; saveUiPrefs(); }
    if (!up.onboarded) { pg = PG_ONB_WELCOME; sel = 0; sp = 0; render(); }
    else goHome();
  }
  void finishOnboarding() { up.onboarded = 1; saveUiPrefs(); goHome(); }

  void popToThread() {                       // leave action pages, landing on the chat thread again
    while (sp > 0) { uint8_t t = stk[--sp]; if (t == PG_THREAD) break; }
    pg = PG_THREAD;
  }

  void sendText(const char* text) {
    if (cur_conv == NO_CONV || !text[0]) return;
    bool ok = (blob.convs[cur_conv].kind == 1) ? startDirect(cur_conv, text) : startChannel(cur_conv, text);
    thr_off = 0;
    if (ok) render(); else toast("send failed");
  }

  void resendFailed() {
    for (int n = 0; n < MSG_MAX; n++) {
      Msg* m = nthMsg(cur_conv, n);
      if (!m) break;
      if (m->dir == 1 && m->state == 3) {
        char t[MSG_TEXT + 1];
        strncpy(t, m->text, sizeof(t));
        m->seq = 0;                                       // drop the failed copy, send a fresh one
        sendText(t);
        return;
      }
    }
    toast("nothing to resend");
  }

  void clearChat() {
    for (int i = 0; i < MSG_MAX; i++) { if (blob.msgs[i].seq && blob.msgs[i].conv == cur_conv) blob.msgs[i].seq = 0; }
    markDirty();
    thr_off = 0;
  }

  void sendAdvert(bool flood) {
    mesh::Packet* pkt;
    if (prefs->advert_loc_policy == ADVERT_LOC_NONE) pkt = the_mesh.createSelfAdvert(prefs->node_name);
    else pkt = the_mesh.createSelfAdvert(prefs->node_name, sensors.node_lat, sensors.node_lon);
    if (!pkt) { toast("advert failed"); return; }
    if (flood) the_mesh.sendFlood(pkt); else the_mesh.sendZeroHop(pkt);
    toast(flood ? "Advert sent (flood)" : "Advert sent (local)");
  }

  void applyPreset(int i) {
    st.freq = PRESETS[i].freq; st.bw = PRESETS[i].bw; st.sf = PRESETS[i].sf; st.cr = PRESETS[i].cr;
  }

  void timeLoad() {
    localTime(tm[0], tm[1], tm[2], tm[3], tm[4]);
    if (tm[0] < 2024) tm[0] = 2024;
    tm_field = 0;
  }
  static int daysIn(int y, int m) {
    static const int D[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return D[(m - 1) % 12];
  }
  void timeAdj(int delta) {
    static const int lo[5] = { 2024, 1, 1, 0, 0 };
    static const int hi[5] = { 2099, 12, 31, 23, 59 };
    int f = tm_field;
    tm[f] += delta;
    if (tm[f] > hi[f]) tm[f] = lo[f];
    if (tm[f] < lo[f]) tm[f] = hi[f];
    int dm = daysIn(tm[0], tm[1]);
    if (tm[2] > dm) tm[2] = dm;
    render();
  }
  void timeSave() {
    int64_t days = daysFromCivil(tm[0], tm[1], tm[2]);
    int64_t t = days * 86400 + tm[3] * 3600 + tm[4] * 60 - utcOffsetSecs();
    if (t < 0) t = 0;
    rtc_clock.setCurrentTime((uint32_t) t);
    updateStatus();
  }

  void editAccepted() {
    switch (edit_kind) {
      case E_NAME:
        strncpy(prefs->node_name, ebuf, sizeof(prefs->node_name) - 1);
        prefs->node_name[sizeof(prefs->node_name) - 1] = 0;
        the_mesh.savePrefs();
        updateStatus();
        render();
        break;
      case E_MSG:
        sendText(ebuf);
        break;
      case E_QR:
        strncpy(qr[qr_edit], ebuf, 40);
        qr[qr_edit][40] = 0;
        saveQuick();
        render();
        break;
      case E_FREQ: {
        float f = atof(ebuf);
        if (f >= 150.0f && f <= 960.0f) st.freq = f; else toast("out of range");
        render();
        break; }
      case E_CHAN_NEW: {
        uint8_t secret[16];
        fast_rng.random(secret, sizeof(secret));
        char b64[48];
        b64Encode(secret, sizeof(secret), b64);
        if (addChannelKey(ebuf, b64)) {
          strncpy(tmp_name, ebuf, sizeof(tmp_name) - 1); tmp_name[sizeof(tmp_name) - 1] = 0;
          strncpy(tmp_key, b64, sizeof(tmp_key) - 1); tmp_key[sizeof(tmp_key) - 1] = 0;
          go(PG_CHAN_KEY_SHOW);
          render();
        } else { toast("no free slot"); }
        break; }
      case E_CHAN_JOIN_NAME:
        strncpy(tmp_name, ebuf, sizeof(tmp_name) - 1);
        tmp_name[sizeof(tmp_name) - 1] = 0;
        startEdit(E_CHAN_JOIN_KEY, "Key (base64)", "", 44, PG_CHANNELS);
        break;
      case E_CHAN_JOIN_KEY:
        if (addChannelKey(tmp_name, ebuf)) { render(); toast("Channel added"); }
        else toast("bad key or full");
        break;
    }
  }

  // ================================================================ key handling
  void handleKey(int k) {
    int c;
    switch (pg) {

    case PG_SPLASH: leaveSplash(); break;

    case PG_ONB_WELCOME:
      c = menuInput(k, 2);
      if (c == 0) { pg = PG_ONB_NAME; sel = 0; render(); }
      else if (c == 1) finishOnboarding();
      break;
    case PG_ONB_NAME:
      c = menuInput(k, 2);
      if (c == -1) { pg = PG_ONB_WELCOME; sel = 0; render(); }
      else if (c == 0) startEdit(E_NAME, "Device name", prefs->node_name, 31, PG_ONB_NAME);
      else if (c == 1) { pg = PG_ONB_REGION; sel = 0; list_pg = 0; stageLoad(); render(); }
      break;
    case PG_ONB_REGION:
      c = listInput(k, onPage(PRESET_COUNT, LP), PRESET_COUNT, LP);
      if (c == -1) { pg = PG_ONB_NAME; sel = 0; render(); }
      else if (c >= 0) { applyPreset(list_pg * LP + c); stageApply(); pg = PG_ONB_TIME; sel = 0; render(); }
      break;
    case PG_ONB_TIME:
      c = menuInput(k, 3);
      if (c == -1) { pg = PG_ONB_REGION; sel = 0; list_pg = 0; render(); }
      else if (c == 0) { timeLoad(); open(PG_TIME); }
      else if (c == 1) open(PG_TZ);
      else if (c == 2) { pg = PG_ONB_DONE; sel = 0; render(); }
      break;
    case PG_ONB_DONE:
      c = menuInput(k, 1);
      if (c == 0) finishOnboarding();
      break;

    case PG_HOME:
      c = menuInput(k, 9);
      switch (c) {
        case 0: stageLoad(); open(PG_RADIO); break;
        case 1: open(PG_ADVERT); break;
        case 2: open(PG_MSGS); break;
        case 3: open(PG_CONTACTS); break;
        case 4: open(PG_SETTINGS); break;
        case 5: open(PG_CHANNELS); break;
        case 6: open(PG_HEARD); break;
        case 7: open(PG_AIRTIME); break;
        case 8: open(PG_ABOUT); break;
      }
      break;

    case PG_RADIO:
      c = menuInput(k, stageDiffers() ? 7 : 6);
      if (c == -1) back();
      else if (c == 0) open(PG_PRESETS);
      else if (c == 1) open(PG_FREQ);
      else if (c == 2) open(PG_BW);
      else if (c == 3) open(PG_SF);
      else if (c == 4) open(PG_CR);
      else if (c == 5) open(PG_TX);
      else if (c == 6) { stageApply(); sel = 0; toast("Radio applied"); }
      break;
    case PG_PRESETS:
      c = listInput(k, onPage(PRESET_COUNT, LP), PRESET_COUNT, LP);
      if (c == -1) back();
      else if (c >= 0) { applyPreset(list_pg * LP + c); back(); }
      break;
    case PG_FREQ:
      c = menuInput(k, 5);
      if (c == -1) back();
      else if (c >= 0 && c <= 3) {
        static const float step[4] = { 1.0f, -1.0f, 0.025f, -0.025f };
        float nf = st.freq + step[c];
        if (nf >= 150.0f && nf <= 960.0f) st.freq = nf; else toast("out of range");
        render();
      }
      else if (c == 4) startEdit(E_FREQ, "Frequency MHz", "", 8, PG_FREQ);
      break;
    case PG_BW:
      c = menuInput(k, 8);
      if (c == -1) back(); else if (c >= 0) { st.bw = BW_LIST[c]; back(); }
      break;
    case PG_SF:
      c = menuInput(k, 8);
      if (c == -1) back(); else if (c >= 0) { st.sf = 5 + c; back(); }
      break;
    case PG_CR:
      c = menuInput(k, 4);
      if (c == -1) back(); else if (c >= 0) { st.cr = 5 + c; back(); }
      break;
    case PG_TX:
      c = menuInput(k, 8);
      if (c == -1) back(); else if (c >= 0) { st.tx = TX_LIST[c]; back(); }
      break;

    case PG_ADVERT:
      c = menuInput(k, 2);
      if (c == -1) back();
      else if (c == 0) sendAdvert(true);
      else if (c == 1) sendAdvert(false);
      break;

    case PG_MSGS: {
      uint8_t ord[CONV_MAX];
      int total = orderConvs(ord);
      c = listInput(k, onPage(total, LP), total, LP);
      if (c == -1) back();
      else if (c >= 0) {
        cur_conv = ord[list_pg * LP + c];
        thr_off = 0;
        blob.convs[cur_conv].unread = 0;
        updateStatus();
        open(PG_THREAD);
      }
      break; }

    case PG_THREAD: {
      int cnt = countMsgs(cur_conv);
      if (k == '0' || k == TK_ESC) { back(); }
      else if (k == '1') startEdit(E_MSG, "Message", "", MSG_TEXT, PG_THREAD);
      else if (k == '2') open(PG_QUICK);
      else if (k == '3' || k == TK_ENTER) open(PG_THREAD_ACT);
      else if (k == TK_UP) { if (thr_off < cnt - 1) { thr_off++; render(); } }
      else if (k == TK_DOWN) { if (thr_off > 0) { thr_off--; render(); } }
      else if (k == TK_NEXT) { thr_off = (thr_off < cnt - 1) ? thr_off + 1 : 0; render(); }
      else if (k == TK_PREV) { thr_off = (thr_off > 0) ? thr_off - 1 : (cnt > 0 ? cnt - 1 : 0); render(); }
      break; }
    case PG_THREAD_ACT:
      c = menuInput(k, 4);
      if (c == -1) back();
      else if (c == 0) { popToThread(); startEdit(E_MSG, "Message", "", MSG_TEXT, PG_THREAD); }
      else if (c == 1) open(PG_QUICK);
      else if (c == 2) { popToThread(); resendFailed(); }
      else if (c == 3) { popToThread(); clearChat(); render(); }
      break;
    case PG_QUICK:
      c = menuInput(k, 6);
      if (c == -1) back();
      else if (c >= 0) { popToThread(); sendText(qr[c]); }
      break;

    case PG_CONTACTS:
      c = menuInput(k, 5);
      if (c == -1) back();
      else if (c >= 0 && c <= 3) { cur_contact_filter = c; cidx_valid = false; open(PG_CONTACT_LIST); }
      else if (c == 4) open(PG_HEARD);
      break;
    case PG_CONTACT_LIST:
      if (!cidx_valid) buildContactIndex();
      c = listInput(k, onPage(cidx_n, LP), cidx_n, LP);
      if (c == -1) back();
      else if (c >= 0) {
        ContactInfo ci;
        if (the_mesh.getContactByIdx(cidx[list_pg * LP + c], ci)) { memcpy(cur_pub, ci.id.pub_key, PUB_KEY_SIZE); open(PG_CONTACT); }
      }
      break;
    case PG_CONTACT: {
      ContactInfo* ct = contactByPub(cur_pub);
      if (!ct) { back(); break; }
      bool has_msg = (ct->type == ADV_TYPE_CHAT || ct->type == ADV_TYPE_ROOM);
      c = menuInput(k, has_msg ? 4 : 3);
      if (c == -1) { back(); break; }
      if (c < 0) break;
      int act = has_msg ? c : c + 1;                  // 0 message, 1 favourite, 2 route, 3 remove
      if (act == 0) openContactChat(*ct);
      else if (act == 1) { ct->flags ^= 0x01; saveContacts(); render(); }
      else if (act == 2) { the_mesh.resetPathTo(*ct); saveContacts(); toast("Route reset"); }
      else open(PG_CONTACT_DEL);
      break; }
    case PG_CONTACT_DEL:
      c = menuInput(k, 1);
      if (c == -1) back();
      else if (c == 0) {
        ContactInfo* ct = contactByPub(cur_pub);
        if (ct) the_mesh.removeContact(*ct);
        saveContacts();
        back(); back();
        toast("Removed");
      }
      break;
    case PG_HEARD:
    case PG_AIRTIME:
    case PG_ABOUT:
    case PG_CHAN_KEY_SHOW:
      if (k == '0' || k == TK_ESC || k == TK_ENTER) back();
      break;

    case PG_CHANNELS: {
      int used = usedChanCount();
      c = listInput(k, onPage(used + 2, 8), used + 2, 8);
      if (c == -1) back();
      else if (c >= 0) {
        int kk = list_pg * 8 + c;
        if (kk < used) { cur_chan = usedSlot(kk); open(PG_CHAN_ITEM); }
        else if (kk == used) startEdit(E_CHAN_NEW, "New channel name", "", 23, PG_CHANNELS);
        else startEdit(E_CHAN_JOIN_NAME, "Channel name", "", 23, PG_CHANNELS);
      }
      break; }
    case PG_CHAN_ITEM:
      c = menuInput(k, cur_chan != 0 ? 2 : 1);
      if (c == -1) back();
      else if (c == 0) {
        cur_conv = convForChannel(cur_chan, chanName(cur_chan));
        thr_off = 0;
        blob.convs[cur_conv].unread = 0;
        updateStatus();
        open(PG_THREAD);
      }
      else if (c == 1) open(PG_CHAN_DEL);
      break;
    case PG_CHAN_DEL:
      c = menuInput(k, 1);
      if (c == -1) back();
      else if (c == 0) { deleteChannel(cur_chan); back(); back(); toast("Deleted"); }
      break;

    case PG_SETTINGS:
      c = menuInput(k, 9);
      if (c == -1) back();
      else if (c == 0) startEdit(E_NAME, "Node name", prefs->node_name, 31, PG_SETTINGS);
      else if (c == 1) { stageLoad(); open(PG_RADIO); }
      else if (c == 2) open(PG_DISPLAY);
      else if (c == 3) { timeLoad(); open(PG_TIME); }
      else if (c == 4) open(PG_TZ);
      else if (c == 5) open(PG_QR_LIST);
      else if (c == 6) open(PG_CHANNELS);
      else if (c == 7) open(PG_ABOUT);
      else if (c == 8) open(PG_REBOOT);
      break;
    case PG_DISPLAY:
      c = menuInput(k, 3);
      if (c == -1) back();
      else if (c == 0) {
#ifdef TFT_ST7796S
        up.rot = (up.rot == 1) ? 3 : 1;              // this panel is only used in landscape
#else
        up.rot = (up.rot + 1) & 3;
#endif
        display.setRotation(up.rot);
        saveUiPrefs();
        render();
      }
      else if (c == 1) {
        int i = 0;
        for (int j = 0; j < 4; j++) { if (BRIGHT_LIST[j] == up.bright) i = j; }
        up.bright = BRIGHT_LIST[(i + 1) % 4];
        display.setBrightness(up.bright);
        saveUiPrefs(); render();
      }
      else if (c == 2) {
        int i = 0;
        for (int j = 0; j < 5; j++) { if (SLEEP_LIST[j] == up.sleep_s) i = j; }
        up.sleep_s = SLEEP_LIST[(i + 1) % 5];
        saveUiPrefs(); render();
      }
      break;
    case PG_TIME:
      if (k == TK_ESC || k == '0') back();
      else if (k == TK_UP || k == TK_NEXT) timeAdj(+1);
      else if (k == TK_DOWN || k == TK_PREV) timeAdj(-1);
      else if (k == TK_LEFT) { if (tm_field > 0) { tm_field--; render(); } }
      else if (k == TK_ENTER || k == TK_RIGHT) {
        if (tm_field < 4) { tm_field++; render(); }
        else { timeSave(); back(); toast("Time set"); }
      }
      break;
    case PG_TZ:
      c = menuInput(k, 2);
      if (c == -1) back();
      else if (c == 0 && prefs->tz_offset < 14) { prefs->tz_offset++; the_mesh.savePrefs(); updateStatus(); render(); }
      else if (c == 1 && prefs->tz_offset > -12) { prefs->tz_offset--; the_mesh.savePrefs(); updateStatus(); render(); }
      break;
    case PG_QR_LIST:
      c = menuInput(k, 6);
      if (c == -1) back();
      else if (c >= 0) { qr_edit = c; startEdit(E_QR, "Quick reply", qr[c], 40, PG_QR_LIST); }
      break;
    case PG_REBOOT:
      c = menuInput(k, 1);
      if (c == -1) back();
      else if (c == 0) { flushBlob(true); delay(100); board.reboot(); }
      break;

    case PG_EDIT:
      if (kb_present) {
        if (k == TK_ENTER) finishEdit(true);
        else if (k == TK_ESC) finishEdit(false);
        else if (k == TK_BACKSPACE) { if (elen > 0) { ebuf[--elen] = 0; render(); } }
        else if (k >= 32 && k < 127) { if (elen < emax) { ebuf[elen++] = (char) k; ebuf[elen] = 0; render(); } }
      } else {
        int n = kpCells();
        if (k == TK_NEXT || k == TK_DOWN || k == TK_RIGHT) { kp_cur = (kp_cur + 1) % n; render(); }
        else if (k == TK_PREV || k == TK_UP || k == TK_LEFT) { kp_cur = (kp_cur + n - 1) % n; render(); }
        else if (k == TK_ENTER) kpSelect();
        else if (k == TK_ESC) finishEdit(false);
      }
      break;
    }
  }

  // ================================================================ input sources and the main tick
  static int cardKey(uint8_t k) {
    if (k == 0x0D) return TK_ENTER;
    if (k == 0x1B) return TK_ESC;
    if (k == 0x08 || k == 0x7F) return TK_BACKSPACE;
    if (k == 0xB4) return TK_LEFT;
    if (k == 0xB5) return TK_UP;
    if (k == 0xB6) return TK_DOWN;
    if (k == 0xB7) return TK_RIGHT;
    if (k >= 32 && k < 127) return k;
    return 0;
  }

  void key(int k) {
    if (!k) return;
    last_input = millis();
    if (asleep) { asleep = false; display.turnOn(); return; }      // the first key only wakes the screen
    handleKey(k);
  }

  void wake() {
    if (asleep) { asleep = false; display.turnOn(); }
    last_input = millis();
  }

  void tick() {
    char kc = kbd.poll();
    if (kc) { kb_present = true; key(cardKey((uint8_t) kc)); }
#ifdef TERM_BTN_PIN
    int ev = btn.check();
    if (ev == BUTTON_EVENT_CLICK) key(TK_NEXT);
    else if (ev == BUTTON_EVENT_LONG_PRESS) key(TK_ENTER);
    else if (ev == BUTTON_EVENT_DOUBLE_CLICK) key(TK_ESC);
    else if (ev == BUTTON_EVENT_TRIPLE_CLICK) key(TK_PREV);
#endif
    if (pg == PG_SPLASH && millis() > splash_until) leaveSplash();
    if (toast_until && millis() > toast_until) { toast_until = 0; render(); }
    tickRetries();
    if (millis() > next_status) { next_status = millis() + 20000; updateStatus(); }
    if (up.sleep_s && !asleep && millis() - last_input > (unsigned long) up.sleep_s * 1000UL) {
      asleep = true;
      display.turnOff();
    }
    flushBlob(false);
    display.refresh();
  }

  // ================================================================ events from the mesh
  int chanSlotOf(const ChannelDetails& cd) {
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails t;
      if (the_mesh.getChannel(i, t) && t.name[0] && memcmp(t.channel.hash, cd.channel.hash, sizeof(t.channel.hash)) == 0) return i;
    }
    return -1;
  }

  void newMessageShown(int conv) {
    updateStatus();
    wake();
    if (inThread(conv)) { blob.convs[conv].unread = 0; thr_off = 0; updateStatus(); render(); }
    else if (pg == PG_HOME || pg == PG_MSGS) render();
    else display.refresh();
  }

  void onDirect(const ContactInfo& from, uint32_t ts, const char* text, uint8_t hops) {
    int c = convForContact(from.id.pub_key, from.name);
    strncpy(blob.convs[c].name, from.name, sizeof(blob.convs[c].name) - 1);
    addMsg(c, 0, ts ? ts : rtc_clock.getCurrentTime(), text, hops);
    if (!inThread(c) && blob.convs[c].unread < 99) blob.convs[c].unread++;
    newMessageShown(c);
  }

  void onChannel(ChannelDetails& cd, const char* text) {
    int slot = chanSlotOf(cd);
    if (slot < 0) slot = 0;
    int c = convForChannel(slot, cd.name);
    addMsg(c, 0, rtc_clock.getCurrentTime(), text, 0xFF);
    if (!inThread(c) && blob.convs[c].unread < 99) blob.convs[c].unread++;
    newMessageShown(c);
  }

  void begin(NodePrefs* node_prefs) {
    prefs = node_prefs;
    loadUiPrefs();
    loadQuick();
    loadBlob();
    display.setRotation(up.rot);
    display.setBrightness(up.bright);
    kb_present = kbd.begin(Wire);
#ifdef TERM_BTN_PIN
    btn.begin();
#endif
    stageLoad();
    last_input = millis();
    pg = PG_SPLASH;
    splash_until = millis() + 2500;
    updateStatus();
    render();
  }
};

static Ui ui;

// ---------------------------------------------------------------- UITask glue
void UITask::begin(DisplayDriver* disp, SensorManager* sensors, NodePrefs* node_prefs) {
  ui.task = this;
  ui.begin(node_prefs);
}

void UITask::loop() {
  ui.tick();
}

void UITask::onMessageRecv(mesh::Packet* pkt, const ContactInfo& from, uint8_t txt_type, uint32_t sender_timestamp, const char* text) {
  if (txt_type == TXT_TYPE_CLI_DATA) return;
  ui.onDirect(from, sender_timestamp, text, 0xFF);
}

void UITask::onChannelMessageRecv(mesh::Packet* pkt, ChannelDetails& channel_details, const char* text) {
  ui.onChannel(channel_details, text);
}

void UITask::onDiscoveredContact(ContactInfo& contact, bool is_new, uint8_t path_len, const uint8_t* path) {
  ui.cidx_valid = false;
  if (ui.pg == PG_CONTACTS || ui.pg == PG_CONTACT_LIST || ui.pg == PG_HEARD) ui.render();
}

void UITask::onACKRecv(uint32_t ack_crc) {
  ui.ackReceived(ack_crc);
}
