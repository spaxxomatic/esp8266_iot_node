# A robust, configurable ESP8266 MQTT client. Configurable as sensor or actor.

I use this as a replacement for the sonoff default firmware. A lot of work has been put into it to make it resilient to Wlan connection drops or MQTT error/disconnects.

* Uses the ESPAsyncTCP stack by https://github.com/me-no-dev/ESPAsyncTCP, a damn good, fully asynchronous TCP library by Hristo Gochkov. I was not able to stablize the sw behaviour before switching from the standard ESP TCP stact to this one
* Nicely handles lost Wlan connections by trying to reconnect a couple of times and then rebooting in AP mode and presenting a configuration GUI
* Detects MQTT server disconnects and tries to reconnect

* In motion detection mode, the "ON" time is configurable via MQTT (see config section below)
* Mode indication via the on-board LED - in normal mode, LED dim-blinks each 4 seconds


### MQTT topics
Sensor (button) state will be published to /sensor/<MAC_ADDRESS>

MQTT subscribtions:
 * /actor/<MAC_ADDRESS> for setting the actor state (in case of sonoff module, the actor is the on-board relay)
 When the motion sensor is activated, the relais can be switched on by publishing on the /actor/ topic. When motion is detected, the countdown is triggered and the relais is switched off after the <motion_sensor_timer> seconds. 
 For a permanent switch on (means, subsequently ignoring the motion sensor events), the value 255 must be published on the /actor topic. 
 The output will be switched on upon receival of another value <> 255 on the /actor topic.
 
 * /config/<MAC_ADDRESS> for receiving configuration messages.

The config data is JSON-formatted, as follows:
{"deepsleep":1} - enables the deep sleep mode of ESP
{"location":<location_name>} - sets the location info , example {"location":"basement_door"}
{"log_freq":<int>} - logging
{"motion_sensor_timer":<int>} - if a motion sensor is configured, the off-time of the sensor (seconds) can be configured via this parameter
Configuration is stored in the EEPROM.

Rreport request:
By sending a message like {"report":1} on the config topic, the device will answer with a MQTT message on the topic /report/<MAC>, reporting the firmware version, the location and the capabilities

### Future improvements (Todo's)
* Static TCP/IP adress configurable via MQTT - would allow faster reboots

### Useful links
NodeMCU schematic https://github.com/nodemcu/nodemcu-devkit-v1.0/blob/master/NODEMCU_DEVKIT_V1.0.PDF

Sonoff schematic https://www.itead.cc/wiki/File:Sonoff-Schematic.pdf

### Programming notes:
* When flashing, set the SPI mode of your board. Some SONOFF modules are delivered with QIO, some with DOUT flash chips. Set the right value in the platform.ini or flashing will fail. 

* Some cheap China USB-to-Serial modules (CH340) are causing random problems when flashing. Use FT232 USB-Serial converters to avoid headaches.

* Manual programming:
python esptool.py --port=COMx write_flash 0x0 firmware.bin --flash_mode qio

### Uploading a specific env

pio run -t upload -e sonoff_switchonly