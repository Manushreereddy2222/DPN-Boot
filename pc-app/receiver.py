import serial
import csv
import json
import re
from datetime import datetime

PORT = "COM5"  # TODO: replace with your ESP32's paired Bluetooth COM port
BAUD = 115200
CSV_FILE = "dpn_test_results.csv"
THRESHOLDS_FILE = "latest_thresholds.json"

threshold_pattern = re.compile(
    r"Zone (\d+) \| Intensity threshold: ([\d.]+) \| FSR threshold: ([\d.]+)g"
)

def parse_trial_line(line):
    if not line.startswith("Zone:"):
        return None
    data = {}
    for part in line.split("|"):
        key, _, value = part.strip().partition(":")
        data[key.strip()] = value.strip()
    return data

def main():
    ser = serial.Serial(PORT, BAUD, timeout=1)
    print(f"Connected to {PORT}")

    thresholds = {}

    with open(CSV_FILE, mode="a", newline="") as f:
        writer = csv.writer(f)
        if f.tell() == 0:
            writer.writerow(["timestamp", "zone", "felt", "intensity",
                              "reversals", "fsr_raw", "fsr_grams", "reaction_time_ms"])

        while True:
            raw_line = ser.readline().decode(errors="ignore").strip()
            if not raw_line:
                continue
            print(raw_line)

            if raw_line.startswith("Zone:"):
                data = parse_trial_line(raw_line)
                if data:
                    writer.writerow([
                        datetime.now().isoformat(),
                        data.get("Zone"), data.get("Felt"), data.get("Intensity"),
                        data.get("Reversals"), data.get("FSR raw"),
                        data.get("FSR grams"), data.get("Reaction time (ms)")
                    ])
                    f.flush()

            match = threshold_pattern.search(raw_line)
            if match:
                zone_id, intensity_th, fsr_th = match.groups()
                thresholds[zone_id] = {
                    "intensity_threshold": float(intensity_th),
                    "fsr_threshold_grams": float(fsr_th)
                }
                if len(thresholds) == 4:
                    with open(THRESHOLDS_FILE, "w") as jf:
                        json.dump(thresholds, jf, indent=2)
                    print("Saved final thresholds:", thresholds)
                    thresholds = {}

if __name__ == "__main__":
    main()