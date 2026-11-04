#pragma once

// ASCII art for the terminal UI. The small logos fit 20 columns, the large one 40.

static const char* const LOGO_S[] = {
  R"~(    o-----o)~",
  R"~(   / \   / \)~",
  R"~(  o---o-o---o)~",
  R"~(   \ /   \ /)~",
  R"~(    o-----o)~",
  R"~(  M E S H C O R E)~",
};
static const int LOGO_S_N = sizeof(LOGO_S) / sizeof(LOGO_S[0]);

static const char* const LOGO_L[] = {
  R"~( __  __ _____ ____  _   _)~",
  R"~(|  \/  | ____/ ___|| | | |)~",
  R"~(| |\/| |  _| \___ \| |_| |)~",
  R"~(| |  | | |___ ___) |  _  |)~",
  R"~(|_|  |_|_____|____/|_| |_|)~",
  R"~(      C  O  R  E)~",
};
static const int LOGO_L_N = sizeof(LOGO_L) / sizeof(LOGO_L[0]);

static const char* const ICON_ADVERT[] = {
  R"~(  )))   |   (((  )~",
  R"~( )))    |    ((( )~",
  R"~(        |)~",
  R"~(       /|\)~",
  R"~(      / | \)~",
  R"~(     /__|__\)~",
};
static const int ICON_ADVERT_N = sizeof(ICON_ADVERT) / sizeof(ICON_ADVERT[0]);

static const char* const ICON_POWER[] = {
  R"~(      |)~",
  R"~(   .--|--.)~",
  R"~(  /   |   \)~",
  R"~( |    '    |)~",
  R"~(  \       /)~",
  R"~(   '-----')~",
};
static const int ICON_POWER_N = sizeof(ICON_POWER) / sizeof(ICON_POWER[0]);

static const char* const ICON_PICOMESH[] = {
  [0]=R"~(|=============================================|)~",
  [1]=R"~(|    ___ _                            _       |)~",
  [2]=R"~(|   //_ (+) ___ ___   /\/\   ___  ___| |__    |)~",
  [3]=R"~(|  ///_)) |/ __/ _ \ /    \ / _ \( __| '_ \   |)~",
  [4]=R"~(| //___/| | (_( (_) ) /\/\ \ |_)/\__ \ | | |  |)~",
  [5]=R"~(| \/    |_|\___\___/\/    \/\___||___)_| |_|  |)~",
  [6]=R"~(|-+-+-+-+-+-+-+-+-+-+-+-+-+-+-F O R-+-P I C O-|)~",
  [7]=R"~(|=============================================|)~",
};
static const int ICON_PICOMESH_N = sizeof(ICON_PICOMESH) / sizeof(ICON_PICOMESH[0]);
