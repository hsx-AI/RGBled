#pragma once

#include <Arduino.h>
#include <FastLED.h>

#include "config.h"

// Reveal state machine shared by the presence logic and the render pipeline.
enum RunState : uint8_t { RS_OFF = 0, RS_ENTER, RS_ON, RS_EXIT };

// Everything the user can tweak. Stored verbatim in NVS, so only append fields
// and bump SETTINGS_REVISION when the layout changes.
constexpr uint16_t SETTINGS_REVISION = 0x0102;

struct Settings {
  uint16_t revision = SETTINGS_REVISION;

  bool power = true;
  uint8_t brightness = 130;
  uint8_t effect = 3;
  uint8_t speed = 128;     // 128 = nominal, 0 freezes the animation
  uint8_t intensity = 128; // per-effect density / trail length / count
  uint8_t palette = 0;
  CRGB color1 = CRGB(255, 40, 0);
  CRGB color2 = CRGB(0, 90, 255);

  bool reverse = false;
  bool mirror = false;
  uint8_t segments = 1; // 1..8 repeats of the virtual strip

  // presence interaction
  bool autoMode = true;
  uint16_t holdSec = 20;    // keep the light on this long after the last detection
  uint8_t entranceFx = 4;   // reveal used when somebody shows up
  uint8_t exitFx = 5;       // reveal used when the room empties
  uint8_t idleBright = 0;   // dim floor while nobody is around (0 = fully off)
  bool motionBoost = true;  // moving target speeds the animation up
  bool distanceFx = false;  // highlight tracks the reported distance
  bool pingOnEnter = true;  // ripple pulse the moment presence appears
  uint16_t maxRangeCm = 600;
  uint16_t enterMs = 1400;
  uint16_t exitMs = 2200;

  uint16_t currentMa = 3000; // power budget handed to FastLED

  // scene helpers
  bool autoCycle = false;   // rotate through animations while the strip is on
  uint16_t cycleSec = 45;   // seconds between auto-cycle switches
  bool approachDim = false; // nearer targets raise overall brightness
  bool moodSync = false;    // pick calm/energy effects from radarResult
};

struct SensorState {
  bool present = false;
  uint8_t radarResult = 0;
  uint8_t source = 0;
  uint16_t distanceCm = 0;
  uint32_t sequence = 0;
  uint32_t lastPacketMs = 0;
  uint32_t packets = 0;
  uint32_t badPackets = 0;
  uint8_t mac[6] = {};

  bool linked() const { return packets && (millis() - lastPacketMs) < 3000; }
};

extern Settings cfg;
extern SensorState sensor;
extern CRGB leds[NUM_LEDS];
extern String apSsid;
extern String apPass;
extern String apIpStr;
extern bool espNowReady;

// ---- render engine (effects.cpp) -----------------------------------------
void ledsBegin();
void ledsTick();
void ledsApplyPowerLimit();
void ledsMarkPaletteDirty();
void ledsRequestOn();
void ledsRequestOff(bool keepIdleFloor);
void ledsTriggerPing(uint16_t atCm);
RunState ledsRunState();
uint8_t ledsFps();
void ledsPreviewHex(String &out);

uint8_t effectCount();
const char *effectName(uint8_t index);
uint8_t paletteCount();
const char *paletteName(uint8_t index);
uint8_t transitionCount();
const char *transitionName(uint8_t index);

// ---- web console (web.cpp) ----------------------------------------------
void webBegin();
void webTick();

// ---- persistence (main.cpp) ---------------------------------------------
void settingsSanitize();
void settingsTouch();               // schedule a debounced NVS write
void settingsSaveNow();
bool presetSave(uint8_t slot);
bool presetLoad(uint8_t slot);
void saveApCredentials(const String &ssid, const String &pass);
constexpr uint8_t PRESET_SLOTS = 4;
