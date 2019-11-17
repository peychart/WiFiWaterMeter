//Reference: https://www.arduino.cc/en/Reference/HomePage
//See: http://esp8266.github.io/Arduino/versions/2.1.0-rc1/doc/libraries.html
//Librairies et cartes ESP8266 sur IDE Arduino: http://arduino.esp8266.com/stable/package_esp8266com_index.json
//http://arduino-esp8266.readthedocs.io/en/latest/
//JSon lib: see https://github.com/bblanchon/ArduinoJson.git
//peychart@netcourrier.com 20171021
// Licence: GNU v3
#include <string.h>
#include "FS.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
#include <TimeLib.h>
#include <NtpClientLib.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>
//#include <Ethernet.h>
#include <vector>
#include <map>

#include "setting.h"   //Can be adjusted according to the project...

//Avoid to change the following:
#define ulong                         long unsigned int
#define INFINY                        60000UL
String                                hostname(DEFAULTHOSTNAME);    //Can be change by interface
String                                ssid[SSIDCount()];            //Identifiants WiFi /Wifi idents
String                                password[SSIDCount()];        //Mots de passe WiFi /Wifi passwords
String                                counterName(DEFAULTHOSTNAME), leakMsg(WATERLEAK_MESSAGE), ntpServer(NTPSERVER);
short                                 localTimeZone(TIMEZONE);
ulong                                 counterValue(0UL), multiplier(MULTIPLIER);
bool                                  WiFiAP(false);
#ifdef DEFAULTWIFIPASS
  ushort                              nbWifiAttempts(MAXWIFIRETRY), WifiAPTimeout;
#endif
#define MIN_LEAKNOTIF_PERIOD          3600000UL
#define MAX_LEAKNOTIF_PERIOD          25200000UL
#define MIN_MAXCONSUM_TIME            60000UL
#define MAX_MAXCONSUM_TIME            3600000UL
#define PUSHDATA_DELAY                120UL
ulong                                 next_reconnect(0UL),
                                      next_leakCheck(0UL), next_leakDetected(MIN_LEAKNOTIF_PERIOD),
                                      leakNotifPeriod(MIN_LEAKNOTIF_PERIOD), maxConsumTime(MIN_MAXCONSUM_TIME);
ushort                                leakStatus(0);
volatile bool                         intr(false);
std::map<ulong,ulong>                 dailyData;

ESP8266WebServer                      ESPWebServer(80);
ESP8266HTTPUpdateServer               httpUpdater;

WiFiClient                            ethClient;
PubSubClient                          mqttClient(ethClient);
String                                mqttBroker, mqttIdent, mqttUser, mqttPwd, mqttQueue;
ushort                                mqttPort;
std::vector<std::vector<String>>      mqttFieldName, mqttValue;
std::vector<std::vector<ushort>>      mqttNature, mqttType;
std::vector<ushort>                   mqttEnable(0);

#define Serial_print(m)              {if(Serial) Serial.print(m);}
#define Serial_printf(m,n)           {if(Serial) Serial.printf(m,n);}
#ifdef DEBUG
  WiFiServer                          telnetServer(23);
  WiFiClient                          telnetClient;
  #ifdef DEFAULTWIFIPASS
    #define DEBUG_print(m)           {if(telnetClient && telnetClient.connected()) telnetClient.print(m);    Serial_print(m);}
    #define DEBUG_printf(m,n)        {if(telnetClient && telnetClient.connected()) telnetClient.printf(m,n); Serial_printf(m,n);}
  #else
    #define DEBUG_print(m)            Serial_print(m);
    #define DEBUG_printf(m,n)         Serial_printf(m,n);
  #endif
#else
  #define DEBUG_print(m)              ;
  #define DEBUG_printf(m,n)           ;
#endif

#define WIFI_STA_Connected()         (WiFi.status()==WL_CONNECTED)

bool notifyProxy(ushort, String="");
bool readConfig(bool=true);
void writeConfig();
ulong time(bool b=false);

inline bool isNow(ulong v)                    {ulong ms(millis()); return((v<ms) && (ms-v)<INFINY);}  //Because of millis() rollover:
inline bool isTimeSynchronized(ulong t=now()) {return(t>-1UL/10UL);}
inline ulong getCounter()                     {return counterValue/10L;}

bool addMQTT(ushort i, ushort j){
  bool isNew(false);
  std::vector<String> s;
  std::vector<ushort> n;
  while(mqttFieldName.size()<=i){isNew=true;
    mqttFieldName.push_back(s);
    mqttNature.push_back(n);
    mqttType.push_back(n);
    mqttValue.push_back(s);
  }while(mqttFieldName[i].size()<=j){isNew=true;
    mqttFieldName[i].push_back("");mqttNature[i].push_back(0);mqttType[i].push_back(0);mqttValue[i].push_back("");
  }return isNew;
}

#define WEB_S(n) ESPWebServer.sendContent(n)
#define WEB_F(n) ESPWebServer.sendContent(F(n))
void sendHTML_input(String name, String type, String more){
  WEB_S( "<input"+ (name.length() ?(" id='" + name + "' name='" + name + "'") : String("")) + " type='" + type + (more.length()?"' ":"'") + more + ">\n" );
}void sendHTML_inputText   (String name, String val, String more=""){sendHTML_input(name, F("text"),    "value='" + val + (more.length()?"' ":"'") + more);}
void  sendHTML_inputPwd    (String name, String val, String more=""){sendHTML_input(name, F("password"),"value='" + val + (more.length()?"' ":"'") + more);}
void  sendHTML_inputNumber (String name, String val, String more=""){sendHTML_input(name, F("number"),  "value='" + val + (more.length()?"' ":"'") + more);}
void  sendHTML_checkbox    (String name, bool   val, String more=""){sendHTML_input(name, F("checkbox"), more + (val ?" checked" :""));}
void  sendHTML_button      (String name, String val, String more=""){sendHTML_input(name, F("button"),  "value='" + val + (more.length()?"' ":"'") + more);}
void  sendHTML_optionSelect(String lib,  String val, String more=""){WEB_S("<option value='" + val + (more.length()?"' ":"'") + more + ">" + lib + "</option>\n");}

void sendHTML(bool blankPage=false){
  ESPWebServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  ESPWebServer.send(200, "text/html", F("<!DOCTYPE HTML>\n<html lang='us-US'>\n<head>\n <meta charset='utf-8'/>\n"));
  if(!blankPage){
    WEB_F(" <title>");
    WEB_S(hostname);
    WEB_F(" Water Meter</title>\n\
<style>\n\
 *{margin:0; padding:0;}\n\
 body, html{height:100%;}\n\
 body {\n\
  color: white;font-size:150%;background-color:green;font-family: Arial,Helvetica,Sans-Serif;\n\
  background-image:url(https://static.mycity.travel/manage/uploads/7/36/12705/989bd67a1aad43055bd0322e9694f8dd8fab2b43_1080.jpg);/*background-repeat:no-repeat;*/\n\
 }\n\
 #main{max-width:1280px;min-height:100%;margin:0 auto;position:relative;}\n\
 footer{text-align:right;position:absolute;bottom:0;width:100%;padding-top:35px;height:35px;}\n\
 .table {width:100%;\n min-width:700px;\n padding-right:100%;\n height:50px;\n border-spacing: 0px;}\n\
\n\
 .modal         {display: none;position: fixed;z-index: 1;left: 0%;top: 0%;height: 100%;width: 100%;overflow: scroll;background-color: #000000;}\n\
 .modal-content {background-color: #fff7e6;color: #000088;margin: 5% auto;padding: 15px;border: 2px solid #888;height: 90%;width: 90%;min-height: 755px;}\n\
 .close         {color: #aaa;float: right;font-size: 30px;font-weight: bold;\n}\n\
 .safe          {border: none;height: 48px;width: 48px;cursor: pointer;background-repeat: no-repeat;background: transparent;background-position: center;vertical-align: center;background-image:url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAYAAABXAvmHAAAABmJLR0QA/wD/AP+gvaeTAAAErklEQVRoge2ZTUgcZxjH/8/MrAeJtFhXsn600hLd3VgLXbUUUtCQi8XFrGZNW1vooaQVDfReClsovZe6NubQk4eoUVd2TSmE4LEYhRYbbYT0kFiVbpQGF0V3Z54eojHdeXdnZmf0Ev+3fd6P+f3nfXbeL+BEJ3qxRU50Eh4NyztKppklrVViCmiAlwAPAacAgIEUmFaJ+L5GPC+p8p3AQsNsJBLR7D7bloGOWEe1pkl9DHwM4kqLzVdAPKyqcvTnromVQhkKMtA2GnYrrvS3DHwKoKjQh+9rD8Q/kax9HQ/GH1ttbNlAMNbxETP9AKDUalsDbTBT/3Tn5A0rjUwbCAxdcXncyUEQf2adzbyYaWg96b46//n1tJn6pgwE48Fizsg3AbTZojOvW6So4Xgwvm1UUTKqEBi64jpmeAB4nzPyVHg0bPj/MjTgcScHcbzwB7qwU7T3vVGlvCnUPhHqAfGwc0wFiPiDxMWpkZzFuQpCE6FX0sR/Aig7EjDz2iRFrcv1ic2ZQhni73CM8GUlxbmKSrWM/E2uQuEItI13VsmS9gD2JylTqj1diu7mevz6YAW37/0lqrJHinomHow/zC4QjoBC3I9jhlckCefOvIoLZ18XVStiVeoVFegMRCIRiYl7nAYVyedx43Lzm1CkQ4x336iGW5ROmvRJeDQsZ4d1BuYafn8HQJWzqHrVni5FV5MPsnSYxarGuHl3EcktwfxFXLnjSgeywzoDLGmtDrPq5K8o0735A/iltWTOdsR0PjumM0Ca1OgUqEj+ijJ0NZ4Vvvl88ADAgPEIAKi1TZlDduABgIjrsmN6A8Qes0B5vt06+TxuW/AAwICOTTQCp8x01uKtQW9rE3wet2Fdf0UZLjX5bcHvqyQ7YLiYE6nFW4MWbw1kiXCpyZ/XhN20MZLIQCpfA3dJMd6rfe3Z73wmjgB+KzugN8C0lq+H5NY2RmYXkNEODxRkidDd7Ed9VflRwoMAHZtoBJaNOlpe38To7B//M0FE6Ar4UF9VfmRpw0z3s2OiiWzOTGfL65sYv7sEVeNnMSJC6G2fEH587p7tnBex6QxIqnzHbIdLa0lhOone/OKq5RMTnURsOgOBhYZZAI/MdipKpwM5+bUB8DCw0DCfHdSt7mZmZrj2w7pygM6Z7XkjtYP1J1vwVbgh0dO37zA8AAxe7712OzsonAdUVY4C2LPS+/MjcQTwu1AyUVFBzj1xe6zjRzB9YfVJ/oqnu1Ancv45DSRCsauigpwzcXqv6CsAlikWVx87Db9BippzT5zTwC/dY5vMJHR9nGKm3nyHvnnXQtOdkzeYach5LJMijk53To7lq2K4mCvOKH0AYo5BmRQRT6deevKlUT1DA2PdYyopag+AW46QmVMCstY90zqTMapoajkdD8a31/4pvwjia/bZDEQcTb38b8jMyTRQwAVHe6zjMpgG4PypXZKZ+oxyPluWNzSJi1MjLiYvA4MAdq22F2gXwEA67fJahQdsXvK1x9srocr9YOoBUG2x+SMAw1Ay0UQw8XehDI5cs0YiEWnurd8aiek8AwEirmOgEof76xSYVgAss6TN7V+zzjtxzXqiE73o+g+dpfqwtFDpgQAAAABJRU5ErkJggg==');}\n\
 .warning       {border: none;height: 48px;width: 48px;title:'Warning: probable water leak!';cursor: pointer;background-repeat: no-repeat;background: transparent;background-position: center;vertical-align: center;background-image:url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAYAAABXAvmHAAAABmJLR0QA/wD/AP+gvaeTAAAChUlEQVRoge2YT2sTQRjGf9Nmd6NFIlHx4EUQPbR48AOIVtFDUD9Bj6LFPwj2JngRBMFiBC9+Bc+CJ8GmQu9SAoIEL7UgDShEoWnavF7aZLvN7szszlbB/Z2WzOy8z5tn5mESKCgoKCj4n1F5LSwLwRUUrwHoq9tqev19HnVyaUCWqNIrfwGpbn/0k75/Wk132q5rjbleEIBe8DQkHuAw4xtP8ijl3AFZ9CcR9QkoRYa2QM6pCxvLLuu5d0BUnb3iAcZBvXRdzmkD0ghuAFcTplySheC6y5rOtpA08WkHy8AZTcUWv7pTqkbXRV13DrSD++jEAwinOBjcdVXWiQMjYlOHs1h148De2NThLFYzOxAXmw/mp/jR8QA4UulRf9iMvuokVrM7EBObXqk/ePa9fnQYHMVqpgaSYtP3ZPAcbiZC5lhN3YA08YHncePhbz3czAgFdXlHkFZHegc0semVhqL9eAcyx2qqBmSJKqhHSXN2O5DQAIDisXw4dDSNlnQOGMRmWLSnayBDrFo3IIv+JHBTNy+8bfxSwhkYLMwtafhnbfXYOxB/29yF8RkYkipWrRowuG0OsDoDQ6xj1bgBXWxGsTwDYUVWsWrugOltc5uwaM/kDOxgGatGd6EUt82sGN9WzRywv21mxThWtQ4k/EhP5Ntambcfj6OAa+e/c+LYus3rYHhb1YsyjM0or96cZHWtDMDX1QM8u/fZdomdWL2cNClxC9nEZpTOb2/ksyXaWI1twDY2o8zUVqhMbFKZ2GSmtpJ2GW2sxp4BaQRzwHz6yg4R5tTF7otRQ0lb6E5OcuwZi9eSz3+j+0h8A8IsitY+ahmNosWWmv3bMgoKCgoK/k3+AILkzValVvu5AAAAAElFTkSuQmCC');}\n\
 .blink {animation: blink 2s steps(5, start) infinite; -webkit-animation: blink 1s steps(5, start) infinite;}\n\
 @keyframes blink {to {visibility: hidden;}}\n\
 @-webkit-keyframes blink {to {visibility: hidden;}}\n\
 .confPopup            {position: relative;opacity: 0;display: none;-webkit-transition: opacity 400ms ease-in;-moz-transition: opacity 400ms ease-in;transition: opacity 400ms ease-in;}\n\
 .confPopup:target     {opacity: 1;display: block;}\n\
 .confPopup > div      {width: 750px;position: fixed;top: 25px;left: 25px;margin: 10% auto;padding: 5px 20px 13px 20px;border-radius: 10px;background: #71a6fc;background: -moz-linear-gradient(#71a6fc, #fff);background: -webkit-linear-gradient(#71a6fc, #999);}\n\
 .closeconfPopup       {background: #606061;color: #FFFFFF;line-height: 25px;position: absolute;right: -12px;text-align: center;top: -10px;width: 24px;text-decoration: none;-webkit-border-radius: 12px;-moz-box-shadow: 1px 1px 3px #000;}\n\
 .closeconfPopup:hover {background: #00d9ff;}\n\
</style></head>\n");
  }if(blankPage){
    WEB_F("<body>\n");
  }else{
    WEB_F("<body onload='init();'><div id='main'>\n\
<div id='about' class='modal'><div class='modal-content'><span class='close' onClick='refresh();'>&times;</span><h2>About:</h2>\
This WiFi Water Meter is a connected device that allows to control (from a home automation application like Domoticz or Jeedom) the \
water consumption of a distribution network located after a water meter with a reed sensor.<br><br>\
This device has its own WEB interface which can be used to configure and control it from a web browser (its firmware can also be upgraded from this page). \
Through the MQTT protocol, the device periodically transmits its data to the data formatting server and send notifications in the event of network leak.<br><br>\
As long as no SSID is set and it is not connected to a master, the device acts as an access point with its own SSID and default password: \"");
    WEB_S(String(DEFAULTHOSTNAME)); WEB_F("-xxx/");
#ifdef DEFAULTWIFIPASS
    WEB_S(String(DEFAULTWIFIPASS).length() ?F(DEFAULTWIFIPASS) :F("none"));
#else
    WEB_F("none");
#endif
    WEB_F("\" on: 192.168.4.1.<br><br>\n<table style='width: 100%'>\n\
<th style='text-align:left;'><h3>Network name:</h3></th>\n<th style='text-align:left;'><h3>NTP server - TimeZone:</h3></th>\n<th style='text-align:left;'><h3>Reboot device:</h3></th>\n\
<tr style='white-space: nowrap;'><td style='text-align:left;'>\n<form method='POST'>\n");
    sendHTML_inputText(F("hostname"), hostname, "style='width:150px'");
    sendHTML_button("", F("Submit"), F("onclick='submit();'"));
    WEB_F("</form>\n</td><td style='text-align: left;'>\n<form method='POST'>");
    sendHTML_inputText(F("ntpServer"), ntpServer, "style='width:200px'");
    sendHTML_inputNumber(F("localTimeZone"), String(localTimeZone, DEC), "min=-11 max=11 size=2 style='width:60px'");
    sendHTML_button("", F("Submit"), F("onclick='submit();'"));
    WEB_F("</form>\n</td><td style='text-align: center;'>\n<form method='POST'>");
    sendHTML_button(F("restart"), F("Save Data"), F("onclick='submit();'")); sendHTML_checkbox("reboot", true, "style='display:none;'");
    WEB_F("</form>\n</td></tr></table>\n<br><h3>Network connection:</h3><table><tr>");
    for(ushort i(0); i<SSIDCount(); i++){
      WEB_F("<td>\n<form method='POST'><table>\n<tr><td>SSID ");
      WEB_S(String(i+1, DEC));
      WEB_F(":</td></tr>\n<tr><td><input type='text' name='SSID' value='");
      WEB_S(ssid[i] + (ssid[i].length() ?F("' readonly><br>\n"): F("'></td></tr>\n")));
      WEB_F("<tr><td>Password:</td></tr>\n<tr><td><input type='password' name='password' value='");
      if(password[i].length()) WEB_S(password[i]);
      WEB_F("'></td></tr>\n<tr><td>Confirm password:</td></tr>\n<tr><td><input type='password' name='confirm' value='");
      if(password[i].length()) WEB_S(password[i]);
      WEB_F("'></td></tr>\n<tr><td><input type='button' value='Submit' onclick='saveSSID(this);'>&nbsp;<input type='button' value='Remove' onclick='deleteSSID(this);'></td></tr>\n</table></form>\n</td>\n");
    }WEB_F("</tr></table>\n\
<h6><a href='update' onclick='javascript:event.target.port=80'>Firmware update</a> - <a href='https://github.com/peychart/wifi-WaterMeter'>Website here</a></h6>\n\
</div></div>\n");
//                             -----------------------------------------------------------------
    WEB_F("<header>\n\
 <div style='text-align:right;white-space: nowrap;'><p><span class='close' onclick='showHelp();'>?&nbsp;</span></p></div>\n\
 </header>\n\
\n\
<!MAIN SECTION>\n\
<section>");

    WEB_F("<table id='main' style='width: 100%'><col width='145px'><col width='260px'><tr>\n<td>\n<h3>");
    WEB_S(hostname); WEB_F("</h3>\n\
</td><td>\n<form id='0'>\n\
	<canvas id='canvasOdometer' width='100' height='40' onclick='initConfPopup(this);'></canvas>\n\
</form></td><td>\n<form id='1'>\n\
  <button id='leakStatusOk' name='status' class='safe' title='' onclick='initConfPopup(this);' style='display:");
    WEB_S(leakStatus ?"none" :"inline-block");
    WEB_F(";'>\n<button id='leakStatusFail' name='status' class='warning blink' title='Warning: probable water leak!' onclick='initConfPopup(this);' style='display:");
    WEB_S(leakStatus ?"inline-block" :"none");
    WEB_F(";'></button>\n</form></td></tr></table>\n");

    //MQTT Parameters:
    WEB_F("\n<!Parameters:>\n<div style='display: none;'>\n");
    for(ushort i(0); i<2; i++){   //Only one sensor, here... :-(
      sendHTML_checkbox ("mqttEnable"      +String(i, DEC),                    mqttEnable[i]);
      for(ushort j(0); j<mqttEnable[i]; j++){
        sendHTML_inputText ("mqttFieldName"+String(i, DEC)+"."+String(j, DEC), mqttFieldName[i][j]);
        sendHTML_inputText ("mqttNature"   +String(i, DEC)+"."+String(j, DEC), String(mqttNature[i][j], DEC));
        sendHTML_inputText ("mqttType"     +String(i, DEC)+"."+String(j, DEC), String(mqttType[i][j], DEC));
        sendHTML_inputText ("mqttValue"    +String(i, DEC)+"."+String(j, DEC), mqttValue[i][j]);
    } }
    WEB_F("</div>\n");

    WEB_F("\n<!Configuration popup:>\n<div id='confPopup' class='confPopup' style='color:black;'><div>\n<form id='mqttConf' method='POST'>");
    sendHTML_inputText(F("plugNum"), "", F("style='display:none;'"));
    WEB_F("<a title='Save configuration' class='closeconfPopup' onclick='closeConfPopup();'>X</a>\n");
    WEB_F("<div style='text-align:center;'><h2>Parameters setting</h2></div>\n\
<div id='groupName'><h3 title='notification settings' style='display:inline-block;'>Index:</h3><h3 title='notification settings' style='display:none;'>Leak monitoring:</h3></div>\n\
<table style='width: 100%'>\n<col width=33%><col width=33%>\n\
<th id='label1' style='text-align:center;'><div style='display:inline-block;'>Counter Name</div><div style='display:none;'>Notification period</div></th>\n\
<th id='label2' style='text-align:center;'><div style='display:inline-block;'>Deciliter per pulse</div><div style='display:none;'>Maximum consumption time</div></th>\n\
<th id='label3' style='text-align:center;'><div style='display:inline-block;'>Initial value</div><div style='display:none;'>Leak msg</div></th>\n\
<tr><td id='param1' style='text-align:center;'><div style='display:inline-block;'>");
    sendHTML_inputText(F("counterName"), counterName, F("style='width:120px;'"));
    WEB_F("</div><div style='display:none;'>");
    sendHTML_inputNumber(F("leakNotifPeriod"), String(leakNotifPeriod/3600000L, DEC), "min=" + String(MIN_LEAKNOTIF_PERIOD/3600000L, DEC) + " max=" + String(MAX_LEAKNOTIF_PERIOD/3600000L, DEC) + " style='width:50px;text-align:right;'");
    WEB_F("h</div></td>\n<td id='param2' align=center><div style='display:inline-block;'>");
    sendHTML_inputNumber(F("multiplier"), String(multiplier, DEC), F("min=1 max=100 style='width:50px;text-align:right;'"));
    WEB_F("dl/pulse</div><div style='display:none;'>");
    sendHTML_inputNumber(F("maxConsumTime"), String(maxConsumTime/60000L, DEC), "min=" + String(MIN_MAXCONSUM_TIME/60000L, DEC) + " max=" + String(MAX_MAXCONSUM_TIME/60000L, DEC) + " style='width:50px;text-align:right;'");
    WEB_F("mn</div></td>\n<td id='param3' align=center><div style='display:inline-block;'>");
    sendHTML_inputNumber(F("counterValue"), String(getCounter(), DEC), F("min=0 max=999999999 style='width:80px;text-align:right;'"));
    WEB_F("liters</div><div style='display:none;'>");
    sendHTML_inputText(F("leakMsg"), leakMsg, F("style='width:200px;'"));
    WEB_F("</div></td></tr>\n</table>\n");

    WEB_F("<br><h3 title='Server settings'>MQTT parameters \n");
    //MQTT configuration:
    sendHTML_checkbox(F("mqttEnable"), false, "style='vertical-align:right;' onclick='refreshConfPopup();'");
    WEB_F("</h3>\n<div id='mqttParams'><p align=center title='Server settings'>Broker: ");
    sendHTML_inputText(F("mqttBroker"), mqttBroker, F("style='width:65%;'"));
    WEB_S(":");
    sendHTML_inputNumber(F("mqttPort"), String(mqttPort,DEC), F("min='0' max='-1' style='width:10%;'"));
    WEB_F("</p>\n<table style='width: 100%'>\n<col width='42%'><col width='30%'><tr title='Server settings' style='white-space: nowrap;'><td>\nIdentification: ");
    sendHTML_inputText(F("mqttIdent"), mqttIdent, F("style='width:120px;'"));
    WEB_F("</td><td>\nUser: ");
    sendHTML_inputText(F("mqttUser"), mqttUser, F("style='width:120px;'"));
    WEB_F("</td><td>\nPassword: ");
    sendHTML_inputPwd(F("mqttPwd"), mqttPwd, F("style='width:75px;'"));
    WEB_F("</td></tr></table>\n<p align=center title='Server settings'>Topic: ");
    sendHTML_inputText(F("mqttTopic"), mqttQueue, F("style='width:80%;'"));
    WEB_F("</p><br>\n<table id='mqttRaws' title='notification settings' border='1' cellpadding='10' cellspacing='1' style='width: 100%'>\n\
<col width='25%'><col width='25%'><col width='20%'><col width='25%'>\n\
<tr>\n<th>FieldName</th><th>Nature</th><th>Type</th><th>Value</th><th>");
    sendHTML_button("mqttPlus", "+", "title='Add a field name' style='background-color: rgba(0, 0, 0, 0);' onclick='mqttAddRaw();'");
    WEB_F("</th></tr>\n</table>\n</div></form></div></div>\n");

    WEB_F("</section>\n\n<footer>\n<h6>(<div id='date' style='display:inline-block;'></div>V");
    WEB_S(String(ResetConfig,DEC));
    WEB_F(", Uptime: ");
    ulong sec=millis()/1000UL;
    WEB_S(String(sec/(24UL*3600UL)) + "d-");
    WEB_S(String((sec%=24UL*3600UL)/3600UL) + "h-");
    WEB_S(String((sec%=3600UL)/60UL) + "mn");
    WEB_F(")</h6>\n\
</footer>\n\
\n\
<!FRAME /dev/null><iframe name='blankFrame' height='0' width='0' frameborder='0'></iframe>\n\
<script>\n\
var odometer;\n\
this.timer=0;\n\
function init(){odometer=new steelseries.Odometer('canvasOdometer', {'decimals':3});refresh(1);}\n\
function refresh(v=20){\n\
 clearTimeout(this.timer);document.getElementById('about').style.display='none';\n\
 if(v>0)this.timer=setTimeout(function(){RequestStatus();refresh();},v*1000);\n\
}\n\
function RequestStatus(){var ret,req=new XMLHttpRequest();\n\
 req.open('GET',location.protocol+'//'+location.host+'/index',false);req.send(null);ret=req.responseText.trim();\n\
 if(ret.indexOf('[')>=0 && ret.indexOf(',')>=0 && ret.indexOf(']')>=0){\n\
  var s='No NTP sync', v=parseInt(ret.substring(ret.indexOf('[')+1,ret.indexOf(',')));\n\
  if(v>946684800){s=(new Date(v*1000)).toISOString();s=s.substring(0,s.indexOf('T')).split('-').join('/');}\n\
  document.getElementById('date').innerHTML=s + '&nbsp;-&nbsp;';\n\
  odometer.setValue(parseInt(ret.substring(ret.indexOf(',')+1,ret.indexOf(']')))/1000.0);\n\
}}\n\
function showHelp(){refresh(120);document.getElementById('about').style.display='block';}\n\
function saveSSID(e){var f,s;\n\
 for(f=e;f.tagName!='FORM';)f=f.parentNode;\n\
 if((s=f.querySelectorAll('input[type=text]')).length && s[0]==''){alert('Empty SSID...');f.reset();s.focus();}\n\
 else{var p=f.querySelectorAll('input[type=password]');\n\
  if(p[0].value!=p[1].value || p[0].value==''){\n\
   var ssid=s[0].value;s[0].value=ssid;\n\
   alert('Incorrect password...');p[0].focus();\n\
  }else f.submit();\n\
}}\n\
function deleteSSID(e){var f,s;\n\
 for(f=e;f.tagName!='FORM';)f=f.parentNode;\n\
 if((s=f.querySelectorAll('input[type=text]')).length && s[0].value!=''){\n\
  if(confirm('Are you sure to remove this SSID?')){\n\
   f.reset();s=f.getElementsByTagName('input');\n\
   for(var i=0; i<s.length; i++)if(s[i].type=='password')s[i].value='';\n\
   f.submit();\n\
 }}else alert('Empty SSID...');\n\
}\n\
//Main Form:\n\
function setDisabled(v, b){for(var i=0;v[i];i++)v[i].disabled=b;}\n\
function mqttAllRawsRemove(){var t,r;for(t=document.getElementById('mqttPlus');t.tagName!='TR';)t=t.parentNode;t=t.parentNode;\n\
 r=t.getElementsByTagName('TR');while(r[1])t.removeChild(r[1]);\n\
}\n\
function mqttRawRemove(e){var n,t,r=document.getElementById('mqttRaws').getElementsByTagName('TR');for(t=e;t.tagName!='TR';)t=t.parentNode;\n\
 for(var i=0,b=false;i<r.length-2;i++)if(b|=(r[i+1].getElementsByTagName('input')[0].name===t.getElementsByTagName('input')[0].name)){\n\
  document.getElementById('mqttFieldName'+i).value=document.getElementById('mqttFieldName'+(i+1)).value;\n\
  document.getElementById('mqttNature'+i).value=document.getElementById('mqttNature'+(i+1)).value;\n\
  document.getElementById('mqttType'+i).value=document.getElementById('mqttType'+(i+1)).value;\n\
  document.getElementById('mqttValue'+i).value=document.getElementById('mqttValue'+(i+1)).value;\n\
 }t.parentNode.removeChild(r[r.length-1]);refreshConfPopup();\n\
}\n\
function mqttAddRaw(){mqttRawAdd();refreshConfPopup();}\n\
function mqttRawAdd(){var t;for(t=document.getElementById('mqttPlus');t.tagName!='TR';)t=t.parentNode;t=t.parentNode;\n\
 var i,tr,td,j=t.getElementsByTagName('TR'),n=j.length-1;\n\
 for(i=1;i<=n;i++)if(j[i].querySelectorAll('input[type=text]')[0].value==='')return false;\n\
 t.appendChild(tr=document.createElement('tr'));\n\
 tr.appendChild(td=document.createElement('td'));td.style='text-align:center;';\n\
 td.appendChild(i=document.createElement('input'));i.id=i.name='mqttFieldName'+n;i.type='text';i.style='width:80%;';\n\
 tr.appendChild(td=document.createElement('td'));td.style='text-align:center;';\n\
 td.appendChild(i=document.createElement('select'));i.id=i.name='mqttNature'+n;i.setAttribute('onchange','refreshConfPopup();');i.style='width:80%;text-align:center;';\n\
 i.appendChild(j=document.createElement('option'));j.value='0';j.innerHTML=(document.getElementById('plugNum').value=='0' ?'Counter-value' :'Warning-notification');\n\
 i.appendChild(j=document.createElement('option'));j.value='1';j.innerHTML='Constant';\n\
 tr.appendChild(td=document.createElement('td'));td.style='text-align:center;';\n\
 td.appendChild(i=document.createElement('select'));i.id=i.name='mqttType'+n;i.setAttribute('onchange','refreshConfPopup();');i.style='width:80%;text-align:center;';\n\
 i.appendChild(j=document.createElement('option'));j.value='0';j.innerHTML='String';\n\
 i.appendChild(j=document.createElement('option'));j.value='1';j.innerHTML='Number';\n\
 tr.appendChild(td=document.createElement('td'));td.style='text-align:center;';\n\
 td.appendChild(i=document.createElement('b'));i.innerHTML='\"';\n\
 td.appendChild(i=document.createElement('input'));i.id=i.name='mqttValue'+n;i.value='0';i.type='text';i.style='width:45%;text-align:center;';\n\
 td.appendChild(i=document.createElement('b'));i.innerHTML='\"';\n\
 tr.appendChild(td=document.createElement('td'));td.style='text-align:center;';\n\
 td.appendChild(i=document.createElement('input'));i.id='mqttMinus'+n;i.type='button';i.value='-';i.style='background-color: rgba(0, 0, 0, 0);';i.setAttribute('onclick','mqttRawRemove(this);');\n\
}\n\
function checkConfPopup(){var r;\n\
 if(document.getElementById('counterName').value==='')return false;\n\
 if(document.getElementById('multiplier').value==='')return false;\n\
 if(document.getElementById('counterValue').value==='')return false;\n\
 if(document.getElementById('leakNotifPeriod').value==='')return false;\n\
 if(document.getElementById('maxConsumTime').value==='')return false;\n\
 if(document.getElementById('leakMsg').value==='')return false;\n\
 if(!document.getElementById('mqttEnable').checked)return true;\n\
 if(document.getElementById('mqttBroker').value==='')return false;\n\
 if(!(r=document.getElementById('mqttRaws').getElementsByTagName('TR').length-1)){\n\
  document.getElementById('mqttEnable').checked=false;\n\
  return true;\n\
 }for(var i=0;i<r;i++){\n\
  if(document.getElementById('mqttFieldName'+i).value==='')return false;\n\
  if(document.getElementById('mqttValue'+i).value===''&&document.getElementById('mqttNature'+i).value==='1')return false;\n\
 }return true;\n\
}\n\
function refreshConfPopup(){\n\
 document.getElementById('mqttPlus').disabled=!document.getElementById('mqttEnable').checked;\n\
 for(var b,v,i=0,r=document.getElementById('mqttRaws').getElementsByTagName('TR');i<r.length-1;i++)\n\
  if((b=r[i+1].getElementsByTagName('B')).length){\n\
   b[0].innerHTML=b[1].innerHTML=(document.getElementById('mqttType'+i).value==='0'?'\"':'');\n\
   b[0].style.display=b[1].style.display=document.getElementById('mqttValue'+i).style.display=(document.getElementById('mqttNature'+i).value==='0'?'none' :'inline-block');\n\
  }\n\
 setDisabled(document.getElementById('mqttParams').getElementsByTagName('input'),!document.getElementById('mqttEnable').checked);\n\
 setDisabled(document.getElementById('mqttParams').getElementsByTagName('select'),!document.getElementById('mqttEnable').checked);\n\
}\n\
function getPlugNum(e){var f;for(f=e;f.tagName!='FORM';)f=f.parentNode;return f.id;}\n\
function setConfPopup(n){var v;\n\
 //set title:\n\
 v=document.getElementById('groupName').children; v[0].style.display=(n=='0' ?'inline-block' :'none');v[1].style.display=(n=='0' ?'none' :'inline-block');\n\
 //set labels:\n\
 v=document.getElementById('label1').children; v[0].style.display=(n=='0' ?'inline-block' :'none');v[1].style.display=(n=='0' ?'none' :'inline-block');\n\
 v=document.getElementById('label2').children; v[0].style.display=(n=='0' ?'inline-block' :'none');v[1].style.display=(n=='0' ?'none' :'inline-block');\n\
 v=document.getElementById('label3').children; v[0].style.display=(n=='0' ?'inline-block' :'none');v[1].style.display=(n=='0' ?'none' :'inline-block');\n\
 //set fields:\n\
 v=document.getElementById('param1').children; v[0].style.display=(n=='0' ?'inline-block' :'none');v[1].style.display=(n=='0' ?'none' :'inline-block');\n\
 v=document.getElementById('param2').children; v[0].style.display=(n=='0' ?'inline-block' :'none');v[1].style.display=(n=='0' ?'none' :'inline-block');\n\
 v=document.getElementById('param3').children; v[0].style.display=(n=='0' ?'inline-block' :'none');v[1].style.display=(n=='0' ?'none' :'inline-block');\n\
}\n\
function initConfPopup(e){\nvar f;for(f=e;f.tagName!='FORM';)f=f.parentNode;\n\
 f.setAttribute('target','blankFrame'); window.location.href='#confPopup';\n\
 var v=document.getElementById('plugNum').value=getPlugNum(e); setConfPopup(v);\n\
 document.getElementById('mqttEnable').checked=document.getElementById('mqttEnable'+v).checked;\n\
 mqttAllRawsRemove(); for(var i=0;document.getElementById('mqttFieldName'+v+'.'+i);i++){ mqttRawAdd();\n\
  document.getElementById('mqttFieldName'+i).value=document.getElementById('mqttFieldName'+v+'.'+i).value;\n\
  document.getElementById('mqttNature'+i).value=document.getElementById('mqttNature'+v+'.'+i).value;\n\
  document.getElementById('mqttType'+i).value=document.getElementById('mqttType'+v+'.'+i).value;\n\
  document.getElementById('mqttValue'+i).value=document.getElementById('mqttValue'+v+'.'+i).value;\n\
 }refreshConfPopup();\n\
}\n\
function closeConfPopup(){\n\
 if(checkConfPopup()){\n\
  var f=document.getElementById('mqttConf');f.setAttribute('target','blankFrame');\n\
  setTimeout(function(){window.location.href='';}, 1000);f.submit();\n\
}}\n\
</script>\n\
<script src='https://cdn.rawgit.com/HanSolo/SteelSeries-Canvas/master/tween-min.js'></script>\n\
<script src='https://cdn.rawgit.com/HanSolo/SteelSeries-Canvas/master/steelseries-min.js'></script>\n\
"); }
  WEB_F("</div></body>\n</html>\n\n");
  ESPWebServer.sendContent("");
  ESPWebServer.client().stop();
}

void shiftSSID(){
  for(ushort i(0); i<SSIDCount(); i++){
    if(!ssid[i].length() || !password[i].length()) ssid[i]=password[i]="";
    if(!ssid[i].length()) for(ushort j(i+1); j<SSIDCount(); j++)
      if(ssid[j].length() && password[j].length()){
        ssid[i]=ssid[j]; password[i]=password[j]; password[j]="";
        break;
      }else j++;
} }

void writeConfig(){                                     //Save current config:
  if(!readConfig(false))
    return;
  if( !SPIFFS.begin() ){
    DEBUG_print("Cannot open SPIFFS!...\n");
    return;
  }File f=SPIFFS.open("/config.txt", "w+");
  DEBUG_print("Writing SPIFFS.\n");
  if(f){
    f.println(ResetConfig);
    f.println(hostname);                           //Save hostname
    shiftSSID(); for(ushort i(0); i<SSIDCount(); i++){  //Save SSIDs
      f.println(ssid[i]);
      f.println(password[i]);
    }f.println(counterName);
    f.println(counterValue);
    f.println(multiplier);
    f.println(leakNotifPeriod);
    f.println(maxConsumTime);
    f.println(leakStatus);
    f.println(leakMsg);
    f.println(ntpServer);
    f.println(localTimeZone);
    f.println(dailyData.size());
    for (std::map<ulong,ulong>::iterator it=dailyData.begin(); it!=dailyData.end(); it++){
      f.println(it->first);
      f.println(it->second);
    }//MQTT parameters:
    f.println(mqttBroker);
    f.println(mqttPort);
    f.println(mqttIdent);
    f.println(mqttUser);
    f.println(mqttPwd);
    f.println(mqttQueue);
    for(ushort i(0); i<2; i++){      //Save output states
      f.println(mqttEnable[i]);
      for(ushort j(0); j<mqttEnable[i]; j++){
        f.println(mqttFieldName[i][j]);
        f.println(mqttNature[i][j]);
        f.println(mqttType[i][j]);
        f.println(mqttValue[i][j]);
    } }
    f.close(); SPIFFS.end();
    DEBUG_print("SPIFFS writed.\n");
} }

String readString(File f){ String ret=f.readStringUntil('\n'); ret.remove(ret.indexOf('\r')); return ret; }
inline bool getConfig(std::vector<String>::iterator v, File& f, bool w=true){String r(readString(f).c_str());      if(r==*v) return false; if(w)*v=r; return true;}
inline bool getConfig(std::vector<ushort>::iterator v, File& f, bool w=true){ushort r(atoi(readString(f).c_str()));if(r==*v) return false; if(w)*v=r; return true;}
inline bool getConfig(String& v, File& f, bool w=true){String r(readString(f).c_str());       if(r==v) return false; if(w)v=r; return true;}
inline bool getConfig(bool&   v, File& f, bool w=true){bool   r(atoi(readString(f).c_str())); if(r==v) return false; if(w)v=r; return true;}
inline bool getConfig(ushort& v, File& f, bool w=true){ushort r((unsigned)atoi(readString(f).c_str())); if(r==v) return false; if(w)v=r; return true;}
inline bool getConfig(short&  v, File& f, bool w=true){short  r(atoi(readString(f).c_str())); if(r==v) return false; if(w)v=r; return true;}
inline bool getConfig(ulong&  v, File& f, bool w=true){ulong  r((unsigned)atol(readString(f).c_str())); if(r==v) return false; if(w)v=r; return true;}
bool readConfig(bool w){                                //Get config (return false if config is not modified):
  bool isNew(false);
  if( !SPIFFS.begin() ){
    DEBUG_print("Cannot open SPIFFS!...\n");
    return false;
  }File f(SPIFFS.open("/config.txt", "r"));
  if(f && ResetConfig!=atoi(readString(f).c_str())){
    f.close();
    if(w) DEBUG_print("New configFile version...\n");
  }if(!f){
    if(w){    //Write default config:
#ifdef DEFAULT_MQTT_SERVER
      mqttBroker=DEFAULT_MQTT_SERVER; mqttPort=DEFAULT_MQTT_PORT;
      mqttIdent=DEFAULT_MQTT_IDENT; mqttUser=DEFAULT_MQTT_USER; mqttPwd=DEFAULT_MQTT_PWD;
      mqttQueue=DEFAULT_MQTT_QUEUE;
#endif
      SPIFFS.format(); SPIFFS.end(); writeConfig();
      DEBUG_print("SPIFFS initialized.\n");
    }return true;
  }isNew|=getConfig(hostname, f, w);
  for(ushort i(0); i<SSIDCount(); i++){
    isNew|=getConfig(ssid[i], f, w);
    isNew|=getConfig(password[i], f, w);
  }isNew|=getConfig(counterName, f, w);
  isNew|=getConfig(counterValue, f, w);
  isNew|=getConfig(multiplier, f, w);
  isNew|=getConfig(leakNotifPeriod, f, w);
  isNew|=getConfig(maxConsumTime, f, w);
  isNew|=getConfig(leakStatus, f, w);
  isNew|=getConfig(leakMsg, f, w);
  isNew|=getConfig(ntpServer, f, w);
  isNew|=getConfig(localTimeZone, f, w);
  ushort n=dailyData.size();
  isNew|=getConfig(n, f); dailyData.erase( dailyData.begin(), dailyData.end() );
  for(ushort i(0); (!isNew||w) && i<n; i++){
    std::pair<ulong,ulong> data;
    isNew|=getConfig(data.first, f, w);
    isNew|=getConfig(data.second, f, w);
    dailyData.insert(data);
  }//MQTT parameters:
  isNew|=getConfig(mqttBroker, f, w);
  isNew|=getConfig(mqttPort, f, w);
  isNew|=getConfig(mqttIdent, f, w);
  isNew|=getConfig(mqttUser, f, w);
  isNew|=getConfig(mqttPwd, f, w);
  isNew|=getConfig(mqttQueue, f, w);
  for(ushort i(0); (!isNew||w) && i<2; i++){
    isNew|=getConfig(mqttEnable[i], f, w);
    for(ushort j(0); (!isNew||w) && j<mqttEnable[i] && (!isNew||w); j++){
      isNew|=addMQTT(i, j);
      isNew|=getConfig(mqttFieldName[i][j], f, w);
      isNew|=getConfig(mqttNature[i][j], f, w);
      isNew|=getConfig(mqttType[i][j], f, w);
      isNew|=getConfig(mqttValue[i][j], f, w);
  } }
  f.close(); SPIFFS.end();
  if(w){
    DEBUG_print("Config restored.\n");
  }else{
    DEBUG_print("Config read.\n");
  }return isNew;
}

bool WiFiHost(){
#ifdef DEFAULTWIFIPASS
  if(String(DEFAULTWIFIPASS).length()){
    DEBUG_print("\nNo custom SSID found: setting soft-AP configuration ... \n");
    WifiAPTimeout=(WIFIAPDELAYRETRY/WIFISTADELAYRETRY); nbWifiAttempts=MAXWIFIRETRY;
    WiFi.mode(WIFI_AP);
  //WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,254), IPAddress(255,255,255,0));
    WiFiAP=WiFi.softAP(String(DEFAULTHOSTNAME)+"-"+String(ESP.getChipId()), DEFAULTWIFIPASS);
    DEBUG_print(
      WiFiAP
      ?(String("Connecting \"" + hostname+ "\" [") + WiFi.softAPIP().toString() + "] from: " + DEFAULTHOSTNAME + "-" + String(ESP.getChipId()) + "/" + DEFAULTWIFIPASS + "\n\n").c_str()
      :"WiFi Timeout.\n\n");
    return WiFiAP;
  }
#endif
  return false;
}

void WiFiDisconnect(){
  next_reconnect=millis()+WIFISTADELAYRETRY;
  if(WiFiAP || WIFI_STA_Connected())
    DEBUG_print("Wifi disconnected!...\n");
  WiFi.softAPdisconnect(); WiFi.disconnect(); WiFiAP=false;
}

bool WiFiConnect(){
#ifdef DEFAULTWIFIPASS
  WiFiDisconnect();
  DEBUG_print("\n");
  for(ushort i(0); i<SSIDCount(); i++) if(ssid[i].length()){

    //Connection au reseau Wifi /Connect to WiFi network
    WiFi.mode(WIFI_STA);
    DEBUG_print(String("Connecting \"" + hostname+ "\" [") + String(WiFi.macAddress()) + "] to: " + ssid[i]);
    WiFi.begin(ssid[i].c_str(), password[i].c_str());

    //Attendre la connexion /Wait for connection
    for(ushort j(0); j<12 && !WIFI_STA_Connected(); j++){
      delay(500L);
      DEBUG_print(".");
    }DEBUG_print("\n");

    if(WIFI_STA_Connected()){
      nbWifiAttempts=MAXWIFIRETRY;
      //Affichage de l'adresse IP /print IP address:
      DEBUG_print("WiFi connected\n");
      DEBUG_print("IP address: "); DEBUG_print(WiFi.localIP()); DEBUG_print(", dns: "); DEBUG_print(WiFi.dnsIP()); DEBUG_print("\n\n");
      return true;
    } WiFi.disconnect();
  }
  nbWifiAttempts--;
  if(ssid[0].length()){
    DEBUG_print("WiFi Timeout ("); DEBUG_print(nbWifiAttempts);
    DEBUG_print((nbWifiAttempts>1) ?" more attempts)." :" more attempt).\n");
  }else nbWifiAttempts=0;

  if(!nbWifiAttempts)
    return WiFiHost();
#endif
  return false;
}

inline void reboot(){writeConfig(); ESP.restart();}

inline void memoryTest(){
#ifdef MEMORYLEAKS
  ulong f=ESP.getFreeHeap();
  if(f<MEMORYLEAKS) reboot();
  DEBUG_print("FreeMem: " + String(f, DEC) + "\n");
#endif
}

inline void onConnect(){
  ;
#ifdef DEBUG
  if(!WiFiAP){
    telnetServer.begin();
    telnetServer.setNoDelay(true);
  }
#endif
}

inline void ifConnected(){
  MDNS.update();
  if(!isTimeSynchronized()){
    DEBUG_print("Retry NTP synchro...\n");
    NTP.getTime();
  }
#ifdef DEBUG
  if(!WiFiAP){
    if(telnetServer.hasClient()){  //Telnet client connection:
      if (!telnetClient || !telnetClient.connected()){
        if(telnetClient){
          telnetClient.stop();
          DEBUG_print("Telnet Client Stop\n");
        }telnetClient=telnetServer.available();
        telnetClient.flush();
        DEBUG_print("New Telnet client connected...\n");
  } } }
#endif
}

void connectionTreatment(){                              //Test connexion/Check WiFi every mn:
  if(isNow(next_reconnect)){
    next_reconnect=millis()+WIFISTADELAYRETRY;
    memoryTest();

#ifdef DEFAULTWIFIPASS
    if( (!WiFiAP && !WIFI_STA_Connected()) || (WiFiAP && ssid[0].length() && !WifiAPTimeout--) ){
      if(WiFiConnect())
        onConnect();
    }else ifConnected();
#endif
} }

void handleSubmitSSIDConf(){                              //Setting:
  ushort count=0;
  for(ushort i(0); i<SSIDCount(); i++) if(ssid[i].length()) count++;
  for(ushort i(0); i<count;     i++)
    if(ssid[i]==ESPWebServer.arg("SSID")){                      //Modify password if SSID exist
      password[i]=ESPWebServer.arg("password");
      if(!password[i].length())                           //Delete this ssid if no more password
        ssid[i]=="";
      return;
    }
  if(count<SSIDCount()){                                  //Add ssid:
    ssid[count]=ESPWebServer.arg("SSID");
    password[count]=ESPWebServer.arg("password");
} }

inline bool handleCounterNameSubmit(ushort i){               //Set outputs names:
  if(ESPWebServer.hasArg("counterName"+String(i, DEC)) && ESPWebServer.arg("counterName"+String(i, DEC)))
    if(counterName!=ESPWebServer.arg("counterName"+String(i, DEC)))
      return(counterName=ESPWebServer.arg("counterName"+String(i, DEC)));
  return false;
}

#define setMQTT_S(n,m) if(            ESPWebServer.arg(n)         !=m){m=     ESPWebServer.arg(n);         isNew=true;};
#define setMQTT_N(n,m) if((ulong)atol(ESPWebServer.arg(n).c_str())!=m){m=atol(ESPWebServer.arg(n).c_str());isNew=true;};
inline void check_leakNotifPeriod(){
  leakNotifPeriod*=3600000L;
  leakNotifPeriod=min(leakNotifPeriod, MAX_LEAKNOTIF_PERIOD);
  leakNotifPeriod=max(leakNotifPeriod, MIN_LEAKNOTIF_PERIOD);
}inline void check_maxConsumTime(){
  maxConsumTime*=60000L;
  maxConsumTime=min(maxConsumTime, MAX_MAXCONSUM_TIME);
  maxConsumTime=max(maxConsumTime, MIN_MAXCONSUM_TIME);
}bool handleSubmitMQTTConf(ushort n){
  bool isNew(false);
  setMQTT_S("counterName",     counterName    );
  setMQTT_N("counterValue",    counterValue   ); counterValue*=10L;
  setMQTT_N("multiplier",      multiplier     );
  setMQTT_N("leakNotifPeriod", leakNotifPeriod); check_leakNotifPeriod();
  setMQTT_N("maxConsumTime",   maxConsumTime  ); check_maxConsumTime();
  setMQTT_S("leakMsg",         leakMsg        );
  if((mqttEnable[n]=ESPWebServer.hasArg("mqttEnable"))){ushort i;
    if(mqttClient.connected()) mqttClient.disconnect();
    setMQTT_S("mqttBroker",    mqttBroker);
    setMQTT_N("mqttPort",      mqttPort  );
    setMQTT_S("mqttIdent",     mqttIdent );
    setMQTT_S("mqttUser",      mqttUser  );
    setMQTT_S("mqttPwd",       mqttPwd   );
    for(i=0; ESPWebServer.hasArg("mqttFieldName"+String(i,DEC)); i++){
      isNew|=addMQTT(n, i);
      setMQTT_S("mqttFieldName"+String(i,DEC), mqttFieldName[n][i]);
      setMQTT_N("mqttNature"   +String(i,DEC), mqttNature[n][i]   );
      setMQTT_N("mqttType"     +String(i,DEC), mqttType[n][i]     );
      setMQTT_S("mqttValue"    +String(i,DEC), mqttValue[n][i]    );
    } //Remove any erased:
    for(mqttEnable[n]=i; i<mqttFieldName[n].size(); i++){isNew=true;
      mqttFieldName[n].pop_back();
      mqttNature[n].pop_back();
      mqttType[n].pop_back();
      mqttValue[n].pop_back();
  } }
  return isNew;
}

void  handleRoot(){ bool w, blankPage=false;
  if((w=ESPWebServer.hasArg("hostname"))){
    hostname=ESPWebServer.arg("hostname");                //Set host name
    reboot();
  }else if(ESPWebServer.hasArg("ntpServer") || ESPWebServer.hasArg("localTimeZone")){
    if((w|=ESPWebServer.hasArg("ntpServer")))     ntpServer=ESPWebServer.arg("ntpServer");
    if((w|=ESPWebServer.hasArg("localTimeZone"))) localTimeZone=atoi(ESPWebServer.arg("localTimeZone").c_str());
    reboot();
  }else if((w=ESPWebServer.hasArg("reboot"))){
    reboot();
  }else if((w=ESPWebServer.hasArg("password"))){
    handleSubmitSSIDConf(); shiftSSID();                  //Set WiFi connections
    if(WiFiAP && ssid[0].length()) WiFiConnect();
  }else{
    w|=handleCounterNameSubmit(0);                        //Set counter name
  }if(w) writeConfig();
  sendHTML(blankPage);
}

void setIndex(){
  String v;
  v=counterName; v.toLowerCase();
  if ((v=ESPWebServer.arg(v))!=""){
    ulong val;
    val=atol(v.c_str());
    DEBUG_print("HTTP request on counter(" + counterName + ") value: " + String(val, DEC) + "...\n");
    if(val) counterValue=val*10L;
} }

bool mqttNotify(const String& msg, const ushort n){
  if(!mqttEnable[n]) return true;
  if(mqttBroker.length()){
    mqttClient.setServer(mqttBroker.c_str(), mqttPort);
    if(!mqttClient.connected())
      mqttClient.connect(mqttIdent.c_str(), mqttUser.c_str(), mqttPwd.c_str());
    if(mqttClient.connected()){
      String s="{";
      for(ushort i(0); i<mqttEnable[n]; i++){
        if(mqttNature[n][i] || (!getCounter()&&mqttValue[n][i].length())){
          if(i) s+=",";
          s+="\n \"" + mqttFieldName[n][i] + "\": ";
          if(mqttType[n][i]==0) s+= "\"";
          if(!mqttNature[n][i])
                s+= mqttValue[n][i];
          else  s+= msg;
          if(mqttType[n][i]==0) s+= "\"";
      } }
      if(s=="{"){
        DEBUG_print("Nothing to published to \"" + mqttBroker + "\"!\n");
        return false;
      }s+="\n}\n";
      mqttClient.publish(mqttQueue.c_str(), s.c_str());
      DEBUG_print((n ?"Warning message published to \"" :"Counter Value published to \"") + mqttBroker + "\".\n");
      return true;
  } }
  DEBUG_print("MQTT server \"" + mqttBroker + ":" + String(mqttPort,DEC) + "\" not found...\n");
  return false;
}inline bool mqttNotify(String message){return(mqttNotify(message, 1));}              //Warning on possible leaks
inline  bool mqttNotify(ulong volume)  {return(mqttNotify(String(volume, DEC), 0));}  //Volume notification

void getDataFile(){
  File f;
  ESPWebServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  ESPWebServer.send(200, "text/plain");
  if(SPIFFS.begin() && (f=SPIFFS.open("dataStorage", "r"))){
    ESPWebServer.sendContent(F("["));
    for(String s=readString(f); s.length(); ){
      ESPWebServer.sendContent("\n {\n  \"date\": ");
      ESPWebServer.sendContent(s.substring(0, s.indexOf(",")));
      ESPWebServer.sendContent(",\n  \"index\": ");
      ESPWebServer.sendContent(s.substring(s.indexOf(",")+1));
      ESPWebServer.sendContent("\n }");
      if((s=readString(f)).length())
        ESPWebServer.sendContent(",");
      else
        ESPWebServer.sendContent("\n]\n");
  } }
  else{
    ESPWebServer.sendContent("Error: cannot access data...\n");
    DEBUG_print("Cannot open SPIFFS data file!...\n");
  }ESPWebServer.sendContent("");
  ESPWebServer.client().stop();
  if(f) f.close(); SPIFFS.end();
}

bool deleteSPIFFSDataFile(bool b){
  static bool  deleteDataFile=false;
  static ulong next_canDeleteFileStorage;
  if(b){
    deleteDataFile=true;
    next_canDeleteFileStorage = millis() + DELETEDATAFILE_DELAY;
  }else if(deleteDataFile && isNow(next_canDeleteFileStorage)){
    if(SPIFFS.begin()){
      deleteDataFile=false;
      SPIFFS.remove("dataStorage");
      SPIFFS.end();
      DEBUG_print("Data file removed.\n");
    }else{
      next_canDeleteFileStorage = millis() + DELETEDATAFILE_DELAY;
      DEBUG_print("Cannot open SPIFFS!...\n");
    } }
    return deleteDataFile;
} // C-- object... ;-)
void deleteDataFile(bool b=false) {deleteSPIFFSDataFile(b);}
bool isRemovingDataFile()         {return deleteSPIFFSDataFile(false);};

bool reindexMap(){
  if(isTimeSynchronized() && dailyData.size()){
    while(!isTimeSynchronized(dailyData.begin()->first)){
      std::pair<ulong,ulong> v(dailyData.begin()->first+now()-millis()/1000UL, dailyData.begin()->second);
      dailyData.erase(dailyData.begin()); dailyData.insert(v);
    }return true;
  }return false;
}

inline ulong currentHour(const ulong& h) {return ((h/3600UL )*3600UL );}
inline ulong currentDay (const ulong& d) {return ((d/86400UL)*86400UL);}

void dataFileWrite(){
  if(!isRemovingDataFile() && reindexMap()){
    File f;
    if(SPIFFS.begin() && (f=SPIFFS.open("dataStorage", "a"))){
      while(dailyData.size()){
        f.print(dailyData.begin()->first);
        f.print(",");
        f.print(dailyData.begin()->second);
        f.print("\n");
        dailyData.erase(dailyData.begin());
      }f.close(); SPIFFS.end();
      DEBUG_print("SPIFFS Data writed...\n");
    }else{
      DEBUG_print("Cannot open data file!...\n");
} } }

void pushData(){   // Every hours...
  static ulong next_pushData=PUSHDATA_DELAY*1000UL;
  if(isNow(next_pushData)){
    ulong sec(now());
    if((sec-=currentHour(sec))<PUSHDATA_DELAY){
      dailyData[currentHour(now())]=getCounter();
      DEBUG_print("Data writed...\n");
      mqttNotify(getCounter());
      dataFileWrite();
    }if(!isTimeSynchronized())
      next_pushData=millis()+PUSHDATA_DELAY*1000UL;
    else
      next_pushData=(3600UL - sec)*1000UL;
  }deleteDataFile();
}

//Gestion des switchs/Switchs management
void ICACHE_RAM_ATTR counterInterrupt(){intr=true;}

void interruptTreatment(){
  if(intr){
    delay(DEBOUNCE_DELAY);
    if(!digitalRead(COUNTERPIN)){ //Counter++ on falling pin...
      counterValue+=multiplier;
      DEBUG_print("Counter: " + String(getCounter(), DEC) + "\n");
      next_leakCheck=millis() + maxConsumTime;
      intr=false;
} } }

void leakChecker(){
  if(isNow(next_leakCheck)){           // Minimal fixing time reached...
    if(leakStatus>0) leakStatus--;
    next_leakDetected=millis() + leakNotifPeriod;
    next_leakCheck=millis() + maxConsumTime;
  }else if(isNow(next_leakDetected)){  // ...in control period.
    if(++leakStatus > LEAKDETECTLIMITE){
      DEBUG_print("Leak notification!...\n");
      mqttNotify(leakMsg);
    }next_leakDetected=millis() + leakNotifPeriod;
} }

// ***********************************************************************************************
// **************************************** SETUP ************************************************
void setup(){
  Serial.begin(115200);
  while(!Serial);
  Serial_print("\n\nChipID(" + String(ESP.getChipId(), DEC) + ") says:\nHello World!\n\n");

  mqttEnable.push_back(0); mqttEnable.push_back(0);
  readConfig();
  //initialisation des broches /pins init
  if(COUNTERPIN==3 || COUNTERPIN==1) Serial.end();
  //See: https://www.arduino.cc/en/Reference/attachInterrupt
  // or: https://www.arduino.cc/reference/en/language/functions/external-interrupts/attachinterrupt/
  pinMode(COUNTERPIN, INPUT_PULLUP);     //WARNING: only FALLING mode works on all inputs !...
  attachInterrupt(digitalPinToInterrupt(COUNTERPIN), counterInterrupt, FALLING);

  // Servers:
  WiFi.softAPdisconnect(); WiFi.disconnect();
  //Definition des URLs d'entree /Input URL definitions
  ESPWebServer.on("/",           [](){handleRoot();      ESPWebServer.client().stop();});
  ESPWebServer.on("/restart",    [](){reboot();});
  ESPWebServer.on("/data",       [](){getDataFile();});
  ESPWebServer.on("/removeData", [](){deleteDataFile();});
  ESPWebServer.on("/index",      [](){setIndex(); ESPWebServer.send(200, "text/plain", "[" + String(now(), DEC) + "," + String(getCounter(), DEC) + "]");});
//ESPWebServer.on("/about",      [](){ ESPWebServer.send(200, "text/plain", getHelp()); });
  ESPWebServer.onNotFound(       [](){ESPWebServer.send(404, "text/plain", "404: Not found");});

  httpUpdater.setup(&ESPWebServer);  //Adds OnTheAir updates:
  ESPWebServer.begin();              //Demarrage du serveur web /Web server start
  MDNS.begin(hostname.c_str());
  MDNS.addService("http", "tcp", 80);
  Serial_print("HTTP server started\n");

  NTP.begin(ntpServer, localTimeZone);
  NTP.setInterval(NTP_INTERVAL);

#ifdef DEBUG
  NTP.onNTPSyncEvent([](NTPSyncEvent_t error) {
    if (error) {
      DEBUG_print("Time Sync error: ");
      if (error == noResponse){
        DEBUG_print("NTP server not reachable\n");
      }else if (error == invalidAddress){
        DEBUG_print("Invalid NTP server address\n");
      }else{
        DEBUG_print(error);DEBUG_print("\n");
      }
    }else {
      DEBUG_print("Got NTP time: ");
      DEBUG_print(NTP.getTimeDateString(NTP.getLastNTPSync()));
      DEBUG_print("\n");
    }
  });
#endif
  NTP.getTime();
}

// **************************************** LOOP *************************************************
void loop(){
  ESPWebServer.handleClient(); delay(1L);

  connectionTreatment();                //WiFi watcher
  interruptTreatment();                 //Gestion du compteur/Counter management
  pushData();                           //add data
  leakChecker();                        //Check for leaks...
 }
// ***********************************************************************************************
