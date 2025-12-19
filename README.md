## 🛠 Step 1 — Install Arduino Library Dependencies
This project requires several Arduino libraries to be installed before uploading firmware to the ESP32-S3.

A bundled archive of all required libraries is provided as:
```python
libraries.zip

1. Download and unzip libraries.zip
2. Copy the extracted folders into your Arduino libraries directory
3. Restart the Arduino IDE completely
4. Open osu-v4.ino and verify it compiles without missing-library errors

⚠️ Missing libraries will cause compile errors when uploading to the ESP32.

## 🛠 Step 2 — Install Python Dependencies

Inside the **OSU-V4** folder, run:

```bash
pip install -r requirements.txt
```

This installs:
- **flask** — runs the local web server  
- **flask-cors** — enables cross-origin requests  
- **requests** — used for sending HTTP requests  
- **werkzeug** — utilities used internally by Flask

## 🔧 Step 3 — Update WiFi / Hotspot Credentials (On the ESP32)

In `osu-v4.ino`, modify:

```cpp
const char* ssid = "YOUR_WIFI";
const char* password = "YOUR_PASSWORD";
```

Re-upload the firmware to the ESP32.

When the board boots, check the Serial Monitor — it will print the new assigned IP address, which must match the ROBOT_IP in server.py (next step).

## 🔧 Step 4 — Update the Robot IP 

In `server.py`, update:

```python
ESP32_IP = "192.168.x.x"
ESP32_PORT = 3333
```

Set ESP32_IP to the ESP32’s WiFi IP (printed in the Serial Monitor after the robot connects).

## 🌐 Step 5 — Run the Local Server

The project includes a Flask backend that:

- Hosts the Blockly interface  
- Translates Blockly → Python → Robot commands  
- Forwards commands to the ESP32 robot via TCP  

Start the server with:

```bash
python server.py
```

If your environment is active, you'll see output similar to:
```python
 * Running on http://127.0.0.1:5001
 * Running on http://<your-local-ip>:5001
```

## ✔️ Access the Blockly UI

On your computer:

```python
http://127.0.0.1:5001
```

From another device on the same WiFi:

```python
http://<your-local-ip>:5001
```

Example:
```python
http://10.175.8.103:5001
```

## 🗑️ Reset / Clear Uploaded Pictures (SPIFFS)

By default, uploaded images are stored persistently in SPIFFS and will remain on the ESP32 across reboots.

If you want to **delete all uploaded pictures** and start fresh:

1. Open the osu-v4.ino.
2. Locate the SPIFFS initialization code:
   ```cpp
   if (!SPIFFS.begin(true)) {  
     Serial.println("Failed to mount SPIFFS");
     return;
   }
   Serial.println("SPIFFS mounted successfully.");

   // clearSPIFFS();  // ← Uncomment to delete all images

## 📝 Notes

- If you need to modify the **UI**, most front-end files are located inside the **`data/`** folder.  
- The primary entry point for UI updates is **`index.html`**, where you can adjust layout, buttons, styling, and Blockly behavior.  
- Any images or assets displayed on the robot’s screen are typically stored in the **`pics/`** directory.  
- To **add a new picture to the robot**, open the UI → go to **Options (3 dots on the top left)** → click the **“Load picture file”** icon. This will let you choose an image from your computer and upload it to SPIFFS.
- After making UI changes, simply refresh the browser — no need to restart the server unless backend logic was modified.
- Commands are sent **over WiFi**, so the robot **does not need to be physically plugged into the same laptop** that is controlling it. It only needs to be connected to **a power source** and successfully joined to the same WiFi network.  


