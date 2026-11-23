#pragma once

// Community radio presets (frequency MHz, spreading factor, bandwidth kHz, coding rate 5..8).
// Values taken from the MeshCore-mishmesh preset table (burakcan/MeshCore-mishmesh, MIT licence).

struct RadioPreset {
  const char* name;
  float freq;
  uint8_t sf;
  float bw;
  uint8_t cr;
};

static const RadioPreset PRESETS[] = {
  { "Australia",               915.800f, 10, 250.0f, 5 },
  { "Australia Narrow",      916.575f,  7,  62.5f, 8 },
  { "Australia Mid",         915.075f,  9, 125.0f, 5 },
  { "Australia SA/WA",       923.125f,  8,  62.5f, 8 },
  { "Australia QLD",          923.125f,  8,  62.5f, 5 },
  { "Brazil",                  923.125f,  8,  62.5f, 8 },
  { "EU/UK Narrow",          869.618f,  8,  62.5f, 8 },
  { "Czech Narrow", 869.432f,  7,  62.5f, 5 },
  { "EU 433 Long Rng",  433.650f,  8,  62.5f, 8 },
  { "Netherlands",             869.618f,  7,  62.5f, 5 },
  { "New Zealand",             917.375f, 11, 250.0f, 5 },
  { "NZ Narrow",    917.375f,  7,  62.5f, 5 },
  { "Portugal 433",            433.375f,  9,  62.5f, 6 },
  { "Portugal 868",            869.618f,  7,  62.5f, 6 },
  { "Switzerland",             869.618f,  8,  62.5f, 8 },
  { "USA/Canada",              910.525f,  7,  62.5f, 5 },
  { "Vietnam Narrow",        920.250f,  8,  62.5f, 5 },
};
static const int PRESET_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);

/*
struct RegionScopePreset {
  const char* default_scope_name;
  uint8_t default_scope_key;
};

static const RegionScopePreset PRESETS[] = {
  { "Australia", au },
  { "Vic", au-vic },
  { "Vic: Metro", au-vic-metro },
  { "Vic: Hume", au-vic-hume },
  { "Vic: Mansfield", au-vic-mansfield },
  { "Vic: Bendigo", au-vic-bendigo },
  { "Vic: Ballarat", au-vic-ballarat },
  { "Vic: Geelong", au-vic-geelong },
  { "Vic: Casey", au-vic-casey },
  { "Vic: Bright", au-vic-bright },
  { "Vic: WBool", au-vic-wbool },
  { "Vic: PtFairy", au-vic-ptfairy },
  {"Vic: Gippsland", gippsland },
  { "CBR: ACT ", au-act },
  { "NSW", au-nsw },
  { "NSW: Sydney", au-nsw-syd },
};
static const int PRESET_COUNT = sizeof(PRESETS) / sizeof(PRESETS[0]);
*/