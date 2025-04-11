from flask import Flask, send_from_directory, jsonify, request
from flask_cors import CORS
import os
import serial
import time
import threading
import socket
import os
from glob import glob

# Get the absolute path of the "data" folder
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_FOLDER = os.path.join(BASE_DIR, "data")

app = Flask(__name__)
CORS(app)  # Allow cross-origin requests from any domain


ESP32_IP = "192.168.1.50"
ESP32_PORT = 3333

# Save in the directory where server.py is located.
UPLOAD_FOLDER = os.path.dirname(os.path.abspath(__file__))
app.config['UPLOAD_FOLDER'] = UPLOAD_FOLDER


def regenerate_my_images_header():
    """
    Scans the upload directory for all header files (.h) except my_images.h and
    lv_xiao_round_screen.h, then generates a master header file (my_images.h)
    listing all found headers.
    """
    upload_folder = app.config['UPLOAD_FOLDER']
    header_pattern = os.path.join(upload_folder, "*.h")
    # Exclude my_images.h and lv_xiao_round_screen.h
    excluded_files = {"my_images.h", "lv_xiao_round_screen.h"}
    header_files = sorted([
        f for f in glob(header_pattern)
        if os.path.basename(f) not in excluded_files
    ])
    
    # Generate include statements for each header.
    includes = "\n".join(f'#include "{os.path.basename(f)}"' for f in header_files)
    
    # Build the array elements based on the filename (without extension).
    image_names = ", ".join(os.path.splitext(os.path.basename(f))[0] for f in header_files)
    image_sizes = ", ".join(f"sizeof({os.path.splitext(os.path.basename(f))[0]})" for f in header_files)
    image_count = len(header_files)
    
    header_content = f"""#ifndef MY_IMAGES_H
#define MY_IMAGES_H

{includes}

static const uint8_t* images[] = {{ {image_names} }};
static const size_t imageSizes[] = {{ {image_sizes} }};
static const int imageCount = {image_count};

#endif
"""
    master_header_path = os.path.join(upload_folder, "my_images.h")
    with open(master_header_path, "w") as f:
        f.write(header_content)
    print("my_images.h regenerated successfully.")


def convert_image_to_header(file_bytes, original_filename):
    total_bytes = len(file_bytes)
    # Use the original file name (without extension) as the array name.
    array_name = os.path.splitext(original_filename)[0]
    # Build the header content.
    header_lines = []
    header_lines.append(f"// array size is {total_bytes}")
    header_lines.append(f"static const unsigned char {array_name}[] PROGMEM = {{")
    
    bytes_per_line = 16
    for i, byte in enumerate(file_bytes):
        # Convert byte to hex string.
        hex_str = f"0x{byte:02x}"
        # Determine if we need a comma. (Don't add a comma after the last byte)
        if i < total_bytes - 1:
            hex_str += ", "
        else:
            hex_str += ""
        # Append the hex value to the current line.
        # Start a new line every bytes_per_line entries.
        if (i % bytes_per_line) == 0:
            header_lines.append("    " + hex_str)
        else:
            header_lines[-1] += hex_str

    header_lines.append("};\n")
    return "\n".join(header_lines)

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
