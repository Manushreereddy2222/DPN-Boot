from flask import Flask, jsonify, render_template
import json, os

app = Flask(__name__)
THRESHOLDS_FILE = "latest_thresholds.json"

@app.route("/")
def index():
    return render_template("index.html")

@app.route("/api/thresholds")
def get_thresholds():
    if not os.path.exists(THRESHOLDS_FILE):
        return jsonify({})
    with open(THRESHOLDS_FILE) as f:
        return jsonify(json.load(f))

if __name__ == "__main__":
    app.run(debug=True)