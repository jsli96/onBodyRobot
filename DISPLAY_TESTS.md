# Display drawing and animation test sketches

These are standalone Arduino sketches for the Seeed XIAO ESP32-S3 with the
240 × 240 round GC9A01 display. Open and upload one sketch at a time; uploading
another sketch replaces the firmware currently on the board. Use the Arduino
Serial Monitor at 115200 baud for diagnostic messages.

## Setup

1. In the Arduino IDE, select the XIAO ESP32-S3 board and its serial port.
2. Install/configure `TFT_eSPI` for the round GC9A01 display. Its driver, SPI
   pins, and backlight setup must match the XIAO round display. The existing
   project [README](README.md) describes the bundled Arduino libraries.
3. For the two `robot-control` sketches, install `lvgl`,
   `Adafruit_MCP23X17`, and the SparkFun `ICM-20948` library. The heartbeat
   sketch also needs `FastLED`. The PNG animation sketch also needs `PNGdec`.
   Keep each sketch's `lv_xiao_round_screen.h` alongside its `.ino` file.
4. The `robot-control` sketches use the round display's touch input. They also
   check for an MCP23017 motor controller and an ICM-20948 IMU; missing devices
   are reported in the Serial Monitor. GO still advances to the animation after
   five seconds if the motor controller is absent.

## Display color diagnostic

Open [`display-test/display-test.ino`](display-test/display-test.ino) and upload
it. The screen should show red, green, blue, and white in sequence, changing
every two seconds. The Serial Monitor prints the TFT_eSPI driver and pin setup.
This checks the display connection before trying the animation sketches.

## Procedural pixel drawing: beating heart

Open
[`osu-v5/robot-control-heartbeat/robot-control-heartbeat.ino`](osu-v5/robot-control-heartbeat/robot-control-heartbeat.ino)
and upload it. Tap **GO** in the top half of the screen. After five seconds,
the sketch draws a beating heart with scanlines and animates it without external
image files. Double-tap the heart to return to GO/STOP. Tap **STOP** during the
five-second interval to cancel. On the GO/STOP screen, swipe left to see IMU
data; swipe right to return. To change the heart rate, send a number from 30
to 220 through the Serial Monitor.

## PNG frame animation

Open [`osu-v5/robot-control/robot-control.ino`](osu-v5/robot-control/robot-control.ino)
and upload it after putting frames in the board's SPIFFS partition. This sketch
looks for consecutive files `/anim/frame_001.png`, `/anim/frame_002.png`, and
so on. It stops scanning at the first missing number, with a maximum of 200
frames. Tap **GO**; after five seconds, the frames loop on the display.
Double-tap the animation to return to GO/STOP. The same STOP and IMU gestures
described above apply.

To make frames from your own video:

1. Install Python packages: `python3 -m pip install opencv-python Pillow`.
2. From the repository root, run:

   ```bash
   python3 osu-v5/robot-control/convert_frames.py input.mp4 osu-v5/robot-control/data/anim --fps 8 --size 240 --max-total-kb 1400
   ```

3. Check that the generated PNGs fit the SPIFFS partition selected for the
   board. The 1400 KB limit is a converter default, not a guarantee of space
   on every partition layout.
4. Upload the contents of `osu-v5/robot-control/data/` to SPIFFS using a
   compatible ESP32 sketch data upload tool, preserving the `anim/` directory.
   Upload the `robot-control.ino` firmware without erasing flash or changing
   the partition layout afterward. Reboot the board and check the Serial
   Monitor for `Found N animation frames.`

Generated PNGs and source videos are local test data. They are not needed in
the repository to build the sketches; generate and upload your own frames for
the PNG test.
