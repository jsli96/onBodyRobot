/***************************************************************************
 * New Program: Rotating Display with Swipe and Motor Control
 *
 * Features:
 *  - Continuously rotates the displayed image based on the IMU’s tilt.
 *  - Dynamically loads PNG (or JPG) images stored in SPIFFS; swiping left/right
 *    cycles through them.
 *  - Includes motor control functions via an MCP23017 (for future integration).
 *  - Uses CHSC6x-based touch functions from lv_xiao_round_screen.h.
 *  - WiFi is enabled with an AsyncWebServer upload endpoint.
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
//#define USE_SPI         // Uncomment if using SPI for the IMU

#include <FastLED.h>
#include "SPIFFS.h"
#include <vector>
#include <ESPAsyncWebServer.h>

// --------------------- Hardware Definitions ---------------------
#define SERIAL_PORT         Serial
#define WIRE_PORT           Wire
#define AD0_VAL             1
#define SPI_PORT            SPI
#define CS_PIN              1

#define LED_PIN             D2       // For NeoPixel LED
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

const char* ssid = "TP-LINK_lab0102";
const char* password = "LAB0102!!!";

// --------------------- Global Variables ---------------------
std::vector<String> imageFiles;  // To store filenames found in SPIFFS

// Create an AsyncWebServer instance for uploads on port 80.
AsyncWebServer espServer(80);
// WiFiServer for additional commands.
WiFiServer server(3333);

PNG png;  
Adafruit_MCP23X17 mcp;  // MCP23017 for motor control

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
#define MOTOR_IN1 14
#define MOTOR_IN2 15

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

// Resets the image index.
void resetImageIndex() {
  currentImageIndex = 0;
}

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
    // Print out filename and size.
    Serial.printf("Found file: %s (size: %d bytes)\n", fileName.c_str(), fileSize);
    
    // Only add files that appear to have nonzero size.
    if ((fileName.endsWith(".png")) && fileSize > 0) {
      imageFiles.push_back(fileName);
    } else {
      Serial.printf("File %s is ignored (non-image or size 0).\n", fileName.c_str());
    }
    file = root.openNextFile();
    yield();
  }
}


// This function uses PNGdec's open() function with streaming callbacks.
// (Callbacks myPNGOpen, myPNGClose, etc., are defined below.)
void showImageFromSPIFFS_Stream(const char* filename) {
  // If already decoding, do not start a new decode.
  if (isDecoding) {
    Serial.println("Decoder busy. Ignoring new decode request.");
    return;
  }
  
  isDecoding = true;  // Set flag so other swipes will wait.

  String fullPath = String(filename);

  if (!fullPath.startsWith("/")) {
    fullPath = "/" + fullPath;
  }
  
  int rc = png.open(fullPath.c_str(), myPNGOpen, myPNGClose, myPNGRead, myPNGSeek, PNGDrawCallback);
  if (rc != PNG_SUCCESS) {
    Serial.printf("PNG openFS failed: %d\n", rc);
    png.close();  // Always close to reset state.
    isDecoding = false;  // Reset flag if opening fails.
    return;
  }
  
  imgWidth = png.getWidth();
  imgHeight = png.getHeight();
  rc = png.decode(nullptr, PNG_FAST_PALETTE);
  if (rc != PNG_SUCCESS) {
    Serial.printf("PNG decode failed for %s: %d\n", filename, rc);
    png.close();  // Always close to reset state.
    isDecoding = false;  // Reset flag if opening fails.
    return;
  }
  
  
  // Check if the image dimensions are valid.
  if (imgWidth <= 0 || imgHeight <= 0) {
    Serial.printf("Displayed image %s has invalid dimensions (w:%d, h:%d)\n", filename, imgWidth, imgHeight);
  } else {
    Serial.printf("Displayed image %s (w:%d, h:%d)\n", filename, imgWidth, imgHeight);
  }

  // Always call close() to free up the decoder.
  png.close();
  isDecoding = false;  // Clear the busy flag once done.
}

void showImage(int index) {
  if (index < 0 || index >= imageFiles.size()) {
    Serial.println("Index out of range");
    return;
  }
  const char* filename = imageFiles[index].c_str();
  showImageFromSPIFFS_Stream(filename);
}

// --------------------- LED Functions ---------------------

void setLEDColorRed() {
  leds[0] = CRGB::Red;
  currentLEDColor = 0xFF0000;
  Serial.print("DEBUG: Setting LED to RED, currentLEDColor = 0x");
  Serial.println(currentLEDColor, HEX);
  FastLED.show();
}

void setLEDColorGreen() {
  leds[0] = CRGB::Green;
  currentLEDColor = 0x00FF00;
  Serial.print("DEBUG: Setting LED to GREEN, currentLEDColor = 0x");
  Serial.println(currentLEDColor, HEX);
  FastLED.show();
}

void setLEDColorBlue() {
  leds[0] = CRGB::Blue;
  currentLEDColor = 0x0000FF;
  Serial.print("DEBUG: Setting LED to BLUE, currentLEDColor = 0x");
  Serial.println(currentLEDColor, HEX);
  FastLED.show();
}

String getCurrentLEDColor() {
  char buffer[8];
  sprintf(buffer, "#%06X", (unsigned int)(currentLEDColor & 0xFFFFFF));
  String colorStr = String(buffer);
  Serial.print("DEBUG: getCurrentLEDColor() returning ");
  Serial.println(colorStr);
  return colorStr;
}

// --------------------- Display / Rotation Functions ---------------------

void PNGDrawCallback(PNGDRAW *pDraw) {
  if (pDraw->y >= 240) return;
  uint16_t *dest = &rawImage[pDraw->y * imgWidth];
  png.getLineAsRGB565(pDraw, dest, PNG_RGB565_BIG_ENDIAN, 0xFFFFFFFF);
}

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

// --------------------- IMU Functions ---------------------

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

// --------------------- Swipe Processing ---------------------

void processSwipe() {
  int deltaX = lastX - startX;
  if (deltaX > SWIPE_THRESHOLD) {
    currentImageIndex++;
    if (currentImageIndex >= imageFiles.size()) currentImageIndex = 0;
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

// --------------------- Motor Control Functions ---------------------

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
  Serial.println("Setup starting...");

  if (!SPIFFS.begin(true)) {  
    Serial.println("Failed to mount SPIFFS");
    return;
  }
  Serial.println("SPIFFS mounted successfully.");

  // Do not call SPIFFS.format() if you want to preserve files.
  // SPIFFS.format();

  // Immediately scan for images stored in SPIFFS.
  scanForImageFiles();
  Serial.print("Found ");
  Serial.print(imageFiles.size());
  Serial.println(" image(s).");
  if (imageFiles.size() > 0) {
    // Reset index when first scanning images.
    resetImageIndex();
    showImage(currentImageIndex);
  } else {
    Serial.println("No image files found.");
  }

  // Configure static IP.
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("Static IP configuration failed");
  }
  WiFi.begin(ssid, password);
  delay(500);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    yield();
  }
  Serial.println("\nConnected!");
  Serial.println(WiFi.localIP());

  // Disable WiFi power saving.
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Start the ESP32 file-upload endpoint.
  setupESP32UploadEndpoint();

  // Start the secondary web server.
  server.begin();
  Serial.print("Setup task running on core ");
  Serial.println(xPortGetCoreID());

  // Initialize TFT display.
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  // Initialize touch.
  pinMode(TOUCH_INT, INPUT_PULLUP);
  Wire.begin();

  // Initialize IMU.
  WIRE_PORT.begin();
  WIRE_PORT.setClock(400000);
  myICM.begin(WIRE_PORT, AD0_VAL);
  if (myICM.status != ICM_20948_Stat_Ok) {
    SERIAL_PORT.println("IMU init failed!");
  } else {
    SERIAL_PORT.println("IMU init OK!");
  }

  // Initialize motor control.
  setupMotorControl();

  // Initialize FastLED.
  FastLED.addLeds<WS2812, LED_PIN, GRB>(leds, NUM_LEDS);
  leds[0] = CRGB::Red;
  FastLED.show();
}

// --------------------- Process Serial Commands ---------------------

void processSerialCommand(String cmd, WiFiClient &client) {
  cmd.trim();
  if (cmd == "setRed") {
    pendingLEDCommand = LED_RED;
    return;
  }
  if (cmd == "setGreen") {
    pendingLEDCommand = LED_GREEN;
    return;
  }
  if (cmd == "setBlue") {
    pendingLEDCommand = LED_BLUE;
    return;
  }
  if (cmd == "GET_LED_COLOR") {
    String currentColor = getCurrentLEDColor();
    Serial.print("DEBUG: GET_LED_COLOR command processed. LED Color = ");
    Serial.println(currentColor);
    client.println(currentColor);
    return;
  }
  
  if (motorActive) {
    Serial.println("Motor command already active. Ignoring new motor command.");
    return;
  }
  
  if (cmd.startsWith("moveForward(")) {
    int startIdx = cmd.indexOf('(');
    int endIdx = cmd.indexOf(')');
    String durationStr = cmd.substring(startIdx + 1, endIdx);
    motorDuration = durationStr.toInt();
    moveForward();
    Serial.print("Executed moveForward for ");
    Serial.print(motorDuration);
    Serial.println(" ms");
    motorActive = true;
    delay(motorDuration);
    stopMotor();
    Serial.println("Motor command duration elapsed; motor stopped.");
    motorActive = false;
    return;
  } else if (cmd.startsWith("moveBackward(")) {
    int startIdx = cmd.indexOf('(');
    int endIdx = cmd.indexOf(')');
    String durationStr = cmd.substring(startIdx + 1, endIdx);
    motorDuration = durationStr.toInt();
    moveBackward();
    Serial.print("Executed moveBackward for ");
    Serial.print(motorDuration);
    Serial.println(" ms");
    motorActive = true;
    delay(motorDuration);
    stopMotor();
    Serial.println("Motor command duration elapsed; motor stopped.");
    motorActive = false;
    return;
  } else if (cmd == "moveForward") {
    motorDuration = 5000;
    moveForward();
    Serial.print("Executed moveForward for default duration ");
    Serial.print(motorDuration);
    Serial.println(" ms");
    motorActive = true;
    delay(motorDuration);
    stopMotor();
    Serial.println("Motor command duration elapsed; motor stopped.");
    motorActive = false;
    return;
  } else if (cmd == "moveBackward") {
    motorDuration = 5000;
    moveBackward();
    Serial.print("Executed moveBackward for default duration ");
    Serial.print(motorDuration);
    Serial.println(" ms");
    motorActive = true;
    delay(motorDuration);
    stopMotor();
    Serial.println("Motor command duration elapsed; motor stopped.");
    motorActive = false;
    return;
  } else if (cmd == "stopMotor") {
    stopMotor();
    Serial.println("Executed stopMotor()");
    motorActive = false;
    return;
  }
  
  Serial.print("Unrecognized command: ");
  Serial.println(cmd);
}

// --------------------- Main Loop ---------------------

void loop() {
  // Check for a new file upload.
  if (newFileUploaded) {
    newFileUploaded = false;
    scanForImageFiles();
    // Reset the image index since the number of images may have changed.
    resetImageIndex();
    if (imageFiles.size() > 0) {
      showImage(currentImageIndex);
    } else {
      Serial.println("No image files found after scanning.");
    }
  }
  
  bool rawPressed = chsc6x_is_pressed();

  if (pendingLEDCommand != LED_NONE) {
    switch (pendingLEDCommand) {
      case LED_RED:   setLEDColorRed(); break;
      case LED_GREEN: setLEDColorGreen(); break;
      case LED_BLUE:  setLEDColorBlue(); break;
      default: break;
    }
    pendingLEDCommand = LED_NONE;
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  
  // Touch state machine.
  switch (touchState) {
    case IDLE:
      if (rawPressed) {
        chsc6x_get_xy(&startX, &startY);
        touchState = TOUCH_ACTIVE;
      }
      break;
    case TOUCH_ACTIVE:
      if (rawPressed) {
        chsc6x_get_xy(&lastX, &lastY);
      } else {
        releaseTime = millis();
        touchState = TOUCH_RELEASED;
      }
      break;
    case TOUCH_RELEASED:
      if (!rawPressed && (millis() - releaseTime >= RELEASE_DEBOUNCE_MS)) {
        chsc6x_get_xy(&lastX, &lastY);
        processSwipe();
        touchState = GESTURE_PROCESSED;
      } else if (rawPressed) {
        touchState = TOUCH_ACTIVE;
      }
      break;
    case GESTURE_PROCESSED:
      if (!rawPressed && (millis() - releaseTime >= (RELEASE_DEBOUNCE_MS + 200))) {
        touchState = IDLE;
      }
      if (rawPressed) {
        chsc6x_get_xy(&startX, &lastY);
        touchState = TOUCH_ACTIVE;
      }
      break;
  }
  
  // Continuous rotation update.
  unsigned long currentTime = millis();
  if (currentTime - lastRotationUpdate > ROTATION_UPDATE_INTERVAL) {
    float angle = readTiltAngle();
    if (fabs(angle - lastAngle) > 5.0) {
      drawRotated(-angle);
      lastAngle = angle;
    }
    lastRotationUpdate = currentTime;
  }
  
  // Motor control check.
  if (motorActive && (millis() - motorCommandStart >= motorDuration)) {
    stopMotor();
    motorActive = false;
    SERIAL_PORT.println("Motor command duration elapsed; motor stopped.");
  }

  // WiFi client command processing.
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
  if (!fullPath.startsWith("/")) {
    fullPath = "/" + fullPath;
  }
  fs::File file = SPIFFS.open(fullPath.c_str(), "r");
  if (!file) {
    Serial.printf("Failed to open file: %s\n", fullPath.c_str());
    return NULL;
  }
  *pFileSize = file.size();
  fs::File *pFileHandle = new fs::File(file);
  return (void*)pFileHandle;
}

void myPNGClose(void *pHandle) {
  fs::File *pFile = (fs::File *)pHandle;
  if (pFile) {
    pFile->close();
    delete pFile;
  }
}

int32_t myPNGRead(PNGFILE *pPNGFile, uint8_t *pBuf, int32_t iLen) {
  fs::File *pFile = (fs::File *)pPNGFile->fHandle;
  if (pFile) {
    return pFile->read(pBuf, iLen);
  }
  return -1;
}

int32_t myPNGSeek(PNGFILE *pPNGFile, int32_t iPosition) {
  fs::File *pFile = (fs::File *)pPNGFile->fHandle;
  if (pFile) {
    bool ok = pFile->seek(iPosition, fs::SeekSet);
    return ok ? 0 : -1;
  }
  return -1;
}
