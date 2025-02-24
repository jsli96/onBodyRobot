/***************************************************************************
 * Smooth Rotation Demo (Software-based)
 * Using ICM-20948 Accelerometer for tilt angle,
 * PNGdec for image decode, and software rotation of a full framebuffer.
 ***************************************************************************/

#include <TFT_eSPI.h>
#include <Wire.h>
#include <SPI.h>
#define USE_TFT_ESPI_LIBRARY
#include "lv_xiao_round_screen.h"  // For CHSC6x-based touch
#include <PNGdec.h>
#include <Adafruit_MCP23X17.h>

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


// PNG Callback: decode each line into rawImage[]

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


// Show image: decode the PNG into rawImage[] (no rotation yet)

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


// Check Swipe

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


// Compute tilt angle from accelerometer

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


// drawRotated(angle):
//   Takes the data in rawImage[] (16-bit, up to 240x240),
//   rotates it by 'angle' degrees around the center,
//   then draws to the screen.

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

  setupMotorControl();
}


// *** "Main" loop for Display/Touch/IMU Program ***

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

  loopMotorControl();
}

// Define motor control pins for DRV8833
#define MOTOR_IN1 D0  // Connect to IN1 on DRV8833
#define MOTOR_IN2 D11  // Connect to IN2 on DRV8833

int speedValue = 0; 
String command = ""; 


// (Renamed) setup() -> setupMotorControl()

void setupMotorControl() {
    pinMode(MOTOR_IN1, OUTPUT);
    pinMode(MOTOR_IN2, OUTPUT);
    stopMotor(); // Ensure the motor is stopped initially

    Serial.println("Motor Controller Ready!");
    Serial.println("Commands:");
    Serial.println("'F <speed>' - Move forward (speed 0-255)");
    Serial.println("'B <speed>' - Move backward (speed 0-255)");
    Serial.println("'S' - Stop motor");
}


// (Renamed) loop() -> loopMotorControl()

void loopMotorControl() {
    if (Serial.available() > 0) {
        command = Serial.readStringUntil('\n');
        command.trim();

        // Handle Forward Command
        if (command.startsWith("F")) {
            speedValue = getSpeed(command);
            moveForward(speedValue);
            Serial.print("Moving Forward at Speed: ");
            Serial.println(speedValue);
        }
        // Handle Backward Command
        else if (command.startsWith("B")) {
            speedValue = getSpeed(command);
            moveBackward(speedValue);
            Serial.print("Moving Backward at Speed: ");
            Serial.println(speedValue);
        }
        // Handle Stop Command
        else if (command.equalsIgnoreCase("S")) {
            stopMotor();
            Serial.println("Motor Stopped.");
        }
        else {
            Serial.println("Invalid Command! Use 'F <speed>', 'B <speed>', or 'S'");
        }
    }
}

// Function to move motor forward
void moveForward(int speed) {
    analogWrite(MOTOR_IN1, speed);
    analogWrite(MOTOR_IN2, 0);
}

// Function to move motor backward
void moveBackward(int speed) {
    analogWrite(MOTOR_IN1, 0);
    analogWrite(MOTOR_IN2, speed);
}

// Function to stop the motor
void stopMotor() {
    analogWrite(MOTOR_IN1, 0);
    analogWrite(MOTOR_IN2, 0);
}

// Function to extract speed value from command
int getSpeed(String cmd) {
    int spaceIndex = cmd.indexOf(' ');
    if (spaceIndex != -1) {
        int speed = cmd.substring(spaceIndex + 1).toInt();
        return constrain(speed, 0, 255);
    }
    return 0; // Default to 0 if no speed provided
}
