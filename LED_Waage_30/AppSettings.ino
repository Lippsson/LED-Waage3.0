// =============================================
// RUNTIME SETTINGS (editable from the phone app)
// =============================================
// Table driven: the app asks for the table (GET) and builds its UI from
// key / value / min / max, so a new entry here needs no firmware protocol change.


Setting settingsTable[] = {
  // calibration offsets (normally written by the calibration, can be fine-tuned by hand)
  { "offH",      'f', &offsetHorizontal,    -10.0,   10.0,  -0.6   },
  { "offVp",     'f', &offsetVerticalPos,   -10.0,   10.0,   0.4   },
  { "offVn",     'f', &offsetVerticalNeg,   -10.0,   10.0,  -0.4   },
  // smoothing
  { "smooth",    'f', &SMOOTHING_FACTOR,      0.01,   1.0,   0.10  },
  { "dlpf",      'i', &MPU_DLPF,               0,      6,      3     },
  { "round",     'i', &ANGLE_ROUND,            0,      2,      0     },
  { "bsmooth",   'f', &BUBBLE_SMOOTHING,      0.01,   1.0,   0.15  },
  // bubble
  { "brange",    'f', &BUBBLE_RANGE,          0.2,    5.0,   1.0   },
  { "bubbleLeds",'i', &BUBBLE_LEDS,           2,     40,    12     },
  { "travel",    'i', &BUBBLE_TRAVEL,         1,     60,    38     },
  { "fadeMs",    'i', &FADE_DURATION,         0,   2000,   300     },
  { "holdLevel", 'i', &WHITE_ZONE_MIN_TIME,   0,   5000,  1000     },
  // hardware
  { "bright",    'i', &LED_BRIGHTNESS,        5,    255,    255     },
  { "sound",     'i', &SOUND_ENABLED,         0,      1,      1     },
  { "sleepMin",  'i', &AUTO_OFF_MIN,           0,     60,      0     },
  { "calWait",   'i', &CAL_WAIT_MS,        2000,  30000,   5000     },
  // sensor mounting
  { "axes",      'i', &AXIS_PERM,               0,      5,      0     },
  { "invX",      'i', &AXIS_INV_X,              0,      1,      0     },
  { "invY",      'i', &AXIS_INV_Y,              0,      1,      0     },
  { "invZ",      'i', &AXIS_INV_Z,              0,      1,      0     },
  { "rate",      'i', &LEVEL_RATE_HZ,         5,    100,     25     },
};
const int SETTINGS_COUNT = sizeof(settingsTable) / sizeof(settingsTable[0]);

void applyBrightness() {
  stripLeft.setBrightness(constrain(LED_BRIGHTNESS, 1, 255));
  stripRight.setBrightness(constrain(LED_BRIGHTNESS, 1, 255));
}

Setting* settingFind(const String& key) {
  for (int i = 0; i < SETTINGS_COUNT; i++) {
    if (key == settingsTable[i].key) return &settingsTable[i];
  }
  return nullptr;
}

float settingGet(const Setting* s) {
  return (s->type == 'f') ? *(float*)s->ptr : (float)*(int*)s->ptr;
}

void settingSet(Setting* s, float v) {
  v = constrain(v, s->minV, s->maxV);
  if (s->type == 'f') *(float*)s->ptr = v;
  else                *(int*)s->ptr = (int)lroundf(v);
  if (strcmp(s->key, "bright") == 0) applyBrightness();
  if (strcmp(s->key, "dlpf") == 0) reqMpuFilter = true;   // applied in loop(), I2C is not thread safe
}

// Called from setup(). Only keys that exist in NVS are applied, so the legacy
// offset migration done in setup() and the compiled-in defaults stay intact.
void settingsLoad() {
  prefs.begin("spiritlevel", false);
  if (prefs.getUChar("cfgv", 0) < 1) {  // migration 1: reset the angle smoothing to its default
    prefs.remove("smooth");
    prefs.putUChar("cfgv", 1);
  }
  for (int i = 0; i < SETTINGS_COUNT; i++) {
    Setting* s = &settingsTable[i];
    if (!prefs.isKey(s->key)) continue;
    if (s->type == 'f') settingSet(s, prefs.getFloat(s->key, s->defV));
    else                settingSet(s, (float)prefs.getInt(s->key, (int)s->defV));
  }
  prefs.end();
  for (int i = 0; i < SETTINGS_COUNT; i++) {
    Serial.print("[CFG] "); Serial.print(settingsTable[i].key); Serial.print(" = "); Serial.println(settingGet(&settingsTable[i]), 3);
  }
}

void settingsSave() {
  prefs.begin("spiritlevel", false);
  for (int i = 0; i < SETTINGS_COUNT; i++) {
    Setting* s = &settingsTable[i];
    if (s->type == 'f') prefs.putFloat(s->key, *(float*)s->ptr);
    else                prefs.putInt(s->key, *(int*)s->ptr);
  }
  prefs.end();
  Serial.println("[CFG] settings saved to flash");
}

void settingsReset() {
  // Calibration offsets are measured values, not preferences: keep them.
  for (int i = 0; i < SETTINGS_COUNT; i++) {
    if (strncmp(settingsTable[i].key, "off", 3) == 0) continue;
    settingSet(&settingsTable[i], settingsTable[i].defV);
  }
  settingsSave();
  Serial.println("[CFG] defaults restored (calibration offsets kept)");
}

// Map raw sensor axes (a,b,c = sensor X,Y,Z) to the logical X/Y/Z used by the level.
// logical[i] = sensor[perm[AXIS_PERM][i]] * (invert[i] ? -1 : 1)
// Changing the mounting invalidates the stored calibration: run it again.
void remapAxes(float a, float b, float c, float *ox, float *oy, float *oz) {
  static const uint8_t perm[6][3] = { {0,1,2}, {0,2,1}, {1,0,2}, {1,2,0}, {2,0,1}, {2,1,0} };
  const float s[3] = { a, b, c };
  const uint8_t *p = perm[constrain(AXIS_PERM, 0, 5)];
  *ox = s[p[0]] * (AXIS_INV_X ? -1.0f : 1.0f);
  *oy = s[p[1]] * (AXIS_INV_Y ? -1.0f : 1.0f);
  *oz = s[p[2]] * (AXIS_INV_Z ? -1.0f : 1.0f);
}
// Rounds the calibrated tilt for the numeric displays (app and OLED); LEDs keep full precision.
float roundAngle(float v) {
  switch (ANGLE_ROUND) {
    case 1:  return roundf(v / 0.05f) * 0.05f;
    case 2:  return roundf(v / 0.1f) * 0.1f;
    default: return v;
  }
}
// MPU6050 CONFIG register (0x1A): digital low-pass filter, bits 0..2.
// A lower bandwidth means less noise per sample but a slower sensor response.
void applyMpuFilter() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write((uint8_t)0x1A);
  Wire.write((uint8_t)constrain(MPU_DLPF, 0, 6));
  Wire.endTransmission();
}