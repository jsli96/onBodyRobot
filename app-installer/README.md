# Calico App Installer Guide (Please Read)

This is a guide to package the Calico project into a user-friendly application. If you are a developer who would like to replicate the work, please continue with the steps below. If you are an end-user, please skip to the bottom where it says **For End-Users**.

# For Developers

## Purpose

This project is **not the main Calico implementation**.

Instead, it serves as a prototype for:
- Packaging the main project into a single application
- Simplifying the user experience
- Providing a user-friendly interface
- Exploring how users could interact with the system without manually running individual scripts

## Requirements

Before starting, make sure you have:
- Arduino CLI
- Python 3.x
- Required Python dependencies

### Arduino CLI

The application uses Arduino CLI to compile and upload the firmware to the Calico robot.

Install Arduino CLI and make sure the `arduino-cli` command is available through your system PATH.

Verify the installation by running:

```bash
arduino-cli version
```

### Python Dependencies

The application uses the following Python libraries:

#### Standard Library

These libraries are included with Python and do not need to be installed separately:

- `platform`
- `shutil`
- `pathlib`
- `sys`
- `subprocess`
- `tkinter`
- `json`
- `re`
- `webbrowser`
- `os`
- `time`
- `socket`

#### External Libraries

The following libraries must be installed:

- `pyserial`

Install the external dependency using:

```bash
pip install pyserial
```

## Developer Build Instructions

This section explains how to package the Calico project into a user-friendly application.

The application is composed of two Python programs:

1. `Calico_Installer.py`
   - Downloads and prepares the required files and folders.
   - Collects the user's Wi-Fi credentials and ESP32 IP address.
   - Installs and prepares the required components.

2. `Calico_Launcher.py`
   - Launches the packaged Flask server.
   - Opens the Calico website in the user's browser.

The Flask server is based on the `server.py` file from the original `osu-v4` project.

---

## Step 1: Prepare the `osu-v4` project

Before packaging the application, make the following changes to the original `osu-v4` project.

### `data` and `pics` Folders

The `data` and `pics` folders are provided as part of the original `osu-v4` project.

These folders are required by `server.py` and should remain inside the `osu-v4` project directory.

```text
osu-v4/
├── data/
├── pics/
├── osu-v4.ino
└── server.py
```

### Modify `osu-v4.ino` file (Windows & macOS)

Open the `osu-v4.ino` file and locate the following section:

```cpp
// ---- Setup -----
```

Under the `// WiFi` section, locate the following code:

```cpp
while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
}
Serial.println();
```

Immediately after this code, add:

```cpp
Serial.print("Connected! IP=");
Serial.println(WiFi.localIP());
```

The resulting code should look like:

```cpp
while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
}
Serial.println();

Serial.print("Connected! IP=");
Serial.println(WiFi.localIP());
```

This modification allows the ESP32 to print its local IP address after successfully connecting to Wi-Fi. The IP address is required by the Calico application to communicate with the ESP32.

### Modify `server.py` (macOS only)

Before packaging `server.py` with PyInstaller, add the following import near the other imports:

```python
import multiprocessing
```

Then locate the section where the server is started:

```python
if __name__ == "__main__":
```

Add:

```python
multiprocessing.freeze_support()
```

The resulting section should look like:

```python
if __name__ == "__main__":
    multiprocessing.freeze_support()
    # Start the server
```

`multiprocessing.freeze_support()` is required to ensure that the packaged server works correctly when bundled into an executable with PyInstaller.

> **Note:** These modifications should be made before packaging `server.py` into the `osu_server` executable.

## Step 2: Prepare the Flask server

Before packaging the installer and launcher, the original `server.py` from the `osu-v4` project must first be packaged into an executable named:

```text
osu_server
```

On Windows, run the following command to bundle `server.py`:

```bash
pyinstaller --onefile --name osu_server --add-data "data;data" --add-data "pics;pics" --hidden-import=flask --hidden-import=flask_cors --hidden-import=werkzeug --hidden-import=requests server.py
```

On macOS, run the following command:
```bash
pyinstaller --onefile --name osu_server --add-data "data:data" --add-data "pics:pics" --hidden-import=flask --hidden-import=flask_cors --hidden-import=werkzeug --hidden-import=requests server.py
```

> **Note:** The `data` and `pics` here are the folders provided in `osu-v4` project. Now, you can find `osu_server.exe` (Windows) or `osu_server` (macOS) inside the `dist` folder. The generated executable must then be provided to the next packaging step for `Calico_Installer.py` and `Calico_Launcher.py`.

## Step 3: Bundle the `Calico_Installer.py`:

On Windows, run the following command:

```bash
pyinstaller --onefile --add-data "libraries;libraries" --add-data "osu-v4;osu-v4" --add-binary "C:\Users\...\arduino-cli\arduino-cli.exe;." --add-binary "osu_server.exe;." --collect-all serial --collect-all tkinter Calico_Installer.py
```

On macOS, run the following command:
```bash
pyinstaller --onefile --add-data "libraries:libraries" --add-data "osu-v4:osu-v4" --add-binary "/opt/homebrew/arduino-cli:." --add-binary "osu_server:." --collect-all serial --collect-all tkinter Calico_Installer.py
```

> **Note:** Make sure the executable file `osu_server.exe` (Windows) or `osu_server` (macOS) is reachable in the current directory where you're running the commands by providing the full path to that file, or simply moving it outside of `dist`. In addition, make sure you provide the full path to `arduino-cli` (where you installed it initially).

## Step 4: Bundle the `Calico_Launcher.py`:

On Windows, run the command:
```bash
pyinstaller --onefile --add-binary "osu_server.exe;." --hidden-import=flask Calico_Launcher.py 
```

On macOS, run the command:
```bash
pyinstaller --onefile --add-binary "osu_server:." --hidden-import=flask Calico_Launcher.py 
```

Now, you should be able to find `Calico_Installer` and `Calico_Launcher` inside the `dist` folder. These are the applications that let end users set up the wifi credentials (Installer) and launch the Calico server (Launcher). Run the `Calico_Installer` first before `Calico_Launcher`. If users change locations, they can run the `Calico_Installer` again to enter the new wifi credentials, which should skip the entire downloading of the `osu-v4` folder and straight to entering new wifi credentials.

# For End-Users

## Download

Download the appropriate version for your operating system from the
[latest GitHub Release](../../releases).

### Windows

Download:

- `Calico_Installer.exe`
- `Calico_Launcher.exe`

### macOS

Download:

- `Calico_Installer`
- `Calico_Launcher`

Once downloaded, make sure you connect your laptop device to the Calico robot first, then you can launch the first application `Calico_Installer` to set up wifi credentials, and then launch the second application `Calico_Launcher` to open Calico's server. If wifi's location changes, run the `Calico_Installer` again to enter the new wifi credentials, which should skip the entire downloading of the `osu-v4` folder and straight to entering new wifi credentials. Then launch `Calico_Launcher` again.
