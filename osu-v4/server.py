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

# # Open a serial connection to the robot.
# try:
#     # Replace with your actual serial port, e.g., '/dev/tty.usbmodem1101'
#     ser = serial.Serial(port='/dev/cu.usbmodem101', baudrate=115200, timeout=1)
#     time.sleep(2)  # Allow time for the connection to establish
#     print("Serial connection established on", ser.port)
# except Exception as e:
#     print("Error opening serial port:", e)
#     ser = None

# def read_from_serial(ser):
#     while True:
#         try:
#             if ser.in_waiting > 0:
#                 line = ser.readline().decode('utf-8').strip()
#                 if line:
#                     print("Serial:", line)
#             else:
#                 time.sleep(0.1)
#         except Exception as e:
#             print("Error reading from serial:", e)
#             time.sleep(1)

# # Start the background thread if the serial port is available.
# if ser is not None:
#     threading.Thread(target=read_from_serial, args=(ser,), daemon=True).start()

@app.route('/')
def index():
    return send_from_directory(DATA_FOLDER, "index.html")

@app.route('/<path:filename>')
def serve_file(filename):
    return send_from_directory(DATA_FOLDER, filename)

# @app.route('/execute/<command>', methods=['GET'])
# def execute_command(command):
#     global ser
#     if ser is None:
#         return jsonify({"error": "Serial connection not available"}), 500

#     command_str = command + "\n"  # Append newline as needed by ESP32 parser
#     try:
#         ser.write(command_str.encode())
#         return jsonify({"status": "Command sent", "command": command}), 200
#     except Exception as e:
#         return jsonify({"error": str(e)}), 500

@app.route('/execute/<command>', methods=['GET'])
def execute_command(command):
    if send_command_via_wifi(command):
        return jsonify({"status": "Command sent", "command": command}), 200
    else:
        return jsonify({"error": "Failed to send command via WiFi"}), 500

if __name__ == "__main__":
    app.run(host='0.0.0.0', port=5001, debug=True)
