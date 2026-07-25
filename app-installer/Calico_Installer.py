import platform
import shutil
from pathlib import Path
import sys
import subprocess
import tkinter as tk
from tkinter import ttk
import json
import serial
from serial.tools import list_ports
import re
import webbrowser

SANDBOX = True # Set to False for actual installation
BASE_DIR = (
    Path.home() / "Downloads" / "INSTALL_TEST"
    if SANDBOX
    else Path.home() / "Documents"
)
FQBN = "esp32:esp32:XIAO_ESP32S3"
CONFIG_FILE = (
    BASE_DIR / "Arduino" / "config.json"
)

system = platform.system()

def get_resource(path):
    if not getattr(sys, "frozen", False):
        return Path(__file__).parent / path
    return Path(sys._MEIPASS) / path

def check_arduino_ide():
    possible_paths = []
    home = Path.home()

    if system == "Windows":
        possible_paths = [
            r"C:\Program Files\Arduino IDE",
            r"C:\Program Files (x86)\Arduino IDE",
            home / "AppData/Local/Programs/Arduino IDE",
            home / "AppData/Local/Programs/Arduino IDE 2.0",
        ]

    elif system == "Darwin":  # macOS
        possible_paths = [
            "/Applications/Arduino IDE.app",
            "/Applications/Arduino.app",
            home / "Applications/Arduino IDE.app"
        ]
        
    print("\nSearching Arduino IDE...")

    for path in possible_paths:
        path = Path(path)

        if path.exists():
            print("Arduino IDE found at:", path)
            return path
    
    if shutil.which("arduino") or shutil.which("arduino-ide"):
        print("Arduino IDE found in system PATH")
        return path

    print("Arduino IDE NOT found")
    return None

def bootstrap_check():
    print("Checking system requirements...")

    arduino_path = check_arduino_ide()

    if not arduino_path:
        print("Arduino IDE NOT found.")
        webbrowser.open("https://www.arduino.cc/en/software")

    return arduino_path

def install_libraries():
    src = get_resource("libraries")

    arduino_lib = BASE_DIR / "Arduino" / "libraries"
    arduino_lib.mkdir(parents=True, exist_ok=True)

    def ignore_files(dir, files):
        ignored = []

        for f in files:
            if (
                f == ".DS_Store"
                or f.startswith("._")
                or f == "Thumbs.db"
            ):
                ignored.append(f)

        return ignored

    shutil.copytree(
        src,
        arduino_lib,
        dirs_exist_ok=True,
        ignore=ignore_files
    )

    print("Libraries installed to:", arduino_lib)

def install_project():
    src = get_resource("osu-v4")

    dest = BASE_DIR / "Arduino" / "osu-v4"
    dest.mkdir(parents=True, exist_ok=True)

    shutil.copytree(src, dest, dirs_exist_ok=True)

    print("Project installed:", dest)
    return dest
    
def patch_ino_wifi(project_path, ssid, password):
    ino_path = project_path / "osu-v4.ino"

    content = ino_path.read_text(encoding="utf-8")

    content = re.sub(
        r'const char\* ssid = ".*?";',
        f'const char* ssid = "{ssid}";',
        content
    )

    content = re.sub(
        r'const char\* password = ".*?";',
        f'const char* password = "{password}";',
        content
    )

    ino_path.write_text(content, encoding="utf-8")

    print("Updated WiFi credentials in .ino")
    
def launch_arduino(project_path, arduino_path):
    ino_path = project_path / "osu-v4.ino"

    if system == "Windows":
       subprocess.Popen([
            str(arduino_path / "Arduino IDE.exe"),
            str(ino_path)
    ])

    elif system == "Darwin":
        subprocess.Popen([
            "open",
            "-a",
            "Arduino IDE",
            str(ino_path)
        ])

    print("Arduino IDE launched")
    
def compile_firmware(project_path):
    arduino_cli = str(get_resource("arduino-cli.exe"))
    library_path = BASE_DIR / "Arduino" / "libraries"

    print("Compiling firmware...")

    subprocess.run([
        arduino_cli,
        "compile",
        "--fqbn",
        FQBN,
        "--libraries",
        str(library_path),
        str(project_path)
    ], check=True)

    print("Compile successful")
    
def upload_firmware(project_path, port):
    arduino_cli = str(get_resource("arduino-cli.exe"))
    print("Uploading firmware...")

    subprocess.run([
        arduino_cli,
        "upload",
        "-p",
        port,
        "--fqbn",
        FQBN,
        str(project_path)
    ], check=True)

    print("Upload successful")
    
def detect_esp32_port():
    ports = list_ports.comports()

    for port in ports:
        desc = port.description.lower()

        if (
            "esp32" in desc
            or "cp210" in desc
            or "ch340" in desc
            or "usb serial" in desc
        ):
            print("ESP32 found on:", port.device)
            return port.device

    raise Exception("ESP32 board not found")

def wait_for_esp32_ip(port):
    print("Waiting for ESP32 IP...")

    ser = serial.Serial(port, 115200, timeout=1)

    while True:
        line = ser.readline().decode(errors="ignore").strip()

        if line:
            print(line)

        match = re.search(
            r"ESP32_IP=([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+)",
            line
        )

        if match:
            ip = match.group(1)

            print("ESP32 IP:", ip)

            return ip
    
def save_config(esp_ip):
    CONFIG_FILE.parent.mkdir(parents=True, exist_ok=True)

    with open(CONFIG_FILE, "w") as f:
        json.dump(
            {
                "esp_ip": esp_ip
            },
            f,
            indent=4
        )
    
def submit():
    ssid = ssid_var.get().strip()
    password = password_var.get().strip()

    if not ssid or not password:
        status_label.config(
            text="Please enter WiFi SSID and password."
        )
        return

    try:
        project_path = BASE_DIR / "Arduino" / "osu-v4"
        libraries_dir = BASE_DIR / "Arduino" / "libraries"
        
        if not libraries_dir.exists():
            install_libraries()

        if not project_path.exists():
            project_path = install_project()
        
        status_label.config(text="Patching firmware...")
        root.update()
        
        ino_file = project_path / "osu-v4.ino"

        if not ino_file.exists():
            raise Exception(f"Firmware file not found:\n{ino_file}")
        
        patch_ino_wifi(project_path, ssid, password)

        status_label.config(text="Detecting ESP32...")
        root.update()

        port = detect_esp32_port()

        status_label.config(text="Compiling firmware...")
        root.update()

        compile_firmware(project_path)

        status_label.config(text="Uploading firmware...")
        root.update()

        upload_firmware(project_path, port)

        status_label.config(text="Waiting for ESP32 IP...")
        root.update()

        esp_ip = wait_for_esp32_ip(port)
        
        status_label.config(text=f"ESP32 connected:\n{esp_ip}")
        root.update()
        
        save_config(esp_ip)

        status_label.config(text="Windows may ask for firewall permission.\nPlease click Allow.")
        root.update()
        
        status_label.config(text="Setup complete.\nYou may now open Calico Launcher.")
        root.update()

    except Exception as e:
        status_label.config(text=f"Error: {e}")
        root.update()

if __name__ == "__main__":
    arduino_path = bootstrap_check()
    if not arduino_path:
        print("Please install Arduino IDE and run the installer again.")
        sys.exit(1)      
    
    root = tk.Tk()
    root.configure(bg="white")
    root.title("Calico Installer")
    root.geometry("550x400")
    root.resizable(False, False)
    
    style = ttk.Style()
    style.theme_use("clam")

    style.configure("Green.TButton", foreground="white", background="green",font=("Segoe UI", 10, "bold"), padding=10)

    ssid_var = tk.StringVar()
    password_var = tk.StringVar()

    ttk.Label(root, text="WiFi SSID/Name", font=("Segoe UI", 12, "bold"), background="white").pack(pady=10)
    ttk.Entry(root, textvariable=ssid_var, font=("Segoe UI", 12)).pack(fill="x", padx=30, ipady=6)

    ttk.Label(root, text="WiFi Password", font=("Segoe UI", 12, "bold"), background="white").pack(pady=5)
    ttk.Entry(root, textvariable=password_var, show="*", font=("Segoe UI", 12)).pack(fill="x", padx=30, ipady=6)

    ttk.Button(root, text="Install", command=submit, style="Green.TButton").pack(pady=20)

    status_label = ttk.Label(root, text="", font=("Segoe UI", 11), background="white")
    status_label.pack()

    root.mainloop()