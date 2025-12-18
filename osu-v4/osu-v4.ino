/***************************************************************************
 * Rotating Display with Swipe and Motor Control (PCB pinout)
 * Minimal changes:
 *  - I2C pins explicit (SDA=21, SCL=22)
 *  - IMU at 0x68 (AD0_VAL = 0)
 *  - Motors on MCP23017 Port B0..B3 => pins 8..11
 *  - Single Wire.begin(...) before IMU/MCP/touch
 *  - No hard-block if MCP missing
 *  - NEW: draw first image on boot and right after uploads
 ***************************************************************************/

#include <TFT_eSPI.h>
#include <Wire.h>
#include <SPI.h>
#define USE_TFT_ESPI_LIBRARY
#include "lv_xiao_round_screen.h"  // Touch functions
#include <PNGdec.h>
#include <Adafruit_MCP23X17.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "ICM_20948.h"     // IMU library (I2C)

#include <FastLED.h>
#include "SPIFFS.h"
#include <vector>
#include <ESPAsyncWebServer.h>
#include "esp_log.h"  

// --------------------- Hardware Definitions ---------------------
#define SERIAL_PORT         Serial
#define WIRE_PORT           Wire

// I2C pins discovered via scan() using default Wire on ESP32/S3
#define SDA_PIN 21
#define SCL_PIN 22

// IMU at 0x68 => AD0 = 0 
#undef AD0_VAL
#define AD0_VAL 0

#define SPI_PORT            SPI
#define CS_PIN              1

#define LED_PIN             D2       // For NeoPixel LED (unchanged)
#define NUM_LEDS            1        // Adjust if needed

#ifdef USE_SPI
ICM_20948_SPI myICM;
#else
ICM_20948_I2C myICM;
#endif

// --------------------- Network Settings ---------------------
IPAddress local_IP(192, 168, 1, 50);   // ESP32 IP
IPAddress gateway(192, 168, 1, 1);      // Router IP
IPAddress subnet(255, 255, 255, 0);     // Subnet mask
IPAddress primaryDNS(8, 8, 8, 8);        // DNS
IPAddress secondaryDNS(8, 8, 4, 4);      // Secondary DNS

const char* ssid = "Verizon_B4TKXX";
const char* password = "fox3-veto-dun";

// --------------------- Global Variables ---------------------
std::vector<String> imageFiles;  // To store filenames found in SPIFFS

// Create an AsyncWebServer instance for uploads on port 80.
AsyncWebServer espServer(80);
// WiFiServer for additional commands.
WiFiServer server(3333);

PNG png;  
Adafruit_MCP23X17 mcp;  // MCP23017 for motor control

// MCP23017 Port B pins for motors (B0..B3 => 8..11)
#define MCP_M1_IN1 8    // B0
#define MCP_M1_IN2 9    // B1
#define MCP_M2_IN1 10   // B2
#define MCP_M2_IN2 11   // B3
bool mcp_ok = false;

// FastLED global array.
CRGB leds[NUM_LEDS];

enum LEDCommand {
  LED_NONE,
  LED_RED,
  LED_GREEN,
  LED_BLUE
};

volatile LEDCommand pendingLEDCommand = LED_NONE;
volatile bool newFileUploaded = false;  // Set when a file is fully uploaded.
volatile bool isDecoding = false; // Indicates if a decode is currently in progress.
String lastUploadedFilename = "";         // To track the last file processed.

// For dynamic image display.
int currentImageIndex = 0;
static uint16_t rawImage[240 * 240];  // Framebuffer (240x240)
static int imgWidth = 0, imgHeight = 0;
uint32_t currentLEDColor = 0xFF0000;

float lastAngle = -1;
unsigned long lastRotationUpdate = 0;
const int ROTATION_UPDATE_INTERVAL = 150; // ms

// Touch state variables.
enum TouchState { IDLE, TOUCH_ACTIVE, TOUCH_RELEASED, GESTURE_PROCESSED };
TouchState touchState = IDLE;
unsigned long releaseTime = 0;
const unsigned long RELEASE_DEBOUNCE_MS = 50;
lv_coord_t startX = 0, startY = 0;
lv_coord_t lastX = 0, lastY = 0;
const int SWIPE_THRESHOLD = 30;

// Motor control variables.
unsigned long motorCommandStart = 0;
unsigned long motorDuration = 0;
bool motorActive = false;

// --------------------- ESP32 Upload Endpoint Functions ---------------------

void handleUpload(AsyncWebServerRequest *request, String filename,
                  size_t index, uint8_t *data, size_t len, bool final) {
  if (!index) {  
    Serial.printf("Upload Start: %s\n", filename.c_str());
  }
  fs::File file = SPIFFS.open("/" + filename, FILE_APPEND);
  if (file) {
    file.write(data, len);
    file.close();
  } else {
    Serial.println("Failed to open file for writing");
  }
  
  if (final) {  // Entire file received.
    Serial.printf("Upload Finished: %s\n", filename.c_str());
    if (lastUploadedFilename != filename) {
      lastUploadedFilename = filename;
      newFileUploaded = true;
    }
  }
}

void setupESP32UploadEndpoint() {
  espServer.on("/upload_image", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Upload complete");
  }, handleUpload);
  espServer.begin();
}

// --------------------- File Scanning & Image Loading ---------------------

void resetImageIndex() { currentImageIndex = 0; }

void scanForImageFiles() {
  imageFiles.clear();
  fs::File root = SPIFFS.open("/");
  if (!root) {
    Serial.println("Failed to open SPIFFS root directory");
    return;
  }
  fs::File file = root.openNextFile();
  while (file) {
    String fileName = file.name();
    size_t fileSize = file.size();
    Serial.printf("Found file: %s (size: %d bytes)\n", fileName.c_str(), fileSize);
    if (fileName.endsWith(".png") && fileSize > 0) {
      imageFiles.push_back(fileName);
    } else {
      Serial.printf("File %s is ignored (non-image or size 0).\n", fileName.c_str());
    }
    file = root.openNextFile();
    yield();
  }
}

int PNGDrawCallback(PNGDRAW *pDraw) {
  if (pDraw->y >= 240) return 1;

  uint16_t *dest = &rawImage[pDraw->y * 240];

  png.getLineAsRGB565(pDraw, dest, PNG_RGB565_BIG_ENDIAN, 0xFFFFFFFF);

  return 1;
}

void showImageFromSPIFFS_Stream(const char* filename) {
  if (isDecoding) {
    Serial.println("Decoder busy. Ignoring new decode request.");
    return;
  }
  isDecoding = true;

  String fullPath = String(filename);
  if (!fullPath.startsWith("/")) fullPath = "/" + fullPath;
  
  int rc = png.open(fullPath.c_str(), myPNGOpen, myPNGClose, myPNGRead, myPNGSeek, PNGDrawCallback);
  if (rc != PNG_SUCCESS) {
    Serial.printf("PNG openFS failed: %d\n", rc);
    png.close(); isDecoding = false; return;
  }
  
  imgWidth = png.getWidth();
  imgHeight = png.getHeight();
  rc = png.decode(nullptr, PNG_FAST_PALETTE);
  if (rc != PNG_SUCCESS) {
    Serial.printf("PNG decode failed for %s: %d\n", filename, rc);
    png.close(); isDecoding = false; return;
  }
  
  if (imgWidth <= 0 || imgHeight <= 0) {
    Serial.printf("Displayed image %s has invalid dimensions (w:%d, h:%d)\n", filename, imgWidth, imgHeight);
  } else {
    Serial.printf("Displayed image %s (w:%d, h:%d)\n", filename, imgWidth, imgHeight);
  }

  png.close();
  isDecoding = false;
}

void showImage(int index) {
  if (index < 0 || index >= (int)imageFiles.size()) {
    Serial.println("Index out of range");
    return;
  }
  const char* filename = imageFiles[index].c_str();
  showImageFromSPIFFS_Stream(filename);
}

// --------------------- LED Functions ---------------------

void setLEDColorRed()   { leds[0] = CRGB::Red;   currentLEDColor = 0xFF0000; Serial.print("DEBUG: RED 0x");   Serial.println(currentLEDColor, HEX); FastLED.show(); }
void setLEDColorGreen() { leds[0] = CRGB::Green; currentLEDColor = 0x00FF00; Serial.print("DEBUG: GREEN 0x"); Serial.println(currentLEDColor, HEX); FastLED.show(); }
void setLEDColorBlue()  { leds[0] = CRGB::Blue;  currentLEDColor = 0x0000FF; Serial.print("DEBUG: BLUE 0x");  Serial.println(currentLEDColor, HEX); FastLED.show(); }

String getCurrentLEDColor() {
  char buffer[8];
  sprintf(buffer, "#%06X", (unsigned int)(currentLEDColor & 0xFFFFFF));
  String colorStr = String(buffer);
  Serial.print("DEBUG: getCurrentLEDColor() returning "); Serial.println(colorStr);
  return colorStr;
}

// --------------------- Display / Rotation Functions ---------------------

void drawRotated(float angleDeg) {
  float angleRad = angleDeg * (PI / 180.0);
  float cosA = cos(angleRad), sinA = sin(angleRad);
  float cx = imgWidth * 0.5, cy = imgHeight * 0.5;
  float screenCx = 120.0, screenCy = 120.0;
  static uint16_t lineBuf[240];
  
  for (int yS = 0; yS < 240; yS++) {
    for (int xS = 0; xS < 240; xS++) {
      float dx = xS - screenCx, dy = yS - screenCy;
      float xSrcF =  cosA * dx + sinA * dy + cx;
      float ySrcF = -sinA * dx + cosA * dy + cy;
      int xSrc = (int)(xSrcF + 0.5f);
      int ySrc = (int)(ySrcF + 0.5f);
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

// --------------------- IMU Functions ---------------------

float readTiltAngle() {
  static float last = 0.0f;
  if (myICM.status == ICM_20948_Stat_Ok && myICM.dataReady()) {
    myICM.getAGMT();
    float ax = myICM.accX(), ay = myICM.accY(), az = myICM.accZ();
    float roll = atan2(ay, az) * 180.0f / PI;
    if (roll < 0) roll += 360.0f;
    last = roll;
  }
  return last;
}

// --------------------- Swipe Processing ---------------------

void processSwipe() {
  int deltaX = lastX - startX;
  if (deltaX > SWIPE_THRESHOLD) {
    currentImageIndex = (currentImageIndex + 1) % (int)imageFiles.size();
    showImage(currentImageIndex);
    float angle = readTiltAngle();
    drawRotated(-angle);
    SERIAL_PORT.println("Swipe Right -> Next Image");
  } else if (deltaX < -SWIPE_THRESHOLD) {
    currentImageIndex--;
    if (currentImageIndex < 0) currentImageIndex = imageFiles.size() - 1;
    showImage(currentImageIndex);
    float angle = readTiltAngle();
    drawRotated(-angle);
    SERIAL_PORT.println("Swipe Left -> Previous Image");
  } else {
    SERIAL_PORT.println("No valid swipe detected.");
  }
}

// --------------------- Motor Control (MCP23017 B0..B3) ---------------------

void setupMotorControl() {
  mcp_ok = mcp.begin_I2C(0x20);
  if (!mcp_ok) {
    Serial.println("MCP23017 not found at 0x20 (motor disabled).");
    return; 
  }
  mcp.pinMode(MCP_M1_IN1, OUTPUT);
  mcp.pinMode(MCP_M1_IN2, OUTPUT);
  mcp.pinMode(MCP_M2_IN1, OUTPUT);
  mcp.pinMode(MCP_M2_IN2, OUTPUT);
  // stop
  mcp.digitalWrite(MCP_M1_IN1, LOW);
  mcp.digitalWrite(MCP_M1_IN2, LOW);
  mcp.digitalWrite(MCP_M2_IN1, LOW);
  mcp.digitalWrite(MCP_M2_IN2, LOW);
  SERIAL_PORT.println("Motor control initialized (MCP23017 B0..B3).");
}

void moveForward() {
  if (!mcp_ok) return;
  mcp.digitalWrite(MCP_M1_IN1, HIGH);
  mcp.digitalWrite(MCP_M1_IN2, LOW);
  mcp.digitalWrite(MCP_M2_IN1, HIGH);
  mcp.digitalWrite(MCP_M2_IN2, LOW);
}

void moveBackward() {
  if (!mcp_ok) return;
  mcp.digitalWrite(MCP_M1_IN1, LOW);
  mcp.digitalWrite(MCP_M1_IN2, HIGH);
  mcp.digitalWrite(MCP_M2_IN1, LOW);
  mcp.digitalWrite(MCP_M2_IN2, HIGH);
}

void stopMotor() {
  if (!mcp_ok) return;
  mcp.digitalWrite(MCP_M1_IN1, LOW);
  mcp.digitalWrite(MCP_M1_IN2, LOW);
  mcp.digitalWrite(MCP_M2_IN1, LOW);
  mcp.digitalWrite(MCP_M2_IN2, LOW);
}

void listImageFiles() {
  fs::File root = SPIFFS.open("/");
  fs::File file = root.openNextFile();
  while (file) {
    String fileName = file.name();
    if (fileName.endsWith(".png")) {
      Serial.println("Found image: " + fileName);
    }
    file = root.openNextFile();
  }
}

// --------------------- Setup ---------------------

void setup() {
  Serial.begin(115200);
  esp_log_level_set("i2c", ESP_LOG_ERROR); // quiet I2C spam
  Serial.println("Setup starting...");

  if (!SPIFFS.begin(true)) {  
    Serial.println("Failed to mount SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted successfully.");


// OPTIONAL: Clear all images/files stored in SPIFFS
// Uncomment the line below and recompile/upload if you want
// to completely reset SPIFFS (remove all uploaded pictures).
//
// Note: Leaving this uncommented will DELETE files on EVERY boot.
//
// clearSPIFFS();

  scanForImageFiles();
  Serial.printf("Found %d image(s).\n", (int)imageFiles.size());

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println();
  Serial.print("Connected! IP=");      Serial.println(WiFi.localIP());
  Serial.print("Gateway=");           Serial.println(WiFi.gatewayIP());
  Serial.print("Subnet=");            Serial.println(WiFi.subnetMask());
  Serial.print("SSID=");              Serial.println(WiFi.SSID());
  esp_wifi_set_ps(WIFI_PS_NONE);

  setupESP32UploadEndpoint();
  server.begin();
  Serial.print("Setup task running on core "); Serial.println(xPortGetCoreID());

  // TFT
  Serial.println("A: before tft.init");
  tft.init();
  Serial.println("B: after tft.init");
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  // I2C: use default PCB pins 
  Wire.begin();
  Wire.setClock(400000);
  delay(10);

  // Touch
  pinMode(TOUCH_INT, INPUT_PULLUP);

  // IMU (I2C, AD0=0 -> 0x68)
  myICM.begin(Wire, AD0_VAL);
  if (myICM.status != ICM_20948_Stat_Ok) SERIAL_PORT.println("IMU init failed!");
  else SERIAL_PORT.println("IMU init OK!");

  // MCP23017 motors
  setupMotorControl();

  // LED
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  leds[0] = CRGB::Red; FastLED.show();

  // Draw first image immediately if present
  if (!imageFiles.empty()) {
    resetImageIndex();
    showImage(currentImageIndex);
    float angle0 = 0.0f;                 
    drawRotated(-angle0);                
    lastAngle = angle0;                  
  } else {
    Serial.println("No image files found.");
  }
}

// --------------------- Process Serial Commands ---------------------

void processSerialCommand(String cmd, WiFiClient &client) {
  cmd.trim();
  if (cmd == "setRed")   { pendingLEDCommand = LED_RED;   return; }
  if (cmd == "setGreen") { pendingLEDCommand = LED_GREEN; return; }
  if (cmd == "setBlue")  { pendingLEDCommand = LED_BLUE;  return; }
  if (cmd == "GET_LED_COLOR") {
    String currentColor = getCurrentLEDColor();
    Serial.print("DEBUG: GET_LED_COLOR -> "); Serial.println(currentColor);
    client.println(currentColor);
    return;
  }
  if (motorActive) { Serial.println("Motor command already active. Ignoring."); return; }

  if (cmd.startsWith("moveForward(")) {
    int startIdx = cmd.indexOf('('), endIdx = cmd.indexOf(')');
    String durationStr = cmd.substring(startIdx + 1, endIdx);
    motorDuration = durationStr.toInt();
    moveForward();
    Serial.printf("Executed moveForward for %u ms\n", (unsigned)motorDuration);
    motorActive = true; delay(motorDuration); stopMotor(); Serial.println("Motor stopped."); motorActive = false; return;
  } else if (cmd.startsWith("moveBackward(")) {
    int startIdx = cmd.indexOf('('), endIdx = cmd.indexOf(')');
    String durationStr = cmd.substring(startIdx + 1, endIdx);
    motorDuration = durationStr.toInt();
    moveBackward();
    Serial.printf("Executed moveBackward for %u ms\n", (unsigned)motorDuration);
    motorActive = true; delay(motorDuration); stopMotor(); Serial.println("Motor stopped."); motorActive = false; return;
  } else if (cmd == "moveForward") {
    motorDuration = 5000; moveForward(); Serial.printf("moveForward default %u ms\n", (unsigned)motorDuration);
    motorActive = true; delay(motorDuration); stopMotor(); Serial.println("Motor stopped."); motorActive = false; return;
  } else if (cmd == "moveBackward") {
    motorDuration = 5000; moveBackward(); Serial.printf("moveBackward default %u ms\n", (unsigned)motorDuration);
    motorActive = true; delay(motorDuration); stopMotor(); Serial.println("Motor stopped."); motorActive = false; return;
  } else if (cmd == "stopMotor") {
    stopMotor(); Serial.println("Executed stopMotor()"); motorActive = false; return;
  }

  Serial.print("Unrecognized command: "); Serial.println(cmd);
}

// --------------------- Main Loop ---------------------

void loop() {
  if (newFileUploaded) {
    newFileUploaded = false;
    scanForImageFiles();
    resetImageIndex();
    if (!imageFiles.empty()) {
      showImage(currentImageIndex);
      float angle0 = 0.0f;               
      drawRotated(-angle0);             
      lastAngle = angle0;                
    } else {
      Serial.println("No image files found after scanning.");
    }
  }
  
  bool rawPressed = chsc6x_is_pressed();

  if (pendingLEDCommand != LED_NONE) {
    switch (pendingLEDCommand) {
      case LED_RED:   setLEDColorRed();   break;
      case LED_GREEN: setLEDColorGreen(); break;
      case LED_BLUE:  setLEDColorBlue();  break;
      default: break;
    }
    pendingLEDCommand = LED_NONE;
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  
  switch (touchState) {
    case IDLE:
      if (rawPressed) { chsc6x_get_xy(&startX, &startY); touchState = TOUCH_ACTIVE; }
      break;
    case TOUCH_ACTIVE:
      if (rawPressed) { chsc6x_get_xy(&lastX, &lastY); }
      else { releaseTime = millis(); touchState = TOUCH_RELEASED; }
      break;
    case TOUCH_RELEASED:
      if (!rawPressed && (millis() - releaseTime >= RELEASE_DEBOUNCE_MS)) {
        chsc6x_get_xy(&lastX, &lastY); processSwipe(); touchState = GESTURE_PROCESSED;
      } else if (rawPressed) { touchState = TOUCH_ACTIVE; }
      break;
    case GESTURE_PROCESSED:
      if (!rawPressed && (millis() - releaseTime >= (RELEASE_DEBOUNCE_MS + 200))) touchState = IDLE;
      if (rawPressed) { chsc6x_get_xy(&startX, &lastY); touchState = TOUCH_ACTIVE; }
      break;
  }
  
  unsigned long currentTime = millis();
  if (currentTime - lastRotationUpdate > ROTATION_UPDATE_INTERVAL) {
    float angle = readTiltAngle();
    if (fabs(angle - lastAngle) > 5.0f) { drawRotated(-angle); lastAngle = angle; }
    lastRotationUpdate = currentTime;
  }
  
  if (motorActive && (millis() - motorCommandStart >= motorDuration)) {
    stopMotor(); motorActive = false; SERIAL_PORT.println("Motor command duration elapsed; motor stopped.");
  }

  WiFiClient client = server.available();
  if (client) {
    Serial.println("TCP client connected.");
    String command = "";
    while (client.connected()) {
      while (client.available()) {
        char c = client.read();
        if (c == '\n') {
          command.trim();
          Serial.print("Received over WiFi: "); Serial.println(command);
          processSerialCommand(command, client);
          command = "";
        } else {
          command += c;
        }
      }
      yield();
    }
    client.stop();
    Serial.println("TCP client disconnected.");
  }
}

// --------------------- PNGdec Streaming Callbacks ---------------------

void* myPNGOpen(const char *szFilename, int32_t *pFileSize) {
  String fullPath = String(szFilename);
  if (!fullPath.startsWith("/")) fullPath = "/" + fullPath;
  fs::File file = SPIFFS.open(fullPath.c_str(), "r");
  if (!file) { Serial.printf("Failed to open file: %s\n", fullPath.c_str()); return NULL; }
  *pFileSize = file.size();
  fs::File *pFileHandle = new fs::File(file);
  return (void*)pFileHandle;
}

void myPNGClose(void *pHandle) {
  fs::File *pFile = (fs::File *)pHandle;
  if (pFile) { pFile->close(); delete pFile; }
}

int32_t myPNGRead(PNGFILE *pPNGFile, uint8_t *pBuf, int32_t iLen) {
  fs::File *pFile = (fs::File *)pPNGFile->fHandle;
  if (pFile) return pFile->read(pBuf, iLen);
  return -1;
}

int32_t myPNGSeek(PNGFILE *pPNGFile, int32_t iPosition) {
  fs::File *pFile = (fs::File *)pPNGFile->fHandle;
  if (pFile) { bool ok = pFile->seek(iPosition, fs::SeekSet); return ok ? 0 : -1; }
  return -1;
}