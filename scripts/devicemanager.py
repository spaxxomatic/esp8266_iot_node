import threading
import PySimpleGUI as sg
from flask import Flask
import paho.mqtt.client as mqtt
import json 
import config
import queue

data_queue = queue.Queue()

# Flask application
app = Flask(__name__)

@app.route('/')
def home():
    return "Hello from the web server!"

@app.route('/config/<path:mac>', methods=['GET'])
def send_data(mac):
    global data_queue
    # Get the request body
    data = mac
    data_queue.put(mac) 
    
    return {"status": "Received", "data": data}

# Function to run the web server
# Function to run the web server
def run_server():
    app.run(host='127.0.0.1', port=5000, debug=False, use_reloader=False)

# Function to start the server in a separate thread
def start_server():
    server_thread = threading.Thread(target=run_server)
    server_thread.daemon = True  # Allows the program to exit even if thread is running
    server_thread.start()
    #server_thread.join()

# Start the web server thread
start_server()


sg.theme('Topanga')      # Add some color to the window

def get_devices_reports():
    print("Game started!")
def list_online_devices():
    print("GEt devices list from mqtt")
def set_location():
    print("GEt devices list from mqtt")
    

layout = [
    [sg.Text("Web Server is Running on http://127.0.0.1:5000")],
    [sg.Button("List online devices"), sg.Button("list_online_devices")],
    [sg.Button("Set location"), sg.Button("set_location")],
    [sg.Button("List online devices"), sg.Button("list_online_devices")],
    [sg.Text("Request Data:"), sg.Multiline(size=(50, 10), key="DATA", disabled=True)],
    [sg.Button("Exit")]
]


window = sg.Window('ESP Config server', layout)

# Event loop
while True:
    event, values = window.read(timeout=100)
    if event == "list_online_devices":
        list_online_devices()
    elif event == "set_location":
        get_devices_reports()
    if event == sg.WINDOW_CLOSED or event == "Exit":
        break
  # Check if new data is available from the web server
    while not data_queue.empty():
        item = data_queue.get()  # Thread-safe dequeue            
        window["DATA"].update(item + "\n", append=True)

window.close()
