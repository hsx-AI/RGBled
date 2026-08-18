// WS2812 (180 beads, GPIO6) controller for an AirM2M Core ESP32-C3.
//
//  * SoftAP + DNS captive portal on channel 6 -> the console opens by itself
//    once a phone joins the hotspot.
//  * Listens to the LD2402 presence station's ESP-NOW broadcast (same channel)
//    and drives entrance / exit animations from it.
//  * All settings live in NVS, written back a few seconds after the last edit.

#include <Preferences.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "state.h"

Settings cfg;
SensorState sensor;
String apSsid = AP_SSID_DEFAULT;
String apPass = AP_PASS_DEFAULT;
String apIpStr = "192.168.4.1";
bool espNowReady = false;

namespace {

Preferences prefs;
bool prefsDirty = false;
uint32_t prefsDirtyMs = 0;
constexpr uint32_t SAVE_DEBOUNCE_MS = 4000;

volatile bool presenceRising = false;
uint32_t lastPresentMs = 0;
bool hasSeenPresence = false;
bool lastDesired = false;
bool desiredKnown = false;

// The ESP-NOW callback runs in the Wi-Fi task.  Hand the newest complete packet
// to loop() through a one-element queue instead of publishing a partly updated
// SensorState across two tasks.
struct ReceivedPresence {
  PresencePacket packet;
  uint8_t mac[6];
  uint32_t receivedMs;
};

QueueHandle_t presenceQueue = nullptr;
volatile uint32_t pendingBadPackets = 0;

uint8_t packetChecksum(const PresencePacket &p) {
  const uint8_t *raw = reinterpret_cast<const uint8_t *>(&p);
  uint8_t v = 0;
  for (size_t i = 0; i < sizeof(PresencePacket) - 1; ++i) v ^= raw[i];
  return v;
}

void onEspNowRecv(const uint8_t *mac, const uint8_t *data, int len) {
  if (len != (int)sizeof(PresencePacket)) {
    __atomic_fetch_add(&pendingBadPackets, 1, __ATOMIC_RELAXED);
    return;
  }
  PresencePacket p;
  memcpy(&p, data, sizeof(p));
  if (p.magic != PRESENCE_MAGIC || p.version != PRESENCE_VERSION ||
      packetChecksum(p) != p.checksum) {
    __atomic_fetch_add(&pendingBadPackets, 1, __ATOMIC_RELAXED);
    return;
  }

  if (!presenceQueue) return;
  ReceivedPresence received = {};
  received.packet = p;
  received.receivedMs = millis();
  memcpy(received.mac, mac, sizeof(received.mac));
  xQueueOverwrite(presenceQueue, &received);
}

bool initEspNow() {
  presenceQueue = xQueueCreate(1, sizeof(ReceivedPresence));
  if (!presenceQueue) return false;
  if (esp_now_init() != ESP_OK) return false;
  esp_now_register_recv_cb(onEspNowRecv);
  // Receiving broadcasts needs no peer, but registering one keeps the door open
  // for talking back to the station later on.
  esp_now_peer_info_t peer = {};
  memset(peer.peer_addr, 0xFF, 6);
  peer.channel = WIFI_CHANNEL;
  peer.ifidx = WIFI_IF_AP;
  peer.encrypt = false;
  esp_err_t r = esp_now_add_peer(&peer);
  return r == ESP_OK || r == ESP_ERR_ESPNOW_EXIST;
}

void receivePresenceTick() {
  uint32_t bad = __atomic_exchange_n(&pendingBadPackets, 0, __ATOMIC_RELAXED);
  sensor.badPackets += bad;

  ReceivedPresence received;
  if (!presenceQueue || xQueueReceive(presenceQueue, &received, 0) != pdTRUE) return;

  bool wasPresent = sensor.present && sensor.linked();
  const PresencePacket &p = received.packet;
  sensor.present = p.presence != 0;
  sensor.radarResult = p.radarResult;
  sensor.source = p.source;
  sensor.distanceCm = p.distanceCm;
  sensor.sequence = p.sequence;
  sensor.lastPacketMs = received.receivedMs;
  sensor.packets++;
  memcpy(sensor.mac, received.mac, sizeof(sensor.mac));

  if (sensor.present) {
    lastPresentMs = received.receivedMs;
    hasSeenPresence = true;
  }
  if (!wasPresent && sensor.present) presenceRising = true;
}

void loadSettings() {
  // RW so the namespace is created on first boot (RO begin logs NOT_FOUND and fails).
  if (!prefs.begin("rgbled", false)) return;
  apSsid = prefs.getString("ssid", AP_SSID_DEFAULT);
  apPass = prefs.getString("pass", AP_PASS_DEFAULT);
  Settings stored;
  size_t n = prefs.getBytesLength("cfg");
  if (n == sizeof(Settings) && prefs.getBytes("cfg", &stored, sizeof(stored)) == sizeof(stored) &&
      stored.revision == SETTINGS_REVISION) {
    cfg = stored;
  }
  prefs.end();
  settingsSanitize();
}

// The presence packet decides the target state; the engine handles the fades.
void presenceTick() {
  receivePresenceTick();

  if (presenceRising) {
    presenceRising = false;
    if (cfg.pingOnEnter) ledsTriggerPing(sensor.distanceCm);
  }

  uint32_t now = millis();
  // A stale link is not evidence of presence.  The old fail-safe forced
  // `present=true` after three seconds and could cancel a short exit fade,
  // which made values such as a two-second hold appear not to work at all.
  bool present = cfg.autoMode ? (sensor.linked() && sensor.present) : true;
  bool holding = cfg.autoMode
                     ? (hasSeenPresence &&
                        now - lastPresentMs < (uint32_t)cfg.holdSec * 1000UL)
                     : true;
  bool desired = cfg.power && (present || holding);

  if (!desiredKnown || desired != lastDesired) {
    desiredKnown = true;
    lastDesired = desired;
    if (desired) ledsRequestOn();
    else ledsRequestOff(cfg.power && cfg.autoMode);
  }
}

void maybeSave() {
  if (!prefsDirty || millis() - prefsDirtyMs < SAVE_DEBOUNCE_MS) return;
  settingsSaveNow();
}

}  // namespace

void settingsSanitize() {
  cfg.revision = SETTINGS_REVISION;
  if (cfg.brightness < 1) cfg.brightness = 1;
  if (cfg.effect >= effectCount()) cfg.effect = 0;
  if (cfg.palette >= paletteCount()) cfg.palette = 0;
  if (cfg.entranceFx >= transitionCount()) cfg.entranceFx = 1;
  if (cfg.exitFx >= transitionCount()) cfg.exitFx = 1;
  if (cfg.segments < 1) cfg.segments = 1;
  if (cfg.segments > 8) cfg.segments = 8;
  if (cfg.holdSec > 3600) cfg.holdSec = 3600;
  if (cfg.idleBright > 120) cfg.idleBright = 120;
  if (cfg.maxRangeCm < 70 || cfg.maxRangeCm > 1000) cfg.maxRangeCm = 600;
  if (cfg.enterMs < 100 || cfg.enterMs > 10000) cfg.enterMs = 1400;
  if (cfg.exitMs < 100 || cfg.exitMs > 15000) cfg.exitMs = 2200;
  if (cfg.currentMa < 300 || cfg.currentMa > 20000) cfg.currentMa = 3000;
  if (cfg.cycleSec < 5) cfg.cycleSec = 5;
  if (cfg.cycleSec > 600) cfg.cycleSec = 600;
}

void settingsTouch() {
  prefsDirty = true;
  prefsDirtyMs = millis();
}

void settingsSaveNow() {
  prefsDirty = false;
  prefs.begin("rgbled", false);
  prefs.putBytes("cfg", &cfg, sizeof(cfg));
  prefs.end();
}

bool presetSave(uint8_t slot) {
  if (slot >= PRESET_SLOTS) return false;
  char key[8];
  snprintf(key, sizeof(key), "ps%u", slot);
  prefs.begin("rgbled", false);
  size_t written = prefs.putBytes(key, &cfg, sizeof(cfg));
  prefs.end();
  return written == sizeof(cfg);
}

bool presetLoad(uint8_t slot) {
  if (slot >= PRESET_SLOTS) return false;
  char key[8];
  snprintf(key, sizeof(key), "ps%u", slot);
  Settings stored;
  prefs.begin("rgbled", true);
  bool ok = prefs.getBytesLength(key) == sizeof(Settings) &&
            prefs.getBytes(key, &stored, sizeof(stored)) == sizeof(stored) &&
            stored.revision == SETTINGS_REVISION;
  prefs.end();
  if (!ok) return false;
  cfg = stored;
  settingsSanitize();
  settingsTouch();
  return true;
}

void saveApCredentials(const String &ssid, const String &pass) {
  prefs.begin("rgbled", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

void setup() {
  Serial.begin(115200);
  delay(100);

  loadSettings();

  // Bring the SoftAP up before FastLED: a strip crash must not hide the hotspot.
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid.c_str(), apPass.c_str(), WIFI_CHANNEL, false, 8);
  WiFi.setSleep(false);  // modem sleep would drop ESP-NOW frames
  apIpStr = WiFi.softAPIP().toString();
  espNowReady = initEspNow();

  webBegin();
  ledsBegin();

  Serial.printf("\nWS2812 %u LEDs on GPIO%u\nAP: %s / %s  http://%s  channel %u\nESP-NOW: %s\n",
                NUM_LEDS, LED_PIN, apSsid.c_str(), apPass.c_str(), apIpStr.c_str(), WIFI_CHANNEL,
                espNowReady ? "listening" : "FAILED");
}

void loop() {
  // Presence always gets first chance to run; a busy HTTP client must not add
  // perceptible latency to an ESP-NOW state change.
  presenceTick();
  ledsTick();
  webTick();
  maybeSave();
}
