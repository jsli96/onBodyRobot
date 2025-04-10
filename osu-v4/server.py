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

ESP32_IP = "192.168.1.50"
ESP32_PORT = 3333

def send_command_via_wifi(command):
    command_str = command + "\n"  # Append newline for the ESP32 command parser
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.connect((ESP32_IP, ESP32_PORT))
            s.sendall(command_str.encode())
            # Set a timeout so we don't block indefinitely
            s.settimeout(2.0)
            response = s.recv(1024).decode().strip()
            return response  # Return the actual response from the ESP32
    except Exception as e:
        print("Error sending command via wifi:", e)
        return None


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
        # Send the command to the ESP32 and get the actual LED color back.
        response = send_command_via_wifi(command)
        # If the ESP32 is written to respond to CHECK_COLOR, then response will contain the current LED color.
        if response is not None:
            return jsonify({"status": "Color check response", "LED_color": response})
        else:
            return jsonify({"error": "Failed to retrieve LED color"}), 500

    response = send_command_via_wifi(command)
    if response is not None:
        return jsonify({"status": "Command sent", "command": command, "response": response}), 200
    else:
        return jsonify({"error": "Failed to send command via WiFi"}), 500


if __name__ == "__main__":
    app.run(host='0.0.0.0', port=5001, debug=True)
