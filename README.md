# A robust, configurable ESP8266 MQTT client. Configurable as sensor or actor.

I use this as a replacement for the sonoff default firmware. A lot of work has been put into it to make it resilient to Wlan connection drops or MQTT error/disconnects.

* Uses the ESPAsyncTCP stack by https://github.com/me-no-dev/ESPAsyncTCP, a damn well implemented, fully asynchronous TCP library by Hristo Gochkov. I was not able to stablize the sw behaviour before switching from the standard ESP TCP stack to this one
* Lost Wlan or MQTT connection are indicated by a blinking LED. For reconfiguration, a long press on the button will connect the esp to a preconfigured WLAN (see config.h) from where the module will try to download the connection string via HTTP 
* Detects MQTT server disconnects and tries to reconnect periodically
* I tried to use the WifiManager library for configuring the modules. It works, but is unreliable. It causes a lot of problems, core dumps, etc. I finally switched to a simpler mechanism - connect to a predefined SSID and download the conn string from a web server. Scripts for the server side are in /scripts folder
* In motion detection mode, the "ON" time is configurable via MQTT (see config section below)
* The on-board LED is used for healthcheck - in normal mode, LED dim-blinks constantly. Fast blink indicates a lost connectivity. 
* Commands can be issued via the serial port. Type ?? for help. 

### MQTT topics
Sensor (button) state will be published to /sensor/<MAC_ADDRESS>

MQTT subscribtions:
 * /actor/<MAC_ADDRESS> for setting the actor state (in case of sonoff module, the actor is the on-board relay)
 When the motion sensor is activated, the relais can be switched on by publishing on the /actor/ topic. When motion is detected, the countdown is triggered and the relais is switched off after the <motion_sensor_timer> seconds. 
 For a permanent switch on (means, subsequently ignoring the motion sensor events), the value 255 must be published on the /actor topic. 
 The output will be switched on upon receival of another value <> 255 on the /actor topic.
 
 * /config/<MAC_ADDRESS> for receiving configuration messages.

The config data is JSON-formatted, as follows:
{"deepsleep":1} - enables the deep sleep mode of ESP (if allowed by the firmware, actors will not allow it)
{"location":<location_name>} - sets the location info , example {"location":"basement_door"}
{"log_freq":<int>} - logging frequency in seconds
{"motion_sensor_timer":<int>} - if a motion sensor is configured, the off-time of the sensor (seconds) can be configured via this parameter
Configuration is persisten in the flash.

Report request:
By sending the message {"report":1} on the config topic, the device will answer with a MQTT message on the topic /report/<MAC>, reporting the firmware version, the location and the capabilities
The report is also sent periodically with a frequency of <log_freq>

OTA:
OTA is initiated by sending the message {"update":<ver>} on the config topic. The device will reboot immediately, after restart will try to download the firmware file <ver>.bin via http from the same ip address the MQTT server, port 9999. The update will be only attempted if the update version is newer as the installed version.
Example: {"update":100} will result in a "GET /100.bin HTTP/1.0" request.

### Future improvements (Todo's)
* Static TCP/IP adress configurable via MQTT - would allow faster reboots

### Useful links
NodeMCU schematic https://github.com/nodemcu/nodemcu-devkit-v1.0/blob/master/NODEMCU_DEVKIT_V1.0.PDF

Sonoff schematic https://www.itead.cc/wiki/File:Sonoff-Schematic.pdf

### Programming notes:
* Sonoff modules are a pain in the ass to flash. Better just dump them in the bin and use NodeMCU's. Use an external power supply for the module and short serial wires. Still, I bricked a lot of them, all kind of errors can occur. They also have a small flash,  OTA updates are not possible on all variants. 

* When flashing via serial, set the right SPI mode of your board. Some SONOFF modules are delivered with QIO, some with DOUT flash chips. Set the right value in the platform.ini or flashing will fail.

* WIRE AN EXTERNAL POWER SUPPLY WHEN PROGRAMMING SONOFF. Many failures during programming are caused by the insufficient power delivered by the on-board supply

* Some cheap China USB-to-Serial modules (CH340) are causing random problems when flashing. Use FT232 USB-Serial converters to avoid headaches.

* Manual programming:
python esptool.py --port=COMx write_flash 0x0 firmware.bin --flash_mode <qio/qout/dio/dout>

----

* Strange errors, ugly bugs and other mysteries:

    * Those ESP's are buggy. There is no other explanation for a lot of strange things that happen when flashing and also when operating them. The rathole is deep. 
    * Problems I encountered: After a while, they behave erratically when programming. They don't enter programming mode, don't respect the serial console speed, garbled output, etc. It's not flash wear - the chip itself must have some problems, because it doesn't even print the boot message correctly. Reset it 1000 times, then it works again for a while. I have no logical explanation. 
    Addendum: I had a LED lamp that was so ESR-noisy that it was interfering with the serial comm. But not sure if this was the cause - I still have some modules that don't behave right. 

    * ESP32 : hit this problem https://www.esp32.com/viewtopic.php?t=1201 on a module that worked fine for many days.    

```
rst:0x10 (RTCWDT_RTC_RESET),boot:0x33 (SPI_FAST_FLASH_BOOT)
flash read err, 1000
Falling back to built-in command interpreter.
OK
```

    They say: 
```
If we look at the ESP32 data sheet, we find that a set of pins are defined as strapping pins. What this means is that the signals present on these pins at boot time affects the operation of the device. Think of these as operational flags that are read only at boot time. The pins are:

GPIO0
GPIO2
GPIO05
GPIO12 - MTDI
GPIO15 - MTDO

GPIO12 (MTDI) - seems to control the voltage used for SDIO. The default is low.
Also, in case this is an issue in production, it's actually possible to program an efuse to force the ESP32 to use a certain flash voltage and ignore GPIO12.

```

Addendum: this was caused by the supply falling to 2V or less. 


### Uploading a specific env

pio run -t upload -e sonoff