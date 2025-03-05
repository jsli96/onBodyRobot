#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_NeoPixel.h>

#include <TFT_eSPI.h>
#include <Wire.h>
#include <SPI.h>
#define USE_TFT_ESPI_LIBRARY
#include "lv_xiao_round_screen.h"  // For CHSC6x-based touch
#include <PNGdec.h>
#include <Adafruit_MCP23X17.h>

Adafruit_MCP23X17 mcp;

// ICM-20948
#include "ICM_20948.h"
//#define USE_SPI // Uncomment if using SPI

#define SERIAL_PORT Serial
#define WIRE_PORT   Wire
#define AD0_VAL     1
#define SPI_PORT    SPI
#define CS_PIN      1

#ifdef USE_SPI
ICM_20948_SPI myICM;
#else
ICM_20948_I2C myICM;
#endif

PNG png;

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

// Network credentials
const char* ssid_dev = "TP-LINK_lab0102";
const char* password_dev = "LAB0102!!!";
// Change to your own WiFi 
const char* ssid = "atp236";
const char* password = "88888888";

// Pixel setup
// #define LEDS_COUNT 4
// #define LEDS_PIN 6
// Adafruit_NeoPixel on_board_led(1, 21, NEO_RGB);
// Adafruit_NeoPixel strip_led(LEDS_COUNT, LEDS_PIN, NEO_GRB);  // Note R/G are reversed compared to onboard LED

AsyncWebServer server(80);
String ipAddr;

// Calico setup


#define MOTOR_IN1 14  // MCP23017 Pin D3
#define MOTOR_IN2 15  // MCP23017 Pin D4

// Calico movement state variable
String moveStatus = "stop";

// Global vars for lighting task
// lighting task treats these as read-only to avoid race conditions
String ledStatus = "off";
int rrr = 0;
int ggg = 0;
int bbb = 0;
int br = 0;  //blink rate in msecs (really this is the duration of half a cycle)

//----------------------------------------------------------------------------------
// PNG Callback: decode each line into rawImage[]
//----------------------------------------------------------------------------------
void PNGDrawCallback(PNGDRAW *pDraw)
{
  if (pDraw->y >= 240) return; // safety check

  // Where to place this row in rawImage
  uint16_t *dest = &rawImage[pDraw->y * imgWidth];

  png.getLineAsRGB565(
    pDraw,
    dest,                     // store row into rawImage
    PNG_RGB565_BIG_ENDIAN,    // might need BIG_ENDIAN if colors appear off
    0xFFFFFFFF                // ignore alpha
  );
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
    // Right swipe => previous
    currentImageIndex--;
    if (currentImageIndex < 0) currentImageIndex = imageCount - 1;
    showImage(currentImageIndex);
    SERIAL_PORT.println("Swipe Right -> Previous Image");
  }
  else if (deltaX < -SWIPE_THRESHOLD) {
    // Left swipe => next
    currentImageIndex++;
    if (currentImageIndex >= imageCount) currentImageIndex = 0;
    showImage(currentImageIndex);
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

  // Example: use "roll" around X axis
  float roll = atan2(ay, az) * 180.0 / PI;

  // Shift into [0..360)
  float angle = roll + 180.0;
  if (angle < 0)   angle += 360.0;
  if (angle >= 360.0) angle -= 360.0;

  return angle;
}

//----------------------------------------------------------------------------------
// drawRotated(angle):
//   Takes the data in rawImage[] (16-bit, up to 240x240),
//   rotates it by 'angle' degrees around the center,
//   then draws to the screen.
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

  for (int yS = 0; yS < 240; yS++)
  {
    for (int xS = 0; xS < 240; xS++)
    {
      float dx = xS - screenCx;
      float dy = yS - screenCy;

      float xSrcF =  cosA * dx + sinA * dy + cx;
      float ySrcF = -sinA * dx + cosA * dy + cy;

      int xSrc = (int)(xSrcF + 0.5f);
      int ySrc = (int)(ySrcF + 0.5f);

      if (xSrc < 0 || xSrc >= imgWidth || ySrc < 0 || ySrc >= imgHeight) {
        lineBuf2[xS] = 0x0000; // black
      }
      else {
        lineBuf2[xS] = rawImage[ySrc * imgWidth + xSrc];
      }
    }
    tft.pushImage(0, yS, 240, 1, lineBuf2);
  }
}

float lastAngle = -1;
unsigned long lastRotationUpdate = 0;
const int ROTATION_UPDATE_INTERVAL = 300; // ms


void setup() {
  Serial.begin(115200);
  delay(500);

  while (!Serial)
    ;  // Delay until Serial port is available?
  Serial.print("Calico is here: ");
  Serial.println(calico_version);

  Serial.print("Setup task running on core ");
  Serial.println(xPortGetCoreID());

  // Connect to Wi-Fi show network with SSID and password
  Serial.print("Connecting to show network:");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  int tries = 0;
  //setColor(0x020000);
  while (WiFi.status() != WL_CONNECTED && tries < 6) {
    vTaskDelay(500 / portTICK_PERIOD_MS);  // Use non-blocking delay
    tries++;
    Serial.print(".");
  }

  // Dev network
  if (WiFi.status() != WL_CONNECTED) {
    //setColor(0x000200);
    WiFi.disconnect();
    Serial.print("\nConnecting to dev network: ");
    Serial.println(ssid_dev);
    WiFi.begin(ssid_dev, password_dev);
    while (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(500 / portTICK_PERIOD_MS);  // Use non-blocking delay
      Serial.print(".");
    }
  }

  // Print local IP address and start web server
  Serial.println("");
  Serial.print("WiFi connected. IP address: ");
  Serial.println(WiFi.localIP());
  ipAddr = WiFi.localIP().toString();

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

  // TFT init
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  // Touch init
  pinMode(TOUCH_INT, INPUT_PULLUP);

  // Show first image
  showImage(currentImageIndex);

  // --------------------------------------------------
  // Call the Motor Control setup next
  // (Renamed setupMotorControl())
  // --------------------------------------------------
  setupMotorControl();

  // Define web routes before starting the server
  // Movement
  server.on("/move/stop", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Stop");
    moveStatus = "stop";
    stopMotor();
    request->send(200, "text/plain", "Stop movement");
  });

  server.on("/move/forward", HTTP_GET, [](AsyncWebServerRequest *request){
    moveForward();
    Serial.println("Forward");
    
    request->send(200, "text/plain", "Move forward");
  });

  server.on("/move/backward", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Backward");
    moveBackward();
    request->send(200, "text/plain", "Move backward");
  });

  // Vibe
  server.on("/v1", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Vibe 1");
    moveStatus = "V1";
    for (int i = 0; i < 5; i++) {
      moveForward();
      vTaskDelay(100 / portTICK_PERIOD_MS);  // Replaced delay(100)
      moveBackward();
      vTaskDelay(100 / portTICK_PERIOD_MS);  // Replaced delay(100)
    }
    stopMotor();
    request->send(200, "text/plain", "Vibe 1 executed");
  });

  server.on("/v2", HTTP_GET, [](AsyncWebServerRequest *request){
    Serial.println("Vibe 2");
    moveStatus = "V2";
    for (int i = 0; i < 10; i++) {
      moveForward();
      vTaskDelay(200 / portTICK_PERIOD_MS);  // Replaced delay(100)
      moveBackward();
      vTaskDelay(200 / portTICK_PERIOD_MS);  // Replaced delay(100)
    }
    stopMotor();
    request->send(200, "text/plain", "Vibe 2 executed");
  });

  server.on("/setDuration", HTTP_GET, [](AsyncWebServerRequest *request) {
      if (request->hasParam("value")) {
          String value = request->getParam("value")->value();
          br = value.toInt();  // Convert string to integer
          Serial.print("Updated duration: ");
          Serial.println(br);
          request->send(200, "text/plain", "Duration updated");
      } else {
          request->send(400, "text/plain", "Missing value");
      }
  });

  server.begin();
  Serial.println("called server.begin()");

}

// -----------------------------------------------------
//        Motor Control Program
// -----------------------------------------------------

String command = ""; 

void setupMotorControl() {
    // Ensure I2C is initialized once in the main setup()
    if (!mcp.begin_I2C(0x20)) {  // Initialize MCP23017 at address 0x20
        Serial.println("Failed to initialize MCP23017!");
        while (1);  // Stop execution if MCP23017 is not found
    }

    // Set motor control pins as OUTPUT on GPIO Expander
    mcp.pinMode(14, OUTPUT);  // D3 (Motor_IN1)
    mcp.pinMode(15, OUTPUT);  // D4 (Motor_IN2)

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

void loop() {
  bool isPressed = chsc6x_is_pressed();
  // 1) Handle swipe
  if (isPressed) {
    lv_coord_t x, y;
    chsc6x_get_xy(&x, &y);

    if (!lastPressed) {
      startX = x;
      startY = y;
    }
    lastX = x;
    lastY = y;
  } 
  else {
    if (lastPressed) {
      checkSwipe();
    }
  }
  lastPressed = isPressed;

  // 2) Update Rotation if enough time has passed
  unsigned long currentTime = millis();
  if (!isPressed && (currentTime - lastRotationUpdate > ROTATION_UPDATE_INTERVAL)) {
    float angle = readTiltAngle();
    if (fabs(angle - lastAngle) > 10.0) {
      drawRotated(-angle);
      lastAngle = angle;
      lastRotationUpdate = currentTime;
    }
  }

  // 3) Slight delay
  delay(20);
}
