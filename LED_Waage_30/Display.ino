// =============================================
// SH1106 128x64 OLED (I2C) - shows the calibrated tilt in big digits
// =============================================
// Minimal driver without an external library (no extra flash for fonts):
// the 1 KB frame buffer is drawn with rectangles (seven-segment style digits)
// and pushed to the display ONE PAGE (128 bytes) per loop() call, so the
// sensor loop is never blocked for more than ~3 ms.
// Wiring: shares the I2C bus of the MPU6050 (SDA = GPIO15, SCL = GPIO4), 3.3 V, address 0x3C/0x3D.

#define OLED_W 128
#define OLED_H 64
#define OLED_COL_OFFSET 2          // SH1106 has 132 columns, the glass is centred
#define OLED_FRAME_MS 100          // at most 10 refreshes per second

uint8_t oledBuf[OLED_W * OLED_H / 8];
uint8_t oledAddr = 0;              // 0 = no display found
int oledPage = 8;                  // next page to send, 8 = idle
unsigned long oledLastFrame = 0;
long oledLastKey = -2;             // what is currently drawn (avoids needless refreshes)

static void oledCommands(const uint8_t *cmds, size_t n) {
  Wire.beginTransmission(oledAddr);
  Wire.write((uint8_t)0x00);       // control byte: following bytes are commands
  Wire.write(cmds, n);
  Wire.endTransmission();
}

static void oledFill(int x, int y, int w, int h, bool on) {
  for (int yy = y; yy < y + h; yy++) {
    if (yy < 0 || yy >= OLED_H) continue;
    for (int xx = x; xx < x + w; xx++) {
      if (xx < 0 || xx >= OLED_W) continue;
      uint8_t bit = 1 << (yy & 7);
      if (on) oledBuf[(yy >> 3) * OLED_W + xx] |= bit;
      else    oledBuf[(yy >> 3) * OLED_W + xx] &= ~bit;
    }
  }
}

// Digit geometry (pixels)
#define DIG_W 18
#define DIG_H 40
#define DIG_T 5
#define DIG_CELL (DIG_W + 4)
#define SIGN_CELL 12
#define DOT_CELL 8
#define DEG_CELL 10

// segment bits: a=1 top, b=2 upper right, c=4 lower right, d=8 bottom, e=16 lower left, f=32 upper left, g=64 middle
static const uint8_t SEG_MASK[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

static void oledDigit(int x, int y, int d) {
  uint8_t m = SEG_MASK[d];
  const int hw = DIG_W - 4, vh = DIG_H / 2 - 3;
  if (m & 1)  oledFill(x + 2, y, hw, DIG_T, true);
  if (m & 8)  oledFill(x + 2, y + DIG_H - DIG_T, hw, DIG_T, true);
  if (m & 64) oledFill(x + 2, y + (DIG_H - DIG_T) / 2, hw, DIG_T, true);
  if (m & 32) oledFill(x, y + 2, DIG_T, vh, true);
  if (m & 2)  oledFill(x + DIG_W - DIG_T, y + 2, DIG_T, vh, true);
  if (m & 16) oledFill(x, y + DIG_H / 2 + 1, DIG_T, vh, true);
  if (m & 4)  oledFill(x + DIG_W - DIG_T, y + DIG_H / 2 + 1, DIG_T, vh, true);
}

static void oledRenderAngle(float angle) {
  memset(oledBuf, 0, sizeof(oledBuf));
  long hundredths = lroundf(fabsf(angle) * 100.0f);
  if (hundredths > 9999) hundredths = 9999;
  bool neg = angle < 0 && hundredths > 0;     // never show "-0.00"
  int whole = hundredths / 100, frac = hundredths % 100;

  int total = (neg ? SIGN_CELL : 0) + (whole >= 10 ? 2 : 1) * DIG_CELL + DOT_CELL + 2 * DIG_CELL + DEG_CELL;
  int x = (OLED_W - total) / 2;
  const int y = (OLED_H - DIG_H) / 2;

  if (neg) { oledFill(x, y + (DIG_H - DIG_T) / 2, SIGN_CELL - 3, DIG_T, true); x += SIGN_CELL; }
  if (whole >= 10) { oledDigit(x, y, whole / 10); x += DIG_CELL; }
  oledDigit(x, y, whole % 10); x += DIG_CELL;
  oledFill(x + 1, y + DIG_H - 6, 6, 6, true); x += DOT_CELL;                 // decimal point
  oledDigit(x, y, frac / 10); x += DIG_CELL;
  oledDigit(x, y, frac % 10); x += DIG_CELL;
  oledFill(x, y, 8, 8, true); oledFill(x + 2, y + 2, 4, 4, false);           // degree sign
}

void oledBegin() {
  // list every device that answers on the I2C bus (helps with wiring problems)
  Serial.print("[I2C] devices:");
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.print(" 0x"); Serial.print(a, HEX); }
  }
  Serial.println();
  for (uint8_t a = 0x3C; a <= 0x3D; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { oledAddr = a; break; }
  }
  if (!oledAddr) { Serial.println("[OLED] not found"); return; }

  static const uint8_t init[] = {
    0xAE,             // display off
    0xD5, 0x80,       // clock divide
    0xA8, 0x3F,       // multiplex 1/64
    0xD3, 0x00,       // display offset
    0x40,             // start line
    0xAD, 0x8B,       // SH1106 internal DC-DC on
    0xA1, 0xC8,       // segment remap + COM scan direction (normal orientation)
    0xDA, 0x12,       // COM pins
    0x81, 0xCF,       // contrast
    0xD9, 0xF1,       // pre-charge
    0xDB, 0x40,       // VCOM level
    0x32,             // pump voltage
    0xA6, 0xA4,       // normal (not inverted), follow RAM
    0xAF              // display on
  };
  oledCommands(init, sizeof(init));
  Serial.print("[OLED] found at 0x"); Serial.println(oledAddr, HEX);

  oledRenderAngle(0);
  oledPage = 0;
}

// Called every loop(): refreshes the picture at most 10x per second, one page per call.
void oledService(unsigned long now) {
  if (!oledAddr) return;

  if (oledPage < 8) {
    const uint8_t pageCmd[] = { (uint8_t)(0xB0 | oledPage), (uint8_t)(OLED_COL_OFFSET & 0x0F), (uint8_t)(0x10 | (OLED_COL_OFFSET >> 4)) };
    oledCommands(pageCmd, sizeof(pageCmd));
    const uint8_t *row = &oledBuf[oledPage * OLED_W];
    for (int i = 0; i < OLED_W; i += 32) {            // 32-byte chunks fit the Wire buffer
      Wire.beginTransmission(oledAddr);
      Wire.write((uint8_t)0x40);                      // control byte: following bytes are data
      Wire.write(row + i, 32);
      Wire.endTransmission();
    }
    oledPage++;
    return;
  }

  if (now - oledLastFrame < OLED_FRAME_MS) return;
  oledLastFrame = now;

  long key;
  if (appMode == APP_LEVEL) key = lroundf(roundAngle(smoothedPitch) * 100.0f);
  else key = -1000000;                                 // lightsaber: blank screen
  if (key == oledLastKey) return;
  oledLastKey = key;

  if (appMode == APP_LEVEL) oledRenderAngle(roundAngle(smoothedPitch));
  else memset(oledBuf, 0, sizeof(oledBuf));
  oledPage = 0;
}

// Switch the display (and its DC-DC converter) off, e.g. before deep sleep.
void oledOff() {
  if (!oledAddr) return;
  static const uint8_t off[] = { 0xAE, 0xAD, 0x8A };   // display off, SH1106 DC-DC off
  oledCommands(off, sizeof(off));
}