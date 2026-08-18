// Render pipeline for the 180 pixel WS2812 strip.
//
//   effect -> canvas[vLen]  ->  segment/mirror/reverse expansion -> leds[]
//          -> presence overlays -> reveal mask (entrance/exit) -> FastLED.show()
//
// Animations advance on `fxTime`, a virtual clock scaled by the speed slider, so
// changing the speed never makes an animation jump and speed 0 freezes a frame.

#include <math.h>

#include "state.h"

CRGB leds[NUM_LEDS];

namespace {

CRGB canvas[NUM_LEDS];
CRGBPalette16 curPal(RainbowColors_p);
bool palDirty = true;

uint32_t fxTime = 0;   // virtual milliseconds
uint16_t fxDelta = 0;  // virtual milliseconds elapsed in this frame
uint32_t speedAcc = 0;
float speedEase = 128.0f;

uint32_t lastFrameMs = 0;
uint32_t lastShowMs = 0;
float briEase = 0.0f;

RunState rstate = RS_OFF;
float reveal = 0.0f;    // 0 = hidden, 1 = fully shown
bool idleFloor = false; // absence keeps a dim floor instead of going black

uint32_t pingStartMs = 0;
float pingCenter = 0.5f;

uint16_t frameCounter = 0;
uint32_t fpsWindowMs = 0;
uint8_t fpsValue = 0;

uint16_t gVLen = NUM_LEDS;
uint16_t gLastVLen = 0;

uint32_t cycleAtMs = 0;
uint8_t lastMood = 255;

// ---------------------------------------------------------------- utilities
inline uint16_t clampU16(uint16_t v, uint16_t lo, uint16_t hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

// Stable per-index pseudo random value; lets twinkle/dissolve effects look
// random without keeping a table around.
inline uint16_t hash16(uint16_t x) {
  x = (x ^ 61) ^ (x >> 8);
  x += x << 3;
  x ^= x >> 5;
  x *= 0x2545;
  x ^= x >> 7;
  return x;
}

// FastLED's beat helpers, re-based on the virtual clock.
inline uint16_t vbeat88(accum88 bpm88) {
  return (uint16_t)(((uint32_t)fxTime * (uint32_t)bpm88 * 280u) >> 16);
}
inline uint8_t vbeat8(uint8_t bpm) { return vbeat88((accum88)(bpm << 8)) >> 8; }
inline uint16_t vbeat16(uint8_t bpm) { return vbeat88((accum88)(bpm << 8)); }
inline uint8_t vbeatsin8(uint8_t bpm, uint8_t lo = 0, uint8_t hi = 255, uint8_t phase = 0) {
  return lo + scale8(sin8(vbeat8(bpm) + phase), hi - lo);
}
inline uint16_t vbeatsin16(uint8_t bpm, uint16_t lo, uint16_t hi, uint16_t phase = 0) {
  uint16_t b = sin16(vbeat16(bpm) + phase) + 32768;
  return lo + scale16(b, hi - lo);
}
// Rising 0..65535 ramp, one full ramp per `bpm` beats a minute.
inline uint16_t vsaw16(uint8_t bpm) { return vbeat16(bpm); }

// Trail decay expressed in "units per virtual second" so the tail length stays
// the same whatever the frame rate is.
void fadeV(uint16_t n, uint16_t perSec) {
  uint32_t amt = ((uint32_t)perSec * fxDelta) / 1000u;
  if (amt) fadeToBlackBy(canvas, n, amt > 255 ? 255 : (uint8_t)amt);
}

inline CRGB pal(uint8_t index, uint8_t bright = 255) {
  return ColorFromPalette(curPal, index, bright, LINEARBLEND);
}

// ------------------------------------------------------------------ palettes
DEFINE_GRADIENT_PALETTE(ice_gp){0, 0, 4, 40, 90, 0, 70, 180, 170, 70, 200, 255, 255, 255, 255, 255};
DEFINE_GRADIENT_PALETTE(sunset_gp){0, 110, 0, 60, 70, 255, 60, 0, 150, 255, 170, 20, 215, 255, 90, 60, 255, 60, 0, 90};
DEFINE_GRADIENT_PALETTE(neon_gp){0, 255, 0, 120, 64, 130, 0, 255, 128, 0, 200, 255, 192, 0, 255, 140, 255, 255, 0, 120};
DEFINE_GRADIENT_PALETTE(mint_gp){0, 0, 50, 35, 90, 0, 200, 140, 180, 190, 255, 225, 255, 0, 110, 90};
DEFINE_GRADIENT_PALETTE(sakura_gp){0, 60, 0, 25, 80, 255, 120, 170, 165, 255, 205, 225, 255, 170, 35, 90};
DEFINE_GRADIENT_PALETTE(aurora_gp){0, 0, 16, 12, 60, 0, 180, 90, 120, 40, 255, 180, 190, 120, 60, 255, 255, 8, 0, 40};

const char *const PALETTE_NAMES[] = {
    "彩虹",   "彩虹条纹", "派对",   "熔岩",     "海洋",     "森林",
    "热力",   "云朵",     "冰蓝",   "日落",     "霓虹",     "薄荷",
    "樱花",   "极光",     "自定义双色", "自定义单色"};
constexpr uint8_t PALETTE_COUNT = sizeof(PALETTE_NAMES) / sizeof(PALETTE_NAMES[0]);

void buildPalette() {
  switch (cfg.palette) {
    case 0: curPal = RainbowColors_p; break;
    case 1: curPal = RainbowStripeColors_p; break;
    case 2: curPal = PartyColors_p; break;
    case 3: curPal = LavaColors_p; break;
    case 4: curPal = OceanColors_p; break;
    case 5: curPal = ForestColors_p; break;
    case 6: curPal = HeatColors_p; break;
    case 7: curPal = CloudColors_p; break;
    case 8: curPal = ice_gp; break;
    case 9: curPal = sunset_gp; break;
    case 10: curPal = neon_gp; break;
    case 11: curPal = mint_gp; break;
    case 12: curPal = sakura_gp; break;
    case 13: curPal = aurora_gp; break;
    // Seamless loop between the two user colours so flowing effects do not jump.
    case 14: curPal = CRGBPalette16(cfg.color1, cfg.color2, cfg.color1, cfg.color2); break;
    default: curPal = CRGBPalette16(cfg.color1); break;
  }
  palDirty = false;
}

// ------------------------------------------------------------------- effects
void fxSolid(uint16_t n) { fill_solid(canvas, n, cfg.color1); }

void fxBreath(uint16_t n) {
  uint8_t b = ease8InOutCubic(vbeatsin8(11, 6, 255));
  CRGB c = cfg.color1;
  c.nscale8_video(b);
  fill_solid(canvas, n, c);
}

void fxGradient(uint16_t n) {
  uint16_t span = n > 1 ? n - 1 : 1;
  for (uint16_t i = 0; i < n; i++) canvas[i] = pal((uint16_t)i * 255 / span);
}

void fxRainbowFlow(uint16_t n) {
  fill_rainbow(canvas, n, vbeat8(11), 1 + (cfg.intensity >> 4));
}

void fxRainbowSync(uint16_t n) { fill_solid(canvas, n, CHSV(vbeat8(6), 255, 255)); }

void fxPaletteFlow(uint16_t n) {
  uint8_t start = vbeat8(10);
  uint8_t step = 1 + (cfg.intensity >> 4);
  for (uint16_t i = 0; i < n; i++) canvas[i] = pal(start + (uint8_t)(i * step));
}

void fxTheater(uint16_t n) {
  uint8_t gap = 3 + (cfg.intensity >> 6);
  uint16_t off = (fxTime / 110) % gap;
  uint8_t hue = vbeat8(4);
  for (uint16_t i = 0; i < n; i++)
    canvas[i] = (i % gap == off) ? pal(hue + (uint8_t)(i * 3)) : CRGB::Black;
}

void fxRunningLights(uint16_t n) {
  uint8_t freq = 2 + (cfg.intensity >> 5);
  uint8_t phase = vbeat8(14);
  for (uint16_t i = 0; i < n; i++) {
    uint8_t b = sin8((uint8_t)(i * freq) + phase);
    canvas[i] = pal((uint8_t)(i * 2) + (uint8_t)(phase >> 1), scale8(b, b));
  }
}

void fxColorWipe(uint16_t n) {
  const uint32_t period = 1600;
  uint32_t t = fxTime % (period * 2);
  bool second = t >= period;
  uint16_t head = (uint32_t)(t % period) * n / period;
  CRGB a = second ? cfg.color2 : cfg.color1;
  CRGB b = second ? cfg.color1 : cfg.color2;
  for (uint16_t i = 0; i < n; i++) canvas[i] = (i <= head) ? a : b;
}

void fxComet(uint16_t n) {
  fadeV(n, 700 + (uint16_t)cfg.intensity * 6);
  uint16_t pos = ((uint32_t)vsaw16(12) * n) >> 16;
  uint8_t hue = vbeat8(3);
  for (uint8_t k = 0; k < 4; k++) {
    int32_t p = (int32_t)pos - k;
    if (p >= 0 && p < n) canvas[p] += pal(hue, 255 >> k);
  }
}

void fxCylon(uint16_t n) {
  fadeV(n, 600 + (uint16_t)cfg.intensity * 6);
  uint16_t pos = vbeatsin16(13, 0, n - 1);
  canvas[pos] += pal((uint8_t)(pos * 3) + vbeat8(2));
}

struct Meteor {
  float pos;
  float spd;
  uint8_t idx;
  bool on;
};

void fxMeteor(uint16_t n) {
  static Meteor met[10] = {};
  fadeV(n, 900 + (uint16_t)cfg.intensity * 5);
  uint8_t maxM = 2 + (cfg.intensity >> 5);
  float dt = fxDelta / 1000.0f;
  for (uint8_t i = 0; i < 10; i++) {
    if (!met[i].on) {
      if (i < maxM && fxDelta && random8() < 10) {
        met[i].pos = -4.0f;
        met[i].spd = n * (0.25f + random8() / 400.0f);
        met[i].idx = random8();
        met[i].on = true;
      }
      continue;
    }
    met[i].pos += met[i].spd * dt;
    if (met[i].pos > n + 6) {
      met[i].on = false;
      continue;
    }
    for (uint8_t t = 0; t < 6; t++) {
      int32_t p = (int32_t)met[i].pos - t;
      if (p >= 0 && p < n) canvas[p] += pal(met[i].idx, 255 >> t);
    }
  }
}

void fxBalls(uint16_t n) {
  static float h[8], v[8];
  static uint8_t idx[8];
  static uint16_t lastN = 0;
  float g = n * 1.5f;
  if (lastN != n) {
    lastN = n;
    for (uint8_t i = 0; i < 8; i++) {
      h[i] = 0;
      v[i] = sqrtf(2.0f * g * n * (0.35f + 0.08f * i));
      idx[i] = random8();
    }
  }
  fadeV(n, 1100);
  uint8_t count = 1 + (cfg.intensity >> 5);
  float dt = fxDelta / 1000.0f;
  for (uint8_t b = 0; b < count; b++) {
    v[b] -= g * dt;
    h[b] += v[b] * dt;
    if (h[b] <= 0.0f) {
      h[b] = 0.0f;
      v[b] = sqrtf(2.0f * g * n * (0.35f + 0.08f * b));
      idx[b] = random8();
    }
    if (h[b] > n - 1) {
      h[b] = n - 1;
      v[b] = -fabsf(v[b]) * 0.55f;
    }
    canvas[(uint16_t)h[b]] += pal(idx[b]);
  }
}

void fxConfetti(uint16_t n) {
  fadeV(n, 500 + (uint16_t)(255 - cfg.intensity) * 4);
  uint16_t spawns = ((uint32_t)fxDelta * (2 + (cfg.intensity >> 5))) / 24;
  for (uint16_t s = 0; s < spawns; s++) canvas[random16(n)] += pal(random8());
}

void fxTwinkle(uint16_t n) {
  uint8_t density = 30 + (cfg.intensity >> 1);
  uint8_t thresh = 255 - density;
  uint32_t t = fxTime >> 4;
  for (uint16_t i = 0; i < n; i++) {
    uint16_t h = hash16(i + 3);
    uint8_t v = sin8((uint8_t)(t * (1 + (h & 3)) + (h & 0xFF)));
    if (v <= thresh) {
      canvas[i] = CRGB::Black;
      continue;
    }
    uint16_t b = (uint16_t)(v - thresh) * 255 / density;
    canvas[i] = pal((uint8_t)(h >> 8), b > 255 ? 255 : (uint8_t)b);
  }
}

void fxGlitter(uint16_t n) {
  uint8_t start = vbeat8(6);
  for (uint16_t i = 0; i < n; i++) canvas[i] = pal(start + (uint8_t)(i * 2), 90);
  uint16_t sparks = ((uint32_t)fxDelta * (1 + (cfg.intensity >> 4))) / 30;
  for (uint16_t s = 0; s < sparks; s++) canvas[random16(n)] = CRGB::White;
}

void fxSnow(uint16_t n) {
  fadeV(n, 1400);
  for (uint16_t i = 0; i < n; i++) nblend(canvas[i], CRGB(10, 14, 30), 40);
  uint16_t flakes = ((uint32_t)fxDelta * (1 + (cfg.intensity >> 5))) / 60;
  for (uint16_t s = 0; s < flakes; s++) canvas[random16(n)] = CRGB(235, 245, 255);
}

void fxFire(uint16_t n) {
  static uint8_t heat[NUM_LEDS] = {};
  static uint32_t acc = 0;
  uint8_t cooling = 14 + ((255 - cfg.intensity) >> 3);
  uint8_t sparking = 60 + (cfg.intensity >> 1);
  acc += fxDelta;
  while (acc >= 22) {
    acc -= 22;
    for (uint16_t i = 0; i < n; i++)
      heat[i] = qsub8(heat[i], random8(0, ((cooling * 10) / n) + 2));
    for (int32_t k = n - 1; k >= 2; k--)
      heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
    if (random8() < sparking) {
      uint8_t y = random8(7);
      if (y < n) heat[y] = qadd8(heat[y], random8(170, 255));
    }
  }
  for (uint16_t i = 0; i < n; i++) canvas[i] = pal(scale8(heat[i], 240), 255);
}

void fxCandle(uint16_t n) {
  for (uint16_t i = 0; i < n; i++) {
    uint8_t v = inoise8(i * 22, fxTime / 3);
    uint8_t b = 70 + scale8(v, 185);
    canvas[i] = CRGB(b, scale8(b, 95 + (v >> 2)), scale8(b, 14));
  }
}

void fxLightning(uint16_t n) {
  static uint32_t nextAt = 0;
  static uint8_t left = 0;
  static uint16_t p0 = 0, plen = 0;
  fadeV(n, 1600);
  if (fxDelta && fxTime >= nextAt) {
    if (left == 0) {
      left = 3 + random8(6);
      p0 = random16(n);
      plen = n / 10 + random16(n / 3);
    }
    if (p0 + plen > n) plen = n - p0;
    uint8_t b = (left & 1) ? 255 : 150;
    for (uint16_t i = p0; i < p0 + plen; i++) canvas[i] = CRGB(b, b, qadd8(b, 40));
    left--;
    nextAt = fxTime + (left ? random16(30, 130) : random16(1000, 4200));
  }
}

void fxStrobe(uint16_t n) {
  uint16_t period = 110 + (uint16_t)(255 - cfg.intensity) * 3;
  bool on = (fxTime % period) < 45;
  fill_solid(canvas, n, on ? cfg.color1 : CRGB::Black);
}

void fxPolice(uint16_t n) {
  uint32_t t = fxTime % 900;
  bool leftPhase = t < 450;
  bool blink = (t % 150) < 95;
  uint16_t half = n / 2;
  for (uint16_t i = 0; i < n; i++) {
    bool inLeft = i < half;
    bool lit = blink && (inLeft == leftPhase);
    canvas[i] = lit ? (inLeft ? CRGB(255, 0, 0) : CRGB(0, 40, 255)) : CRGB::Black;
  }
}

void fxHeartbeat(uint16_t n) {
  uint16_t t = fxTime % 1150;
  uint8_t env = 0;
  if (t < 170) env = ease8InOutCubic(255 - (uint16_t)t * 255 / 170);
  else if (t >= 290 && t < 470) env = ease8InOutCubic(190 - (uint16_t)(t - 290) * 190 / 180);
  float halfSpan = n / 2.0f;
  float radius = halfSpan * (env / 255.0f) + 1.0f;
  float center = (n - 1) / 2.0f;
  for (uint16_t i = 0; i < n; i++) {
    float d = fabsf(i - center);
    uint8_t b = d <= radius ? (uint8_t)(255.0f * (1.0f - d / (radius + 1.0f))) : 0;
    CRGB c = cfg.color1;
    c.nscale8_video(qadd8(b, 14));
    canvas[i] = c;
  }
}

void fxBpm(uint16_t n) {
  uint8_t bpm = 40 + (cfg.intensity >> 2);
  uint8_t beat = vbeatsin8(bpm, 64, 255);
  uint8_t hue = vbeat8(5);
  for (uint16_t i = 0; i < n; i++)
    canvas[i] = pal(hue + (uint8_t)(i * 2), beat - hue + (uint8_t)(i * 10));
}

void fxJuggle(uint16_t n) {
  fadeV(n, 1300);
  uint8_t dots = 3 + (cfg.intensity >> 5);
  for (uint8_t k = 0; k < dots; k++) {
    uint16_t p = vbeatsin16(6 + k * 2, 0, n - 1);
    canvas[p] |= pal((uint8_t)(k * (256 / dots)));
  }
}

void fxPlasma(uint16_t n) {
  uint8_t t1 = vbeat8(7), t2 = vbeat8(11), t3 = vbeat8(5);
  uint8_t f = 2 + (cfg.intensity >> 6);
  for (uint16_t i = 0; i < n; i++) {
    uint16_t v = sin8((uint8_t)(i * f * 2) + t1);
    v += sin8((uint8_t)(i * f) - t2);
    v += sin8((uint8_t)(i * f * 3) + t3);
    canvas[i] = pal((uint8_t)(v / 3));
  }
}

void fxNoise(uint16_t n) {
  uint16_t sc = 8 + (cfg.intensity >> 2);
  for (uint16_t i = 0; i < n; i++) canvas[i] = pal(inoise8(i * sc, fxTime / 4));
}

void fxAurora(uint16_t n) {
  for (uint16_t i = 0; i < n; i++) {
    uint8_t n1 = inoise8(i * 13, fxTime / 5);
    uint8_t n2 = inoise8(i * 8 + 3000, fxTime / 9);
    uint8_t b = qsub8(n1, 55);
    b = scale8(b, 220 + (cfg.intensity >> 3));
    uint8_t shimmer = sin8((uint8_t)(i * 4) + vbeat8(9));
    canvas[i] = pal(n2, scale8(b, 160 + (shimmer >> 1)));
  }
}

void fxOcean(uint16_t n) {
  uint8_t b1 = vbeat8(4), b2 = vbeat8(7);
  for (uint16_t i = 0; i < n; i++) {
    uint8_t a = sin8((uint8_t)(i * 4) + b1);
    uint8_t b = sin8((uint8_t)(i * 7) - b2);
    uint8_t c = inoise8(i * 12, fxTime / 6);
    uint16_t v = scale8(a, 110) + scale8(b, 80) + scale8(c, 95);
    uint8_t s = v > 255 ? 255 : (uint8_t)v;
    canvas[i] = CRGB(scale8(s, 18), scale8(s, 135), s);
    if (s > 235) canvas[i] += CRGB(60, 60, 60);
  }
}

void fxRipple(uint16_t n) {
  struct Rip {
    uint16_t center;
    uint16_t step;
    uint8_t idx;
    bool on;
  };
  static Rip rip[5] = {};
  static uint32_t acc = 0;
  fadeV(n, 1000);
  acc += fxDelta;
  while (acc >= 34) {
    acc -= 34;
    for (uint8_t r = 0; r < 5; r++) {
      if (!rip[r].on) continue;
      rip[r].step++;
      if (rip[r].step > n / 2) rip[r].on = false;
    }
    uint8_t density = 4 + (cfg.intensity >> 5);
    for (uint8_t r = 0; r < 5; r++) {
      if (rip[r].on || r >= density) continue;
      if (random8() < 12) {
        rip[r].on = true;
        rip[r].step = 0;
        rip[r].center = random16(n);
        rip[r].idx = random8();
        break;
      }
    }
  }
  for (uint8_t r = 0; r < 5; r++) {
    if (!rip[r].on) continue;
    uint16_t decay = (uint32_t)rip[r].step * 510 / (n ? n : 1);
    uint8_t b = decay >= 255 ? 0 : (uint8_t)(255 - decay);
    int32_t a = (int32_t)rip[r].center - rip[r].step;
    int32_t c = (int32_t)rip[r].center + rip[r].step;
    if (a >= 0) canvas[a] += pal(rip[r].idx, b);
    if (c < n) canvas[c] += pal(rip[r].idx, b);
  }
}

void fxFireworks(uint16_t n) {
  struct Spark {
    float pos;
    float vel;
    float life;
    uint8_t idx;
  };
  static Spark sp[36] = {};
  static bool rocketOn = false;
  static float rp = 0, rv = 0;
  static uint8_t ridx = 0;
  static uint32_t nextLaunch = 0;

  fadeV(n, 950);
  float dt = fxDelta / 1000.0f;
  if (!rocketOn && fxDelta && fxTime >= nextLaunch) {
    rocketOn = true;
    rp = 0;
    rv = n * (0.6f + random8() / 500.0f);
    ridx = random8();
  }
  if (rocketOn) {
    rv -= n * 0.85f * dt;
    rp += rv * dt;
    if (rv <= n * 0.06f || rp >= n - 1) {
      rocketOn = false;
      nextLaunch = fxTime + random16(350, 1500);
      uint8_t want = 8 + (cfg.intensity >> 4);
      for (uint8_t i = 0; i < 36 && want; i++) {
        if (sp[i].life > 0.0f) continue;
        sp[i].pos = rp;
        sp[i].vel = ((int16_t)random8() - 128) / 128.0f * n * 0.55f;
        sp[i].life = 1.0f;
        sp[i].idx = ridx + random8(50) - 25;
        want--;
      }
    } else if (rp >= 0 && rp < n) {
      canvas[(uint16_t)rp] += CRGB(255, 210, 150);
    }
  }
  for (uint8_t i = 0; i < 36; i++) {
    if (sp[i].life <= 0.0f) continue;
    sp[i].vel *= (1.0f - 1.8f * dt);
    sp[i].pos += sp[i].vel * dt;
    sp[i].life -= dt * 0.75f;
    if (sp[i].life <= 0.0f || sp[i].pos < 0 || sp[i].pos >= n) {
      sp[i].life = 0.0f;
      continue;
    }
    canvas[(uint16_t)sp[i].pos] += pal(sp[i].idx, (uint8_t)(sp[i].life * 255));
  }
}

void fxSinelon(uint16_t n) {
  fadeV(n, 800 + (uint16_t)cfg.intensity * 4);
  uint16_t p = vbeatsin16(14, 0, n - 1);
  canvas[p] += CHSV(vbeat8(3), 220, 255);
}

void fxDualChase(uint16_t n) {
  uint16_t width = 4 + (uint32_t)n * cfg.intensity / 1400;
  uint16_t a = ((uint32_t)vsaw16(13) * n) >> 16;
  uint16_t b = n - 1 - a;
  for (uint16_t i = 0; i < n; i++) canvas[i] = CRGB::Black;
  for (uint16_t k = 0; k < width; k++) {
    uint8_t fade = 255 - (uint16_t)k * 200 / width;
    CRGB c1 = cfg.color1, c2 = cfg.color2;
    c1.nscale8_video(fade);
    c2.nscale8_video(fade);
    int32_t pa = (int32_t)a - k;
    int32_t pb = (int32_t)b + k;
    if (pa < 0) pa += n;
    if (pb >= n) pb -= n;
    canvas[pa] += c1;
    canvas[pb] += c2;
  }
}

void fxCandyCane(uint16_t n) {
  uint16_t w = 3 + (cfg.intensity >> 4);
  uint16_t off = (fxTime / 45) % (w * 2);
  for (uint16_t i = 0; i < n; i++)
    canvas[i] = (((i + off) / w) & 1) ? cfg.color2 : cfg.color1;
}

void fxStarfield(uint16_t n) {
  fill_solid(canvas, n, CRGB(2, 3, 12));
  uint8_t stars = 12 + (cfg.intensity >> 2);
  uint32_t drift = fxTime / 24;
  for (uint8_t s = 0; s < stars; s++) {
    uint16_t h = hash16(s * 37 + 11);
    uint16_t pos = (h + drift * (1 + (h & 3))) % n;
    uint8_t b = 70 + ((h >> 8) & 0x7F);
    uint8_t tw = sin8((uint8_t)((fxTime >> 3) + (h & 0xFF)));
    canvas[pos] += pal((uint8_t)(h >> 6), scale8(b, 120 + (tw >> 1)));
  }
}

// Presence aware: paints a bar up to the reported distance plus a radar sweep.
void fxDistanceRadar(uint16_t n) {
  fadeV(n, 1500);
  bool live = sensor.linked();
  uint16_t range = cfg.maxRangeCm ? cfg.maxRangeCm : 600;
  uint16_t d = sensor.distanceCm > range ? range : sensor.distanceCm;
  uint16_t target = live && sensor.present ? (uint32_t)d * (n - 1) / range : 0;
  uint16_t sweep = ((uint32_t)vsaw16(16) * n) >> 16;

  for (uint16_t i = 0; i < n; i++) {
    if (live && sensor.present && i <= target) {
      uint8_t b = 40 + (uint16_t)i * 120 / (target ? target : 1);
      canvas[i] += pal((uint8_t)(190 - (uint16_t)i * 150 / (n - 1)), b);
    }
    if (i == sweep) canvas[i] += live && sensor.present ? CRGB(255, 255, 255) : CRGB(40, 0, 0);
  }
  if (live && sensor.present && target < n) {
    canvas[target] += CRGB(255, 255, 220);
    if (target + 1 < n) canvas[target + 1] += CRGB(90, 90, 70);
    if (target > 0) canvas[target - 1] += CRGB(90, 90, 70);
  }
}

// Presence aware: colour temperature follows nobody / moving / stationary.
void fxPresenceGlow(uint16_t n) {
  static float hue = 160.0f;
  float target = 160.0f;  // calm blue while the room is empty
  if (sensor.linked() && sensor.present) target = sensor.radarResult == 1 ? 20.0f : 96.0f;
  float k = fxDelta / 900.0f;
  if (k > 1.0f) k = 1.0f;
  hue += (target - hue) * k;
  uint8_t h = (uint8_t)hue;
  for (uint16_t i = 0; i < n; i++) {
    uint8_t v = inoise8(i * 10, fxTime / 7);
    uint8_t b = 60 + scale8(v, 195);
    canvas[i] = CHSV(h + (v >> 4), 220, b);
  }
}

void fxRainbowBreath(uint16_t n) {
  uint8_t b = ease8InOutCubic(vbeatsin8(9, 20, 255));
  fill_rainbow(canvas, n, vbeat8(7), 1 + (cfg.intensity >> 5));
  for (uint16_t i = 0; i < n; i++) canvas[i].nscale8_video(b);
}

void fxMatrix(uint16_t n) {
  fadeV(n, 900 + (uint16_t)(255 - cfg.intensity) * 3);
  static float drops[12] = {};
  static float spd[12] = {};
  static bool init = false;
  if (!init) {
    for (uint8_t i = 0; i < 12; i++) {
      drops[i] = random16(n);
      spd[i] = 18.0f + random8(40);
    }
    init = true;
  }
  float dt = fxDelta / 1000.0f;
  uint8_t count = 4 + (cfg.intensity >> 5);
  for (uint8_t i = 0; i < count && i < 12; i++) {
    drops[i] += spd[i] * dt;
    if (drops[i] > n + 8) {
      drops[i] = -random8(8);
      spd[i] = 18.0f + random8(50);
    }
    for (uint8_t t = 0; t < 8; t++) {
      int32_t p = (int32_t)drops[i] - t;
      if (p < 0 || p >= n) continue;
      uint8_t b = 255 - t * 28;
      canvas[p] += CRGB(0, b, scale8(b, 40 + (t == 0 ? 80 : 0)));
    }
  }
}

void fxBlendWave(uint16_t n) {
  uint8_t s1 = vbeat8(8), s2 = vbeat8(13);
  for (uint16_t i = 0; i < n; i++) {
    uint8_t a = sin8((uint8_t)(i * 3) + s1);
    uint8_t b = sin8((uint8_t)(i * 5) - s2);
    CRGB c1 = pal(a), c2 = pal(b + 128);
    canvas[i] = blend(c1, c2, scale8(a, b));
  }
}

void fxPride(uint16_t n) {
  static uint16_t sPseudotime = 0;
  static uint16_t sLastMillis = 0;
  static uint16_t sHue16 = 0;
  uint8_t sat8 = beatsin88(87, 220, 250);
  uint8_t brightdepth = beatsin88(341, 96, 224);
  uint16_t brightnessthetainc16 = beatsin88(203, (25 * 256), (40 * 256));
  uint8_t msmultiplier = beatsin88(147, 23, 60);
  uint16_t hue16 = sHue16;
  uint16_t hueinc16 = beatsin88(113, 1, 3000);
  uint16_t ms = fxTime;
  uint16_t deltams = ms - sLastMillis;
  sLastMillis = ms;
  sPseudotime += deltams * msmultiplier;
  sHue16 += deltams * beatsin88(400, 5, 9);
  uint16_t brightnesstheta16 = sPseudotime;
  for (uint16_t i = 0; i < n; i++) {
    hue16 += hueinc16;
    uint8_t hue8 = hue16 >> 8;
    brightnesstheta16 += brightnessthetainc16;
    uint16_t b16 = sin16(brightnesstheta16) + 32768;
    uint8_t bri8 = (uint32_t)((uint16_t)(b16 * brightdepth) >> 8) / 256 + (255 - brightdepth);
    canvas[i] = CHSV(hue8, sat8, bri8);
  }
}

void fxLaser(uint16_t n) {
  fadeV(n, 1400 + (uint16_t)cfg.intensity * 4);
  uint16_t pos = ((uint32_t)vsaw16(18) * n) >> 16;
  uint8_t width = 2 + (cfg.intensity >> 6);
  for (uint8_t k = 0; k < width; k++) {
    int32_t p = (int32_t)pos - k;
    if (p >= 0 && p < n) {
      CRGB c = blend(cfg.color1, CRGB::White, 180 - k * 40);
      canvas[p] += c;
    }
  }
}

void fxWaterfall(uint16_t n) {
  fadeV(n, 600 + (uint16_t)(255 - cfg.intensity) * 2);
  uint8_t start = vbeat8(20);
  for (uint16_t i = 0; i < n; i++) {
    uint8_t v = sin8(start - (uint8_t)(i * 4));
    if (v > 200) canvas[i] += pal(start + (uint8_t)i, scale8(v, v));
  }
  if (fxDelta && random8() < 40 + (cfg.intensity >> 2))
    canvas[random16(n / 4)] += CRGB::White;
}

void fxExplosion(uint16_t n) {
  static float radius = 0;
  static uint8_t hue = 0;
  static uint32_t nextAt = 0;
  fadeV(n, 1100);
  float center = (n - 1) / 2.0f;
  if (fxDelta && fxTime >= nextAt && radius <= 0) {
    radius = 1.0f;
    hue = random8();
  }
  if (radius > 0) {
    radius += fxDelta * (0.04f + cfg.intensity / 4000.0f);
    float maxR = n * 0.65f;
    uint8_t amp = radius >= maxR ? 0 : (uint8_t)((1.0f - radius / maxR) * 255);
    for (uint16_t i = 0; i < n; i++) {
      float d = fabsf(i - center) - radius;
      if (fabsf(d) < 4.0f) {
        uint8_t b = scale8(amp, 255 - (uint8_t)(fabsf(d) * 50));
        canvas[i] += pal(hue + (uint8_t)(i * 2), b);
      }
    }
    if (radius > maxR) {
      radius = 0;
      nextAt = fxTime + random16(400, 1600);
    }
  }
}

void fxMultiWave(uint16_t n) {
  for (uint16_t i = 0; i < n; i++) {
    uint16_t v = 0;
    v += sin8((uint8_t)(i * 2) + vbeat8(6));
    v += sin8((uint8_t)(i * 5) + vbeat8(11));
    v += sin8((uint8_t)(i * 9) - vbeat8(4));
    canvas[i] = pal((uint8_t)(v / 3), scale8((uint8_t)(v / 3), 200 + (cfg.intensity >> 3)));
  }
}

void fxLavaLamp(uint16_t n) {
  for (uint16_t i = 0; i < n; i++) {
    uint8_t n1 = inoise8(i * 18, fxTime / 5);
    uint8_t n2 = inoise8(i * 11 + 2000, fxTime / 11);
    uint8_t heat = qadd8(scale8(n1, 180), scale8(n2, 120));
    canvas[i] = ColorFromPalette(HeatColors_p, heat, 255, LINEARBLEND);
  }
}

void fxChaseFill(uint16_t n) {
  uint32_t period = 2200;
  uint32_t t = fxTime % (period * 2);
  bool erase = t >= period;
  uint16_t head = (uint32_t)(t % period) * n / period;
  fadeV(n, erase ? 800 : 0);
  if (!erase) {
    for (uint16_t i = 0; i <= head && i < n; i++) canvas[i] = pal((uint8_t)(i * 2) + vbeat8(2));
    if (head < n) canvas[head] += CRGB::White;
  } else {
    for (uint16_t i = head; i < n; i++) canvas[i] = CRGB::Black;
  }
}

void fxFairy(uint16_t n) {
  fill_solid(canvas, n, CRGB(8, 6, 14));
  uint8_t count = 8 + (cfg.intensity >> 3);
  for (uint8_t s = 0; s < count; s++) {
    uint16_t h = hash16(s * 91 + 5);
    uint8_t phase = (uint8_t)((fxTime / (18 + (h & 15))) + (h & 0xFF));
    uint8_t b = scale8(sin8(phase), 200);
    if (b < 20) continue;
    uint16_t pos = (h + (fxTime / 40) * (1 + (h & 1))) % n;
    canvas[pos] += pal((uint8_t)(h >> 5), b);
    if (b > 180 && pos + 1 < n) canvas[pos + 1] += CRGB(40, 40, 40);
  }
}

void fxRaindrops(uint16_t n) {
  fadeV(n, 700);
  for (uint16_t i = 0; i < n; i++) nblend(canvas[i], CRGB(4, 8, 22), 30);
  uint16_t drops = ((uint32_t)fxDelta * (2 + (cfg.intensity >> 4))) / 40;
  for (uint16_t s = 0; s < drops; s++) {
    uint16_t p = random16(n);
    canvas[p] += CRGB(60, 120, 255);
    if (p > 0) canvas[p - 1] += CRGB(10, 30, 80);
  }
}

void fxSoftTwinkle(uint16_t n) {
  for (uint16_t i = 0; i < n; i++) {
    uint16_t h = hash16(i * 17 + 3);
    uint8_t base = 18 + (cfg.intensity >> 3);
    uint8_t tw = sin8((uint8_t)((fxTime >> 4) * (1 + (h & 3)) + (h & 0xFF)));
    uint8_t b = base + scale8(tw, 180);
    canvas[i] = pal((uint8_t)(h >> 7) + vbeat8(1), b);
  }
}

void fxScanner(uint16_t n) {
  fadeV(n, 500 + (uint16_t)cfg.intensity * 3);
  uint16_t pos = vbeatsin16(10, 0, n - 1);
  uint8_t w = 6 + (cfg.intensity >> 5);
  for (int16_t k = -((int16_t)w); k <= (int16_t)w; k++) {
    int32_t p = (int32_t)pos + k;
    if (p < 0 || p >= n) continue;
    uint8_t b = 255 - (uint16_t)(k < 0 ? -k : k) * 255 / (w + 1);
    canvas[p] += pal(vbeat8(3) + (uint8_t)(p * 2), scale8(b, b));
  }
}

void fxColorMorph(uint16_t n) {
  uint8_t mix = ease8InOutCubic(vbeatsin8(5, 0, 255));
  CRGB a = cfg.color1, b = cfg.color2;
  CRGB mid = blend(a, b, mix);
  uint8_t wave = vbeat8(9);
  for (uint16_t i = 0; i < n; i++) {
    uint8_t local = scale8(sin8((uint8_t)(i * 3) + wave), 90);
    canvas[i] = blend(mid, pal((uint8_t)(i * 2) + mix), local);
  }
}

void fxBreathBars(uint16_t n) {
  uint8_t bars = 3 + (cfg.intensity >> 6);
  uint16_t barW = n / bars;
  if (!barW) barW = 1;
  for (uint16_t i = 0; i < n; i++) {
    uint8_t bar = i / barW;
    uint8_t phase = bar * 40;
    uint8_t b = ease8InOutCubic(vbeatsin8(8, 10, 255, phase));
    canvas[i] = pal((uint8_t)(bar * 40) + vbeat8(2), b);
  }
}

// Closer people light a warm halo that grows toward the viewer.
void fxApproachHalo(uint16_t n) {
  fadeV(n, 1200);
  bool live = sensor.linked() && sensor.present;
  uint16_t range = cfg.maxRangeCm ? cfg.maxRangeCm : 600;
  float nearness = 0.15f;
  if (live) {
    uint16_t d = sensor.distanceCm > range ? range : sensor.distanceCm;
    nearness = 1.0f - (float)d / range;
    if (nearness < 0.05f) nearness = 0.05f;
  }
  float radius = nearness * n * 0.55f + 2.0f;
  float center = (n - 1) / 2.0f;
  uint8_t hue = sensor.radarResult == 1 ? 12 : 140;
  for (uint16_t i = 0; i < n; i++) {
    float d = fabsf(i - center);
    if (d > radius) {
      canvas[i] += CHSV(hue, 180, 18);
      continue;
    }
    uint8_t b = (uint8_t)((1.0f - d / radius) * (40 + nearness * 215));
    canvas[i] += CHSV(hue + (uint8_t)(d), 200, b);
  }
}

struct EffectDef {
  const char *name;
  void (*fn)(uint16_t);
};

const EffectDef EFFECTS[] = {
    {"纯色", fxSolid},           {"呼吸", fxBreath},
    {"渐变色带", fxGradient},    {"彩虹流动", fxRainbowFlow},
    {"彩虹同步", fxRainbowSync}, {"调色板流动", fxPaletteFlow},
    {"追逐跑马", fxTheater},     {"波浪流光", fxRunningLights},
    {"颜色擦除", fxColorWipe},   {"彗星", fxComet},
    {"来回扫描", fxCylon},       {"流星雨", fxMeteor},
    {"弹跳球", fxBalls},         {"五彩纸屑", fxConfetti},
    {"星光闪烁", fxTwinkle},     {"闪耀微光", fxGlitter},
    {"雪花飘落", fxSnow},        {"火焰", fxFire},
    {"烛光", fxCandle},          {"闪电", fxLightning},
    {"频闪", fxStrobe},          {"警灯", fxPolice},
    {"心跳", fxHeartbeat},       {"律动节拍", fxBpm},
    {"交错光点", fxJuggle},      {"等离子", fxPlasma},
    {"柏林噪声", fxNoise},       {"极光", fxAurora},
    {"海洋波涛", fxOcean},       {"涟漪", fxRipple},
    {"烟花", fxFireworks},       {"正弦游走", fxSinelon},
    {"双色追逐", fxDualChase},   {"糖果条纹", fxCandyCane},
    {"星河", fxStarfield},       {"距离雷达", fxDistanceRadar},
    {"存在热度", fxPresenceGlow},{"彩虹呼吸", fxRainbowBreath},
    {"数字雨", fxMatrix},        {"双波混合", fxBlendWave},
    {"彩虹Pride", fxPride},      {"激光扫射", fxLaser},
    {"瀑布流", fxWaterfall},     {"中心爆开", fxExplosion},
    {"多层波浪", fxMultiWave},   {"熔岩灯", fxLavaLamp},
    {"追逐填充", fxChaseFill},   {"仙女灯", fxFairy},
    {"雨滴", fxRaindrops},       {"柔和闪烁", fxSoftTwinkle},
    {"光束扫描", fxScanner},     {"双色渐变", fxColorMorph},
    {"呼吸灯条", fxBreathBars},  {"接近光晕", fxApproachHalo},
};
constexpr uint8_t EFFECT_COUNT = sizeof(EFFECTS) / sizeof(EFFECTS[0]);

// Effects skipped by auto-cycle (aggressive / sensor-only).
inline bool effectIsAmbient(uint8_t idx) {
  // 20 频闪, 21 警灯, 35 距离雷达, 36 存在热度, 53 接近光晕
  return idx != 20 && idx != 21 && idx != 35 && idx != 36 && idx != 53 &&
         idx < EFFECT_COUNT;
}

// ---------------------------------------------------------------- transitions
const char *const TRANSITIONS[] = {"立即",     "整体淡入", "头部扫入", "尾部扫入",
                                   "中心扩散", "两端收拢", "随机溶解", "彗星扫入",
                                   "交错拉链", "闪白淡入", "波纹扫入", "棋盘展开",
                                   "两端淡入", "像素喷发"};
constexpr uint8_t TRANSITION_COUNT = sizeof(TRANSITIONS) / sizeof(TRANSITIONS[0]);

inline uint8_t softEdge(float d, float w) {
  if (d <= 0.0f) return 0;
  if (d >= w) return 255;
  return (uint8_t)(d / w * 255.0f);
}

uint8_t revealMask(uint8_t type, uint16_t i, float p) {
  const float n = NUM_LEDS;
  const float w = 16.0f;
  const float center = (n - 1) / 2.0f;
  switch (type) {
    case 0: return p > 0.5f ? 255 : 0;
    case 1: return (uint8_t)(p * 255.0f);
    case 2: return softEdge(p * (n + w) - i, w);
    case 3: return softEdge(p * (n + w) - (n - 1 - i), w);
    case 4: return softEdge(p * (n * 0.5f + w) - fabsf(i - center), w);
    case 5: return softEdge(p * (n * 0.5f + w) - (n * 0.5f - fabsf(i - center)), w);
    case 6: return hash16(i + 7) < (uint16_t)(p * 65535.0f) ? 255 : 0;
    case 7: return softEdge(p * (n + w * 2.0f) - i, w * 2.0f);
    case 8: {
      float pos = (i & 1) ? (n - 1 - i) * 0.5f : i * 0.5f;
      return softEdge(p * (n * 0.5f + w) - pos, w);
    }
    case 9: return ease8InOutCubic((uint8_t)(p * 255.0f));
    case 10: {
      float d = p * (n + w) - i + (sin8((uint8_t)(i * 6)) - 128) / 10.0f;
      return softEdge(d, w);
    }
    case 11: {  // checkerboard cells dissolve in
      uint16_t cell = (i / 4) + ((i / 4) & 1) * 17;
      return hash16(cell + 19) < (uint16_t)(p * 65535.0f) ? 255 : 0;
    }
    case 12: {  // fade in from both ends
      float edge = fminf((float)i, n - 1 - i);
      return softEdge(p * (n * 0.5f + w) - edge, w);
    }
    case 13: {  // pixel spray from a moving seed
      float seed = p * n;
      float d = fabsf(i - seed);
      uint16_t sprinkle = hash16(i * 3 + 11);
      if (sprinkle < (uint16_t)(p * 50000.0f)) return 255;
      return softEdge(6.0f - d + p * 8.0f, 6.0f);
    }
    default: return 255;
  }
}

void applyReveal(uint8_t type, float p, uint8_t floorV) {
  if (p >= 0.999f) return;  // fully revealed, the mask would be a no-op
  for (uint16_t i = 0; i < NUM_LEDS; i++) {
    uint8_t m = revealMask(type, i, p);
    leds[i].nscale8(lerp8by8(floorV, 255, m));
  }
  if (type == 7 && p > 0.0f) {  // comet head glow
    int32_t head = (int32_t)(p * (NUM_LEDS + 32.0f)) - 1;
    for (int8_t k = -2; k <= 2; k++) {
      int32_t idx = head + k;
      if (idx < 0 || idx >= NUM_LEDS) continue;
      uint8_t b = 255 >> (k < 0 ? -k : k);
      leds[idx] += CRGB(b, b, scale8(b, 210));
    }
  }
  if (type == 9 && p < 0.3f) {  // white flash that decays into the fade-in
    uint8_t amt = (uint8_t)((1.0f - p / 0.3f) * 255.0f);
    for (uint16_t i = 0; i < NUM_LEDS; i++) nblend(leds[i], CRGB::White, amt);
  }
}

// ------------------------------------------------------------------ overlays
void overlayDistance() {
  if (!cfg.distanceFx || !sensor.linked() || !sensor.present) return;
  uint16_t range = cfg.maxRangeCm ? cfg.maxRangeCm : 600;
  uint16_t d = sensor.distanceCm > range ? range : sensor.distanceCm;
  int32_t center = (int32_t)((uint32_t)d * (NUM_LEDS - 1) / range);
  const int32_t half = 11;
  for (int32_t k = -half; k <= half; k++) {
    int32_t idx = center + k;
    if (idx < 0 || idx >= NUM_LEDS) continue;
    uint8_t b = 255 - (uint16_t)(k < 0 ? -k : k) * 255 / half;
    b = scale8(b, b);
    leds[idx] += CRGB(scale8(b, 130), scale8(b, 225), b);
  }
}

void overlayPing() {
  if (!pingStartMs) return;
  uint32_t age = millis() - pingStartMs;
  const uint32_t life = 750;
  if (age >= life) {
    pingStartMs = 0;
    return;
  }
  float t = age / (float)life;
  float radius = t * (NUM_LEDS * 0.55f);
  uint8_t amp = (uint8_t)((1.0f - t) * 255.0f);
  float center = pingCenter * (NUM_LEDS - 1);
  for (int8_t dir = -1; dir <= 1; dir += 2) {
    for (int8_t k = -3; k <= 3; k++) {
      int32_t idx = (int32_t)(center + dir * radius) + k;
      if (idx < 0 || idx >= NUM_LEDS) continue;
      uint8_t b = scale8(amp, 255 - (uint16_t)(k < 0 ? -k : k) * 60);
      leds[idx] += CRGB(scale8(b, 200), scale8(b, 255), b);
    }
  }
}

// ------------------------------------------------------------------ geometry
uint16_t virtualLen() {
  uint16_t segs = clampU16(cfg.segments, 1, 8);
  uint16_t n = NUM_LEDS / segs;
  if (cfg.mirror) n /= 2;
  return n < 2 ? 2 : n;
}

void expandCanvas(uint16_t n) {
  uint16_t segs = clampU16(cfg.segments, 1, 8);
  uint16_t segLen = NUM_LEDS / segs;
  for (uint16_t s = 0; s < segs; s++) {
    uint16_t base = s * segLen;
    for (uint16_t j = 0; j < segLen; j++) {
      uint16_t src;
      if (cfg.mirror) src = (j < n) ? j : (uint16_t)(segLen - 1 - j);
      else src = j % n;
      if (src >= n) src = n - 1;
      leds[base + j] = canvas[src];
    }
  }
  for (uint16_t i = segs * segLen; i < NUM_LEDS; i++) leds[i] = canvas[n - 1];
  if (cfg.reverse) {
    for (uint16_t i = 0, j = NUM_LEDS - 1; i < j; i++, j--) {
      CRGB t = leds[i];
      leds[i] = leds[j];
      leds[j] = t;
    }
  }
}

}  // namespace

// ------------------------------------------------------------------- public
void ledsBegin() {
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setDither(DISABLE_DITHER);  // dithering flickers once WiFi steals cycles
  ledsApplyPowerLimit();
  FastLED.setBrightness(0);
  // clear(true) would FastLED.show() here; on ESP32-C3 + FLASH_LOCK that can
  // assert inside RMT (xSemaphoreTake while flash lock holds the scheduler).
  // First real frame is pushed from ledsTick() once setup() finishes.
  FastLED.clear(false);
  random16_set_seed((uint16_t)esp_random());
  lastFrameMs = millis();
  fpsWindowMs = lastFrameMs;
}

void ledsApplyPowerLimit() {
  FastLED.setMaxPowerInVoltsAndMilliamps(5, cfg.currentMa ? cfg.currentMa : 500);
}

void ledsMarkPaletteDirty() { palDirty = true; }

void ledsRequestOn() {
  idleFloor = false;
  if (rstate != RS_ON) rstate = RS_ENTER;
}

void ledsRequestOff(bool keepIdleFloor) {
  idleFloor = keepIdleFloor;
  if (rstate != RS_OFF) rstate = RS_EXIT;
}

void ledsTriggerPing(uint16_t atCm) {
  uint16_t range = cfg.maxRangeCm ? cfg.maxRangeCm : 600;
  float p = atCm ? (float)(atCm > range ? range : atCm) / range : 0.5f;
  pingCenter = p;
  pingStartMs = millis();
}

RunState ledsRunState() { return rstate; }
uint8_t ledsFps() { return fpsValue; }

uint8_t effectCount() { return EFFECT_COUNT; }
const char *effectName(uint8_t index) {
  return index < EFFECT_COUNT ? EFFECTS[index].name : "";
}
uint8_t paletteCount() { return PALETTE_COUNT; }
const char *paletteName(uint8_t index) {
  return index < PALETTE_COUNT ? PALETTE_NAMES[index] : "";
}
uint8_t transitionCount() { return TRANSITION_COUNT; }
const char *transitionName(uint8_t index) {
  return index < TRANSITION_COUNT ? TRANSITIONS[index] : "";
}

void ledsPreviewHex(String &out) {
  static const char kHex[] = "0123456789abcdef";
  uint16_t step = NUM_LEDS / PREVIEW_PIXELS;
  if (!step) step = 1;
  uint8_t bri = (uint8_t)briEase;
  out = "";
  out.reserve((NUM_LEDS / step) * 6 + 2);
  for (uint16_t i = 0; i < NUM_LEDS; i += step) {
    CRGB c = leds[i];
    c.nscale8_video(bri);
    uint8_t ch[3] = {c.r, c.g, c.b};
    for (uint8_t k = 0; k < 3; k++) {
      out += kHex[ch[k] >> 4];
      out += kHex[ch[k] & 0x0F];
    }
  }
}

void ledsTick() {
  uint32_t now = millis();
  if (now - lastShowMs < FRAME_INTERVAL_MS) return;
  uint32_t dt = now - lastFrameMs;
  lastFrameMs = now;
  lastShowMs = now;
  if (dt > 120) dt = 120;  // a long HTTP burst must not fast-forward animations

  if (palDirty) buildPalette();

  // Auto-cycle through ambient animations while the strip is lit.
  if (cfg.autoCycle && rstate == RS_ON) {
    uint32_t span = (uint32_t)(cfg.cycleSec ? cfg.cycleSec : 30) * 1000UL;
    if (!cycleAtMs) cycleAtMs = now + span;
    if (now >= cycleAtMs) {
      cycleAtMs = now + span;
      uint8_t next = cfg.effect;
      for (uint8_t tries = 0; tries < EFFECT_COUNT; tries++) {
        next = (next + 1 + random8(3)) % EFFECT_COUNT;
        if (effectIsAmbient(next)) break;
      }
      cfg.effect = next;
      settingsTouch();
    }
  } else {
    cycleAtMs = 0;
  }

  // Mood sync: motion -> energetic palette/effect, stationary -> calm.
  if (cfg.moodSync && sensor.linked() && sensor.present && rstate == RS_ON) {
    uint8_t mood = sensor.radarResult;
    if (mood != lastMood && mood > 0) {
      lastMood = mood;
      if (mood == 1) {
        static const uint8_t energetic[] = {3, 11, 13, 23, 25, 30, 41, 43, 50};
        cfg.effect = energetic[random8(sizeof(energetic))];
        cfg.speed = qadd8(cfg.speed, 20);
      } else {
        static const uint8_t calm[] = {1, 18, 27, 28, 34, 37, 47, 49, 52};
        cfg.effect = calm[random8(sizeof(calm))];
        if (cfg.speed > 40) cfg.speed = qsub8(cfg.speed, 15);
      }
      settingsTouch();
    }
  } else if (!sensor.present) {
    lastMood = 255;
  }

  // A moving target energises the scene, a stationary one calms it down.
  float speedTarget = cfg.speed;
  if (cfg.motionBoost && sensor.linked() && sensor.present) {
    if (sensor.radarResult == 1) speedTarget = speedTarget * 1.5f + 12.0f;
    else if (sensor.radarResult == 2) speedTarget *= 0.65f;
  }
  if (speedTarget > 255.0f) speedTarget = 255.0f;
  float ke = dt / 600.0f;
  if (ke > 1.0f) ke = 1.0f;
  speedEase += (speedTarget - speedEase) * ke;

  speedAcc += (uint32_t)(dt * (speedEase < 0.0f ? 0.0f : speedEase));
  fxDelta = (uint16_t)(speedAcc / 128);
  speedAcc -= (uint32_t)fxDelta * 128;
  fxTime += fxDelta;

  // reveal progress
  if (rstate == RS_ENTER) {
    reveal += dt / (float)(cfg.enterMs ? cfg.enterMs : 1);
    if (reveal >= 1.0f) {
      reveal = 1.0f;
      rstate = RS_ON;
    }
  } else if (rstate == RS_EXIT) {
    reveal -= dt / (float)(cfg.exitMs ? cfg.exitMs : 1);
    if (reveal <= 0.0f) {
      reveal = 0.0f;
      rstate = RS_OFF;
    }
  }

  float briTarget = cfg.brightness;
  if (cfg.approachDim && sensor.linked() && sensor.present) {
    uint16_t range = cfg.maxRangeCm ? cfg.maxRangeCm : 600;
    uint16_t d = sensor.distanceCm > range ? range : sensor.distanceCm;
    float nearness = 1.0f - (float)d / range;
    briTarget = cfg.brightness * (0.45f + 0.55f * nearness);
  }
  briEase += (briTarget - briEase) * (dt / 220.0f > 1.0f ? 1.0f : dt / 220.0f);

  // Derived every frame so moving the brightness slider keeps the dim floor at
  // the level the user asked for.
  uint8_t floorLevel = 0;
  if (idleFloor && cfg.idleBright && cfg.brightness) {
    uint16_t f = (uint16_t)cfg.idleBright * 255 / cfg.brightness;
    floorLevel = f > 255 ? 255 : (uint8_t)f;
  }

  bool dark = (rstate == RS_OFF) && floorLevel == 0;
  if (dark) {
    fill_solid(leds, NUM_LEDS, CRGB::Black);
    FastLED.setBrightness(0);
    FastLED.show();
  } else {
    gVLen = virtualLen();
    if (gVLen != gLastVLen) {
      gLastVLen = gVLen;
      fill_solid(canvas, NUM_LEDS, CRGB::Black);
    }
    uint8_t fx = cfg.effect < EFFECT_COUNT ? cfg.effect : 0;
    EFFECTS[fx].fn(gVLen);
    expandCanvas(gVLen);
    overlayDistance();
    uint8_t type = (rstate == RS_EXIT || rstate == RS_OFF)
                       ? (cfg.exitFx < TRANSITION_COUNT ? cfg.exitFx : 1)
                       : (cfg.entranceFx < TRANSITION_COUNT ? cfg.entranceFx : 1);
    applyReveal(type, reveal, floorLevel);
    // After the mask on purpose: the ripple acknowledges the detection while the
    // entrance animation is still ramping the rest of the strip up.
    overlayPing();
    FastLED.setBrightness((uint8_t)briEase);
    FastLED.show();
  }

  frameCounter++;
  if (now - fpsWindowMs >= 1000) {
    fpsValue = (uint8_t)(frameCounter > 255 ? 255 : frameCounter);
    frameCounter = 0;
    fpsWindowMs = now;
  }
}
