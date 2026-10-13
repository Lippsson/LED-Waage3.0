// LED-Waage 3.0
// Further development of the LED-Wasserwaage 2.0 from Make Magazin
// (https://github.com/MakeMagazinDE/LED-Waage2.0, Make 2/26, page 116).
// Additions: 2 x 44 LEDs, goal-post colours, battery indicator on the outer LEDs, Bluetooth LE
// interface for the Android app, runtime settings stored in flash, sensor axis mapping and filter,
// optional SH1106 OLED. See README.md and docs/PROTOKOLL.md.
// This sketch is built on the original, which has no licence file: all rights to the original
// parts stay with Maker Media GmbH / Make Magazin.
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <MPU6050_tockn.h>
#include <Preferences.h>
#include "AppSettings.h"

// Pins
#define LED_LEFT_PIN   5
#define LED_RIGHT_PIN  18
#define LED_COUNT      44
#define USB_POWER_PIN  36
#define BUZZER_PIN     25
#define MPU_INT_PIN    13
#define LED_POWER_PIN  27  // BC337 base via 1kΩ — switches GND for both LED strips

#define TOTAL_LEDS (LED_COUNT * 2)
#define MPU_ADDR 0x68

MPU6050 mpu(Wire);

Adafruit_NeoPixel stripLeft(LED_COUNT, LED_LEFT_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel stripRight(LED_COUNT, LED_RIGHT_PIN, NEO_GRB + NEO_KHZ800);

Preferences prefs;

// Phone-app requests (set from the BLE task, handled in loop) — see Bluetooth.ino
volatile bool reqCalStart = false;
volatile bool calCancel   = false;
volatile bool reqCalNext  = false;  // "next step" button in the app
volatile bool bleConnected = false;
bool calUseNext = false;            // true while a calibration started from the app is running

// === SPIRIT LEVEL CONSTANTS ===
#define WHITE_ZONE_ENTER 0.05
#define WHITE_ZONE_EXIT  0.10
#define MAX_ANGLE       45.0
#define PRINT_THRESHOLD 0.3
#define INACTIVITY_TIMEOUT 30000

// Runtime-tunable values (changeable from the phone app, persisted in NVS).
// Defaults, limits and key names live in the table in Settings.ino.
float BUBBLE_RANGE      = 1.0;
int   WHITE_ZONE_MIN_TIME = 1000;
int   FADE_DURATION     = 300;
float SMOOTHING_FACTOR  = 0.10;
float BUBBLE_SMOOTHING  = 0.15;

int   BUBBLE_LEDS       = 12;
// Goal posts are fixed: symmetric around the centre of the bar (13 LEDs apart), not adjustable
#define GOALPOST_LEFT   (LED_COUNT - 7)    // 37 with 2 x 44 LEDs
#define GOALPOST_RIGHT  (LED_COUNT + 6)    // 50 with 2 x 44 LEDs
int   BUBBLE_TRAVEL     = 38;

int   LED_BRIGHTNESS    = 255;   // 1..255
int   SOUND_ENABLED     = 1;     // 0 = buzzer muted
int   CAL_WAIT_MS       = 5000;  // time to reposition between calibration steps
int   LEVEL_RATE_HZ     = 25;    // spirit-level measurements per second (averaged from the fast sensor loop)

// Sensor mounting: which sensor axes feed the logical X/Y/Z (permutation) and sign inversion
int   AXIS_PERM         = 0;     // 0 XYZ, 1 XZY, 2 YXZ, 3 YZX, 4 ZXY, 5 ZYX
int   AXIS_INV_X        = 0;
int   AXIS_INV_Y        = 0;
int   AXIS_INV_Z        = 0;

int   MPU_DLPF          = 3;     // sensor low-pass filter: 0 off(260Hz) 1 184 2 94 3 44 4 21 5 10 6 5 Hz
volatile bool reqMpuFilter = true;   // apply the filter setting from loop() (never from the BLE task)
int   ANGLE_ROUND       = 0;     // rounding of the displayed calibrated tilt: 0 none, 1 = 0.05 deg, 2 = 0.1 deg

#define BATTERY_FULL_ADC   4050
#define BATTERY_LOW_ADC    3300
#define BATT_LOG_SIZE      24     // entries stored across deep sleep (one per wake-up)

#define TONE_LEVEL_FREQ    2000
#define TONE_LEVEL_DUR     80

#define CALIBRATION_SAMPLES 200
#define CALIBRATION_DELAY   10  // 200 samples × 10ms = 2s (MPU DLPF @184Hz is valid every ~5ms)
#define CALIBRATION_MOVE_THRESHOLD 0.05

#define FLIP_HOLD_DURATION 3000

// === LIGHTSABER CONSTANTS ===
#define SABER_IDLE_TIMEOUT   12000
#define SABER_SWING_THRESHOLD 80.0
#define SABER_CLASH_THRESHOLD 2.0

// Shake-to-activate (Seismic-style: HP filter + magnitude ratio + peak count)
#define SHAKE_THRESHOLD_G   0.8f    // linear-accel magnitude (gravity removed) in g
#define SHAKE_MIN_WINDOW_MS 300     // minimum sustained shake duration
#define SHAKE_MAX_WINDOW_MS 600     // sample retention window
#define SHAKE_RATIO         0.55f   // fraction of above-threshold samples needed
#define SHAKE_MIN_PEAKS     3       // minimum oscillation peaks (below→above transitions)
#define SHAKE_COOLDOWN_MS   1500    // ignore after detection
#define SHAKE_HP_ALPHA      0.85f   // gravity low-pass: tau~0.06s, cutoff ~2.7 Hz @ 100 Hz
#define SHAKE_QUEUE_SIZE    64

// Calibration offsets
float offsetHorizontal = -0.6;
float offsetVerticalPos = 0.4;
float offsetVerticalNeg = -0.4;

// === GLOBAL STATE ===
enum AppMode { APP_LEVEL, APP_SABER };
AppMode appMode = APP_LEVEL;

// === SPIRIT LEVEL VARIABLES ===
float lastPrintedPitch = 0;
float smoothedPitch = 0;
float smoothedBubblePos = 0;
unsigned long lastUpdateTime = 0;
unsigned long lastPrintTime = 0;
unsigned long lastMovementTime = 0;
unsigned long updateInterval = 40;  // set from LEVEL_RATE_HZ in loop()
const unsigned long printInterval = 250;

enum DisplayMode { MODE_REDBLUE, MODE_FADE_TO_BUBBLE, MODE_BUBBLE, MODE_LOCKED };
DisplayMode displayMode = MODE_REDBLUE;
unsigned long fadeStartTime = 0;
unsigned long lockedEnteredTime = 0;
bool lockedTonePlayed = false;

unsigned long flipHoldStart = 0;
bool flipHoldActive = false;

unsigned long lastBlinkTime = 0;
bool blinkState = false;

// === VERIFICATION AVERAGING ===
#define AVG_WINDOW 100
float avgRawSum = 0;
float avgCorrSum = 0;
int avgCount = 0;

// === SHAKE DETECTOR STATE ===
struct ShakeSample { unsigned long timestamp; bool aboveThreshold; };
ShakeSample shakeQueue[SHAKE_QUEUE_SIZE];
int shakeQHead = 0, shakeQCount = 0;
float shakeGravX = 0, shakeGravY = 0, shakeGravZ = 0;
bool  shakeGravInitialized = false;
unsigned long lastShakeTime = 0;
unsigned long lastShakeSampleTime = 0;  // rate-limit checkShake to 100 Hz
unsigned long modeSwitchTime = 0;

// === LIGHTSABER VARIABLES ===
uint8_t saberR, saberG, saberB;
unsigned long saberLastActivity = 0;
float smoothedSwing = 0;
float humPhase = 0;
unsigned long lastClashTime = 0;
unsigned long lastClickTime = 0;

// === FORWARD DECLARATIONS ===
void battLogSave();
void battLogPrint();
void setPixel(int pos, uint32_t color);
uint32_t makeColor(uint8_t r, uint8_t g, uint8_t b);
void setAllPixels(uint32_t color);
void getNearestTarget090(float absPitch, float *dist, float *target);
void clearMPUInterrupt();
void rainbowStartup();
void loopLevel(unsigned long currentMillis, float accelX, float accelY, float accelZ, float rawPitch);
void loopSaber(unsigned long currentMillis, float accelX, float accelY, float accelZ,
               float totalAccel, float totalGyro, float gyroX, float gyroY, float gyroZ);
bool checkShake(float rawAx, float rawAy, float rawAz, unsigned long now);
void resetShakeDetector();
void enterSaberMode();
void exitSaberMode(const char* reason);
void saberIgnite();
void saberRetract();
float calcBubbleShift(float target090);
void renderRedBlue(float absPitch);
void renderBubble();
void renderFadeToBubble(float absPitch, float fadeProgress);
void enterDeepSleep();
void configureMPUWakeOnMotion();
void detectFlipHold(float accelY, float accelZ, unsigned long currentMillis);
bool sampleWithMovementCheck(float *result, uint32_t progressColor);
void calibrationAborted(const char* reason);
void runCalibration();
void playTone(int freq, int dur);
int readBatteryADC();
bool isBatteryFull();
bool isBatteryLow();
float mapTiltToBrightness(float absAngle);
void setAllLEDs(Adafruit_NeoPixel &strip, uint32_t color);
void flashColor(uint32_t color, int times, int delayMs);

// =============================================
// SETUP
// =============================================

void setup() {
  Serial.begin(115200);
  delay(10); // minimal serial stabilise delay

  // --- wake reason (must be read before anything resets sleep state) ---
  esp_sleep_wakeup_cause_t wakeReason = esp_sleep_get_wakeup_cause();
  Serial.println();
  if (wakeReason == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("=== WAKE: deep sleep  (MPU motion interrupt) ===");
  } else {
    Serial.println("=== WAKE: cold boot / manual reset ===");
  }

  Wire.begin(15, 4);
  Wire.setClock(400000);  // MPU6050 and the OLED both handle 400 kHz
  mpu.begin();
  delay(200); // MPU6050 needs ~100ms minimum after wake; 200ms is safe

  clearMPUInterrupt();

  prefs.begin("spiritlevel", true);
  offsetHorizontal = prefs.getFloat("offH", -0.6);
  if (prefs.isKey("offVp")) {
    offsetVerticalPos = prefs.getFloat("offVp", 0.4);
    offsetVerticalNeg = prefs.getFloat("offVn", -0.4);
  } else {
    float offV = prefs.getFloat("offV", 0.4);
    if (offV > 45.0) offV = 180.0 - offV;
    offsetVerticalPos = offV;
    offsetVerticalNeg = -offV;
  }
  prefs.end();

  settingsLoad();  // tunable values saved from the app (Settings.ino)

  Serial.print("Calibration - H: ");
  Serial.print(offsetHorizontal, 2);
  Serial.print("  V+: ");
  Serial.print(offsetVerticalPos, 2);
  Serial.print("  V-: ");
  Serial.println(offsetVerticalNeg, 2);

  // Power on LED strips via BC337 transistor before initialising
  pinMode(LED_POWER_PIN, OUTPUT);
  digitalWrite(LED_POWER_PIN, HIGH);
  delay(10);  // let LEDs have stable GND before first data

  stripLeft.begin();
  stripRight.begin();
  applyBrightness();
  stripLeft.show();
  stripRight.show();

  pinMode(USB_POWER_PIN, INPUT);
  pinMode(MPU_INT_PIN, INPUT);  // needed for digitalRead and EXT0 wakeup

  battLogSave();   // store this wake-up's battery reading before LEDs draw current
  battLogPrint();  // print the history to serial

  ledcAttach(BUZZER_PIN, 2000, 8);
  ledcWriteTone(BUZZER_PIN, 0);

  randomSeed(analogRead(0) + micros());

  bleBegin();      // phone app interface (Bluetooth.ino)
  oledBegin();     // optional SH1106 display (Display.ino)

  rainbowStartup();
  lastMovementTime = millis();
}

// =============================================
// MAIN LOOP
// =============================================

void loop() {
  unsigned long currentMillis = millis();

  if (reqMpuFilter) { reqMpuFilter = false; applyMpuFilter(); }

  // Read raw sensor data in one burst
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14, (uint8_t)true);

  int16_t rawAX = (Wire.read() << 8) | Wire.read();
  int16_t rawAY = (Wire.read() << 8) | Wire.read();
  int16_t rawAZ = (Wire.read() << 8) | Wire.read();
  Wire.read(); Wire.read();
  int16_t rawGX = (Wire.read() << 8) | Wire.read();
  int16_t rawGY = (Wire.read() << 8) | Wire.read();
  int16_t rawGZ = (Wire.read() << 8) | Wire.read();

  // sensor axes -> logical axes (mounting orientation, see AppSettings.ino)
  float accelX, accelY, accelZ;
  remapAxes(rawAX / 16384.0, rawAY / 16384.0, rawAZ / 16384.0, &accelX, &accelY, &accelZ);
  float totalAccel = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);

  float gyroX, gyroY, gyroZ;
  remapAxes(rawGX / 131.0, rawGY / 131.0, rawGZ / 131.0, &gyroX, &gyroY, &gyroZ);
  float totalGyro = sqrt(gyroX * gyroX + gyroY * gyroY + gyroZ * gyroZ);

  float rawPitch = atan2(accelY, accelZ) * 180.0 / PI;

  bleService(currentMillis, accelX, accelY, accelZ, gyroX, gyroY, gyroZ, rawPitch);
  oledService(currentMillis);

  // Shake-to-activate lightsaber mode (or exit it)
  // Rate-limited to 100 Hz (10ms) — the HP filter alpha is tuned for this rate.
  // Running faster makes the filter track the shake itself → linAccel stays ~0.
  if (currentMillis - lastShakeSampleTime >= 10) {
    lastShakeSampleTime = currentMillis;
    if (checkShake(accelX, accelY, accelZ, currentMillis)) {
      if (appMode == APP_LEVEL) {
        enterSaberMode();
      } else {
        exitSaberMode("shake");
      }
    }
  }

  // Refresh currentMillis — enterSaberMode()/exitSaberMode() contain blocking
  // animations (saberIgnite/saberRetract ~400ms), so the original timestamp is stale.
  // Without this, loopSaber would see saberLastActivity in the "future" → unsigned
  // underflow → immediate idle timeout exit.
  currentMillis = millis();

  if (appMode == APP_LEVEL) {
    // The sensor loop runs fast (shake detection needs ~100 Hz); the level logic
    // only runs LEVEL_RATE_HZ times per second on the average of the samples in between.
    static float sumX = 0, sumY = 0, sumZ = 0;
    static int sumN = 0;
    static unsigned long lastLevelTick = 0;
    sumX += accelX; sumY += accelY; sumZ += accelZ; sumN++;
    unsigned long levelInterval = 1000UL / constrain(LEVEL_RATE_HZ, 1, 200);
    if (currentMillis - lastLevelTick >= levelInterval) {
      lastLevelTick = currentMillis;
      updateInterval = levelInterval;
      float mX = sumX / sumN, mY = sumY / sumN, mZ = sumZ / sumN;
      sumX = sumY = sumZ = 0; sumN = 0;
      loopLevel(currentMillis, mX, mY, mZ, atan2(mY, mZ) * 180.0 / PI);
    }
  } else {
    loopSaber(currentMillis, accelX, accelY, accelZ, totalAccel, totalGyro, gyroX, gyroY, gyroZ);
  }
}

// =============================================
// PIXEL HELPERS
// =============================================

void setPixel(int pos, uint32_t color) {
  if (pos < 0 || pos >= TOTAL_LEDS) return;
  if (pos < LED_COUNT) {
    stripLeft.setPixelColor(LED_COUNT - 1 - pos, color);
  } else {
    stripRight.setPixelColor(pos - LED_COUNT, color);
  }
}

uint32_t makeColor(uint8_t r, uint8_t g, uint8_t b) {
  return stripLeft.Color(r, g, b);
}

void setAllPixels(uint32_t color) {
  for (int i = 0; i < TOTAL_LEDS; i++) setPixel(i, color);
}

void getNearestTarget090(float absPitch, float *dist, float *target) {
  float distTo0 = absPitch;
  float distTo90 = abs(absPitch - 90.0);
  if (distTo0 <= distTo90) { *dist = distTo0; *target = 0.0; }
  else { *dist = distTo90; *target = 90.0; }
}

// =============================================
// SABER TRIGGER: JERK-BASED DOUBLE-TAP WITH STILLNESS GATE
// =============================================

// Rolling variance of accelY over the stillness window
// -----------------------------------------------------------------
// Shake detector — Seismic-style (HP filter + ratio + peak count)
// A "peak" = transition from below-threshold to above-threshold.
// Requires BOTH sufficient ratio AND enough peaks (oscillations).
// Single swoosh = 1 peak → blocked. Sustained shake = 3+ peaks → fires.
// -----------------------------------------------------------------
void resetShakeDetector() {
  shakeQHead = 0;
  shakeQCount = 0;
}

bool checkShake(float rawAx, float rawAy, float rawAz, unsigned long now) {
  // Cooldown after mode switch (longer than shake cooldown)
  if (now - modeSwitchTime < 3000) return false;

  // Pause during calibration gesture
  if (flipHoldActive) { resetShakeDetector(); return false; }

  // Initialize gravity estimate on first call
  if (!shakeGravInitialized) {
    shakeGravX = rawAx; shakeGravY = rawAy; shakeGravZ = rawAz;
    shakeGravInitialized = true;
    return false;
  }

  // ALWAYS update gravity estimate (even during cooldown)
  shakeGravX = SHAKE_HP_ALPHA * shakeGravX + (1.0f - SHAKE_HP_ALPHA) * rawAx;
  shakeGravY = SHAKE_HP_ALPHA * shakeGravY + (1.0f - SHAKE_HP_ALPHA) * rawAy;
  shakeGravZ = SHAKE_HP_ALPHA * shakeGravZ + (1.0f - SHAKE_HP_ALPHA) * rawAz;

  // Cooldown after detection
  if (now - lastShakeTime < SHAKE_COOLDOWN_MS) return false;

  // Linear acceleration (gravity removed)
  float linX = rawAx - shakeGravX;
  float linY = rawAy - shakeGravY;
  float linZ = rawAz - shakeGravZ;

  float threshSq = SHAKE_THRESHOLD_G * SHAKE_THRESHOLD_G;
  float magSq    = linX * linX + linY * linY + linZ * linZ;
  bool above     = (magSq >= threshSq);

  // Push to circular buffer
  int idx = (shakeQHead + shakeQCount) % SHAKE_QUEUE_SIZE;
  shakeQueue[idx].timestamp      = now;
  shakeQueue[idx].aboveThreshold = above;
  if (shakeQCount < SHAKE_QUEUE_SIZE) {
    shakeQCount++;
  } else {
    shakeQHead = (shakeQHead + 1) % SHAKE_QUEUE_SIZE;
  }

  // Purge entries older than max window
  while (shakeQCount > 0 &&
         (now - shakeQueue[shakeQHead].timestamp) > (unsigned long)SHAKE_MAX_WINDOW_MS) {
    shakeQHead = (shakeQHead + 1) % SHAKE_QUEUE_SIZE;
    shakeQCount--;
  }

  // Evaluate window
  if (shakeQCount < 4) return false;
  unsigned long windowSpan = now - shakeQueue[shakeQHead].timestamp;
  if (windowSpan < (unsigned long)SHAKE_MIN_WINDOW_MS) return false;

  // Count above-threshold samples AND oscillation peaks
  int accelCount = 0;
  int peaks = 0;
  bool prevAbove = false;
  for (int i = 0; i < shakeQCount; i++) {
    bool cur = shakeQueue[(shakeQHead + i) % SHAKE_QUEUE_SIZE].aboveThreshold;
    if (cur) accelCount++;
    if (cur && !prevAbove && i > 0) peaks++;
    prevAbove = cur;
  }

  float ratio = (float)accelCount / (float)shakeQCount;

  if (ratio >= SHAKE_RATIO && peaks >= SHAKE_MIN_PEAKS) {
    Serial.print("[SHAKE] DETECTED  ratio: "); Serial.print(ratio, 2);
    Serial.print("  peaks: "); Serial.println(peaks);
    lastShakeTime = now;
    shakeQCount = 0;
    return true;
  }

  return false;
}

// =============================================
// SPIRIT LEVEL LOOP
// =============================================

void loopLevel(unsigned long currentMillis, float accelX, float accelY, float accelZ, float rawPitch) {
  float pitch;
  if (abs(rawPitch) < 45.0) {
    pitch = rawPitch + offsetHorizontal;
  } else {
    if (rawPitch > 0) {
      pitch = rawPitch + offsetVerticalPos;
    } else {
      pitch = rawPitch + offsetVerticalNeg;
    }
  }

  // The smoothing factors are defined for 25 measurements per second. Scale them with the
  // measurement rate so the smoothing TIME stays the same at any rate (otherwise a higher
  // rate would silently mean much less smoothing).
  const float rateScale = 25.0f / constrain(LEVEL_RATE_HZ, 1, 200);
  const float pitchAlpha = 1.0f - powf(1.0f - SMOOTHING_FACTOR, rateScale);
  smoothedPitch = smoothedPitch * (1.0f - pitchAlpha) + pitch * pitchAlpha;

  // --- Verification averaging (100 samples → one stable line) ---
  avgRawSum += rawPitch;
  avgCorrSum += pitch;
  avgCount++;
  if (avgCount >= AVG_WINDOW) {
    float avgRaw  = avgRawSum  / AVG_WINDOW;
    float avgCorr = avgCorrSum / AVG_WINDOW;
    Serial.print("[AVG/"); Serial.print(AVG_WINDOW); Serial.print("]");
    Serial.print("  raw: "); Serial.print(avgRaw, 3);
    Serial.print("  corrected: "); Serial.print(avgCorr, 3);
    Serial.print("  |corr|: "); Serial.println(abs(avgCorr), 3);
    avgRawSum = 0;
    avgCorrSum = 0;
    avgCount = 0;
  }

  if (abs(smoothedPitch - lastPrintedPitch) >= PRINT_THRESHOLD) {
    lastMovementTime = currentMillis;
    lastPrintedPitch = smoothedPitch;
  }

  detectFlipHold(accelY, accelZ, currentMillis);
  if (reqCalStart) {  // calibration requested from the phone app
    reqCalStart = false;
    calUseNext = true;   // app drives the steps with its Next button
    runCalibration();
    calUseNext = false;
  }
  currentMillis = millis();  // refresh: calibration may have taken many seconds

  if (currentMillis - lastPrintTime >= printInterval) {
    lastPrintTime = currentMillis;
    float absPitchDbg = abs(smoothedPitch);
    float d090Dbg, t090Dbg;
    getNearestTarget090(absPitchDbg, &d090Dbg, &t090Dbg);
    const char* modeNames[] = {"REDBLUE", "FADE_BUBBLE", "BUBBLE", "LOCKED"};
    Serial.println("--- [LEVEL] -------------------------");
    Serial.print("  rawPitch:   "); Serial.println(rawPitch, 2);
    Serial.print("  smoothed:   "); Serial.print(smoothedPitch, 2);
    Serial.println(abs(rawPitch) >= 45.0 ? "  [VERT]" : "  [HORIZ]");
    Serial.print("  target:     "); Serial.print(t090Dbg, 0);
    Serial.print("deg  dist090: "); Serial.println(d090Dbg, 2);
    Serial.print("  bubblePos:  "); Serial.println(smoothedBubblePos, 2);
    Serial.print("  dispMode:   "); Serial.println(modeNames[displayMode]);
    Serial.print("  battery:    "); Serial.println(readBatteryADC());
  }

  // Deep sleep disabled
  // if (currentMillis - lastMovementTime > INACTIVITY_TIMEOUT) {
  //   enterDeepSleep();
  // }

  if (currentMillis - lastUpdateTime >= updateInterval) {
    lastUpdateTime = currentMillis;

    stripLeft.clear();
    stripRight.clear();

    float absPitch = abs(smoothedPitch);
    float dist090, target090;
    getNearestTarget090(absPitch, &dist090, &target090);
    // 0° and 90° with bubble

    switch (displayMode) {
      case MODE_REDBLUE:
        if (dist090 <= BUBBLE_RANGE) {
          displayMode = MODE_FADE_TO_BUBBLE;
          fadeStartTime = currentMillis;
          smoothedBubblePos = calcBubbleShift(target090);
        }
        break;
      case MODE_FADE_TO_BUBBLE:
        if (dist090 > BUBBLE_RANGE + 0.5) displayMode = MODE_REDBLUE;
        else if (currentMillis - fadeStartTime >= FADE_DURATION) displayMode = MODE_BUBBLE;
        break;
      case MODE_BUBBLE:
        if (dist090 > BUBBLE_RANGE + 0.5) displayMode = MODE_REDBLUE;
        else if (abs(smoothedBubblePos) < 0.5) {
          displayMode = MODE_LOCKED;
          lockedEnteredTime = currentMillis;
          lockedTonePlayed = false;
        }
        break;
      case MODE_LOCKED:
        {
          bool te = (currentMillis - lockedEnteredTime) >= WHITE_ZONE_MIN_TIME;
          if (te && abs(smoothedBubblePos) > 1.5) {
            displayMode = MODE_BUBBLE;
            lockedTonePlayed = false;
          }
        }
        break;
    }

    float rawBS = calcBubbleShift(target090);
    const float bubbleAlpha = 1.0f - powf(1.0f - BUBBLE_SMOOTHING, rateScale);
    smoothedBubblePos = smoothedBubblePos * (1.0f - bubbleAlpha) + rawBS * bubbleAlpha;

    switch (displayMode) {
      case MODE_REDBLUE: renderRedBlue(absPitch); break;
      case MODE_FADE_TO_BUBBLE:
        {
          float fp = (float)(currentMillis - fadeStartTime) / FADE_DURATION;
          if (fp > 1) fp = 1;
          renderFadeToBubble(absPitch, fp);
        }
        break;
      case MODE_BUBBLE: renderBubble(); break;
      case MODE_LOCKED:
        // Level reached: keep showing the centered bubble instead of all-green
        renderBubble();
        if (!lockedTonePlayed) { playTone(TONE_LEVEL_FREQ, TONE_LEVEL_DUR); lockedTonePlayed = true; }
        break;
    }

    drawBatteryIndicator(currentMillis);

    stripLeft.show();
    stripRight.show();
  }
}

// =============================================
// LIGHTSABER MODE
// =============================================

void enterSaberMode() {
  Serial.println(">>> ENTERING SABER MODE");
  appMode = APP_SABER;
  saberLastActivity = millis();

  int choice = random(3);
  if (choice == 0) { saberR = 255; saberG = 0; saberB = 0; Serial.println("Color: RED"); }
  else if (choice == 1) { saberR = 0; saberG = 255; saberB = 0; Serial.println("Color: GREEN"); }
  else { saberR = 0; saberG = 80; saberB = 255; Serial.println("Color: BLUE"); }

  smoothedSwing = 0;
  humPhase = 0;

  saberIgnite();

  // Reset shake detector AFTER ignition animation
  modeSwitchTime = millis();
  resetShakeDetector();
  // Also reset saber activity timer after ignition
  saberLastActivity = millis();

  Serial.println("=== LIGHTSABER MODE ACTIVE ===");
}

void exitSaberMode(const char* reason) {
  Serial.print(">>> EXITING SABER MODE (reason: ");
  Serial.print(reason);
  Serial.println(")");

  saberRetract();

  ledcWriteTone(BUZZER_PIN, 0);
  appMode = APP_LEVEL;
  displayMode = MODE_REDBLUE;
  smoothedPitch = 0;
  lastMovementTime = millis();

  modeSwitchTime = millis();
  resetShakeDetector();

  Serial.println("=== SPIRIT LEVEL MODE ===");
}

void saberIgnite() {
  stripLeft.clear();
  stripRight.clear();
  for (int i = 0; i < TOTAL_LEDS; i++) {
    setPixel(i, makeColor(saberR, saberG, saberB));
    stripLeft.show();
    stripRight.show();
    delay(14);
  }
}

void saberRetract() {
  for (int i = TOTAL_LEDS - 1; i >= 0; i--) {
    setPixel(i, 0);
    stripLeft.show();
    stripRight.show();
    delay(10);
  }
}

void loopSaber(unsigned long currentMillis, float accelX, float accelY, float accelZ,
               float totalAccel, float totalGyro, float gyroX, float gyroY, float gyroZ) {

  smoothedSwing = smoothedSwing * 0.6 + totalGyro * 0.4;

  // Activity detection — any movement at all keeps it alive
  if (smoothedSwing > 15 || abs(totalAccel - 1.0) > 0.2) {
    saberLastActivity = currentMillis;
  }

  // Idle timeout
  if (currentMillis - saberLastActivity > SABER_IDLE_TIMEOUT) {
    exitSaberMode("idle timeout");
    return;
  }

  // === CLASH — lightning sparkle ===
  if (totalAccel > SABER_CLASH_THRESHOLD && (currentMillis - lastClashTime > 300)) {
    lastClashTime = currentMillis;
    saberLastActivity = currentMillis;

    ledcWriteTone(BUZZER_PIN, 600);

    for (int flash = 0; flash < 8; flash++) {
      stripLeft.clear();
      stripRight.clear();
      for (int i = 0; i < TOTAL_LEDS; i++) {
        if (random(100) < 60) {
          uint8_t bright = 150 + random(106);
          setPixel(i, makeColor(bright, bright, bright));
        } else {
          setPixel(i, makeColor(saberR / 4, saberG / 4, saberB / 4));
        }
      }
      stripLeft.show();
      stripRight.show();
      delay(25);
    }

    ledcWriteTone(BUZZER_PIN, 0);
    setAllPixels(makeColor(saberR, saberG, saberB));
    stripLeft.show();
    stripRight.show();
    return;
  }

  // === GEIGER COUNTER CLICKS ===
  if (smoothedSwing > 20) {
    int minInterval, maxInterval;

    if (smoothedSwing > 300) {
      minInterval = 20; maxInterval = 50;
    } else if (smoothedSwing > 150) {
      minInterval = 40; maxInterval = 120;
    } else if (smoothedSwing > SABER_SWING_THRESHOLD) {
      minInterval = 80; maxInterval = 200;
    } else {
      minInterval = 200; maxInterval = 500;
    }

    if (currentMillis - lastClickTime > (unsigned long)random(minInterval, maxInterval)) {
      lastClickTime = currentMillis;
      int clickFreq = 300 + random(800);
      int clickDur = 2 + random(8);
      ledcWriteTone(BUZZER_PIN, clickFreq);
      delayMicroseconds(clickDur * 1000);
      ledcWriteTone(BUZZER_PIN, 0);
    }
  } else {
    if (random(1000) < 3) {
      int clickFreq = 400 + random(400);
      ledcWriteTone(BUZZER_PIN, clickFreq);
      delayMicroseconds(3000);
      ledcWriteTone(BUZZER_PIN, 0);
    }
  }

  // === LED UPDATE ===
  if (currentMillis - lastUpdateTime >= 30) {
    lastUpdateTime = currentMillis;

    humPhase += 0.05;

    float breathe = (sin(humPhase * 0.7) + 1.0) / 2.0;
    float baseBright = 0.5 + breathe * 0.5;

    float swingFactor = 0;
    if (smoothedSwing > SABER_SWING_THRESHOLD) {
      swingFactor = (smoothedSwing - SABER_SWING_THRESHOLD) / 300.0;
      if (swingFactor > 1.0) swingFactor = 1.0;
    }

    stripLeft.clear();
    stripRight.clear();

    for (int i = 0; i < TOTAL_LEDS; i++) {
      float bright = baseBright;

      if (swingFactor > 0.1) {
        float shimmer = 0.6 + (float)(random(80)) / 100.0;
        bright *= shimmer;
        bright = min(1.0f, bright + swingFactor * 0.3f);
      } else {
        float shimmer = 0.9 + (float)(random(20)) / 100.0;
        bright *= shimmer;
      }

      if (bright > 1.0) bright = 1.0;

      uint8_t r = saberR * bright;
      uint8_t g = saberG * bright;
      uint8_t b = saberB * bright;
      setPixel(i, makeColor(r, g, b));
    }

    // Swing trail
    if (swingFactor > 0.2) {
      float maxG = max(max(abs(gyroX), abs(gyroY)), abs(gyroZ));
      float direction = gyroZ;
      if (abs(gyroX) == maxG) direction = gyroX;
      else if (abs(gyroY) == maxG) direction = gyroY;

      int trailLen = 6 + (int)(swingFactor * 10);
      int trailStart, trailEnd;
      if (direction > 0) {
        trailEnd = TOTAL_LEDS - 1;
        trailStart = max(0, trailEnd - trailLen);
      } else {
        trailStart = 0;
        trailEnd = min(TOTAL_LEDS - 1, trailLen);
      }

      for (int i = trailStart; i <= trailEnd; i++) {
        float trailBright = 0.8 + swingFactor * 0.2;
        uint8_t tr = min(255, (int)(saberR * trailBright + 60 * swingFactor));
        uint8_t tg = min(255, (int)(saberG * trailBright + 60 * swingFactor));
        uint8_t tb = min(255, (int)(saberB * trailBright + 60 * swingFactor));
        setPixel(i, makeColor(tr, tg, tb));
      }
    }

    stripLeft.show();
    stripRight.show();
  }

  // Serial
  if (currentMillis - lastPrintTime >= 500) {
    lastPrintTime = currentMillis;
    Serial.println("--- [SABER] -------------------------");
    Serial.print("  swing:      "); Serial.println(smoothedSwing, 1);
    Serial.print("  totalAccel: "); Serial.println(totalAccel, 3);
    Serial.print("  totalGyro:  "); Serial.println(totalGyro, 1);
    Serial.print("  accel XYZ:  ");
    Serial.print(accelX, 3); Serial.print("  ");
    Serial.print(accelY, 3); Serial.print("  ");
    Serial.println(accelZ, 3);
    Serial.print("  gyro  XYZ:  ");
    Serial.print(gyroX, 1); Serial.print("  ");
    Serial.print(gyroY, 1); Serial.print("  ");
    Serial.println(gyroZ, 1);
    Serial.print("  idle:       ");
    Serial.print((currentMillis - saberLastActivity) / 1000);
    Serial.println("s");
  }
}

// =============================================
// BUBBLE HELPERS
// =============================================

float calcBubbleShift(float target090) {
  float signedDist;
  if (target090 == 0.0) signedDist = smoothedPitch;
  else signedDist = smoothedPitch - ((smoothedPitch > 0) ? 90.0 : -90.0);
  float shift = (signedDist / BUBBLE_RANGE) * BUBBLE_TRAVEL;
  if (shift > BUBBLE_TRAVEL) shift = BUBBLE_TRAVEL;
  if (shift < -BUBBLE_TRAVEL) shift = -BUBBLE_TRAVEL;
  return shift;
}

// =============================================
// RENDER FUNCTIONS
// =============================================

void renderRedBlue(float absPitch) {
  float brightness = mapTiltToBrightness(absPitch);
  uint8_t rL = 0, bL = 0, rR = 0, bR = 0;
  if (smoothedPitch > 0) { rR = brightness; bL = brightness; }
  else { rL = brightness; bR = brightness; }
  setAllLEDs(stripLeft, stripLeft.Color(rL, 0, bL));
  setAllLEDs(stripRight, stripRight.Color(rR, 0, bR));
}

// Bubble colour: white when horizontal, blue when vertical (>= 45 deg)
void bubbleRGB(uint8_t *r, uint8_t *g, uint8_t *b) {
  if (abs(smoothedPitch) >= 45.0) { *r = 0; *g = 0; *b = 255; }
  else { *r = 255; *g = 255; *b = 255; }
}

// Goal-post colour from the bubble's offset in whole LEDs:
// exactly centred = green, 1 LED off centre = yellow, further out = red
void postRGB(int bubbleStart, uint8_t *r, uint8_t *g, uint8_t *b) {
  int centreStart = (int)((GOALPOST_LEFT + GOALPOST_RIGHT) / 2.0 - BUBBLE_LEDS / 2.0 + 0.5);
  int off = abs(bubbleStart - centreStart);
  if (off == 0)      { *r = 0;   *g = 255; *b = 0; }
  else if (off == 1) { *r = 255; *g = 200; *b = 0; }
  else               { *r = 255; *g = 0;   *b = 0; }
}

void renderBubble() {
  float bubbleCenter = (GOALPOST_LEFT + GOALPOST_RIGHT) / 2.0 + smoothedBubblePos;
  int bubbleStart = (int)(bubbleCenter - BUBBLE_LEDS / 2.0 + 0.5);
  int bubbleEnd = bubbleStart + BUBBLE_LEDS - 1;

  uint8_t br, bg, bb;
  bubbleRGB(&br, &bg, &bb);
  for (int i = bubbleStart; i <= bubbleEnd; i++) {
    if (i >= 0 && i < TOTAL_LEDS) setPixel(i, stripLeft.Color(br, bg, bb));
  }
  uint8_t pr, pg, pb;
  postRGB(bubbleStart, &pr, &pg, &pb);
  setPixel(GOALPOST_LEFT, stripLeft.Color(pr, pg, pb));
  setPixel(GOALPOST_RIGHT, stripLeft.Color(pr, pg, pb));
}

void renderFadeToBubble(float absPitch, float fadeProgress) {
  float brightness = mapTiltToBrightness(absPitch);
  float bubbleCenter = (GOALPOST_LEFT + GOALPOST_RIGHT) / 2.0 + smoothedBubblePos;
  int bubbleStart = (int)(bubbleCenter - BUBBLE_LEDS / 2.0 + 0.5);
  int bubbleEnd = bubbleStart + BUBBLE_LEDS - 1;
  uint8_t bcR, bcG, bcB;
  bubbleRGB(&bcR, &bcG, &bcB);
  uint8_t pR, pG, pB;
  postRGB(bubbleStart, &pR, &pG, &pB);

  for (int pos = 0; pos < TOTAL_LEDS; pos++) {
    uint8_t rbR = 0, rbG = 0, rbB = 0;
    if (pos < LED_COUNT) {
      if (smoothedPitch > 0) rbB = brightness; else rbR = brightness;
    } else {
      if (smoothedPitch > 0) rbR = brightness; else rbB = brightness;
    }

    uint8_t bubR = 0, bubG = 0, bubB = 0;
    if (pos == GOALPOST_LEFT || pos == GOALPOST_RIGHT) { bubR = pR; bubG = pG; bubB = pB; }
    else if (pos >= bubbleStart && pos <= bubbleEnd) { bubR = bcR; bubG = bcG; bubB = bcB; }

    setPixel(pos, stripLeft.Color(
      (uint8_t)(rbR * (1 - fadeProgress) + bubR * fadeProgress),
      (uint8_t)(rbG * (1 - fadeProgress) + bubG * fadeProgress),
      (uint8_t)(rbB * (1 - fadeProgress) + bubB * fadeProgress)
    ));
  }
}

// =============================================
// RAINBOW STARTUP
// =============================================

void rainbowStartup() {
  int totalFrames = 120;
  int trailLength = 8;

  for (int frame = 0; frame < totalFrames; frame++) {
    stripLeft.clear();
    stripRight.clear();
    int halfFrames = totalFrames / 2;
    int pos = (frame < halfFrames) ?
      map(frame, 0, halfFrames - 1, 0, TOTAL_LEDS - 1) :
      map(frame, halfFrames, totalFrames - 1, TOTAL_LEDS - 1, 0);

    for (int t = 0; t < trailLength; t++) {
      int lp = pos - t;
      if (lp < 0 || lp >= TOTAL_LEDS) continue;
      uint16_t hue = (t * 65536 / trailLength + frame * 1500) % 65536;
      uint8_t brt = 255 - (t * 200 / trailLength);
      setPixel(lp, stripLeft.ColorHSV(hue, 255, brt));
    }
    stripLeft.show();
    stripRight.show();
    delay(12); // 120 frames × 12ms ≈ 1.44s total (was 25ms → 3s)
  }

  setAllLEDs(stripLeft, stripLeft.Color(255, 255, 255));
  setAllLEDs(stripRight, stripRight.Color(255, 255, 255));
  stripLeft.show();
  stripRight.show();
  playTone(2500, 150); // tone itself is 150ms
  delay(50);           // brief hold before clearing (was 300ms)
  stripLeft.clear();
  stripRight.clear();
  stripLeft.show();
  stripRight.show();
}

// =============================================
// DEEP SLEEP
// =============================================

void clearMPUInterrupt() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3A);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1);
  if (Wire.available()) Wire.read();
}

void enterDeepSleep() {
  Serial.println("=== ENTERING DEEP SLEEP ===");
  Serial.print("  inactive for:    "); Serial.print((millis() - lastMovementTime) / 1000); Serial.println("s");
  Serial.print("  battery ADC:     "); Serial.println(readBatteryADC());
  Serial.print("  INT pin before:  "); Serial.println(digitalRead(MPU_INT_PIN) ? "HIGH" : "low");

  // fade LEDs to off
  for (int b = 255; b >= 0; b -= 5) {
    stripLeft.setBrightness(b);
    stripRight.setBrightness(b);
    stripLeft.show();
    stripRight.show();
    delay(10);
  }
  stripLeft.clear();
  stripRight.clear();
  stripLeft.setBrightness(255);
  stripRight.setBrightness(255);
  stripLeft.show();
  stripRight.show();

  // Cut GND to LED strips via BC337 — eliminates ~56mA quiescent draw
  digitalWrite(LED_POWER_PIN, LOW);

  ledcWriteTone(BUZZER_PIN, 0);
  configureMPUWakeOnMotion();
  delay(50);
  clearMPUInterrupt();
  delay(50);

  int att = 0;
  while (digitalRead(MPU_INT_PIN) == HIGH && att < 10) {
    clearMPUInterrupt();
    delay(50);
    att++;
  }

  Serial.print("  INT pin after:   "); Serial.println(digitalRead(MPU_INT_PIN) ? "HIGH  <-- WARNING: may wake immediately!" : "low  (ok)");
  Serial.print("  clear attempts:  "); Serial.println(att);
  Serial.println("  sleeping now... zZz");
  Serial.flush(); // ensure all bytes are sent before power-down

  esp_sleep_enable_ext0_wakeup((gpio_num_t)MPU_INT_PIN, 1);
  esp_deep_sleep_start();
}

void configureMPUWakeOnMotion() {
  // Full reset
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x6B); Wire.write(0x80); Wire.endTransmission(); delay(100);
  // Wake from reset, internal clock
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x6B); Wire.write(0x00); Wire.endTransmission(); delay(10);
  // Accel ±2g range
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x1C); Wire.write(0x00); Wire.endTransmission();
  // DLPF mode 1 (184Hz bandwidth)
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x1A); Wire.write(0x01); Wire.endTransmission();
  // MOT_THR = 10 (~500mg).  Previous value of 2 (~100mg) caused phantom wakes
  // from trucks, footsteps, and thermal settling — even with latched INT.
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x1F); Wire.write(10); Wire.endTransmission();
  // MOT_DUR = 5 (5ms — require sustained motion, not single-sample blips)
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x20); Wire.write(5); Wire.endTransmission();
  // INT_PIN_CFG: active HIGH, push-pull, LATCHED (stays HIGH until 0x3A is read).
  // Old value 0x00 produced a 50µs pulse that ESP32 EXT0 wakeup often missed.
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x37); Wire.write(0x20); Wire.endTransmission();
  // INT_ENABLE: motion detection interrupt (bit 6)
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x38); Wire.write(0x40); Wire.endTransmission();
  // PWR_MGMT_2: gyro OFF (bits 2:0=111), LP wake rate 5Hz (bits 7:6=01).
  // Previous 40Hz (0xC7) was overkill and wasted power.  5Hz is responsive
  // enough for deliberate pick-up while ignoring brief vibrations.
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x6C); Wire.write(0x47); Wire.endTransmission();
  // Enter cycle mode (accelerometer-only low-power sampling)
  Wire.beginTransmission(MPU_ADDR); Wire.write(0x6B); Wire.write(0x20); Wire.endTransmission();
}

// =============================================
// FLIP-HOLD CALIBRATION GESTURE
// =============================================

void detectFlipHold(float accelY, float accelZ, unsigned long currentMillis) {
  // Upside-down flat: gravity through -Z, minimal Y component
  if (accelZ < -0.7 && abs(accelY) < 0.3) {
    if (!flipHoldActive) {
      flipHoldActive = true;
      flipHoldStart = currentMillis;
      Serial.println("[FLIP] upside-down detected, holding...");
    } else if (currentMillis - flipHoldStart >= FLIP_HOLD_DURATION) {
      Serial.println("[FLIP] 3s hold complete -> CALIBRATION");
      flipHoldActive = false;
      runCalibration();
    }
  } else {
    if (flipHoldActive) {
      Serial.println("[FLIP] condition lost, reset");
    }
    flipHoldActive = false;
  }
}

// =============================================
// CALIBRATION
// =============================================

bool sampleWithMovementCheck(float *result, uint32_t progressColor) {
  float sum = 0;
  float lY = 0, lZ = 0;
  bool first = true;

  for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x3B);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)6, (uint8_t)true);
    int16_t rAX = (Wire.read() << 8) | Wire.read();
    int16_t rAY = (Wire.read() << 8) | Wire.read();
    int16_t rAZ = (Wire.read() << 8) | Wire.read();
    float aX, aY, aZ;
    remapAxes(rAX / 16384.0, rAY / 16384.0, rAZ / 16384.0, &aX, &aY, &aZ);

    if (calCancel) return false;

    if (!first) {
      if (abs(aY - lY) > CALIBRATION_MOVE_THRESHOLD || abs(aZ - lZ) > CALIBRATION_MOVE_THRESHOLD)
        return false;
    }
    lY = aY; lZ = aZ; first = false;

    sum += atan2(aY, aZ) * 180.0 / PI;

    int lit = map(i, 0, CALIBRATION_SAMPLES - 1, 0, LED_COUNT);
    stripLeft.clear();
    stripRight.clear();
    for (int j = 0; j < lit; j++) {
      stripLeft.setPixelColor(j, progressColor);
      stripRight.setPixelColor(j, progressColor);
    }
    stripLeft.show();
    stripRight.show();
    delay(CALIBRATION_DELAY);
  }

  *result = sum / CALIBRATION_SAMPLES;
  return true;
}

// Wait CAL_WAIT_MS so the user can reposition; keeps BLE alive and honours cancel.
bool calWait(int step) {
  bleSend("C," + String(step) + ",WAIT," + String(CAL_WAIT_MS / 1000.0, 1));
  unsigned long t0 = millis();
  while (millis() - t0 < (unsigned long)CAL_WAIT_MS) {
    if (calCancel) return false;
    delay(20);
  }
  bleSend("C," + String(step) + ",MEAS");
  return true;
}

// After a successful step: when driven by the app, wait for its "next" button.
// Returns false on cancel, lost connection or 5 min timeout.
bool calGate(int doneStep) {
  if (!calUseNext) return true;
  reqCalNext = false;
  setAllLEDs(stripLeft, stripLeft.Color(0, 255, 0));
  setAllLEDs(stripRight, stripRight.Color(0, 255, 0));
  stripLeft.show(); stripRight.show();
  playTone(2000, 120);
  bleSend("C," + String(doneStep) + ",NEXT");
  unsigned long t0 = millis();
  while (!reqCalNext) {
    if (calCancel || !bleConnected || millis() - t0 > 300000UL) return false;
    delay(20);
  }
  return true;
}

void calibrationAborted(const char* reason) {
  if (calCancel) reason = "Cancelled";
  else if (calUseNext && !bleConnected) reason = "Connection lost";
  Serial.print("ABORTED: ");
  Serial.println(reason);
  bleSend("C,0,ERR," + String(reason));
  for (int i = 0; i < 6; i++) {
    setAllLEDs(stripLeft, stripLeft.Color(255, 0, 0));
    setAllLEDs(stripRight, stripRight.Color(255, 0, 0));
    stripLeft.show(); stripRight.show();
    playTone(200, 100);
    delay(50);
    stripLeft.clear(); stripRight.clear();
    stripLeft.show(); stripRight.show();
    delay(100);
  }
  lastMovementTime = millis();
}

void runCalibration() {
  Serial.println("=== 5-POINT REVERSAL CALIBRATION (Umschlagmessung + cross-axis) ===");
  calCancel = false;
  bleSend("C,0,START");

  // --- Step 1: Horizontal A — place device normally on surface ---
  Serial.println("[CAL] Step 1/5: Place HORIZONTAL (normal orientation)");
  playTone(1000, 200);
  flashColor(stripLeft.Color(255, 140, 0), 3, 150);   // orange
  if (!calWait(1)) { calibrationAborted("Cancelled"); return; }

  float avgHA;
  if (!sampleWithMovementCheck(&avgHA, stripLeft.Color(255, 140, 0))) { calibrationAborted("Movement H-A"); return; }
  Serial.print("[CAL] H-A measured: "); Serial.println(avgHA, 3); bleSend("C," + String(1) + ",DONE," + String(avgHA, 2));
  if (abs(avgHA) > 30) { calibrationAborted("Not horizontal!"); return; }

  if (!calGate(1)) { calibrationAborted("Cancelled"); return; }
  // --- Step 2: Horizontal B — rotate 180° in-plane on same surface ---
  Serial.println("[CAL] Step 2/5: Rotate 180 in-plane (same surface)");
  playTone(1500, 200);
  flashColor(stripLeft.Color(0, 255, 255), 5, 300);   // cyan
  if (!calWait(2)) { calibrationAborted("Cancelled"); return; }

  float avgHB;
  if (!sampleWithMovementCheck(&avgHB, stripLeft.Color(0, 255, 255))) { calibrationAborted("Movement H-B"); return; }
  Serial.print("[CAL] H-B measured: "); Serial.println(avgHB, 3); bleSend("C," + String(2) + ",DONE," + String(avgHB, 2));
  if (abs(avgHB) > 30) { calibrationAborted("Not horizontal!"); return; }

  float biasH = (avgHA + avgHB) / 2.0;
  offsetHorizontal = -biasH;
  Serial.print("[CAL] biasH: "); Serial.print(biasH, 3);
  Serial.print("  surface tilt: "); Serial.println((avgHA - avgHB) / 2.0, 3);
  Serial.print("[CAL] offsetH set to: "); Serial.println(offsetHorizontal, 3);

  if (!calGate(2)) { calibrationAborted("Cancelled"); return; }
  // --- Step 3: Vertical — one end up, lean against wall ---
  // rawPitch = atan2(accelY, accelZ).  At vertical accelZ ≈ 0, so the
  // reading is dominated by two Z-axis error sources:
  //   biasZ  — fixed sensor offset, same sign both ends
  //   crossYZ — Y-to-Z cross-axis bleed (~±2%), sign FLIPS with accelY
  // End A up (accelY≈+1): rawPitch ≈ 90  - wallTilt - biasZ - crossYZ
  // End B up (accelY≈-1): rawPitch ≈ -90 + wallTilt + biasZ - crossYZ
  // The face-flip (step 4&5) removes wallTilt; the end-flip (step 3&4)
  // isolates crossYZ.  Together: separate offsets for each end.
  Serial.println("[CAL] Step 3/5: Stand VERTICAL against wall (either end up)");
  playTone(1500, 200);
  flashColor(stripLeft.Color(0, 0, 255), 5, 300);     // blue
  if (!calWait(3)) { calibrationAborted("Cancelled"); return; }

  float avgV1;
  if (!sampleWithMovementCheck(&avgV1, stripLeft.Color(0, 0, 255))) { calibrationAborted("Movement V1"); return; }
  Serial.print("[CAL] V1 measured: "); Serial.println(avgV1, 3); bleSend("C," + String(3) + ",DONE," + String(avgV1, 2));
  if (abs(avgV1) < 45) { calibrationAborted("Not vertical!"); return; }

  if (!calGate(3)) { calibrationAborted("Cancelled"); return; }
  // --- Step 4: Flip which END is up (same wall, same face direction!) ---
  Serial.println("[CAL] Step 4/5: Flip which END is up (same wall, same face direction!)");
  playTone(1500, 200);
  flashColor(stripLeft.Color(255, 0, 255), 5, 300);   // magenta
  if (!calWait(4)) { calibrationAborted("Cancelled"); return; }

  float avgV2;
  if (!sampleWithMovementCheck(&avgV2, stripLeft.Color(255, 0, 255))) { calibrationAborted("Movement V2"); return; }
  Serial.print("[CAL] V2 measured: "); Serial.println(avgV2, 3); bleSend("C," + String(4) + ",DONE," + String(avgV2, 2));
  if (abs(avgV2) < 45) { calibrationAborted("Not vertical!"); return; }

  // Sanity: V1 and V2 must be opposite signs (one ~+90, one ~-90)
  if ((avgV1 > 0) == (avgV2 > 0)) { calibrationAborted("Same end! Flip the other end up"); return; }

  if (!calGate(4)) { calibrationAborted("Cancelled"); return; }
  // --- Step 5: Keep THIS end up, turn FACE to other side ---
  Serial.println("[CAL] Step 5/5: Keep SAME end up, turn FACE to other side");
  playTone(1500, 200);
  flashColor(stripLeft.Color(255, 255, 0), 5, 300);   // yellow
  if (!calWait(5)) { calibrationAborted("Cancelled"); return; }

  float avgV3;
  if (!sampleWithMovementCheck(&avgV3, stripLeft.Color(255, 255, 0))) { calibrationAborted("Movement V3"); return; }
  Serial.print("[CAL] V3 measured: "); Serial.println(avgV3, 3); bleSend("C," + String(5) + ",DONE," + String(avgV3, 2));
  if (abs(avgV3) < 45) { calibrationAborted("Not vertical!"); return; }

  // Sanity: V2 and V3 must have same sign (same end stayed up)
  if ((avgV2 > 0) != (avgV3 > 0)) { calibrationAborted("End changed! Keep same end as step 4"); return; }

  // === OFFSET COMPUTATION ===
  //
  // End-flip pair (V1 & V2, same face direction):
  //   V1 + V2 = -2·crossYZ   (biasZ and wallTilt both cancel)
  //   → crossYZ = -(V1 + V2) / 2
  //
  // Face-flip pair (V2 & V3, same end):
  //   avg(V2,V3) = ±90 + (biasZ ∓ crossYZ)   (wallTilt cancels)
  //   → direct offset for that end
  //
  // Other end's offset = 2·crossYZ - faceFlipEnd's offset
  //
  float crossAxis = -(avgV1 + avgV2) / 2.0;

  float avgFaceFlip = (avgV2 + avgV3) / 2.0;
  float offsetFaceFlipEnd;
  if (avgV2 > 0) {
    offsetFaceFlipEnd = 90.0 - avgFaceFlip;
  } else {
    offsetFaceFlipEnd = -90.0 - avgFaceFlip;
  }
  float offsetOtherEnd = 2.0 * crossAxis - offsetFaceFlipEnd;

  // Assign: faceFlip end's sign determines which is pos/neg
  if (avgV2 > 0) {
    offsetVerticalPos = offsetFaceFlipEnd;   // face-flip end reads +90
    offsetVerticalNeg = offsetOtherEnd;      // other end reads -90
  } else {
    offsetVerticalNeg = offsetFaceFlipEnd;   // face-flip end reads -90
    offsetVerticalPos = offsetOtherEnd;      // other end reads +90
  }

  Serial.print("[CAL] crossAxis: "); Serial.println(crossAxis, 3);
  Serial.print("[CAL] offsetV+ set to: "); Serial.println(offsetVerticalPos, 3);
  Serial.print("[CAL] offsetV- set to: "); Serial.println(offsetVerticalNeg, 3);

  // --- Save to flash ---
  prefs.begin("spiritlevel", false);
  prefs.putFloat("offH", offsetHorizontal);
  prefs.putFloat("offVp", offsetVerticalPos);
  prefs.putFloat("offVn", offsetVerticalNeg);
  prefs.end();
  Serial.println("[CAL] offsets saved to flash");
  bleSend("C,5,OK," + String(offsetHorizontal, 3) + "," + String(offsetVerticalPos, 3) + "," + String(offsetVerticalNeg, 3));

  // --- Success feedback ---
  setAllLEDs(stripLeft, stripLeft.Color(0, 255, 0));
  setAllLEDs(stripRight, stripRight.Color(0, 255, 0));
  stripLeft.show(); stripRight.show();
  playTone(2000, 150); delay(200);
  playTone(2500, 150); delay(200);
  playTone(3000, 300); delay(1000);

  smoothedPitch = 0;
  displayMode = MODE_REDBLUE;
  lastMovementTime = millis();
}

// =============================================
// BUZZER
// =============================================

void playTone(int freq, int dur) {
  if (!SOUND_ENABLED) { delay(dur); return; }  // muted: keep timing identical
  ledcWriteTone(BUZZER_PIN, freq);
  delay(dur);
  ledcWriteTone(BUZZER_PIN, 0);
}

// =============================================
// BATTERY
// =============================================

int readBatteryADC() {
  long t = 0;
  for (int i = 0; i < 64; i++) { t += analogRead(USB_POWER_PIN); delayMicroseconds(50); }
  return t / 64;
}

bool isBatteryFull() { return readBatteryADC() > BATTERY_FULL_ADC; }
bool isBatteryLow() { return readBatteryADC() < BATTERY_LOW_ADC; }

// Battery percentage; the 64-sample ADC read is slow, so it is refreshed once per second.
int batteryAdcCached = 0;
int batteryPercent() {
  static unsigned long last = 0;
  static int pct = 100;
  static bool first = true;
  if (first || millis() - last >= 1000) {
    first = false;
    last = millis();
    batteryAdcCached = readBatteryADC();
    pct = constrain(map(batteryAdcCached, BATTERY_LOW_ADC, BATTERY_FULL_ADC, 0, 100), 0, 100);
  }
  return pct;
}

// Charge indicator on the two outermost LEDs of the bar:
//   0-10 % red blinking, 10-20 % red, 20-40 % yellow, 40-60 % orange, 60-100 % green
void drawBatteryIndicator(unsigned long now) {
  int pct = batteryPercent();
  uint32_t color;
  if (pct < 10) {
    if (now - lastBlinkTime > 500) { blinkState = !blinkState; lastBlinkTime = now; }
    if (!blinkState) return;
    color = stripLeft.Color(255, 0, 0);
  } else if (pct < 20) {
    color = stripLeft.Color(255, 0, 0);
  } else if (pct < 40) {
    color = stripLeft.Color(255, 200, 0);
  } else if (pct < 60) {
    color = stripLeft.Color(255, 90, 0);
  } else {
    color = stripLeft.Color(0, 255, 0);
  }
  setPixel(0, color);
  setPixel(TOTAL_LEDS - 1, color);
}

// --- Battery history log (circular buffer in NVS, survives deep sleep) ---
// One entry is saved on every wake-up (in setup), before LEDs draw current.
// Up to BATT_LOG_SIZE entries are kept; oldest is overwritten once full.

void battLogSave() {
  uint16_t buf[BATT_LOG_SIZE];
  memset(buf, 0, sizeof(buf));

  prefs.begin("battlog", false);
  uint8_t cnt = prefs.getUChar("cnt", 0);
  uint8_t idx = prefs.getUChar("idx", 0);
  if (prefs.getBytesLength("vals") == sizeof(buf)) {
    prefs.getBytes("vals", buf, sizeof(buf));
  }

  buf[idx] = (uint16_t)constrain(readBatteryADC(), 0, 65535);
  idx = (idx + 1) % BATT_LOG_SIZE;
  if (cnt < BATT_LOG_SIZE) cnt++;

  prefs.putBytes("vals", buf, sizeof(buf));
  prefs.putUChar("cnt", cnt);
  prefs.putUChar("idx", idx);
  prefs.end();
}

void battLogPrint() {
  uint16_t buf[BATT_LOG_SIZE];
  memset(buf, 0, sizeof(buf));

  prefs.begin("battlog", true);
  uint8_t cnt = prefs.getUChar("cnt", 0);
  uint8_t idx = prefs.getUChar("idx", 0);
  if (prefs.getBytesLength("vals") == sizeof(buf)) {
    prefs.getBytes("vals", buf, sizeof(buf));
  }
  prefs.end();

  Serial.println("=== BATTERY LOG (" + String(BATT_LOG_SIZE) + " wake-ups max, oldest first) ===");
  if (cnt == 0) {
    Serial.println("  (no entries yet)");
    return;
  }

  int loADC = buf[0], hiADC = buf[0];          // for range info at the end
  for (uint8_t i = 0; i < cnt; i++) {
    uint8_t ri = (uint8_t)((idx - cnt + i + BATT_LOG_SIZE) % BATT_LOG_SIZE);
    int adc = buf[ri];
    int pct = constrain(map(adc, BATTERY_LOW_ADC, BATTERY_FULL_ADC, 0, 100), 0, 100);
    if (adc < loADC) loADC = adc;
    if (adc > hiADC) hiADC = adc;
    bool isNewest = (i == cnt - 1);

    Serial.print(isNewest ? "  >> " : "     ");
    Serial.print("[");
    if (i + 1 < 10) Serial.print(" ");
    Serial.print(i + 1);
    Serial.print("/");
    Serial.print(cnt);
    Serial.print("]  ADC=");
    Serial.print(adc);
    Serial.print("  ~");
    if (pct < 10) Serial.print(" ");
    Serial.print(pct);
    Serial.print("%");
    if (isNewest) Serial.print("  <- this wake-up");
    Serial.println();
  }
  Serial.print("  range: ADC "); Serial.print(loADC);
  Serial.print(" – "); Serial.print(hiADC);
  Serial.print("  (drop="); Serial.print(hiADC - loADC); Serial.println(")");
}

// =============================================
// LED HELPERS
// =============================================

float mapTiltToBrightness(float absAngle) {
  float d0 = absAngle, d90 = abs(absAngle - 90);
  float md = min(d0, d90);
  if (md > MAX_ANGLE) md = MAX_ANGLE;
  float s = 1.0 - (md / MAX_ANGLE);
  return 10 + s * s * 245;
}

void setAllLEDs(Adafruit_NeoPixel &strip, uint32_t color) {
  for (int i = 0; i < LED_COUNT; i++) strip.setPixelColor(i, color);
}

void flashColor(uint32_t color, int times, int delayMs) {
  for (int t = 0; t < times; t++) {
    setAllLEDs(stripLeft, color);
    setAllLEDs(stripRight, color);
    stripLeft.show(); stripRight.show();
    delay(delayMs);
    stripLeft.clear(); stripRight.clear();
    stripLeft.show(); stripRight.show();
    delay(delayMs);
  }
}
