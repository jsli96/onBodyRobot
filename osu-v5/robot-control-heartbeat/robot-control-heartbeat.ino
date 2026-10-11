/***************************************************************************
 * Robot Control — Heartbeat Animation (Procedural Drawing)
 * Standalone sketch for XIAO ESP32-S3 + Seeed Round Display
 *
 * SCREEN FLOW:
 *   1. GO / STOP    — tap GO → motors run 5s → heart animation starts
 *                     tap STOP → halts motors, stays on this screen
 *                     swipe LEFT → IMU screen
 *
 *   2. HEART ANIM   — procedurally drawn beating heart, loops forever
 *                     double-tap → back to Go/Stop
 *
 *   3. IMU DATA     — live accel/gyro/mag + roll/pitch at 10 Hz
 *                     swipe RIGHT → back to Go/Stop
 *
 * HARDWARE:
 *   Display : GC9A01 240×240 SPI (TFT_eSPI + USE_HSPI_PORT)
 *   Touch   : CHSC6X I2C via lv_xiao_round_screen.h
 *   Motors  : MCP23017 Port B0-B3 at I2C 0x20
 *   IMU     : ICM-20948 I2C at 0x68
 *   LED     : NeoPixel on D0
 *
 * NO external files needed — the heart is drawn with math!
 ***************************************************************************/

#include <TFT_eSPI.h>
#include <Wire.h>
#include <SPI.h>
#define USE_TFT_ESPI_LIBRARY
#include "lv_xiao_round_screen.h"
#include <Adafruit_MCP23X17.h>
#include "ICM_20948.h"
#include <FastLED.h>

// ===================== Hardware Pins =====================
#define SDA_PIN  5
#define SCL_PIN  6
#define LED_PIN  D0
#define NUM_LEDS 1

#define MCP_M1_IN1 8
#define MCP_M1_IN2 9
#define MCP_M2_IN1 10
#define MCP_M2_IN2 11

#undef AD0_VAL
#define AD0_VAL 0

// ===================== Objects =====================
Adafruit_MCP23X17 mcp;
bool mcp_ok = false;

ICM_20948_I2C myICM;
bool imu_ok = false;

CRGB leds[NUM_LEDS];

// ===================== State Machine =====================
enum Screen { SCREEN_CONTROL, SCREEN_HEART, SCREEN_IMU };
Screen currentScreen = SCREEN_CONTROL;

// Motor
bool motorRunning = false;
unsigned long motorStartTime = 0;
const unsigned long MOTOR_DURATION = 5000;

// Touch
lv_coord_t touchStartX = 0, touchStartY = 0;
lv_coord_t touchLastX  = 0, touchLastY  = 0;
unsigned long lastTapTime = 0;
bool waitingDoubleTap = false;
const unsigned long DOUBLE_TAP_WINDOW = 400;
const int SWIPE_THRESHOLD = 40;

// IMU refresh
unsigned long lastIMUDraw = 0;
const unsigned long IMU_DRAW_INTERVAL = 100;

// Heart animation
unsigned long heartAnimStart = 0;
unsigned long lastHeartFrame = 0;
const unsigned long HEART_FRAME_MS = 33;  // ~30 FPS for smooth animation

// BPM control via Serial
unsigned int currentBPM = 70;             // default 70 BPM
unsigned long cycleMs = 857;              // 60000 / 70 ≈ 857ms per beat
String serialBuffer = "";                 // accumulates typed digits

// ===================== Colors =====================
#define COLOR_GO_BG       0x2E85
#define COLOR_GO_ACTIVE   0x07E0
#define COLOR_STOP_BG     0xA000
#define COLOR_STOP_ACTIVE 0xF800
#define COLOR_DIVIDER     0x4208
#define COLOR_BG_IMU      0x0000
#define COLOR_TEXT         TFT_WHITE

const int SCREEN_CX = 120;
const int DIVIDER_Y = 120;

// ===================== Heart Shape Math =====================
//
// Uses the parametric heart curve:
//   x(t) = 16 * sin³(t)
//   y(t) = 13*cos(t) - 5*cos(2t) - 2*cos(3t) - cos(4t)
//
// t goes from 0 to 2π. The output ranges roughly:
//   x: [-16, 16],  y: [-17, 12]  (y inverted for screen coords)
//
// We scale this to fill most of the 240×240 display with a pulsing
// scale factor that mimics a real cardiac rhythm:
//   - Quick systole (contraction): ~150ms
//   - Slower diastole (relaxation): ~400ms
//   - Brief rest between beats
//   - Full cycle ~800ms (~75 BPM)

// Returns the beating scale factor (1.0 = resting, peaks ~1.18)
// based on elapsed milliseconds within one cardiac cycle.
// Mimics real "lub-dub" pattern. Phase durations scale with BPM.
//
// At 70 BPM (cycleMs=857): lub=100ms, dip=100ms, dub=150ms, relax=200ms, rest=307ms
// Phases scale proportionally so the beat shape stays natural at any rate.
float heartBeatScale(unsigned long ms) {
  unsigned long phase = ms % cycleMs;

  // Scale factor: how much to stretch/shrink phases relative to base 850ms
  float s = cycleMs / 850.0f;

  // Phase boundaries (scaled)
  unsigned long p1 = (unsigned long)(100 * s);   // end of lub
  unsigned long p2 = (unsigned long)(200 * s);   // end of dip
  unsigned long p3 = (unsigned long)(350 * s);   // end of dub
  unsigned long p4 = (unsigned long)(550 * s);   // end of relaxation
  // p4..cycleMs = rest

  // Phase 1: first beat "lub" (systole) — fast pump
  if (phase < p1) {
    float t = (float)phase / p1;
    return 1.0f + 0.18f * sin(t * PI);
  }
  // Phase 2: brief dip between lub and dub
  if (phase < p2) {
    float t = (float)(phase - p1) / (p2 - p1);
    return 1.0f - 0.04f * sin(t * PI);
  }
  // Phase 3: second beat "dub" (smaller)
  if (phase < p3) {
    float t = (float)(phase - p2) / (p3 - p2);
    return 1.0f + 0.10f * sin(t * PI);
  }
  // Phase 4: relaxation (diastole)
  if (phase < p4) {
    float t = (float)(phase - p3) / (p4 - p3);
    return 1.0f + 0.02f * (1.0f - t) - 0.03f * sin(t * PI);
  }
  // Phase 5: rest
  return 1.0f;
}

// Scanline edge table for the heart shape.
// For each scanline, stores the leftmost and rightmost x pixel.
// Max heart height is about 200px, centered around row 120.
#define HEART_SCAN_MAX 240
int heartScanLeft[HEART_SCAN_MAX];
int heartScanRight[HEART_SCAN_MAX];
int heartScanYMin = 0, heartScanYMax = 0;

// Previous frame scan edges for differential clearing
int prevScanLeft[HEART_SCAN_MAX];
int prevScanRight[HEART_SCAN_MAX];
int prevScanYMin = 0, prevScanYMax = 0;

// Build scanline edge table from parametric heart curve.
// Much faster than per-pixel implicit equation testing.
void buildHeartScanlines(int cx, int cy, float scale) {
  // Base scale: heart curve x range [-16,16], we want ~180px wide at scale 1
  float s = 5.6f * scale;

  // Initialize
  for (int i = 0; i < HEART_SCAN_MAX; i++) {
    heartScanLeft[i]  = 999;
    heartScanRight[i] = -1;
  }
  heartScanYMin = 239;
  heartScanYMax = 0;

  // Sample the parametric curve densely and fill scanline edges
  const int N = 720;  // dense sampling for smooth edges
  float prevPx = 0, prevPy = 0;
  bool first = true;

  for (int i = 0; i <= N; i++) {
    float t = (float)i / N * 2.0f * PI;
    float sinT = sin(t);
    float cosT = cos(t);
    float hx = 16.0f * sinT * sinT * sinT;
    float hy = -(13.0f * cosT - 5.0f * cos(2*t) - 2.0f * cos(3*t) - cos(4*t));

    float px = cx + hx * s;
    float py = cy + hy * s;

    if (!first) {
      // Rasterize the edge between previous point and this point
      int steps = max(1, (int)(max(fabs(px - prevPx), fabs(py - prevPy))));
      for (int j = 0; j <= steps; j++) {
        float frac = (float)j / steps;
        int sx = (int)(prevPx + frac * (px - prevPx));
        int sy = (int)(prevPy + frac * (py - prevPy));
        if (sy >= 0 && sy < HEART_SCAN_MAX) {
          if (sx < heartScanLeft[sy])  heartScanLeft[sy]  = sx;
          if (sx > heartScanRight[sy]) heartScanRight[sy] = sx;
          if (sy < heartScanYMin) heartScanYMin = sy;
          if (sy > heartScanYMax) heartScanYMax = sy;
        }
      }
    }
    prevPx = px;
    prevPy = py;
    first = false;
  }

  // Clamp to screen
  heartScanYMin = max(0, heartScanYMin);
  heartScanYMax = min(239, heartScanYMax);
}

// Draw a filled heart with per-row gradient and differential clearing.
void drawFilledHeart(int cx, int cy, float scale) {
  buildHeartScanlines(cx, cy, scale);

  int totalRows = heartScanYMax - heartScanYMin;
  if (totalRows <= 0) totalRows = 1;

  // Clear rows from previous frame that are outside new frame
  for (int row = prevScanYMin; row < heartScanYMin; row++) {
    if (prevScanLeft[row] <= prevScanRight[row]) {
      int l = max(0, prevScanLeft[row]);
      int r = min(239, prevScanRight[row]);
      tft.drawFastHLine(l, row, r - l + 1, TFT_BLACK);
    }
  }
  for (int row = heartScanYMax + 1; row <= prevScanYMax; row++) {
    if (prevScanLeft[row] <= prevScanRight[row]) {
      int l = max(0, prevScanLeft[row]);
      int r = min(239, prevScanRight[row]);
      tft.drawFastHLine(l, row, r - l + 1, TFT_BLACK);
    }
  }

  // Draw each scanline
  for (int row = heartScanYMin; row <= heartScanYMax; row++) {
    int newL = heartScanLeft[row];
    int newR = heartScanRight[row];

    if (newL > newR) {
      // No heart on this row — clear previous if needed
      if (row >= prevScanYMin && row <= prevScanYMax &&
          prevScanLeft[row] <= prevScanRight[row]) {
        int l = max(0, prevScanLeft[row]);
        int r = min(239, prevScanRight[row]);
        tft.drawFastHLine(l, row, r - l + 1, TFT_BLACK);
      }
      continue;
    }

    // Clamp
    newL = max(0, newL);
    newR = min(239, newR);

    // Clear areas left/right that were filled in previous frame
    if (row >= prevScanYMin && row <= prevScanYMax &&
        prevScanLeft[row] <= prevScanRight[row]) {
      int oldL = max(0, prevScanLeft[row]);
      int oldR = min(239, prevScanRight[row]);
      if (oldL < newL)
        tft.drawFastHLine(oldL, row, newL - oldL, TFT_BLACK);
      if (oldR > newR)
        tft.drawFastHLine(newR + 1, row, oldR - newR, TFT_BLACK);
    }

    // Gradient color: bright red at top (lobes) → darker at bottom (tip)
    float rowNorm = (float)(row - heartScanYMin) / totalRows;
    uint8_t rVal = 230 - (uint8_t)(rowNorm * 90);
    uint8_t gVal = 8   + (uint8_t)(rowNorm * 5);
    uint8_t bVal = 25  - (uint8_t)(rowNorm * 15);

    // Specular highlight near upper portion (lobes area)
    if (rowNorm < 0.4f) {
      float hlDist = fabs(rowNorm - 0.2f);
      if (hlDist < 0.18f) {
        float hl = (0.18f - hlDist) / 0.18f * 0.35f;
        rVal = min(255, (int)(rVal + hl * 80));
        gVal = min(255, (int)(gVal + hl * 30));
        bVal = min(255, (int)(bVal + hl * 30));
      }
    }

    uint16_t color = tft.color565(rVal, gVal, bVal);
    tft.drawFastHLine(newL, row, newR - newL + 1, color);
  }

  // Save current scanlines as "previous" for next frame
  memcpy(prevScanLeft,  heartScanLeft,  sizeof(heartScanLeft));
  memcpy(prevScanRight, heartScanRight, sizeof(heartScanRight));
  prevScanYMin = heartScanYMin;
  prevScanYMax = heartScanYMax;
}

// ===================== Motor Functions =====================

void moveForward() {
  if (!mcp_ok) return;
  mcp.digitalWrite(MCP_M1_IN1, HIGH);
  mcp.digitalWrite(MCP_M1_IN2, LOW);
  mcp.digitalWrite(MCP_M2_IN1, HIGH);
  mcp.digitalWrite(MCP_M2_IN2, LOW);
}

void stopMotor() {
  if (!mcp_ok) return;
  mcp.digitalWrite(MCP_M1_IN1, LOW);
  mcp.digitalWrite(MCP_M1_IN2, LOW);
  mcp.digitalWrite(MCP_M2_IN1, LOW);
  mcp.digitalWrite(MCP_M2_IN2, LOW);
}

// ===================== UI: Control Screen =====================

void drawGoButton(bool active) {
  uint16_t bg = active ? COLOR_GO_ACTIVE : COLOR_GO_BG;
  tft.fillRect(0, 0, 240, DIVIDER_Y, bg);
  tft.setTextColor(COLOR_TEXT, bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("GO", SCREEN_CX, 60, 4);
}

void drawStopButton(bool active) {
  uint16_t bg = active ? COLOR_STOP_ACTIVE : COLOR_STOP_BG;
  tft.fillRect(0, DIVIDER_Y, 240, 120, bg);
  tft.setTextColor(COLOR_TEXT, bg);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("STOP", SCREEN_CX, 180, 4);
}

void drawDivider() {
  tft.drawFastHLine(0, DIVIDER_Y - 1, 240, COLOR_DIVIDER);
  tft.drawFastHLine(0, DIVIDER_Y,     240, COLOR_DIVIDER);
}

void drawTimerBar() {
  if (!motorRunning) return;
  unsigned long elapsed = millis() - motorStartTime;
  if (elapsed > MOTOR_DURATION) elapsed = MOTOR_DURATION;
  int barW = (int)((elapsed * 230UL) / MOTOR_DURATION);
  tft.fillRect(5, 5, 230, 6, 0x0000);
  tft.fillRect(5, 5, barW, 6, COLOR_TEXT);
}

void drawControlScreen() {
  drawGoButton(motorRunning);
  drawStopButton(false);
  drawDivider();
  if (motorRunning) drawTimerBar();
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(COLOR_DIVIDER, COLOR_STOP_BG);
  tft.drawString("swipe left: IMU", SCREEN_CX, 235, 1);
}

// ===================== UI: Heart Animation Screen =====================

void drawHeartScreen() {
  tft.fillScreen(TFT_BLACK);
  // Reset previous frame scanline data
  for (int i = 0; i < HEART_SCAN_MAX; i++) {
    prevScanLeft[i]  = 999;
    prevScanRight[i] = -1;
  }
  prevScanYMin = 0;
  prevScanYMax = 0;
  heartAnimStart = millis();
  lastHeartFrame = 0;

  // Draw hint text and BPM display
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(0x4208, TFT_BLACK);
  tft.drawString("double-tap: exit", SCREEN_CX, 236, 1);

  // Show current BPM at top
  drawBPMLabel();
}

void drawBPMLabel() {
  char bpmStr[16];
  snprintf(bpmStr, sizeof(bpmStr), "%u BPM", currentBPM);
  tft.fillRect(80, 2, 80, 14, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawString(bpmStr, SCREEN_CX, 3, 2);
}

void updateHeartAnimation() {
  unsigned long now = millis();
  if (now - lastHeartFrame < HEART_FRAME_MS) return;
  lastHeartFrame = now;

  unsigned long elapsed = now - heartAnimStart;
  float scale = heartBeatScale(elapsed);

  // Draw the beating heart centered on screen (no fillScreen needed —
  // drawFilledHeart handles its own clearing for flicker-free animation)
  drawFilledHeart(SCREEN_CX, SCREEN_CX + 5, scale);
}

// ===================== UI: IMU Screen =====================

void drawIMUScreen() {
  tft.fillScreen(COLOR_BG_IMU);
  tft.setTextColor(COLOR_TEXT, COLOR_BG_IMU);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("IMU DATA", SCREEN_CX, 8, 4);
  tft.drawFastHLine(10, 32, 220, COLOR_DIVIDER);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);
  tft.drawString("Accel  X:", 10, 40);
  tft.drawString("Y:", 10, 58);
  tft.drawString("Z:", 10, 76);
  tft.drawString("Gyro   X:", 10, 100);
  tft.drawString("Y:", 10, 118);
  tft.drawString("Z:", 10, 136);
  tft.drawString("Mag    X:", 10, 160);
  tft.drawString("Y:", 10, 178);
  tft.drawString("Z:", 10, 196);
  tft.drawString("Roll:", 130, 40);
  tft.drawString("Pitch:", 130, 58);
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(COLOR_DIVIDER, COLOR_BG_IMU);
  tft.drawString("< swipe right to go back >", SCREEN_CX, 235, 1);
}

void updateIMUValues() {
  if (!imu_ok) return;
  if (myICM.dataReady()) myICM.getAGMT();

  float ax = myICM.accX(), ay = myICM.accY(), az = myICM.accZ();
  float gx = myICM.gyrX(), gy = myICM.gyrY(), gz = myICM.gyrZ();
  float mx = myICM.magX(), myy = myICM.magY(), mz = myICM.magZ();
  float roll  = atan2(ay, az) * 180.0f / PI;
  float pitch = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0f / PI;

  char buf[16];
  tft.setTextColor(TFT_CYAN, COLOR_BG_IMU);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);

  tft.fillRect(75, 40, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", ax); tft.drawString(buf, 75, 40);
  tft.fillRect(75, 58, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", ay); tft.drawString(buf, 75, 58);
  tft.fillRect(75, 76, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", az); tft.drawString(buf, 75, 76);

  tft.fillRect(75, 100, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", gx); tft.drawString(buf, 75, 100);
  tft.fillRect(75, 118, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", gy); tft.drawString(buf, 75, 118);
  tft.fillRect(75, 136, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", gz); tft.drawString(buf, 75, 136);

  tft.fillRect(75, 160, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", mx); tft.drawString(buf, 75, 160);
  tft.fillRect(75, 178, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", myy); tft.drawString(buf, 75, 178);
  tft.fillRect(75, 196, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", mz); tft.drawString(buf, 75, 196);

  tft.fillRect(180, 40, 55, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", roll);  tft.drawString(buf, 180, 40);
  tft.fillRect(180, 58, 55, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", pitch); tft.drawString(buf, 180, 58);
}

// ===================== Touch Helpers =====================

bool readTouch(lv_coord_t &x, lv_coord_t &y) {
  if (!chsc6x_is_pressed()) return false;
  chsc6x_get_xy(&x, &y);
  return true;
}

void waitRelease() {
  while (chsc6x_is_pressed()) {
    chsc6x_get_xy(&touchLastX, &touchLastY);
    delay(15);
  }
}

// ===================== Serial BPM Control =====================
// Type a number (30-220) into the Serial Monitor and press Enter to set BPM.

void checkSerialBPM() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n' || c == '\r') {
      if (serialBuffer.length() > 0) {
        int val = serialBuffer.toInt();
        if (val >= 30 && val <= 220) {
          currentBPM = val;
          cycleMs = 60000UL / currentBPM;
          Serial.printf("BPM set to %u (cycle = %lu ms)\n", currentBPM, cycleMs);
          // Update on-screen BPM label if heart animation is active
          if (currentScreen == SCREEN_HEART) {
            drawBPMLabel();
          }
        } else {
          Serial.println("Invalid BPM. Enter a number between 30 and 220.");
        }
        serialBuffer = "";
      }
    } else if (c >= '0' && c <= '9') {
      serialBuffer += c;
    }
  }
}

// ===================== Setup =====================

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Robot Control — Heartbeat ===");

  // Display first (CRITICAL — must be before any blocking calls)
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  Serial.println("Display initialized.");

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);
  delay(10);

  // Touch
  pinMode(TOUCH_INT, INPUT_PULLUP);

  // Motors
  mcp_ok = mcp.begin_I2C(0x20);
  if (!mcp_ok) {
    Serial.println("MCP23017 not found — motors disabled.");
  } else {
    mcp.pinMode(MCP_M1_IN1, OUTPUT);
    mcp.pinMode(MCP_M1_IN2, OUTPUT);
    mcp.pinMode(MCP_M2_IN1, OUTPUT);
    mcp.pinMode(MCP_M2_IN2, OUTPUT);
    stopMotor();
    Serial.println("Motors ready.");
  }

  // IMU
  myICM.begin(Wire, AD0_VAL);
  imu_ok = (myICM.status == ICM_20948_Stat_Ok);
  Serial.println(imu_ok ? "IMU ready." : "IMU init failed.");

  // LED
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  leds[0] = CRGB::Green;
  FastLED.show();

  drawControlScreen();
  Serial.println("Setup complete. No SD card or files needed!");
  Serial.printf("Heart BPM: %u (cycle %lu ms)\n", currentBPM, cycleMs);
  Serial.println("Type a number 30-220 in Serial Monitor to change BPM.");
}

// ===================== Main Loop =====================

void loop() {
  // Always check for BPM input from Serial (works on any screen)
  checkSerialBPM();

  switch (currentScreen) {
    case SCREEN_CONTROL: loopControl(); break;
    case SCREEN_HEART:   loopHeart();   break;
    case SCREEN_IMU:     loopIMU();     break;
  }
}

// ---------- CONTROL SCREEN ----------

void loopControl() {
  // Motor auto-stop → go to heart animation
  if (motorRunning && (millis() - motorStartTime >= MOTOR_DURATION)) {
    Serial.println("Motors done → heart animation.");
    motorRunning = false;
    stopMotor();
    leds[0] = CRGB::Purple; FastLED.show();
    currentScreen = SCREEN_HEART;
    drawHeartScreen();
    return;
  }

  if (motorRunning) drawTimerBar();

  lv_coord_t tx, ty;
  if (readTouch(tx, ty)) {
    touchStartX = tx; touchStartY = ty;
    touchLastX = tx;  touchLastY = ty;

    while (chsc6x_is_pressed()) {
      chsc6x_get_xy(&touchLastX, &touchLastY);
      if (motorRunning) {
        drawTimerBar();
        if (millis() - motorStartTime >= MOTOR_DURATION) {
          motorRunning = false;
          stopMotor();
          leds[0] = CRGB::Purple; FastLED.show();
          currentScreen = SCREEN_HEART;
          drawHeartScreen();
          return;
        }
      }
      delay(15);
    }

    int dx = touchLastX - touchStartX;
    int dy = touchLastY - touchStartY;

    // Swipe LEFT → IMU
    if (dx < -SWIPE_THRESHOLD && abs(dy) < abs(dx) && !motorRunning) {
      Serial.println("Swipe left → IMU");
      currentScreen = SCREEN_IMU;
      drawIMUScreen();
      lastIMUDraw = millis();
      return;
    }

    // Tap
    if (abs(dx) < SWIPE_THRESHOLD && abs(dy) < SWIPE_THRESHOLD) {
      if (touchStartY < DIVIDER_Y) {
        // GO
        if (!motorRunning) {
          Serial.println("GO pressed");
          motorRunning = true;
          motorStartTime = millis();
          moveForward();
          leds[0] = CRGB::Red; FastLED.show();
          drawGoButton(true);
          drawDivider();
        }
      } else {
        // STOP
        if (motorRunning) {
          Serial.println("STOP pressed");
          motorRunning = false;
          stopMotor();
          leds[0] = CRGB::Green; FastLED.show();
          drawGoButton(false);
          drawStopButton(true);
          drawDivider();
          delay(200);
          drawStopButton(false);
          drawDivider();
        }
      }
    }
  }

  delay(20);
}

// ---------- HEART ANIMATION SCREEN ----------

void loopHeart() {
  // Draw the next animation frame
  updateHeartAnimation();

  // Check for double-tap to exit
  lv_coord_t tx, ty;
  if (readTouch(tx, ty)) {
    touchStartX = tx; touchStartY = ty;
    touchLastX = tx;  touchLastY = ty;

    waitRelease();

    int dx = touchLastX - touchStartX;
    int dy = touchLastY - touchStartY;

    // Only process taps (not swipes)
    if (abs(dx) < SWIPE_THRESHOLD && abs(dy) < SWIPE_THRESHOLD) {
      unsigned long now = millis();
      if (waitingDoubleTap && (now - lastTapTime < DOUBLE_TAP_WINDOW)) {
        // Double-tap detected → back to control
        Serial.println("Double-tap → back to control.");
        waitingDoubleTap = false;
        leds[0] = CRGB::Green; FastLED.show();
        currentScreen = SCREEN_CONTROL;
        drawControlScreen();
        return;
      }
      lastTapTime = now;
      waitingDoubleTap = true;
    }
  }

  // Reset double-tap if window expired
  if (waitingDoubleTap && (millis() - lastTapTime >= DOUBLE_TAP_WINDOW)) {
    waitingDoubleTap = false;
  }
}

// ---------- IMU SCREEN ----------

void loopIMU() {
  // Update IMU data at 10 Hz
  if (millis() - lastIMUDraw >= IMU_DRAW_INTERVAL) {
    updateIMUValues();
    lastIMUDraw = millis();
  }

  lv_coord_t tx, ty;
  if (readTouch(tx, ty)) {
    touchStartX = tx; touchStartY = ty;
    touchLastX = tx;  touchLastY = ty;

    waitRelease();

    int dx = touchLastX - touchStartX;
    int dy = touchLastY - touchStartY;

    // Swipe RIGHT → back to control
    if (dx > SWIPE_THRESHOLD && abs(dy) < abs(dx)) {
      Serial.println("Swipe right → control.");
      currentScreen = SCREEN_CONTROL;
      drawControlScreen();
      return;
    }
  }

  delay(20);
}
