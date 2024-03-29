Wifi Water Meter Pulse Sensor
=============================


Software:
---------

C++ source, HTML, Javascript & JSON format.

* MQTT transmission of data each hour,
* log history each hour in flash memory,
* adjustment (setting.h) of the unit of diffusion,
* retention of records on system time synchronization defect and re-indexing on synchronization recovery before historization,
* monitoring of the consumption for detection of possible leaks in the water network,
* configuration of the monitoring parameters via the Web interface,
* MQTT alerts about possible leaks to the home automation device (Domotics, Jeedom, ...).
* 3 configurable SSID,
* web interface configuration,
* definition of IPs (such as the reverse proxy) excluded from configuration changes; allows secure exposure of the (only) home page to the Internet,
* debug trace available by telnet console (#ifdef DEBUG),
* firmware update via WiFi, without loss of data or setting,
* accepts HTML commands from the home automation software: current counter value, log history recovery in JSON format, clear current history, backup of current measures with reboot, ...
* modem sleep option allowed between measures...
* connection to pulse sensor (open collector or dry contact): 3.3v/5mA, bounce filtering.


Http requests available (except from EXCLUDED_IPV4_FROM_TUNE):
<table>
  <tr>
    <td><tt>http://IPAddress</tt></td>
    <td>WEB User Interface</td>
  </tr>
  <tr>
    <td><tt>http://IPAddress/getCurrentIndex</tt></td>
    <td>current counter value</td>
  </tr>
  <tr>
    <td><tt>http://IPAddress/getData</tt></td>
    <td>data file - all measures saved [Unix date, counter index]</td>
  </tr>
  <tr>
    <td><tt>http://IPAddress/getCurrentRecords</tt></td>
    <td>not yet indexed measures (on NTP error)</td>
  </tr>
  <tr>
    <td><tt>http://IPAddress/resetHistory</tt></td>
    <td>current data file will be erased after DELETEDATAFILE_DELAY (ms)</td>
  </tr>
  <tr>
    <td><tt>http://IPAddress/modemSleepAllowed</tt></td>
    <td>UI available during AWAKETIME (s) before and after each hour (minimum power consumption)</td>
  </tr>
  <tr>
    <td><tt>http://IPAddress/modemSleepDenied</tt></td>
    <td>WiFi always connected and UI always available (maximum power consumption)</td>
  </tr>
  <tr>
    <td><tt>http://IPAddress/restart</tt></td>
    <td>save current measure and reboot</td>
  </tr>
</table>

* Screenshots:

* ![](doc/images/screenshot.png)

* ![](doc/images/about.jpg)

* MQTT parameters:

* ![](doc/images/mqtt1.png)

* ![](doc/images/mqtt2.png)

* Virtual sensor creat & edit in Domoticz:

* ![](doc/images/domoticz/edit.png)

* ![](doc/images/domoticz/devices.png)

* ![](doc/images/domoticz/sensors.png)

* Map: ![](doc/images/domoticz/map.png)

* Graph: ![](doc/images/domoticz/graph.png)


Hardware:
---------

* Magnetic sensor example : http://www.compteur-energie.com/eau-emetteurs-impulsions-sappel-izar-pulse-i.htm

* ESP-WROOM-02 Mini WiFi Nodemcu Module with 18650 battery support (autonomy more than 10h in case of power failure), 1 x zener 3.3v, 1 x 360R, 1 x USB-A connector, 1 x 2 pins xh 2.54mm female connector :

* ![](doc/images/esp8266.jpg)

* ![](doc/images/case1.jpg)

* ![](doc/images/case2.jpg)

* ![](doc/images/case3.jpg)

* Pulse counter: ![](doc/images/pulseCounter.jpg)

* Izar magnetic sensor: ![](doc/images/IzarSensor.jpg)

