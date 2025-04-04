from flask import Flask, send_from_directory, jsonify, request
import os
import serial
import time
import threading
import socket

# Get the absolute path of the "data" folder
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_FOLDER = os.path.join(BASE_DIR, "data")

app = Flask(__name__)

ESP32_IP = "172.20.10.10"
ESP32_PORT = 3333

def send_command_via_wifi(command):
    command_str = command + "\n"  # Append newline for the ESP32 command parser
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((ESP32_IP, ESP32_PORT))
            s.sendall(command_str.encode())
            # Optionally, you could wait and read a response here
        return True
    except Exception as e:
        print("Error sending command via wifi:", e)
        return False

@app.route('/')
def index():
    return send_from_directory(DATA_FOLDER, "index.html")

@app.route('/<path:filename>')
def serve_file(filename):
    return send_from_directory(DATA_FOLDER, filename)

@app.route('/execute/<command>', methods=['GET'])
def execute_command(command):
    if command.startswith("CHECK_COLOR:"):
        expected_color = command.split(":", 1)[1]
        print("Received color check for:", expected_color)
        # (Optionally: query the actual LED color if your ESP32 supports it)
        # For now, just return it as confirmation
        return jsonify({"status": "Color check received", "expected_color": expected_color})

    if send_command_via_wifi(command):
        return jsonify({"status": "Command sent", "command": command}), 200
    else:
        return jsonify({"error": "Failed to send command via WiFi"}), 500


if __name__ == "__main__":
    app.run(host='0.0.0.0', port=5001, debug=True)
