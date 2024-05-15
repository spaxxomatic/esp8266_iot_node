import paho.mqtt.client as mqtt


broker_address = "192.168.1.5"  # Replace with your MQTT broker address
broker_port = 1883  # Default MQTT port
username = "mqttactor"  # Replace with your MQTT username, if authentication is enabled
password = "mqttpass"  # Replace with your MQTT password, if authentication is enabled

mac = 'b4e62d3aec70'
name = "Terasse"
# Define payload to publish
payload = {
   "name":name,
   "command_topic":"/actor/%s"%mac,
   "state_topic":"/actor/%s/state"%mac,
   "unique_id":mac,
   "device":{
      "identifiers": mac,
      "name":"sonoff %s"%mac
   }, 
   "payload_on": "1", "payload_off": "0" , "payload_partymode_on":"D" 
}      

# Define topic to publish to
topic = "homeassistant/switch/%s/actor/config"%mac

# Callback function to execute when the connection to the broker is established
def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT broker with result code " + str(rc))
    # Publish the payload to the specified topic
    client.publish(topic, payload)
    print("Payload published to topic:", topic)

# Create MQTT client instance
client = mqtt.Client()

# Set username and password if authentication is enabled
if username and password:
    client.username_pw_set(username, password)

# Assign on_connect callback function
client.on_connect = on_connect

# Connect to MQTT broker
client.connect(broker_address, broker_port)

# Loop to maintain network traffic flow, handles reconnecting
client.loop_forever()
