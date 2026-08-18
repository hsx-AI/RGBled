#pragma once

#include <stdint.h>

// ---- hardware -------------------------------------------------------------
#define LED_PIN 6                        // WS2812 data line, must be a literal for FastLED
constexpr uint16_t NUM_LEDS = 180;       // beads on the strip

// The presence station broadcasts on this channel, so the SoftAP has to use it
// as well: ESP-NOW can only hear traffic on the channel the radio sits on.
constexpr uint8_t WIFI_CHANNEL = 6;

// ---- rendering ------------------------------------------------------------
constexpr uint16_t FRAME_INTERVAL_MS = 16;   // ~60 fps
constexpr uint16_t PREVIEW_PIXELS = 60;      // pixels streamed to the web preview

// ---- defaults -------------------------------------------------------------
#define AP_SSID_DEFAULT "RGB-LED-LAB"
#define AP_PASS_DEFAULT "rgb12345"
#define FIRMWARE_VERSION "1.2"

// ESP-NOW wire format shared with the LD2402 station (hunman_test project).
constexpr uint32_t PRESENCE_MAGIC = 0x53455250;  // ASCII "PRES"
constexpr uint8_t PRESENCE_VERSION = 1;

struct __attribute__((packed)) PresencePacket {
  uint32_t magic;
  uint8_t version;
  uint8_t presence;     // 0 = nobody, 1 = person present
  uint8_t radarResult;  // 0 nobody, 1 moving, 2 stationary
  uint8_t source;       // 1 UART engineering frame, 2 GPIO fallback
  uint16_t distanceCm;
  uint32_t sequence;
  uint32_t uptimeMs;
  uint8_t checksum;     // XOR of all preceding bytes
};
static_assert(sizeof(PresencePacket) == 19, "Unexpected ESP-NOW packet padding");
