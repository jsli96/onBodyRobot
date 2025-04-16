from flask import Flask, send_from_directory, jsonify, request
from flask_cors import CORS
import os
import serial
import time
import threading
import socket
import os
import requests
from glob import glob
from werkzeug.utils import secure_filename

# Get the absolute path of the "data" folder
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_FOLDER = os.path.join(BASE_DIR, "data")
UPLOAD_FOLDER = os.path.join(BASE_DIR, "pics")

app = Flask(__name__)
CORS(app)  # Allow cross-origin requests from any domain

ESP32_IP = "192.168.1.50"
ESP32_PORT = 3333

app.config['UPLOAD_FOLDER'] = UPLOAD_FOLDER

@app.route('/upload_image', methods=['POST'])
def upload_image():
    # Ensure a file was provided.
    if 'file' not in request.files:
        return jsonify({"error": "No file part in the request"}), 400
    file = request.files['file']
    if file.filename == '':
        return jsonify({"error": "No file selected"}), 400

    try:
        # Accept only PNG files.
        if not file.filename.lower().endswith('.png'):
            return jsonify({"error": "Only PNG files are allowed."}), 400

        filename = secure_filename(file.filename)
        image_path = os.path.join(app.config['UPLOAD_FOLDER'], filename)
        
        # Save the file to the pics folder.
        file.save(image_path)
        print("Saved file to:", image_path)
        
        # Now forward the file to the ESP32's upload endpoint.
        # (Adjust the ESP32 URL/port if necessary.)
        esp32_url = "http://192.168.1.50/upload_image"
        with open(image_path, 'rb') as f:
            files = {'file': (filename, f, file.content_type)}
            response = requests.post(esp32_url, files=files)
        
        return jsonify({
            "message": "Image saved and forwarded successfully",
            "filename": filename,
            "esp32_response": response.text
        }), 200
    except Exception as e:
        print("Exception in upload_image:", e)
        return jsonify({"error": str(e)}), 500

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


@app.route('/upload_and_convert', methods=['POST'])
def upload_and_convert():
    # Check if file is in the request.
    if 'file' not in request.files:
        return jsonify({"error": "No file part in the request"}), 400
    
    file = request.files['file']
    if file.filename == '':
        return jsonify({"error": "No file selected"}), 400

    try:
        # Read the file as bytes.
        file_bytes = file.read()
        # Convert to header content.
        header_content = convert_image_to_header(file_bytes, file.filename)
        # Use the original filename (without extension) as the array name.
        array_name = os.path.splitext(file.filename)[0]
        header_filename = array_name + ".h"
        header_file_path = os.path.join(app.config['UPLOAD_FOLDER'], header_filename)
        # Write the new header file to disk.
        with open(header_file_path, 'w') as f:
            f.write(header_content)
        
        # Regenerate my_images.h to incorporate the new header.
        regenerate_my_images_header()
        
        return jsonify({
            "message": "Converted header saved and my_images.h updated",
            "header_file": header_filename,
            "saved_to": header_file_path
        }), 200
    except Exception as e:
        return jsonify({"error": str(e)}), 500


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
