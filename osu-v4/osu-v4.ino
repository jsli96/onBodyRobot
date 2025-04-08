/***************************************************************************
 * New Program: Rotating Display with Swipe and Motor Control
 *
 * Features:
 *  - Continuously rotates the displayed image based on the IMU’s tilt.
 *  - Supports 3 images stored in PROGMEM; swiping left/right cycles through them.
 *    When you lift your finger (after a brief debounce delay), the program
 *    calculates the difference between the starting and ending touch coordinates.
 *    If the horizontal difference exceeds a set threshold, it cycles to the next
 *    or previous image.
 *  - Includes motor control functions via an MCP23017 (for future integration).
 *  - Uses CHSC6x-based touch functions from lv_xiao_round_screen.h.
 *  - WiFi functionality is omitted for now.
 *
 * Note: Ensure that "my_images.h" defines images[], imageSizes[], and imageCount.
 ***************************************************************************/

#include <TFT_eSPI.h>
#include <Wire.h>
#include <SPI.h>
#define USE_TFT_ESPI_LIBRARY
#include "lv_xiao_round_screen.h"  // Provides touch functions: chsc6x_is_pressed(), chsc6x_get_xy()
#include <PNGdec.h>
#include <Adafruit_MCP23X17.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "ICM_20948.h"     // IMU library (using I2C)
//#define USE_SPI         // Uncomment if using SPI for the IMU

#include <Adafruit_NeoPixel.h>

// Hardware definitions
#define SERIAL_PORT Serial
#define WIRE_PORT   Wire
#define AD0_VAL     1
#define SPI_PORT    SPI
#define CS_PIN      1

#define LED_PIN   D2      // For NeoPixel LED
#define NUM_LEDS  1       // Adjust if needed

#ifdef USE_SPI
ICM_20948_SPI myICM;
#else
ICM_20948_I2C myICM;
#endif

IPAddress local_IP(192, 168, 1, 50);  // Choose an IP not likely in use (e.g., 192.168.1.50)
IPAddress gateway(192, 168, 1, 1);     // Typically your router's IP address
IPAddress subnet(255, 255, 255, 0);    // Standard subnet mask for a /24 network
IPAddress primaryDNS(8, 8, 8, 8);       // Public DNS (optional)
IPAddress secondaryDNS(8, 8, 4, 4);     // Public DNS (optional)


const char* ssid = "TP-LINK_lab0102";
const char* password = "LAB0102!!!";

WiFiServer server(3333);  // Listen on port 3333
PNG png;
Adafruit_MCP23X17 mcp;         // MCP23017 for motor control
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Image handling
#include "my_images.h" // Ensure this file defines images[], imageSizes[], and imageCount (3 images)

// Global variables for image display
int currentImageIndex = 0;
static uint16_t rawImage[240 * 240]; // Framebuffer for a 240x240 image
static int imgWidth = 0, imgHeight = 0;

float lastAngle = -1;
unsigned long lastRotationUpdate = 0;
const int ROTATION_UPDATE_INTERVAL = 150; // ms

// --- Touch (swipe) state machine ---
enum TouchState { IDLE, TOUCH_ACTIVE, TOUCH_RELEASED, GESTURE_PROCESSED };
TouchState touchState = IDLE;
unsigned long releaseTime = 0;         // Time when touch was first detected as released
const unsigned long RELEASE_DEBOUNCE_MS = 50;  // Wait 50 ms after release before processing swipe

lv_coord_t startX = 0, startY = 0;  // Where the touch started
lv_coord_t lastX = 0, lastY = 0;    // Latest touch coordinates
const int SWIPE_THRESHOLD = 30;     // Minimum horizontal distance (pixels) for a valid swipe

// Motor control variables
unsigned long motorCommandStart = 0;
unsigned long motorDuration = 0;
bool motorActive = false;
#define MOTOR_IN1 14  // MCP23017 pin for Motor IN1
#define MOTOR_IN2 15  // MCP23017 pin for Motor IN2

//-------------------------------------------------------------
// PNGDrawCallback: Decodes one line of the PNG into rawImage.
void PNGDrawCallback(PNGDRAW *pDraw) {
  if (pDraw->y >= 240) return;
  uint16_t *dest = &rawImage[pDraw->y * imgWidth];
  png.getLineAsRGB565(pDraw, dest, PNG_RGB565_BIG_ENDIAN, 0xFFFFFFFF);
}

//-------------------------------------------------------------
// showImage: Decodes a PROGMEM-stored image into rawImage.
void showImage(int index) {
  SERIAL_PORT.println("Decoding image...");
  int rc = png.openFLASH((uint8_t *)images[index], imageSizes[index], PNGDrawCallback);
  if (rc == PNG_SUCCESS) {
    imgWidth = png.getWidth();
    imgHeight = png.getHeight();
    rc = png.decode(nullptr, PNG_FAST_PALETTE);
    if (rc == PNG_SUCCESS) {
      SERIAL_PORT.println("Image decoded successfully.");
    } else {
      SERIAL_PORT.print("PNG decode failed: ");
      SERIAL_PORT.println(rc);
    }
  } else {
    SERIAL_PORT.print("PNG open failed: ");
    SERIAL_PORT.println(rc);
  }
}

//-------------------------------------------------------------
// drawRotated: Rotates the image by angleDeg and updates the display.
void drawRotated(float angleDeg) {
  float angleRad = angleDeg * (PI / 180.0);
  float cosA = cos(angleRad);
  float sinA = sin(angleRad);
  float cx = imgWidth * 0.5;
  float cy = imgHeight * 0.5;
  float screenCx = 120.0, screenCy = 120.0;
  static uint16_t lineBuf[240];
  
  for (int yS = 0; yS < 240; yS++) {
    for (int xS = 0; xS < 240; xS++) {
      float dx = xS - screenCx;
      float dy = yS - screenCy;
      float xSrcF = cosA * dx + sinA * dy + cx;
      float ySrcF = -sinA * dx + cosA * dy + cy;
      int xSrc = (int)(xSrcF + 0.5);
      int ySrc = (int)(ySrcF + 0.5);
      if (xSrc < 0 || xSrc >= imgWidth || ySrc < 0 || ySrc >= imgHeight) {
        lineBuf[xS] = 0x0000;
      } else {
        lineBuf[xS] = rawImage[ySrc * imgWidth + xSrc];
      }
    }
    tft.pushImage(0, yS, 240, 1, lineBuf);
    yield();
  }
}

//-------------------------------------------------------------
// readTiltAngle: Reads the IMU to compute a tilt angle.
float readTiltAngle() {
  if (myICM.dataReady()) {
    myICM.getAGMT();
  }
  float ax = myICM.accX();
  float ay = myICM.accY();
  float az = myICM.accZ();
  float roll = atan2(ay, az) * 180.0 / PI;
  float angle = roll + 180.0;
  if (angle < 0) angle += 360.0;
  if (angle >= 360.0) angle -= 360.0;
  return angle;
}

//-------------------------------------------------------------
// processSwipe: Called once when a stable release is detected.
// It calculates the difference between the starting and ending positions,
// and if the horizontal difference exceeds SWIPE_THRESHOLD, it cycles images.
void processSwipe() {
  int deltaX = lastX - startX;
  if (deltaX > SWIPE_THRESHOLD) {
    // Finger moved right → next image.
    currentImageIndex++;
    if (currentImageIndex >= imageCount) currentImageIndex = 0;
    showImage(currentImageIndex);
    float angle = readTiltAngle();
    drawRotated(-angle);
    SERIAL_PORT.println("Swipe Right -> Next Image");
  } else if (deltaX < -SWIPE_THRESHOLD) {
    // Finger moved left → previous image.
    currentImageIndex--;
    if (currentImageIndex < 0) currentImageIndex = imageCount - 1;
    showImage(currentImageIndex);
    float angle = readTiltAngle();
    drawRotated(-angle);
    SERIAL_PORT.println("Swipe Left -> Previous Image");
  } else {
    SERIAL_PORT.println("No valid swipe detected.");
  }
}

//-------------------------------------------------------------
// Motor Control Functions
void setupMotorControl() {
  if (!mcp.begin_I2C(0x20)) {
    Serial.println("Failed to initialize MCP23017!");
    while (1) { yield(); }
  }
  mcp.pinMode(MOTOR_IN1, OUTPUT);
  mcp.pinMode(MOTOR_IN2, OUTPUT);
  stopMotor();
  SERIAL_PORT.println("Motor control initialized.");
}

void moveForward() {
  mcp.digitalWrite(MOTOR_IN1, HIGH);
  mcp.digitalWrite(MOTOR_IN2, LOW);
}

void moveBackward() {
  mcp.digitalWrite(MOTOR_IN1, LOW);
  mcp.digitalWrite(MOTOR_IN2, HIGH);
}

void stopMotor() {
  mcp.digitalWrite(MOTOR_IN1, LOW);
  mcp.digitalWrite(MOTOR_IN2, LOW);
}

//-------------------------------------------------------------
// setup: Initializes display, IMU, touch, motor, and loads the first image.
void setup() {
  Serial.begin(115200);
  Serial.println("Setup starting...");

  // Configure static IP
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("Static IP configuration failed");
  }

  // Begin WiFi connection
  WiFi.begin(ssid, password);
  delay(500);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    yield();  // Yield during WiFi connection loop
  }
  Serial.println("\nConnected!");
  Serial.println(WiFi.localIP());

  // Disable WiFi power saving (light sleep)
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Start server
  server.begin();

  Serial.print("Setup task running on core ");
  Serial.println(xPortGetCoreID());

  // Initialize TFT display
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  // Initialize touch
  pinMode(TOUCH_INT, INPUT_PULLUP);
  Wire.begin();

  // Initialize IMU
  WIRE_PORT.begin();
  WIRE_PORT.setClock(400000);
  myICM.begin(WIRE_PORT, AD0_VAL);
  if (myICM.status != ICM_20948_Stat_Ok) {
    SERIAL_PORT.println("IMU init failed!");
  } else {
    SERIAL_PORT.println("IMU init OK!");
  }

  // Initialize motor control
  setupMotorControl();

  // --- Initialize LED strip ---
  strip.begin();
  // Set the first LED (index 0) to red (RGB: 255, 0, 0)
  strip.setPixelColor(0, strip.Color(255, 0, 0));
  strip.show();

  // Load the first image
  showImage(currentImageIndex);
}

void processSerialCommand(String cmd) {
  cmd.trim();
  // Reset previous motor command immediately.
  motorActive = false;

  // if (cmd.startsWith("CHECK_COLOR:")) {
  //   String expected = cmd.substring(String("CHECK_COLOR:").length());
  //   String current = getCurrentLEDColor();  // You’ll define this below
  //   if (expected.equalsIgnoreCase(current)) {
  //     Serial.println("LED color matches: " + expected);
  //   } else {
  //     Serial.println("LED color mismatch. Expected: " + expected + ", Found: " + current);
  //   }
  //   return;
  // }
  
  if (cmd.startsWith("moveForward(")) {
    int startIdx = cmd.indexOf('(');
    int endIdx = cmd.indexOf(')');
    String durationStr = cmd.substring(startIdx + 1, endIdx);
    motorDuration = durationStr.toInt(); // Duration in ms
    moveForward();
    Serial.print("Executed moveForward for ");
    Serial.print(motorDuration);
    Serial.println(" ms");
    motorCommandStart = millis();
    motorActive = true;
  } else if (cmd.startsWith("moveBackward(")) {
    int startIdx = cmd.indexOf('(');
    int endIdx = cmd.indexOf(')');
    String durationStr = cmd.substring(startIdx + 1, endIdx);
    motorDuration = durationStr.toInt();
    moveBackward();
    Serial.print("Executed moveBackward for ");
    Serial.print(motorDuration);
    Serial.println(" ms");
    motorCommandStart = millis();
    motorActive = true;
  } else if (cmd == "moveForward") {
    motorDuration = 5000;
    moveForward();
    Serial.print("Executed moveForward for default duration ");
    Serial.print(motorDuration);
    Serial.println(" ms");
    motorCommandStart = millis();
    motorActive = true;
  } else if (cmd == "moveBackward") {
    motorDuration = 5000;
    moveBackward();
    Serial.print("Executed moveBackward for default duration ");
    Serial.print(motorDuration);
    Serial.println(" ms");
    motorCommandStart = millis();
    motorActive = true;
  } else if (cmd == "stopMotor") {
    stopMotor();
    Serial.println("Executed stopMotor()");
    motorActive = false;
  } else {
    Serial.print("Unrecognized command: ");
    Serial.println(cmd);
  }
}


//-------------------------------------------------------------
// loop: Main program loop with touch state machine.
void loop() {
  bool rawPressed = chsc6x_is_pressed();
  
  // Update state machine:
  switch(touchState) {
    case IDLE:
      if (rawPressed) {
        // Finger has just touched: record start coordinates.
        chsc6x_get_xy(&startX, &startY);
        touchState = TOUCH_ACTIVE;
      }
      break;
      
    case TOUCH_ACTIVE:
      if (rawPressed) {
        // While touching, update current coordinates.
        chsc6x_get_xy(&lastX, &lastY);
      } else {
        // Finger just lifted: record release time.
        releaseTime = millis();
        touchState = TOUCH_RELEASED;
      }
      break;
      
    case TOUCH_RELEASED:
      // Wait until the sensor remains off for RELEASE_DEBOUNCE_MS.
      if (!rawPressed && (millis() - releaseTime >= RELEASE_DEBOUNCE_MS)) {
        // Final coordinates.
        chsc6x_get_xy(&lastX, &lastY);
        processSwipe();   // Process the swipe gesture once.
        touchState = GESTURE_PROCESSED;
      } else if (rawPressed) {
        // If the finger comes back quickly, consider it still active.
        touchState = TOUCH_ACTIVE;
      }
      break;
      
    case GESTURE_PROCESSED:
      // Wait until the sensor stays off for a while before returning to IDLE.
      if (!rawPressed && (millis() - releaseTime >= (RELEASE_DEBOUNCE_MS + 200))) {
        touchState = IDLE;
      }
      // Alternatively, if the finger touches again, go to TOUCH_ACTIVE.
      if (rawPressed) {
        chsc6x_get_xy(&startX, &startY);
        touchState = TOUCH_ACTIVE;
      }
      break;
  }
  
  // --- Continuous Rotation Update ---
  unsigned long currentTime = millis();
  if (currentTime - lastRotationUpdate > ROTATION_UPDATE_INTERVAL) {
    float angle = readTiltAngle();
    if (fabs(angle - lastAngle) > 5.0) {
      drawRotated(-angle);
      lastAngle = angle;
    }
    lastRotationUpdate = currentTime;
  }
  
  // --- Motor Control (for future integration) ---
  if (motorActive && (millis() - motorCommandStart >= motorDuration)) {
    stopMotor();
    motorActive = false;
    SERIAL_PORT.println("Motor command duration elapsed; motor stopped.");
  }

  // Handle incoming WiFi client commands (non-blocking):
  WiFiClient client = server.available();
  if (client) {
    Serial.println("TCP client connected.");
    String command = "";
    while (client.connected()) {
      while (client.available()) {
        char c = client.read();
        if (c == '\n') {
          command.trim();
          Serial.print("Received over WiFi: ");
          Serial.println(command);
          processSerialCommand(command);
          command = "";
        } else {
          command += c;
        }
      }
      yield();  // Yield instead of delay(1) to allow background tasks to run.
    }
    client.stop();
    Serial.println("TCP client disconnected.");
  }
}