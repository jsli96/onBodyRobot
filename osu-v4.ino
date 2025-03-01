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
  // HTML/CSS layout of the webpage
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
      String html = generateWebpage();
      request->send(200, "text/html", html);
  });

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

String generateWebpage() {
    String html = "<!DOCTYPE html><html>";
    html += "<head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>Calico Control</title>";
    html += "<link rel='icon' href='data:,'>";
    
    // CSS for layout and blocks
    html += "<style>";
    html += "html, body { font-family: sans-serif; background: #f0f0f0; margin: 0; padding: 0; width: 100%; height: 100vh; display: flex; }";
    html += ".sidebar { width: 350px; background: #d3d3d3; padding: 10px; border-right: 2px solid #999; overflow-y: auto; height: 100vh; }";
    html += ".workspace { flex: 1; background: #fff; padding: 20px; overflow: auto; border-left: 2px dashed #ccc; display: flex; flex-direction: column; align-items: flex-start; gap: 10px; border-radius: 10px; }";
    html += ".workspace-header { display: flex; align-items: center; gap: 10px; }";
    html += ".workspace-header h3 { margin: 0; font-size: 20px; }";
    html += ".workspace-header p { margin: 0; font-size: 16px; color: gray; }";
    html += ".button { background-color: #ffbf00; border: none; color: white; padding: 15px 20px; border-radius: 20px; font-size: 18px; margin: 5px 0; cursor: grab; display: block; text-align: center; box-shadow: 3px 3px 6px rgba(0,0,0,0.2); transition: transform 0.2s ease, background-color 0.2s ease; }";
    html += ".sidebar .button { width: 90%; }";
    html += ".workspace .button { width: auto; }";
    html += ".button:hover { transform: scale(1.05); }";
    html += ".buttonGreen { background-color: #0DA57A !important; }";
    html += ".buttonRed { background-color: #E84D39 !important; }";
    html += ".buttonBlue { background-color: #5A94E1 !important; }";
    html += ".buttonBlack { background-color: #333333 !important; }";
    html += ".buttonPurple { background-color: #8A55D7 !important; }";
    html += ".buttonYellow { background-color: #FFD43B !important; }";
    html += ".executeButton { background-color: #444; color: white; padding: 15px 20px; border-radius: 20px; font-size: 18px; margin: 10px 0; width: 100%; cursor: pointer; }";
    html += ".delete-icon { font-size: 14px; font-weight: bold; color: white; cursor: pointer; margin-left: 10px; }";
    html += ".timerInput { background-color: #fff; color: #000; border: 1px solid #ccc; border-radius: 4px; padding: 2px 6px; margin: 0 4px; font-size: 18px; }";
    html += ".sliderBlock { text-align: left; }";
    html += ".ifBlock {";
    html += "  position: relative;";
    html += "  background-color: #8A55D7;";
    html += "  color: white;";
    html += "  padding: 15px 20px;";  
    html += "  margin: 5px 0;";         
    html += "  border: none;";
    html += "  border-radius: 20px;";
    html += "  display: inline-block;";
    html += "  cursor: grab;";
    html += "  font-size: 18px;";
    html += "  text-align: center;";
    html += "  box-shadow: 3px 3px 6px rgba(0,0,0,0.2);"; 
    html += "  transition: transform 0.2s ease, background-color 0.2s ease;";
    html += "  font-family: sans-serif;"; 
    html += "}";
    html += ".ifLabel, .thenLabel { font-weight: bold; }";
    html += ".condition-dropzone {";
    html += "    display: inline-block;";
    html += "    min-width: 100px;";
    html += "    min-height: 30px;";
    html += "    padding: 15px 20px;"; 
    html += "    margin: 0 8px;";
    html += "    border: 1px dashed #fff;";
    html += "    background-color: rgba(255,255,255,0.2);";
    html += "    vertical-align: middle;";
    html += "    cursor: pointer;";
    html += "    font-family: sans-serif;"; 
    html += "    font-size: 18px;";
    html += "    color: white;";
    html += "    border-radius: 20px;";         
    html += "    box-shadow: 3px 3px 6px rgba(0,0,0,0.2);";
    html += "    transition: transform 0.2s ease, background-color 0.2s ease;";
    html += "}";
    html += "</style>";
    
    // JavaScript for drag & drop, slider updates, timer editing, and execution
    html += "<script>";
    html += "function dragStart(event) {";
    html += "    var source = '';";
    html += "    if(event.target.closest('.workspace')) {";
    html += "         source = 'workspace';";
    html += "         if(!event.target.id) {";
    html += "              event.target.id = 'block-' + Date.now() + '-' + Math.floor(Math.random()*1000);";
    html += "         }";
    html += "         event.dataTransfer.setData('id', event.target.id);";
    html += "    } else {";
    html += "         source = 'sidebar';";
    html += "         event.dataTransfer.setData('html', event.target.outerHTML);";
    html += "    }";
    html += "    event.dataTransfer.setData('source', source);";
    html += "}";
    html += "function allowDrop(event) { event.preventDefault(); }";
    html += "function drop(event) {";
    html += "    event.preventDefault();";
    html += "    var target = event.target.closest('.workspace');";
    html += "    if (!target) return;";
    html += "    var source = event.dataTransfer.getData('source');";
    html += "    if(source === 'workspace') {";
    html += "         var id = event.dataTransfer.getData('id');";
    html += "         var element = document.getElementById(id);";
    html += "         if(element) {";
    html += "              target.appendChild(element);";
    html += "         }";
    html += "    } else {";
    html += "         var htmlData = event.dataTransfer.getData('html');";
    html += "         if(!htmlData) return;";
    html += "         var tempDiv = document.createElement('div');";
    html += "         tempDiv.innerHTML = htmlData;";
    html += "         var newBlock = tempDiv.firstChild;";
    html += "         newBlock.setAttribute('draggable', 'true');";
    html += "         newBlock.setAttribute('ondragstart', 'dragStart(event)');";
    html += "         newBlock.id = 'block-' + Date.now() + '-' + Math.floor(Math.random()*1000);";
    html += "         var deleteIcon = document.createElement('span');";
    html += "         deleteIcon.innerHTML = '&times;';";
    html += "         deleteIcon.className = 'delete-icon';";
    html += "         deleteIcon.onclick = function(event) { event.stopPropagation(); event.target.parentElement.remove(); };";
    html += "         newBlock.appendChild(deleteIcon);";
    html += "         target.appendChild(newBlock);";
    html += "    }";
    html += "}";
    
    html += "function editTimer(event) {";
    html += "    event.stopPropagation();";
    html += "    var timerButton = event.currentTarget;";
    html += "    var current = timerButton.getAttribute('data-time') || '5';";
    html += "    var newTime = prompt('Enter time in seconds:', current);";
    html += "    if(newTime !== null && !isNaN(newTime) && newTime > 0) {";
    html += "         timerButton.setAttribute('data-time', newTime);";
    html += "         var timerSpan = timerButton.querySelector('.timerInput');";
    html += "         if(timerSpan) { timerSpan.textContent = newTime; }";
    html += "    }";
    html += "}";
    
    html += "function updateSlider(inputElem) {";
    html += "    var sliderBlock = inputElem.parentElement;";
    html += "    var val = inputElem.value;";
    html += "    sliderBlock.setAttribute('data-value', val);";
    html += "    var labelText = (val === '0') ? 'Slow' : (val === '1') ? 'Medium' : 'Fast';";
    html += "    var labelSpan = sliderBlock.querySelector('.sliderLabel');";
    html += "    if (labelSpan) { labelSpan.textContent = labelText; }";
    html += "}";
    
    html += "function executeSequence() {";
    html += "    var blocks = document.querySelectorAll('.workspace .button, .workspace .sliderBlock');";
    html += "    var totalDelay = 0;";
    html += "    var defaultDelay = 5000;";
    html += "    for (var i = 0; i < blocks.length; i++) {";
    html += "         var type = blocks[i].getAttribute('data-type');";
    html += "         if(type === 'timer') { continue; }";
    html += "         var duration = defaultDelay;";
    html += "         if(i > 0) {";
    html += "              var prev = blocks[i].previousElementSibling;";
    html += "              if(prev && prev.getAttribute('data-type') === 'timer') {";
    html += "                   var t = prev.getAttribute('data-time');";
    html += "                   duration = t ? parseFloat(t) * 1000 : defaultDelay;";
    html += "              }";
    html += "         }";
    html += "         var route = '';";
    html += "         if(blocks[i].classList.contains('sliderBlock') && blocks[i].getAttribute('data-type') === 'slider') {";
    html += "              var mode = blocks[i].getAttribute('data-mode');";
    html += "              var sliderValue = blocks[i].getAttribute('data-value');";
    html += "              if(mode === 'blink') {";
    html += "                  route = (sliderValue === '0') ? '/led/blinkslow' : (sliderValue === '1') ? '/led/blinkmedium' : '/led/blinkfast';";
    html += "              } else if(mode === 'pulse') {";
    html += "                  route = (sliderValue === '0') ? '/led/pulseslow' : (sliderValue === '1') ? '/led/pulsemedium' : '/led/pulsefast';";
    html += "              }";
    html += "         } else {";
    html += "              route = blocks[i].getAttribute('data-path');";
    html += "         }";
    html += "         (function(path, scheduledTime) {";
    html += "              setTimeout(function() {";
    html += "                   fetch(path).then(function(response) { console.log('Sent request to:', path); });";
    html += "              }, scheduledTime);";
    html += "         })(route, totalDelay);";
    html += "         totalDelay += duration;";
    html += "    }";
    html += "    setTimeout(function() {";
    html += "         fetch('/alloff').then(function(response) { console.log('Sent request to: /alloff'); });";
    html += "    }, totalDelay);";
    html += "}";
    
    html += "function dropCondition(event) {";
    html += "    event.preventDefault();";
    html += "    event.stopPropagation();";
    html += "    var htmlData = event.dataTransfer.getData('html');";
    html += "    if (htmlData) {";
    html += "        var tempDiv = document.createElement('div');";
    html += "        tempDiv.innerHTML = htmlData;";
    html += "        var newBlock = tempDiv.firstChild;";
    html += "        newBlock.setAttribute('draggable', 'true');";
    html += "        newBlock.setAttribute('ondragstart', 'dragStart(event)');";
    html += "        newBlock.classList.add('conditionBlock');";
    html += "";
    html += "        var deleteIcon = document.createElement('span');";
    html += "        deleteIcon.innerHTML = '&times;';";
    html += "        deleteIcon.className = 'delete-icon';";
    html += "        deleteIcon.onclick = function(event) {";
    html += "            event.stopPropagation();";
    html += "            event.target.parentElement.remove();";
    html += "        };";
    html += "";
    html += "        newBlock.appendChild(deleteIcon);";
    html += "        event.target.innerHTML = '';";
    html += "        event.target.appendChild(newBlock);";
    html += "    } else {";
    html += "        var id = event.dataTransfer.getData('id');";
    html += "        var element = document.getElementById(id);";
    html += "        if (element) {";
    html += "            var deleteIcon = document.createElement('span');";
    html += "            deleteIcon.innerHTML = '&times;';";
    html += "            deleteIcon.className = 'delete-icon';";
    html += "            deleteIcon.onclick = function(event) {";
    html += "                event.stopPropagation();";
    html += "                event.target.parentElement.remove();";
    html += "            };";
    html += "";
    html += "            element.appendChild(deleteIcon);";
    html += "            event.target.innerHTML = '';";
    html += "            event.target.appendChild(element);";
    html += "        }";
    html += "    }";
    html += "}";

    html += "function uploadImage(event) {";
    // html += "    var file = event.target.files[0];";
    // html += "    if (file && file.type === 'image/png') {";
    // html += "        var reader = new FileReader();";
    // html += "        reader.onload = function(e) {";
    // html += "            // Create an image element using the uploaded PNG data";
    // html += "            var img = document.createElement('img');";
    // html += "            img.src = e.target.result;";
    // html += "            img.style.maxWidth = '100%';";
    // html += "            img.style.display = 'block';";
    // html += "            // Append the image to the sidebar";
    // html += "            document.querySelector('.sidebar').appendChild(img);";
    // html += "        };";
    // html += "        reader.readAsDataURL(file);";
    // html += "    } else {";
    // html += "        alert('Please upload a valid PNG image.');";
    // html += "    }";
    html += "}";
    
    html += "</script>";
    
    html += "</head><body>";
    
    // Sidebar with draggable blocks
    html += "<div class='sidebar'><h3>Controls</h3>";

    // PNG Upload Button 
    html += "<button class='button buttonYellow' onclick='document.getElementById(\"uploadPNG\").click()'>Upload PNG</button>";
    // Hidden file input that is triggered by the button above
    html += "<input type='file' id='uploadPNG' accept='.png' style='display:none;' onchange='uploadImage(event)' />";

    String buttons[] = {
        "<button class='button buttonGreen' draggable='true' ondragstart='dragStart(event)' data-path='/move/backward'>&lt;== Backward</button>",
        "<button class='button buttonBlack' draggable='true' ondragstart='dragStart(event)' data-path='/move/stop'>Stop</button>",
        "<button class='button buttonGreen' draggable='true' ondragstart='dragStart(event)' data-path='/move/forward'>Forward ==&gt;</button>",
        "<button class='button buttonGreen' draggable='true' ondragstart='dragStart(event)' data-path='/v1'>Vibe1</button>",
        "<button class='button buttonGreen' draggable='true' ondragstart='dragStart(event)' data-path='/v2'>Vibe2</button>",
        "<button class='button buttonWhite' draggable='true' ondragstart='dragStart(event)' data-path='/led/off'>Off</button>",
        "<button class='button buttonRed' draggable='true' ondragstart='dragStart(event)' data-path='/led/red'>Red</button>",
        "<button class='button buttonGreen' draggable='true' ondragstart='dragStart(event)' data-path='/led/green'>Green</button>",
        "<button class='button buttonBlue' draggable='true' ondragstart='dragStart(event)' data-path='/led/blue'>Blue</button>",
        "<button class='button buttonWhite' draggable='true' ondragstart='dragStart(event)' data-path='/led/white'>White</button>",
        "<button class='button buttonPurple' draggable='true' ondragstart='dragStart(event)' data-type='timer' data-time='5' id='timerButton' onclick='editTimer(event)'>Set time <span class='timerInput'>5</span> s</button>",
        "<div class='button sliderBlock' draggable='true' ondragstart='dragStart(event)' data-type='slider' data-mode='pulse' data-value='0'>"
            "Pulse Speed: <span class='sliderLabel'>Slow</span><br>"
            "<input type='range' min='0' max='2' step='1' value='0' ondragstart='event.stopPropagation(); return false;' "
            "onmousedown='event.stopPropagation();' onchange='updateSlider(this)' style='width:100%;'>"
        "</div>",
        "<div class='button sliderBlock' draggable='true' ondragstart='dragStart(event)' data-type='slider' data-mode='blink' data-value='0'>"
            "Blink Speed: <span class='sliderLabel'>Slow</span><br>"
            "<input type='range' min='0' max='2' step='1' value='0' ondragstart='event.stopPropagation(); return false;' "
            "onmousedown='event.stopPropagation();' onchange='updateSlider(this)' style='width:100%;'>"
        "</div>",
        "<button class='button buttonGray' draggable='true' ondragstart='dragStart(event)' data-path='/led/nightlight'>Night Light</button>",
        "<button class='button buttonBlack' draggable='true' ondragstart='dragStart(event)' data-path='/led/solid'>Solid</button>",
        "<button class='button buttonPurple' draggable='true' ondragstart='dragStart(event)' data-path='/led/multicolor'>Multicolor</button>",
        "<button class='button buttonRed' draggable='true' ondragstart='dragStart(event)' data-path='/alloff'>ALL OFF</button>",
        "<div class='ifBlock' draggable='true' ondragstart='dragStart(event)' data-type='if'>"
            "<span class='ifLabel'>if</span> "
            "<div class='condition-dropzone' ondragover='allowDrop(event)' ondrop='dropCondition(event)'>drop condition here</div> "
            "<span class='thenLabel'>then</span>"
        "</div>",
        "<button class='button buttonPurple' draggable='true' ondragstart='dragStart(event)' data-type='endif'>END IF</button>",
        "<button class='button buttonPurple' draggable='true' ondragstart='dragStart(event)' data-type='else'>ELSE</button>"
    };
    for (int i = 0; i < sizeof(buttons)/sizeof(buttons[0]); i++) {
        html += buttons[i];
    }
    html += "</div>";
    
    // Workspace (Drop area)
    html += "<div class='workspace' ondragover='allowDrop(event)' ondrop='drop(event)' style='flex-grow:1;'>";
    html += "<div class='workspace-header'>";
    html += "<h3>Workspace</h3>";
    html += "<p>Drag blocks here to create sequences.</p>";
    html += "</div>";
    html += "</div>";
    
    // Bottom area with the Execute button
    html += "<div style='position: absolute; bottom: 10px; right: 20px;'>";
    html += "<button class='executeButton' onclick='executeSequence()'>Execute</button>";
    html += "</div>";
    
    html += "</body></html>";
    
    return html;
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