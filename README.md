Wifi Water Meter Pulse Sensor
=============================


Software:
---------

C++ source, HTML, Javascript & JSON format.

* MQTT transmission of data every hour,
* log history every hour in flash memory,
* adjustment (setting.h) of the unit of diffusion,
* retention of records on system time synchronization defect and re-indexing on synchronization recovery before historization,
* monitoring of the consumption for detection of possible leaks in the water network,
* configuration of the monitoring parameters via the Web interface,
* MQTT alerts about possible leaks to the home automation device (Domotics, Jeedom, ...).
* 3 configurable SSID,
* web interface configuration,
* definition of IPs (such as the reverse proxy) excluded from configuration changes; allows secure exposure of the (only) home page to the Internet,
* debug trace available by telnet console,
* firmware update via WiFi, without loss of data and setting,
* accepts HTML commands from the home automation software: current counter value, log history recovery in JSON format, clear current history, backup of current measures with reboot, ...
* Deepsleep option allowed between measures (comming soon)...


* Screenshots:

* ![](doc/images/screenshot.png)

* ![](doc/images/about.png)

* MQTT parameters:

* ![](doc/images/mqtt1.png)

* ![](doc/images/mqtt2.png)

* Virtual sensor creat & edit in Domoticz:

* ![](doc/images/domoticz/edit.png)

* ![](doc/images/domoticz/devices.png)

* ![](doc/images/domoticz/sensors.png)

* Map:

* ![](doc/images/domoticz/map.png)


Hardware:
---------

* ESP8266 Mini WiFi Nodemcu Module with 18650 battery support (autonomy of about 17h in case of power failure - when deepsleep option enabled):

* ![](doc/images/esp8266.jpg)

* Warning : When Hibernation is enabled (soldered strap), GPIO 16 and Rst are connected and flashing through the serial port is no longer possible. So, we have to unsolder the strap or proceed by OTA (flashing WiFi).

