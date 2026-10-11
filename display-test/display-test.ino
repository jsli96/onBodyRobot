/*
 * Minimal Display Diagnostic for XIAO ESP32-S3 + Round Display (GC9A01)
 *
 * Upload this sketch INSTEAD of osu-v4. Open Serial Monitor at 115200.
 * It will print the TFT_eSPI config (pins, driver, etc.) and try to
 * fill the screen with RED, GREEN, BLUE in a loop.
 *
 * NO WiFi, NO I2C, NO SPIFFS, NO motors — just the display.
 */

#include <SPI.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI(240, 240);

setup_t user;

void setup() {
  Serial.begin(115200);
  delay(2000);  // Give serial monitor time to connect
  Serial.println("\n\n===== DISPLAY DIAGNOSTIC =====");

  // Print what TFT_eSPI thinks the config is BEFORE init
  tft.getSetup(user);

  Serial.print("TFT_eSPI ver = "); Serial.println(user.version);
  Serial.print("Processor    = 0x"); Serial.println(user.esp, HEX);
  Serial.print("Driver       = 0x"); Serial.println(user.tft_driver, HEX);
  Serial.print("Display      = "); Serial.print(user.tft_width);
  Serial.print(" x "); Serial.println(user.tft_height);
  Serial.print("Interface    = "); Serial.println((user.serial == 1) ? "SPI" : "Parallel");
  Serial.print("Transactions = "); Serial.println((user.trans == 1) ? "Yes" : "No");

  Serial.println("\n--- Pin Configuration ---");
  if (user.pin_tft_mosi != -1) { Serial.print("MOSI     = GPIO "); Serial.println(user.pin_tft_mosi); }
  if (user.pin_tft_miso != -1) { Serial.print("MISO     = GPIO "); Serial.println(user.pin_tft_miso); }
  if (user.pin_tft_clk  != -1) { Serial.print("SCK      = GPIO "); Serial.println(user.pin_tft_clk); }
  if (user.pin_tft_cs   != -1) { Serial.print("TFT_CS   = GPIO "); Serial.println(user.pin_tft_cs); }
  if (user.pin_tft_dc   != -1) { Serial.print("TFT_DC   = GPIO "); Serial.println(user.pin_tft_dc); }
  if (user.pin_tft_rst  != -1) { Serial.print("TFT_RST  = GPIO "); Serial.println(user.pin_tft_rst); }
#if defined(TFT_BL)
  Serial.print("TFT_BL   = GPIO "); Serial.println(user.pin_tft_led);
#endif

  Serial.print("SPI freq = "); Serial.print(user.tft_spi_freq / 10.0); Serial.println(" MHz");

  // Print compile-time macro values directly
  Serial.println("\n--- Compile-time Macros ---");
#if defined(GC9A01_DRIVER)
  Serial.println("GC9A01_DRIVER = DEFINED");
#else
  Serial.println("GC9A01_DRIVER = NOT DEFINED  *** PROBLEM ***");
#endif
#if defined(ILI9341_DRIVER)
  Serial.println("ILI9341_DRIVER = DEFINED  *** WRONG DRIVER ***");
#endif
#if defined(USER_SETUP_LOADED)
  Serial.println("USER_SETUP_LOADED = DEFINED");
#else
  Serial.println("USER_SETUP_LOADED = NOT DEFINED");
#endif
#if defined(USE_HSPI_PORT)
  Serial.println("USE_HSPI_PORT = DEFINED");
#else
  Serial.println("USE_HSPI_PORT = NOT DEFINED");
#endif

  Serial.print("TFT_MOSI = "); Serial.println(TFT_MOSI);
  Serial.print("TFT_SCLK = "); Serial.println(TFT_SCLK);
  Serial.print("TFT_CS   = "); Serial.println(TFT_CS);
  Serial.print("TFT_DC   = "); Serial.println(TFT_DC);
#if defined(TFT_MISO)
  Serial.print("TFT_MISO = "); Serial.println(TFT_MISO);
#endif

  // Print the D-macro GPIO mappings for verification
  Serial.println("\n--- XIAO D-pin to GPIO mapping ---");
  Serial.print("D1  = "); Serial.println(D1);
  Serial.print("D3  = "); Serial.println(D3);
  Serial.print("D6  = "); Serial.println(D6);
  Serial.print("D8  = "); Serial.println(D8);
  Serial.print("D9  = "); Serial.println(D9);
  Serial.print("D10 = "); Serial.println(D10);

  Serial.println("\n--- Initializing display ---");
  tft.init();
  Serial.println("tft.init() complete");

  tft.setRotation(0);
  Serial.println("setRotation(0) complete");

  Serial.println("\n--- Filling screen RED ---");
  tft.fillScreen(TFT_RED);
  Serial.println("fillScreen(TFT_RED) complete");
  Serial.println("\nIf screen is NOT red, the SPI communication is broken.");
  Serial.println("If screen IS red, the display works and the bug is elsewhere in osu-v4.");
  Serial.println("\nWill cycle colors every 2 seconds...");
}

void loop() {
  delay(2000);
  Serial.println("-> RED");
  tft.fillScreen(TFT_RED);

  delay(2000);
  Serial.println("-> GREEN");
  tft.fillScreen(TFT_GREEN);

  delay(2000);
  Serial.println("-> BLUE");
  tft.fillScreen(TFT_BLUE);

  delay(2000);
  Serial.println("-> WHITE");
  tft.fillScreen(TFT_WHITE);
}
