// SoftAP captive portal: a DNS server that answers every query with our own IP
// plus the redirects the common OS connectivity probes expect, so the phone pops
// the console up by itself right after joining the hotspot.

#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "state.h"
#include "web_page.h"

namespace {

WebServer server(80);
DNSServer dns;
String metaJson;
uint32_t rebootAt = 0;

void appendStringArray(String &out, const char *key, uint8_t count,
                       const char *(*nameOf)(uint8_t)) {
  out += "\"";
  out += key;
  out += "\":[";
  for (uint8_t i = 0; i < count; i++) {
    if (i) out += ',';
    out += '"';
    out += nameOf(i);
    out += '"';
  }
  out += ']';
}

String hex6(const CRGB &c) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%02x%02x%02x", c.r, c.g, c.b);
  return String(buf);
}

CRGB parseHex(const String &raw, CRGB fallback) {
  String t = raw;
  if (t.startsWith("#")) t = t.substring(1);
  if (t.length() != 6) return fallback;
  char *end = nullptr;
  uint32_t v = strtoul(t.c_str(), &end, 16);
  if (end && *end) return fallback;
  return CRGB((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
}

void buildMeta() {
  metaJson.reserve(1400);
  metaJson = "{\"n\":";
  metaJson += NUM_LEDS;
  metaJson += ",\"pin\":";
  metaJson += LED_PIN;
  metaJson += ",\"ch\":";
  metaJson += WIFI_CHANNEL;
  metaJson += ",\"ver\":\"" FIRMWARE_VERSION "\",\"ssid\":\"";
  metaJson += apSsid;
  metaJson += "\",";
  appendStringArray(metaJson, "fx", effectCount(), effectName);
  metaJson += ',';
  appendStringArray(metaJson, "pl", paletteCount(), paletteName);
  metaJson += ',';
  appendStringArray(metaJson, "tr", transitionCount(), transitionName);
  metaJson += '}';
}

void handleRoot() {
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

void handleMeta() { server.send(200, "application/json", metaJson); }

void handleLive() {
  String s;
  s.reserve(900);
  s = "{";
  s += "\"pw\":" + String(cfg.power ? 1 : 0);
  s += ",\"br\":" + String(cfg.brightness);
  s += ",\"sp\":" + String(cfg.speed);
  s += ",\"in\":" + String(cfg.intensity);
  s += ",\"fx\":" + String(cfg.effect);
  s += ",\"pl\":" + String(cfg.palette);
  s += ",\"c1\":\"" + hex6(cfg.color1) + "\"";
  s += ",\"c2\":\"" + hex6(cfg.color2) + "\"";
  s += ",\"rv\":" + String(cfg.reverse ? 1 : 0);
  s += ",\"mi\":" + String(cfg.mirror ? 1 : 0);
  s += ",\"sg\":" + String(cfg.segments);
  s += ",\"am\":" + String(cfg.autoMode ? 1 : 0);
  s += ",\"hs\":" + String(cfg.holdSec);
  s += ",\"en\":" + String(cfg.entranceFx);
  s += ",\"ex\":" + String(cfg.exitFx);
  s += ",\"ib\":" + String(cfg.idleBright);
  s += ",\"mb\":" + String(cfg.motionBoost ? 1 : 0);
  s += ",\"df\":" + String(cfg.distanceFx ? 1 : 0);
  s += ",\"pg\":" + String(cfg.pingOnEnter ? 1 : 0);
  s += ",\"mr\":" + String(cfg.maxRangeCm);
  s += ",\"em\":" + String(cfg.enterMs);
  s += ",\"xm\":" + String(cfg.exitMs);
  s += ",\"ma\":" + String(cfg.currentMa);
  s += ",\"ac\":" + String(cfg.autoCycle ? 1 : 0);
  s += ",\"cs\":" + String(cfg.cycleSec);
  s += ",\"ad\":" + String(cfg.approachDim ? 1 : 0);
  s += ",\"ms\":" + String(cfg.moodSync ? 1 : 0);
  s += ",\"rs\":" + String((uint8_t)ledsRunState());
  s += ",\"pr\":" + String(sensor.present ? 1 : 0);
  s += ",\"rr\":" + String(sensor.radarResult);
  s += ",\"dc\":" + String(sensor.distanceCm);
  s += ",\"lk\":" + String(sensor.linked() ? 1 : 0);
  s += ",\"sq\":" + String(sensor.sequence);
  s += ",\"pk\":" + String(sensor.packets);
  s += ",\"bd\":" + String(sensor.badPackets);
  s += ",\"ag\":" + String(sensor.packets ? millis() - sensor.lastPacketMs : 0);
  s += ",\"fp\":" + String(ledsFps());
  s += ",\"hp\":" + String((uint32_t)ESP.getFreeHeap());
  String px;
  ledsPreviewHex(px);
  s += ",\"px\":\"" + px + "\"}";
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", s);
}

void handleSet() {
  bool paletteTouched = false;
  bool powerTouched = false;

  auto num = [](const char *k, long lo, long hi) -> long {
    long v = server.arg(k).toInt();
    return v < lo ? lo : (v > hi ? hi : v);
  };

  if (server.hasArg("pw")) { cfg.power = num("pw", 0, 1) != 0; powerTouched = true; }
  if (server.hasArg("br")) cfg.brightness = num("br", 1, 255);
  if (server.hasArg("sp")) cfg.speed = num("sp", 0, 255);
  if (server.hasArg("in")) cfg.intensity = num("in", 0, 255);
  if (server.hasArg("fx")) cfg.effect = num("fx", 0, effectCount() - 1);
  if (server.hasArg("pl")) { cfg.palette = num("pl", 0, paletteCount() - 1); paletteTouched = true; }
  if (server.hasArg("c1")) { cfg.color1 = parseHex(server.arg("c1"), cfg.color1); paletteTouched = true; }
  if (server.hasArg("c2")) { cfg.color2 = parseHex(server.arg("c2"), cfg.color2); paletteTouched = true; }
  if (server.hasArg("rv")) cfg.reverse = num("rv", 0, 1) != 0;
  if (server.hasArg("mi")) cfg.mirror = num("mi", 0, 1) != 0;
  if (server.hasArg("sg")) cfg.segments = num("sg", 1, 8);
  if (server.hasArg("am")) { cfg.autoMode = num("am", 0, 1) != 0; powerTouched = true; }
  if (server.hasArg("hs")) cfg.holdSec = num("hs", 0, 3600);
  if (server.hasArg("en")) cfg.entranceFx = num("en", 0, transitionCount() - 1);
  if (server.hasArg("ex")) cfg.exitFx = num("ex", 0, transitionCount() - 1);
  if (server.hasArg("ib")) cfg.idleBright = num("ib", 0, 120);
  if (server.hasArg("mb")) cfg.motionBoost = num("mb", 0, 1) != 0;
  if (server.hasArg("df")) cfg.distanceFx = num("df", 0, 1) != 0;
  if (server.hasArg("pg")) cfg.pingOnEnter = num("pg", 0, 1) != 0;
  if (server.hasArg("mr")) cfg.maxRangeCm = num("mr", 70, 1000);
  if (server.hasArg("em")) cfg.enterMs = num("em", 100, 10000);
  if (server.hasArg("xm")) cfg.exitMs = num("xm", 100, 15000);
  if (server.hasArg("ma")) {
    cfg.currentMa = num("ma", 300, 20000);
    ledsApplyPowerLimit();
  }
  if (server.hasArg("ac")) cfg.autoCycle = num("ac", 0, 1) != 0;
  if (server.hasArg("cs")) cfg.cycleSec = num("cs", 5, 600);
  if (server.hasArg("ad")) cfg.approachDim = num("ad", 0, 1) != 0;
  if (server.hasArg("ms")) cfg.moodSync = num("ms", 0, 1) != 0;

  settingsSanitize();
  if (paletteTouched) ledsMarkPaletteDirty();
  (void)powerTouched;  // the presence loop picks the new target up on its own
  settingsTouch();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handlePreset() {
  uint8_t slot = server.arg("slot").toInt();
  bool save = server.arg("do") == "save";
  if (slot >= PRESET_SLOTS) {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"slot\"}");
    return;
  }
  bool ok = save ? presetSave(slot) : presetLoad(slot);
  if (!save && ok) {
    ledsMarkPaletteDirty();
    ledsApplyPowerLimit();
  }
  server.send(ok ? 200 : 404, "application/json",
              ok ? "{\"ok\":true}" : "{\"ok\":false,\"message\":\"empty\"}");
}

void handleSaveNow() {
  settingsSaveNow();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleWifi() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() < 1 || ssid.length() > 31 || pass.length() < 8 || pass.length() > 63) {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"名称或密码长度不合法\"}");
    return;
  }
  saveApCredentials(ssid, pass);
  server.send(200, "application/json", "{\"ok\":true}");
  rebootAt = millis() + 900;
}

// Any request aimed at a hostname other than our own IP is a connectivity probe
// (or the user typing a URL); a 302 makes Android/iOS/Windows raise the portal.
void handleNotFound() {
  if (server.hostHeader() != apIpStr) {
    server.sendHeader("Location", String("http://") + apIpStr + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain; charset=utf-8", "Not found");
}

}  // namespace

void webBegin() {
  buildMeta();
  dns.setTTL(0);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/meta", HTTP_GET, handleMeta);
  server.on("/api/live", HTTP_GET, handleLive);
  server.on("/api/set", handleSet);
  server.on("/api/preset", handlePreset);
  server.on("/api/save", handleSaveNow);
  server.on("/api/wifi", handleWifi);
  server.onNotFound(handleNotFound);
  server.begin();
}

void webTick() {
  dns.processNextRequest();
  server.handleClient();
  if (rebootAt && millis() > rebootAt) {
    settingsSaveNow();
    ESP.restart();
  }
}
