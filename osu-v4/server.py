from flask import Flask, send_from_directory
import os

# Get the absolute path of the "data" folder
BASE_DIR = os.path.dirname(os.path.abspath(__file__))  # onBodyRobot directory
DATA_FOLDER = os.path.join(BASE_DIR, "data")  # Full path to "data" folder

app = Flask(__name__)

@app.route('/')
def index():
    return send_from_directory(DATA_FOLDER, "index.html")  # Serves main webpage

@app.route('/<path:filename>')
def serve_file(filename):
    return send_from_directory(DATA_FOLDER, filename)

if __name__ == "__main__":
    app.run(host='0.0.0.0', port=5001, debug=True)  # Runs on local network
