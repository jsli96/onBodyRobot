/***************************************************************************
 * Robot Control — Multi-Screen Touch UI
 * Standalone sketch for XIAO ESP32-S3 + Seeed Round Display
 *
 * SCREENS:
 *   1. GO / STOP  — tap GO to run motors 5s then play animation on loop
 *                   tap STOP to halt motors mid-run
 *                   swipe LEFT  → IMU screen
 *   2. VIDEO      — PNG frame animation, loops until double-tap → back to Go/Stop
 *   3. IMU DATA   — live accel/gyro/mag + roll/pitch
 *                   swipe RIGHT → back to Go/Stop
 *
 * HARDWARE:
 *   Display : GC9A01 240x240 SPI  (TFT_eSPI + USE_HSPI_PORT)
 *   Touch   : CHSC6X I2C via lv_xiao_round_screen.h
 *   Motors  : MCP23017 Port B0-B3 at I2C 0x20
 *   IMU     : ICM-20948 I2C at 0x68
 *
 * VIDEO FORMAT:
 *   PNG frames stored on SPIFFS as /anim/frame_001.png … frame_NNN.png
 *   Use the companion convert_frames.py to prepare them.
 ***************************************************************************/

#include <TFT_eSPI.h>
#include <Wire.h>
#include <SPI.h>
#define USE_TFT_ESPI_LIBRARY
#include "lv_xiao_round_screen.h"
#include <Adafruit_MCP23X17.h>
#include "ICM_20948.h"
#include <PNGdec.h>
#include "SPIFFS.h"

// ===================== Hardware Pins =====================
#define SDA_PIN 5   // D4 on XIAO ESP32-S3
#define SCL_PIN 6   // D5 on XIAO ESP32-S3

// MCP23017 motor pins (Port B0..B3 => MCP pins 8..11)
#define MCP_M1_IN1 8
#define MCP_M1_IN2 9
#define MCP_M2_IN1 10
#define MCP_M2_IN2 11

// IMU
#undef AD0_VAL
#define AD0_VAL 0   // AD0 low → address 0x68

// ===================== Objects =====================
Adafruit_MCP23X17 mcp;
bool mcp_ok = false;

ICM_20948_I2C myICM;
bool imu_ok = false;

PNG png;

// ===================== App State Machine =====================
enum Screen { SCREEN_CONTROL, SCREEN_VIDEO, SCREEN_IMU };
Screen currentScreen = SCREEN_CONTROL;

// Motor state
bool motorRunning = false;
unsigned long motorStartTime = 0;
const unsigned long MOTOR_DURATION = 5000;  // 5 seconds
bool motorFinished = false;  // flag: motors done, start video

// Animation frame list
#define MAX_FRAMES 200
String frameFiles[MAX_FRAMES];
int frameCount = 0;
int currentFrame = 0;
static uint16_t lineBuf[240];  // single-line decode buffer

// Touch / gesture
lv_coord_t touchStartX = 0, touchStartY = 0;
lv_coord_t touchLastX  = 0, touchLastY  = 0;
bool wasTouched = false;
unsigned long lastTapTime = 0;
bool waitingDoubleTap = false;
const unsigned long DOUBLE_TAP_WINDOW = 400;  // ms
const int SWIPE_THRESHOLD = 40;

// IMU display refresh
unsigned long lastIMUDraw = 0;
const unsigned long IMU_DRAW_INTERVAL = 100;  // 10 Hz

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

// ===================== Animation / Video =====================

void scanAnimFrames() {
  frameCount = 0;
  // Try sequential filenames: /anim/frame_001.png, frame_002.png, ...
  for (int i = 1; i <= MAX_FRAMES; i++) {
    char path[32];
    snprintf(path, sizeof(path), "/anim/frame_%03d.png", i);
    if (SPIFFS.exists(path)) {
      frameFiles[frameCount++] = String(path);
    } else {
      break;  // stop at first gap
    }
  }
  Serial.printf("Found %d animation frames.\n", frameCount);
}

// PNGdec callbacks for SPIFFS streaming
static fs::File pngFile;

void* pngOpen(const char *filename, int32_t *pFileSize) {
  pngFile = SPIFFS.open(filename, "r");
  if (!pngFile) return NULL;
  *pFileSize = pngFile.size();
  return &pngFile;
}

void pngClose(void *handle) {
  if (handle) ((fs::File*)handle)->close();
}

int32_t pngRead(PNGFILE *pFile, uint8_t *buf, int32_t len) {
  return ((fs::File*)pFile->fHandle)->read(buf, len);
}

int32_t pngSeek(PNGFILE *pFile, int32_t pos) {
  return ((fs::File*)pFile->fHandle)->seek(pos) ? 0 : -1;
}

// Draw callback — pushes each decoded line directly to the display
int pngDrawLine(PNGDRAW *pDraw) {
  if (pDraw->y >= 240) return 1;
  png.getLineAsRGB565(pDraw, lineBuf, PNG_RGB565_BIG_ENDIAN, 0x00000000);
  tft.pushImage(0, pDraw->y, pDraw->iWidth > 240 ? 240 : pDraw->iWidth, 1, lineBuf);
  return 1;
}

// Display one frame directly to screen (no framebuffer needed)
bool showFrame(int idx) {
  if (idx < 0 || idx >= frameCount) return false;
  int rc = png.open(frameFiles[idx].c_str(), pngOpen, pngClose, pngRead, pngSeek, pngDrawLine);
  if (rc != PNG_SUCCESS) return false;
  rc = png.decode(nullptr, 0);
  png.close();
  return (rc == PNG_SUCCESS);
}

// ===================== UI Drawing =====================

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
}

void drawIMUScreen() {
  tft.fillScreen(COLOR_BG_IMU);
  tft.setTextColor(COLOR_TEXT, COLOR_BG_IMU);
  tft.setTextDatum(TC_DATUM);
  tft.drawString("IMU DATA", SCREEN_CX, 8, 4);
  tft.drawFastHLine(10, 32, 220, COLOR_DIVIDER);
  // Labels drawn once; values updated in updateIMUScreen()
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
  // Swipe hint
  tft.setTextDatum(BC_DATUM);
  tft.setTextColor(COLOR_DIVIDER, COLOR_BG_IMU);
  tft.drawString("< swipe right to go back >", SCREEN_CX, 235, 1);
}

void updateIMUValues() {
  if (!imu_ok) return;
  if (myICM.dataReady()) {
    myICM.getAGMT();
  }

  float ax = myICM.accX(), ay = myICM.accY(), az = myICM.accZ();
  float gx = myICM.gyrX(), gy = myICM.gyrY(), gz = myICM.gyrZ();
  float mx = myICM.magX(), my = myICM.magY(), mz = myICM.magZ();
  float roll  = atan2(ay, az) * 180.0f / PI;
  float pitch = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0f / PI;

  char buf[16];
  tft.setTextColor(TFT_CYAN, COLOR_BG_IMU);
  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);

  // Clear value columns then print
  // Accel
  tft.fillRect(75, 40, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", ax); tft.drawString(buf, 75, 40);
  tft.fillRect(75, 58, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", ay); tft.drawString(buf, 75, 58);
  tft.fillRect(75, 76, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", az); tft.drawString(buf, 75, 76);
  // Gyro
  tft.fillRect(75, 100, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", gx); tft.drawString(buf, 75, 100);
  tft.fillRect(75, 118, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", gy); tft.drawString(buf, 75, 118);
  tft.fillRect(75, 136, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", gz); tft.drawString(buf, 75, 136);
  // Mag
  tft.fillRect(75, 160, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", mx); tft.drawString(buf, 75, 160);
  tft.fillRect(75, 178, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", my); tft.drawString(buf, 75, 178);
  tft.fillRect(75, 196, 50, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", mz); tft.drawString(buf, 75, 196);
  // Roll / Pitch
  tft.fillRect(180, 40, 55, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", roll);  tft.drawString(buf, 180, 40);
  tft.fillRect(180, 58, 55, 16, COLOR_BG_IMU);
  snprintf(buf, sizeof(buf), "%.1f", pitch); tft.drawString(buf, 180, 58);
}

// ===================== Touch Helpers =====================

// Returns true while finger is down, fills x/y
bool readTouch(lv_coord_t &x, lv_coord_t &y) {
  if (!chsc6x_is_pressed()) return false;
  chsc6x_get_xy(&x, &y);
  return true;
}

// ===================== Setup =====================

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Robot Control UI ===");

  // SPIFFS
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed!");
  } else {
    Serial.println("SPIFFS mounted.");
    scanAnimFrames();
  }

  // Display (before anything else)
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

  // MCP23017 motors
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

  // Show initial screen
  drawControlScreen();
  Serial.println("Setup complete.");
}

// ===================== Main Loop =====================

void loop() {
  switch (currentScreen) {
    case SCREEN_CONTROL: loopControl(); break;
    case SCREEN_VIDEO:   loopVideo();   break;
    case SCREEN_IMU:     loopIMU();     break;
  }
}

// ---------- CONTROL SCREEN ----------

void loopControl() {
  // Motor auto-stop + transition to video
  if (motorRunning && (millis() - motorStartTime >= MOTOR_DURATION)) {
    Serial.println("Motors done — starting animation.");
    motorRunning = false;
    motorFinished = false;
    stopMotor();
    if (frameCount > 0) {
      currentScreen = SCREEN_VIDEO;
      currentFrame = 0;
      return;
    } else {
      // No frames available, just go back to idle
      drawControlScreen();
    }
  }

  // Update timer bar
  if (motorRunning) drawTimerBar();

  // Touch handling
  lv_coord_t tx, ty;
  if (readTouch(tx, ty)) {
    touchStartX = tx;
    touchStartY = ty;
    touchLastX  = tx;
    touchLastY  = ty;

    // Wait for release, tracking movement
    while (chsc6x_is_pressed()) {
      chsc6x_get_xy(&touchLastX, &touchLastY);
      if (motorRunning) drawTimerBar();
      // Check motor timeout even during touch
      if (motorRunning && (millis() - motorStartTime >= MOTOR_DURATION)) {
        motorRunning = false;
        stopMotor();
        if (frameCount > 0) {
          currentScreen = SCREEN_VIDEO;
          currentFrame = 0;
          return;
        }
        drawControlScreen();
      }
      delay(15);
    }

    int dx = touchLastX - touchStartX;
    int dy = touchLastY - touchStartY;

    // Check for swipe LEFT → IMU screen
    if (dx < -SWIPE_THRESHOLD && abs(dy) < abs(dx) && !motorRunning) {
      Serial.println("Swipe left → IMU screen");
      currentScreen = SCREEN_IMU;
      drawIMUScreen();
      lastIMUDraw = millis();
      return;
    }

    // Tap detection (not a swipe)
    if (abs(dx) < SWIPE_THRESHOLD && abs(dy) < SWIPE_THRESHOLD) {
      if (touchStartY < DIVIDER_Y) {
        // GO button
        if (!motorRunning) {
          Serial.println("GO pressed");
          motorRunning = true;
          motorStartTime = millis();
          moveForward();
          drawGoButton(true);
          drawDivider();
        }
      } else {
        // STOP button
        if (motorRunning) {
          Serial.println("STOP pressed");
          motorRunning = false;
          stopMotor();
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

// ---------- VIDEO SCREEN ----------

void loopVideo() {
  // Show current frame
  if (!showFrame(currentFrame)) {
    Serial.printf("Frame %d decode failed, skipping.\n", currentFrame);
  }
  currentFrame = (currentFrame + 1) % frameCount;

  // Check for double-tap to exit
  lv_coord_t tx, ty;
  if (readTouch(tx, ty)) {
    // Wait for release
    while (chsc6x_is_pressed()) delay(10);

    unsigned long now = millis();
    if (waitingDoubleTap && (now - lastTapTime < DOUBLE_TAP_WINDOW)) {
      // Double tap detected → back to control
      Serial.println("Double-tap → back to control screen");
      waitingDoubleTap = false;
      currentScreen = SCREEN_CONTROL;
      drawControlScreen();
      return;
    } else {
      waitingDoubleTap = true;
      lastTapTime = now;
    }
  }

  // Reset double-tap window if expired
  if (waitingDoubleTap && (millis() - lastTapTime >= DOUBLE_TAP_WINDOW)) {
    waitingDoubleTap = false;
  }
}

// ---------- IMU SCREEN ----------

void loopIMU() {
  // Update values at fixed rate
  if (millis() - lastIMUDraw >= IMU_DRAW_INTERVAL) {
    updateIMUValues();
    lastIMUDraw = millis();
  }

  // Touch → check for swipe RIGHT back to control
  lv_coord_t tx, ty;
  if (readTouch(tx, ty)) {
    touchStartX = tx;
    touchLastX  = tx;
    touchLastY  = ty;
    while (chsc6x_is_pressed()) {
      chsc6x_get_xy(&touchLastX, &touchLastY);
      delay(15);
    }
    int dx = touchLastX - touchStartX;
    int dy = touchLastY - touchStartY;
    if (dx > SWIPE_THRESHOLD && abs(dy) < abs(dx)) {
      Serial.println("Swipe right → back to control screen");
      currentScreen = SCREEN_CONTROL;
      drawControlScreen();
      return;
    }
  }

  delay(10);
}
