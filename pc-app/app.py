from flask import Flask, jsonify, send_from_directory
import json, os

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
THRESHOLDS_FILE = os.path.join(BASE_DIR, "latest_thresholds.json")

app = Flask(__name__)

@app.route("/")
def index():
    return send_from_directory(BASE_DIR, "index.html")

@app.route("/latest_thresholds.json")
def thresholds_file():
    return send_from_directory(BASE_DIR, "latest_thresholds.json")

@app.route("/api/thresholds")
def get_thresholds():
    if not os.path.exists(THRESHOLDS_FILE):
        return jsonify({})
    with open(THRESHOLDS_FILE) as f:
        return jsonify(json.load(f))

if __name__ == "__main__":
    app.run(debug=True)
