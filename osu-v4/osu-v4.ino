#include <TFT_eSPI.h>
#include <Wire.h>
#include <SPI.h>
#define USE_TFT_ESPI_LIBRARY
#include "lv_xiao_round_screen.h"  // For CHSC6x-based touch
#include <PNGdec.h>
#include <Adafruit_MCP23X17.h>
#include <WiFi.h>
#include "esp_wifi.h"


Adafruit_MCP23X17 mcp;

// ICM-20948
#include "ICM_20948.h"
//#define USE_SPI // Uncomment if using SPI

#define SERIAL_PORT Serial
#define WIRE_PORT   Wire
#define AD0_VAL     1
#define SPI_PORT    SPI
#define CS_PIN      1

#include <Adafruit_NeoPixel.h>

#define LED_PIN   D2    // Change to D2 if needed
#define NUM_LEDS  1     // Number of WS2812B LEDs

#ifdef USE_SPI
ICM_20948_SPI myICM;
#else
ICM_20948_I2C myICM;
#endif

PNG png;

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

// Static IP configuration based on your iPhone hotspot settings:
IPAddress local_IP(172, 20, 10, 10);   // Choose an available IP (example: 172.20.10.10)
IPAddress gateway(172, 20, 10, 1);       // Likely your hotspot's IP
IPAddress subnet(255, 255, 255, 240);    // As seen from the netmask
IPAddress primaryDNS(8, 8, 8, 8);         // Public DNS (optional)
IPAddress secondaryDNS(8, 8, 4, 4);       // Public DNS (optional)

const char* ssid = "atp236";
const char* password = "88888888";

WiFiServer server(3333);  // Listen on port 3333

unsigned long motorCommandStart = 0;
unsigned long motorDuration = 0;  // Duration (in ms) for the current motor command
bool motorActive = false;

String incomingCommand = "";

// For reading touch
bool lastPressed = false;
lv_coord_t startX = 0, startY = 0;
lv_coord_t lastX  = 0, lastY  = 0;

// We'll keep track of which image is displayed
int currentImageIndex = 0;

// Include your images in PROGMEM
#include "my_images.h" // e.g. image1.h, image2.h, image3.h, etc.

// 240x240 Buffer to hold the decoded PNG (unrotated).
static uint16_t rawImage[240 * 240]; // ~112 KB in 16-bit color

// Dimensions of the current image (from PNG header)
static int imgWidth  = 0;
static int imgHeight = 0;

// A small struct for PNGdec callback
typedef struct {
  bool convertTo565;
} USERDATA;

const char* calico_version = "osu-v4";  // Make sure this matches file name

#define MOTOR_IN1 14  // MCP23017 Pin D3
#define MOTOR_IN2 15  // MCP23017 Pin D4

// Calico movement state variable
String moveStatus = "stop";

// Global vars for lighting task
String ledStatus = "off";
int rrr = 0;
int ggg = 0;
int bbb = 0;
int br = 0;  // blink rate in msecs (really this is the duration of half a cycle)

float lastAngle = -1;
unsigned long lastRotationUpdate = 0;
const int ROTATION_UPDATE_INTERVAL = 150; // ms

//----------------------------------------------------------------------------------
// PNG Callback: decode each line into rawImage[]
//----------------------------------------------------------------------------------
void PNGDrawCallback(PNGDRAW *pDraw)
{
  if (pDraw->y >= 240) return; // safety check

  uint16_t *dest = &rawImage[pDraw->y * imgWidth];
  png.getLineAsRGB565(pDraw, dest, PNG_RGB565_BIG_ENDIAN, 0xFFFFFFFF);
}

//----------------------------------------------------------------------------------
// Show image: decode the PNG into rawImage[] (no rotation yet)
//----------------------------------------------------------------------------------
void showImage(int index) {
  SERIAL_PORT.println("Attempting to decode image...");
  int rc = png.openFLASH((uint8_t *)images[index], imageSizes[index], PNGDrawCallback);
  if (rc == PNG_SUCCESS) {
    imgWidth  = png.getWidth();
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

//----------------------------------------------------------------------------------
// Check Swipe
//----------------------------------------------------------------------------------
void checkSwipe() {
  const int SWIPE_THRESHOLD = 30;
  int deltaX = lastX - startX;
  if (deltaX > SWIPE_THRESHOLD) {
    currentImageIndex--;
    if (currentImageIndex < 0) currentImageIndex = imageCount - 1;
    showImage(currentImageIndex);
    float angle = readTiltAngle();
    drawRotated(-angle);
    lastAngle = angle;
    SERIAL_PORT.println("Swipe Right -> Previous Image");
  }
  else if (deltaX < -SWIPE_THRESHOLD) {
    currentImageIndex++;
    if (currentImageIndex >= imageCount) currentImageIndex = 0;
    showImage(currentImageIndex);
    float angle = readTiltAngle();
    drawRotated(-angle);
    lastAngle = angle;
    SERIAL_PORT.println("Swipe Left -> Next Image");
  }
  else {
    SERIAL_PORT.println("No valid swipe detected.");
  }
}

//----------------------------------------------------------------------------------
// Compute tilt angle from accelerometer
//----------------------------------------------------------------------------------
float readTiltAngle() {
  if (myICM.dataReady()) {
    myICM.getAGMT(); // update accelerometer
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

//----------------------------------------------------------------------------------
// drawRotated(angle):
//   Modified to yield after each row to avoid blocking for too long.
//----------------------------------------------------------------------------------
void drawRotated(float angleDeg)
{
  float angleRad = angleDeg * (PI / 180.0);
  float cosA = cos(angleRad);
  float sinA = sin(angleRad);
  float cx = imgWidth  * 0.5;
  float cy = imgHeight * 0.5;
  float screenCx = 120.0;
  float screenCy = 120.0;
  static uint16_t lineBuf2[240];

  for (int yS = 0; yS < 240; yS++) {
    for (int xS = 0; xS < 240; xS++) {
      float dx = xS - screenCx;
      float dy = yS - screenCy;
      float xSrcF =  cosA * dx + sinA * dy + cx;
      float ySrcF = -sinA * dx + cosA * dy + cy;
      int xSrc = (int)(xSrcF + 0.5f);
      int ySrc = (int)(ySrcF + 0.5f);
      if (xSrc < 0 || xSrc >= imgWidth || ySrc < 0 || ySrc >= imgHeight) {
        lineBuf2[xS] = 0x0000; // black
      } else {
        lineBuf2[xS] = rawImage[ySrc * imgWidth + xSrc];
      }
    }
    tft.pushImage(0, yS, 240, 1, lineBuf2);
    // Yield after drawing each row so background tasks can run.
    yield();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("Setup starting...");

  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("Static IP configuration failed");
  }

  WiFi.begin(ssid, password);
  delay(500);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    yield();  // Yield during WiFi connection loop
  }
  Serial.println("Connected!");
  Serial.println(WiFi.localIP());

  // Disable WiFi power saving (light sleep)
  esp_wifi_set_ps(WIFI_PS_NONE);

  server.begin();

  Serial.print("Setup task running on core ");
  Serial.println(xPortGetCoreID());

  #ifdef USE_SPI
    SPI_PORT.begin();
    myICM.begin(CS_PIN, SPI_PORT);
  #else
    WIRE_PORT.begin();
    WIRE_PORT.setClock(400000);
    myICM.begin(WIRE_PORT, AD0_VAL);
  #endif

  if (myICM.status != ICM_20948_Stat_Ok) {
    SERIAL_PORT.println("ICM init failed!");
  } else {
    SERIAL_PORT.println("ICM init OK!");
  }

  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  pinMode(TOUCH_INT, INPUT_PULLUP);

  showImage(currentImageIndex);
  setupMotorControl();
}

void setupMotorControl() {
    if (!mcp.begin_I2C(0x20)) {  // Initialize MCP23017 at address 0x20
        Serial.println("Failed to initialize MCP23017!");
        while (1) { yield(); }  // Prevent blocking indefinitely
    }
    mcp.pinMode(14, OUTPUT);  // D3 (Motor_IN1)
    mcp.pinMode(15, OUTPUT);  // D4 (Motor_IN2)
    strip.begin();
    strip.show();
    stopMotor(); // Ensure motor is off initially
    Serial.println("Motor control initialized!");
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

String getCurrentLEDColor() {
  char buf[8];
  sprintf(buf, "#%02X%02X%02X", rrr, ggg, bbb);  // Assuming rrr/ggg/bbb are your current RGB values
  return String(buf);
}


void processSerialCommand(String cmd) {
  cmd.trim();
  // Reset previous motor command immediately.
  motorActive = false;

  if (cmd.startsWith("CHECK_COLOR:")) {
    String expected = cmd.substring(String("CHECK_COLOR:").length());
    String current = getCurrentLEDColor();  // You’ll define this below
    if (expected.equalsIgnoreCase(current)) {
      Serial.println("LED color matches: " + expected);
    } else {
      Serial.println("LED color mismatch. Expected: " + expected + ", Found: " + current);
    }
    return;
  }
  
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

// Global variables for non-blocking LED fill
bool ledFilling = false;
uint32_t targetLEDColor = 0;
uint8_t ledWaitTime = 50; // time per LED update in ms
int currentLED = 0;
unsigned long lastLedUpdate = 0;

void startColorFill(uint32_t color, uint8_t wait) {
  targetLEDColor = color;
  ledWaitTime = wait;
  currentLED = 0;
  ledFilling = true;
  lastLedUpdate = millis();

  // ✅ Set global RGB values
  rrr = (color >> 16) & 0xFF;
  ggg = (color >> 8) & 0xFF;
  bbb = color & 0xFF;
  
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, 0);
  }
  strip.show();
}

void updateColorFill() {
  if (ledFilling && (millis() - lastLedUpdate >= ledWaitTime)) {
    strip.setPixelColor(currentLED, targetLEDColor);
    strip.show();
    currentLED++;
    lastLedUpdate = millis();
    if (currentLED >= strip.numPixels()) {
      ledFilling = false;
    }
  }
}

void loop() {
  bool isPressed = chsc6x_is_pressed();
  if (isPressed) {
    lv_coord_t x, y;
    chsc6x_get_xy(&x, &y);
    if (!lastPressed) {
      startX = x;
      startY = y;
    }
    lastX = x;
    lastY = y;
  } else {
    if (lastPressed) {
      checkSwipe();
    }
  }
  lastPressed = isPressed;

  unsigned long currentTime = millis();
  if (!isPressed && (currentTime - lastRotationUpdate > ROTATION_UPDATE_INTERVAL)) {
    float angle = readTiltAngle();
    if (fabs(angle - lastAngle) > 5.0) {
      drawRotated(-angle);
      lastAngle = angle;
      lastRotationUpdate = currentTime;
    }
  }

  if (motorActive && (millis() - motorCommandStart >= motorDuration)) {
    stopMotor();
    motorActive = false;
    Serial.println("Motor command duration elapsed; motor stopped.");
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

  updateColorFill();

  static unsigned long lastFillChange = 0;
  if (millis() - lastFillChange > 500) {
    static bool toggle = false;
    if (toggle) {
      startColorFill(strip.Color(255, 0, 0), 50);
    } else {
      startColorFill(strip.Color(0, 255, 0), 50);
    }
    toggle = !toggle;
    lastFillChange = millis();
  }
}
