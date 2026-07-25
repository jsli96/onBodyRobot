import os
import sys
from pathlib import Path
import webbrowser
import subprocess
import tkinter as tk
from tkinter import ttk
from pathlib import Path
import json
from serial.tools import list_ports
import time
import webbrowser
import socket

SANDBOX = True # Set to False for actual installation
BASE_DIR = (
    Path.home() / "Downloads" / "INSTALL_TEST"
    if SANDBOX
    else Path.home() / "Documents"
)
CONFIG_FILE = (
    BASE_DIR / "Arduino" / "config.json"
)

def get_resource(path):
    if hasattr(sys, '_MEIPASS'):
        return Path(sys._MEIPASS) / path
    return Path(__file__).parent / path

def load_config():
    if CONFIG_FILE.exists():
        with open(CONFIG_FILE) as f:
            return json.load(f)

    return {}

def esp32_online(ip):
    try:
        with socket.create_connection((ip, 3333), timeout=2):
            return True

    except:
        return False
    
def wait_for_server(host="127.0.0.1", port=5001, timeout=30):
    start = time.time()

    while time.time() - start < timeout:
        try:
            with socket.create_connection((host, port), timeout=1):
                return True
        except:
            time.sleep(0.5)

    return False

def launch_server(esp_ip=None):
    server_exe = get_resource("osu_server")

    env = os.environ.copy()

    if esp_ip:
        env["ESP32_IP"] = esp_ip

    subprocess.Popen(
        [str(server_exe)],
        cwd=str(server_exe.parent),
        env=env
    )

def open_web_interface():
    webbrowser.open("http://127.0.0.1:5001")
    
def run_installer():
    installer = Path(sys.executable).parent / "Calico_Installer"
    subprocess.Popen([str(installer)])

def submit():
    try:

        config = load_config()
        esp_ip = config.get("esp_ip")

        if not esp_ip:
            status_label.config(text=("No device configured.\n"
                                      "Run Calico Installer."))
            return

        status_label.config(text="Checking device...")
        root.update()

        if not esp32_online(esp_ip):
            status_label.config(text=("Device not reachable.\n"
                                      "Run Installer again if your wifi settings changed."))
            
            if not reinstall_button.winfo_ismapped():
                reinstall_button.pack(pady=10)
                
            return

        if not server_running():
            launch_server(esp_ip)

        if wait_for_server():
            status_label.config(text="Calico Ready")

            if not open_button.winfo_ismapped():
                open_button.pack(pady=10)

            webbrowser.open("http://127.0.0.1:5001")
        else:
            status_label.config(text="Server failed to start.")

    except Exception as e:
        status_label.config(text=f"Error: {e}")

def server_running():
    try:
        with socket.create_connection(("127.0.0.1", 5001), timeout=1):
            return True
    except:
        return False

if __name__ == "__main__":
    global open_button

    root = tk.Tk()
    root.configure(bg="white")
    root.title("Calico Launcher")
    root.geometry("550x400")
    root.resizable(False, False)
    
    style = ttk.Style()
    style.theme_use("clam")

    style.configure("Green.TButton", foreground="white", background="green",font=("Segoe UI", 10, "bold"), padding=10)

    ttk.Button(root, text="Launch Calico", command=submit, style="Green.TButton").pack(pady=20)
    
    open_button = ttk.Button(root, text="Open Calico Site", command=open_web_interface, style="Green.TButton")
    reinstall_button = ttk.Button(root, text="Run Installer", command=run_installer, style="Green.TButton")
    open_button.pack_forget()
    reinstall_button.pack_forget()

    status_label = ttk.Label(root, text="", font=("Segoe UI", 11), background="white")
    status_label.pack()
    status_label.config(text="Click 'Launch Calico' to connect.")

    root.mainloop()