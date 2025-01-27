import paho.mqtt.client as mqtt

import json 

broker_address = "192.168.1.5"  # Replace with your MQTT broker address
broker_port = 1883  # Default MQTT port
username = "mqttactor"  # Replace with your MQTT username, if authentication is enabled
password = "mqttpass"  # Replace with your MQTT password, if authentication is enabled

mac = 'a020a6080932'
name = "NodeMCU handverdrahtet"
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
   "payload_on": "1", "payload_off": "0" , "payload_partymode_on":"D",
   "availability":{
        "topic": "/sensor/%s"%mac,
        "payload_available": True,
        "payload_not_available": False,
        "value_template": "{{ value == 'ONLINE' }}"  
   }
}      

# Define topic to publish to
topic = "homeassistant/switch/%s/actor/config"%mac


# Create MQTT client instance
client = mqtt.Client()

# Set username and password if authentication is enabled
if username and password:
    client.username_pw_set(username, password)

#client.on_connect = on_connect

# Connect to MQTT broker
rc = client.connect(broker_address, broker_port)
print("Connected to MQTT broker with result code " + str(rc))
    # Publish the payload to the specified topic
client.publish(topic,  json.dumps(payload), retain=True)
print("Payload published to topic:", topic)

client.disconnect()