import serial
import threading
import time
import json
from flask import Flask, jsonify

# --- CONFIGURATION ---
# IMPORTANT: CHANGE THIS to your HC-05/06 Virtual COM Port (e.g., 'COM3' on Windows, '/dev/ttyACM0' on Linux)
BLUETOOTH_PORT = 'COM8' 
BAUD_RATE = 9600
HOST_IP = '0.0.0.0' # Listen on all available network interfaces (for phone access)
PORT = 5000

# Global variable to hold the latpython Server/server.pyest data
latest_sensor_data = {
    "COUNT": 0, 
    "USAGE_S": 0, 
    "LIGHT": "OFF", 
    "timestamp": time.time()
}

# Initialize Serial Connection
try:
    ser = serial.Serial(BLUETOOTH_PORT, BAUD_RATE, timeout=1)
    print(f"--- Successfully connected to {BLUETOOTH_PORT} at {BAUD_RATE} bps ---")
except serial.SerialException as e:
    print(f"Error: Could not open serial port {BLUETOOTH_PORT}. Is the HC-05 connected and driver installed?")
    # We exit here because the server cannot function without serial data
    exit(1)


def read_bluetooth_data():
    """Reads one line of data from the serial port and parses it."""
    global latest_sensor_data
    
    if ser.in_waiting > 0:
        try:
            line = ser.readline().decode('utf-8').strip() 
            if not line: return
            
            print(f"Received: {line}")

            data_dict = {}
            parts = line.split(',')
            for part in parts:
                if ':' in part:
                    key, value = part.split(':')
                    
                    if key in ["COUNT", "USAGE_S"]:
                        data_dict[key] = int(value.strip())
                    elif key == "LIGHT":
                        data_dict[key] = value.strip()
                    
            if data_dict:
                data_dict["timestamp"] = int(time.time())
                latest_sensor_data.update(data_dict)
                print(f"Updated data: {latest_sensor_data}")
                
        except Exception as e:
            print(f"Parsing/Serial Read Error: {e}")
            
    time.sleep(0.05) 


def bluetooth_reader_loop():
    """Runs the reading function continuously in a separate thread."""
    while True:
        read_bluetooth_data()

# --- FLASK WEB SERVER SETUP ---
app = Flask(__name__)

@app.route('/api/latest', methods=['GET'])
def get_latest_data():
    """REST API endpoint to return the most recently received sensor data."""
    return jsonify(latest_sensor_data)

@app.route('/')
def index():
    """Simple status page for debugging in a web browser."""
    return f"""
    <h1>Smart Counter Server Running</h1>
    <p>Data Source: {BLUETOOTH_PORT}</p>
    <p>Latest Data:</p>
    <pre>{json.dumps(latest_sensor_data, indent=4)}</pre>
    <p>Access the data via API: <a href="/api/latest">/api/latest</a></p>
    """

# --- Main Execution ---
if __name__ == '__main__':
    reader_thread = threading.Thread(target=bluetooth_reader_loop, daemon=True)
    reader_thread.start()
    
    print("\n--- Starting Flask Server ---")
    print(f"Accessible at http://<YOUR_LAPTOP_IP>:{PORT}/\n")
    
    app.run(host=HOST_IP, port=PORT)