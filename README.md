# A robust, configurable ESP8266 MQTT client. Configurable as sensor or actor.

I use this as a replacement for the sonoff default firmware. A lot of work has been put into it to make it resilient to Wlan connection drops or MQTT error/disconnects.

* Uses the ESPAsyncTCP stack by https://github.com/me-no-dev/ESPAsyncTCP, a damn good, fully asynchronous TCP library by Hristo Gochkov. I was not able to stablize the sw behaviour before switching from the standard ESP TCP stact to this one
* Nicely handles lost Wlan connections by trying to reconnect a couple of times and then rebooting in AP mode and presenting a configuration GUI
* Detects MQTT server disconnects and tries to reconnect

* In motion detection mode, the "ON" time is configurable via the MQTT topic
* Mode indication via the on-board LED - in normal mode, LED dim-blinks each 4 seconds


### MQTT topics
Sensor (button) state will be published to /sensor/<MAC_ADDRESS>

The SW subscribes to /config/<MAC_ADDRESS> for receiving configuration messages.
The config must be sent as JSON.
Configuration options:
{"deepsleep":1} - enables the deep sleep mode of ESP
{"location":<location_name>} - sets the location info , example {"location":"basement_door"}
{"log_freq":<int>} - logging
Configuration is stored in the EEPROM.
{"motion_sensor_timer":<int>} - if a motion sensor is configured, the off-time of the sensor (seconds) can be configured via this parameter

and to /actor/<MAC_ADDRESS> for reading the actor state (in case of sonoff module, the actor is the on-board relay)


### Future improvements (Todo's)
* Static TCP/IP adress configurable via MQTT - would allow faster reboots

### Useful links
NodeMCU schematic https://github.com/nodemcu/nodemcu-devkit-v1.0/blob/master/NODEMCU_DEVKIT_V1.0.PDF

Sonoff schematic https://www.itead.cc/wiki/File:Sonoff-Schematic.pdf
