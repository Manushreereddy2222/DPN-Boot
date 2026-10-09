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

trial_pattern = re.compile(
    r"Zone: (\d+) \| Felt: (YES|NO) \| Intensity: (\d+) \| Reversals: (\d+)/\d+ "
    r"\| FSR raw: (\d+) \| FSR grams: ([\d.]+) \| Reaction time \(ms\): (\d+)"
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
    trial_log = {}

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
                    zone_id = data.get("Zone")
                    writer.writerow([
                        datetime.now().isoformat(),
                        zone_id, data.get("Felt"), data.get("Intensity"),
                        data.get("Reversals"), data.get("FSR raw"),
                        data.get("FSR grams"), data.get("Reaction time (ms)")
                    ])
                    f.flush()

                    match = trial_pattern.search(raw_line)
                    if match:
                        z, felt, intensity, reversals, fsr_raw, fsr_grams, rt = match.groups()
                        if z not in trial_log:
                            trial_log[z] = []
                        trial_log[z].append({
                            "felt": felt,
                            "intensity": int(intensity),
                            "fsr_raw": int(fsr_raw),
                            "fsr_grams": float(fsr_grams),
                            "reaction_time_ms": int(rt),
                            "reversals": int(reversals)
                        })

            match = threshold_pattern.search(raw_line)
            if match:
                zone_id, intensity_th, fsr_th = match.groups()
                zone_trials = trial_log.get(zone_id, [])
                reaction_times = [t["reaction_time_ms"] for t in zone_trials if t["felt"] == "YES"]
                avg_reaction = sum(reaction_times) / len(reaction_times) if reaction_times else 0
                total_trials = len(zone_trials)
                felt_count = sum(1 for t in zone_trials if t["felt"] == "YES")

                thresholds[zone_id] = {
                    "intensity_threshold": float(intensity_th),
                    "fsr_threshold_grams": float(fsr_th),
                    "avg_reaction_time_ms": round(avg_reaction, 1),
                    "total_trials": total_trials,
                    "felt_count": felt_count,
                    "missed_count": total_trials - felt_count,
                    "sensitivity_level": "Normal" if float(fsr_th) < 10 else ("Reduced" if float(fsr_th) < 25 else "Low"),
                    "trials": zone_trials
                }
                if len(thresholds) == 4:
                    with open(THRESHOLDS_FILE, "w") as jf:
                        json.dump(thresholds, jf, indent=2)
                    print("Saved final thresholds:", json.dumps(thresholds, indent=2))
                    thresholds = {}
                    trial_log = {}

if __name__ == "__main__":
    main()
