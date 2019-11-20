Wifi Water Meter
================


Software:
---------

C++ source

* MQTT transmissions every hour of meter reading,
* log history every hour in flash memory,
* adjustment (during compilation) of the unit of diffusion of the measurements,
* retention of records on system time synchronization defect and re-indexing on synchronization recovery before historization,
* monitoring of the consumption for detection of possible leaks,
* configuration of the monitoring parameters via the Web interface,
* MQTT alerts about possible leaks to the home automation device (Domotics, Jeedom, ...).
* 3 configurable SSID,
* web interface configuration,
* dedug trace available by telnet console,
* firmware update via WiFi, without loss of settings and measures,
* accepts HTML commands from the home automation software: current counter value, log history recovery in JSON format, clear current history, backup of current measures and reboot, ...


* Screenshots:

* ![](doc/images/screenshot.png)

* ![](doc/images/about.png)

* MQTT parameters:

* ![](doc/images/mqtt1.png)

* ![](doc/images/mqtt2.png)

* Virtual sensor creat & edit:

* ![](doc/images/domoticz/edit.png)

* ![](doc/images/domoticz/devices.png)

* ![](doc/images/domoticz/sensors.png)

* Map:

* ![](doc/images/domoticz/map.png)


Hardware:
---------

* ESP8266 Mini WiFi Nodemcu Module with 18650 battery support:

* ![](doc/images/esp8266.jpg)

