from flask import Flask, send_from_directory, jsonify, request
from flask_cors import CORS
import os
import time
import socket
import requests
from glob import glob
from werkzeug.utils import secure_filename
from urllib.parse import unquote

# --- Paths ---
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_FOLDER = os.path.join(BASE_DIR, "data")
UPLOAD_FOLDER = os.path.join(BASE_DIR, "pics")
os.makedirs(UPLOAD_FOLDER, exist_ok=True)

# --- Network target (set ESP32_IP via env to avoid editing code) ---
ESP32_IP = os.environ.get("ESP32_IP", "172.20.10.13")  # <-- set to the IP printed by your ESP32
ESP32_PORT = 3333

app = Flask(__name__)
CORS(app)  # Allow cross-origin requests from any domain
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
        # Accept only PNG files
        if not file.filename.lower().endswith('.png'):
            return jsonify({"error": "Only PNG files are allowed."}), 400

        filename = secure_filename(file.filename)
        image_path = os.path.join(app.config['UPLOAD_FOLDER'], filename)

        # Save locally 
        file.save(image_path)
        print("Saved file to:", image_path)

        # Forward the file to the ESP32's upload endpoint.
        esp32_url = f"http://{ESP32_IP}/upload_image"
        with open(image_path, 'rb') as f:
            files = {'file': (filename, f, file.mimetype or 'image/png')}
            resp = requests.post(esp32_url, files=files, timeout=5)

        return jsonify({
            "message": "Image saved and forwarded successfully",
            "filename": filename,
            "esp32_ip": ESP32_IP,
            "esp32_response_status": resp.status_code,
            "esp32_response_text": resp.text
        }), 200
    except Exception as e:
        print("Exception in upload_image:", e)
        return jsonify({"error": str(e)}), 500


def send_command_via_wifi(command: str):
    """
    Send a single-line command to the ESP32 TCP server.
    For non-query commands (no GET_/CHECK_), treat fire-and-forget as success ("OK").
    """
    try:
        cmd = (command.rstrip(';') + "\n").encode()
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(1.5)  # set BEFORE connect so connect fails fast
            try:
                s.connect((ESP32_IP, ESP32_PORT))
            except Exception as e:
                print(f"[wifi] CONNECT failed to {ESP32_IP}:{ESP32_PORT} -> {e}")
                return None

            try:
                s.sendall(cmd)
                is_query = command.startswith("GET_") or command.startswith("CHECK_")
                if not is_query:
                    return "OK"  # fire-and-forget: don't wait for reply

                s.settimeout(1.5)
                data = s.recv(1024)
                return data.decode().strip() if data else "OK"
            except Exception as e:
                print(f"[wifi] SEND/RECV failed -> {e}")
                return None
    except Exception as e:
        print("Error sending command via wifi:", e)
        return None


@app.route('/')
def index():
    return send_from_directory(DATA_FOLDER, "index.html")


@app.route('/<path:filename>')
def serve_file(filename):
    return send_from_directory(DATA_FOLDER, filename)

# Accept any path chars
@app.route('/execute/<path:command>', methods=['GET'])
def execute_command(command):
    command = unquote(command).strip()
    if command.endswith(';'):
        command = command[:-1]

    if command.startswith("CHECK_COLOR:"):
        resp = send_command_via_wifi(command)
        if resp is not None:
            return jsonify({"status": "Color check response", "LED_color": resp}), 200
        else:
            return jsonify({"error": "Failed to retrieve LED color"}), 500

    resp = send_command_via_wifi(command)
    if resp is not None:
        return jsonify({"status": "Command sent", "command": command, "response": resp}), 200
    else:
        return jsonify({"error": f"Failed to send command via WiFi to {ESP32_IP}:{ESP32_PORT}"}), 500


if __name__ == "__main__":
    # Run on all interfaces so your browser at 127.0.0.1 can reach it
    app.run(host='0.0.0.0', port=5001, debug=True)
