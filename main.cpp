//Reference: https://www.arduino.cc/en/Reference/HomePage
//Librairies et cartes ESP8266 sur IDE Arduino: http://arduino.esp8266.com/stable/package_esp8266com_index.json
//https://github.com/peychart/WiFiWaterMeter
// Licence: GNU v3
#include <string.h>
#include "FS.h"
#include <LittleFS.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>
//#include <TimeLib.h>
//#include <NtpClientLib.h>
#include <Timezone.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <PubSubClient.h>
//#include <Ethernet.h>
#include <vector>
#include <map>

#define MINIMUM_DELAY                 120UL  //(s)
#include "setting.h"   //Can be adjusted according to the project...

//Avoid to change the following:
typedef long unsigned int             ulong;
typedef short unsigned int            ushort;
#define INFINY                        60000UL
String                                hostname(DEFAULTHOSTNAME);    //Can be change by interface
String                                ssid[SSIDCount()];            //Identifiants WiFi /Wifi idents
String                                password[SSIDCount()];        //Mots de passe WiFi /Wifi passwords
String                                counterName(DEFAULTHOSTNAME), leakMsg(WATERLEAK_MESSAGE), ntpServer(DEFAULTNTPSERVER);
short                                 localTimeZone(DEFAULTTIMEZONE);
ulong                                 deciliterCounter(0UL), pulseValue(PULSE_VALUE);
bool                                  WiFiAP(false), daylight(DEFAULTDAYLIGHT);
#ifdef DEFAULTWIFIPASS
  ushort                              nbWifiAttempts(MAXWIFIRETRY), WifiAPTimeout(0);
#endif
#define MIN_LEAKNOTIF_DELAY           3600000UL   //1h
#define MAX_LEAKNOTIF_DELAY           25200000UL  //7h
#define MIN_MAXCONSUM_DELAY           300000UL    //5mn
#define MAX_MAXCONSUM_DELAY           3600000UL   //1h
#define MIN_MEASUR_PUB_INTERVAL       3600000UL   //1h
#define MEASUREMENT_INTERVAL_SEC      max(MIN_MEASUR_PUB_INTERVAL, MEASUREMENT_INTERVAL)
ulong                                 awakeDelay(max(AWAKEDELAY, MINIMUM_DELAY) * 1000UL),
                                      next_leakCheck(0UL), next_leakPublication(MIN_LEAKNOTIF_DELAY),
                                      leakNotifDelay(MIN_LEAKNOTIF_DELAY), maxConsumTime(MIN_MAXCONSUM_DELAY);
ushort                                leakStatus(0);
const ushort                          UnitDisplay=min(1000L,max(UNIT_DISPLAY,1L));
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

WiFiUDP                               ntpUDP;
NTPClient                             timeClient(ntpUDP);
Timezone                             *myTZ;

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

bool  notifyProxy(ushort, String="");
bool  readConfig(bool=true);
void  writeConfig();
ulong time(bool=false);
bool  isLightSleepAllowed(unsigned char = 0);
void  pushData( bool = false);

inline bool   isNow(ulong v) {ulong ms(millis()); return((v<ms) && (ms-v)<INFINY);}  //Because of millis() rollover:
inline String getM3Counter(ulong c=deciliterCounter){return String(c*UnitDisplay/10000.0, 3-log(UnitDisplay));}
inline ulong  Now() {return( timeClient.isTimeSet() ?myTZ->toLocal(timeClient.getEpochTime()) :(millis()/1000UL) );}
inline bool   isSynchronizedTime(ulong t) {return(t>-1UL/10UL);}
inline ulong  MeasurementInterval(const ulong& t=Now()) {return( (t/MEASUREMENT_INTERVAL_SEC)*MEASUREMENT_INTERVAL_SEC + MEASUREMENT_INTERVAL_SEC);}

bool isDisconnectDelay(bool set=false, bool value=true){
  static ulong next_disconnectDelay(0UL);
  if(!set){
    if( isNow(next_leakPublication - MINIMUM_DELAY) || (leakStatus && isNow(next_leakCheck - MINIMUM_DELAY))){
      isDisconnectDelay(true);
      return false;
    }return(!leakStatus && next_disconnectDelay && isNow(next_disconnectDelay));
  }if(value){
    next_disconnectDelay = 0UL;
    if(isLightSleepAllowed() && !(next_disconnectDelay = millis() + awakeDelay))
      next_disconnectDelay++;
  }return true;
}inline void resetDisconnectDelay() {isDisconnectDelay(true);}
 inline void unsetDisconnectDelay() {isDisconnectDelay(true,false);}

bool isLightSleepAllowed(unsigned char b) {
  static bool lightSleepAllowed(false);
  if(b--) lightSleepAllowed = b;
  return lightSleepAllowed;
}inline void setLightSleep()        {isLightSleepAllowed(2); resetDisconnectDelay();}
 inline void unsetLightSleep()      {isLightSleepAllowed(1); resetDisconnectDelay();}

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

bool authorizedIP(IPAddress v=ESPWebServer.client().remoteIP()){
  IPAddress ip(EXCLUDED_IPV4_FROM_TUNE), mask(EXCLUDED_MASK_FROM_TUNE);
  ESPWebServer.client().remoteIP();
  for(ushort i(0); i<4; i++){
    if((v[i]&mask[i])!=(ip[i]&mask[i]) || !v.isV4())
      return true;
  }DEBUG_print("Client: "); DEBUG_print(v); DEBUG_print(" cannot edit setting!\n");
  return false;
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

void sendHTML(){
  ESPWebServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  ESPWebServer.send(200, "text/html", F("<!DOCTYPE HTML>\n<html lang='us-US'>\n<head>\n\
 <meta charset='utf-8'>\n\
 <meta name='author' content='https://github.com/peychart/WiFiWaterMeter'>\n\
 <meta name='keywords' content='HTML,CSS,JavaScript'>\n"));
  WEB_F(" <title>");
  WEB_S(hostname);
  WEB_F("</title>\n\
<style>\n\
*{margin:0; padding:0;}\n\
body, html{height:100%;}\n\
body {\n\
 color: white;font-size:150%;background-color:green;font-family: Arial,Helvetica,Sans-Serif;\n\
 background-image:url(");
  WEB_S(BACKGROUND_IMAGE);
  WEB_F(");/*background-repeat:no-repeat;*/\n}\n\
#main {max-width:1280px;min-height:100%;margin:0 auto;position:relative;}\n\
footer {text-align:right;position:absolute;bottom:0;width:100%;padding-top:35px;height:35px;}\n\
.table {width:100%;min-width:700px;padding-right:100%;height:50px;border-spacing:0px;}\n\
.modal {display:none;position:fixed;z-index:1;left:0%;top:0%;height:100%;width:100%;overflow:scroll;background-color:#000000;}\n\
.modal-content {background-color:#fff7e6;color:#000088;margin:5% auto;padding:15px;border:2px solid #888;height:90%;width:90%;min-height:755px;}\n\
.close {color:#aaa;float:right;font-size:30px;font-weight:bold;}\n\
.safe {border:none;height:48px;width:48px;cursor:pointer;background-repeat:no-repeat;background:transparent;background-position:center;vertical-align:center;background-image:url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAYAAABXAvmHAAAABmJLR0QA/wD/AP+gvaeTAAAErklEQVRoge2ZTUgcZxjH/8/MrAeJtFhXsn600hLd3VgLXbUUUtCQi8XFrGZNW1vooaQVDfReClsovZe6NubQk4eoUVd2TSmE4LEYhRYbbYT0kFiVbpQGF0V3Z54eojHdeXdnZmf0Ev+3fd6P+f3nfXbeL+BEJ3qxRU50Eh4NyztKppklrVViCmiAlwAPAacAgIEUmFaJ+L5GPC+p8p3AQsNsJBLR7D7bloGOWEe1pkl9DHwM4kqLzVdAPKyqcvTnromVQhkKMtA2GnYrrvS3DHwKoKjQh+9rD8Q/kax9HQ/GH1ttbNlAMNbxETP9AKDUalsDbTBT/3Tn5A0rjUwbCAxdcXncyUEQf2adzbyYaWg96b46//n1tJn6pgwE48Fizsg3AbTZojOvW6So4Xgwvm1UUTKqEBi64jpmeAB4nzPyVHg0bPj/MjTgcScHcbzwB7qwU7T3vVGlvCnUPhHqAfGwc0wFiPiDxMWpkZzFuQpCE6FX0sR/Aig7EjDz2iRFrcv1ic2ZQhni73CM8GUlxbmKSrWM/E2uQuEItI13VsmS9gD2JylTqj1diu7mevz6YAW37/0lqrJHinomHow/zC4QjoBC3I9jhlckCefOvIoLZ18XVStiVeoVFegMRCIRiYl7nAYVyedx43Lzm1CkQ4x336iGW5ROmvRJeDQsZ4d1BuYafn8HQJWzqHrVni5FV5MPsnSYxarGuHl3EcktwfxFXLnjSgeywzoDLGmtDrPq5K8o0735A/iltWTOdsR0PjumM0Ca1OgUqEj+ijJ0NZ4Vvvl88ADAgPEIAKi1TZlDduABgIjrsmN6A8Qes0B5vt06+TxuW/AAwICOTTQCp8x01uKtQW9rE3wet2Fdf0UZLjX5bcHvqyQ7YLiYE6nFW4MWbw1kiXCpyZ/XhN20MZLIQCpfA3dJMd6rfe3Z73wmjgB+KzugN8C0lq+H5NY2RmYXkNEODxRkidDd7Ed9VflRwoMAHZtoBJaNOlpe38To7B//M0FE6Ar4UF9VfmRpw0z3s2OiiWzOTGfL65sYv7sEVeNnMSJC6G2fEH587p7tnBex6QxIqnzHbIdLa0lhOone/OKq5RMTnURsOgOBhYZZAI/MdipKpwM5+bUB8DCw0DCfHdSt7mZmZrj2w7pygM6Z7XkjtYP1J1vwVbgh0dO37zA8AAxe7712OzsonAdUVY4C2LPS+/MjcQTwu1AyUVFBzj1xe6zjRzB9YfVJ/oqnu1Ancv45DSRCsauigpwzcXqv6CsAlikWVx87Db9BippzT5zTwC/dY5vMJHR9nGKm3nyHvnnXQtOdkzeYach5LJMijk53To7lq2K4mCvOKH0AYo5BmRQRT6deevKlUT1DA2PdYyopag+AW46QmVMCstY90zqTMapoajkdD8a31/4pvwjia/bZDEQcTb38b8jMyTRQwAVHe6zjMpgG4PypXZKZ+oxyPluWNzSJi1MjLiYvA4MAdq22F2gXwEA67fJahQdsXvK1x9srocr9YOoBUG2x+SMAw1Ay0UQw8XehDI5cs0YiEWnurd8aiek8AwEirmOgEof76xSYVgAss6TN7V+zzjtxzXqiE73o+g+dpfqwtFDpgQAAAABJRU5ErkJggg==');}\n\
.warning {border:none;height:48px;width:48px;title:'Warning:probable water leak!';cursor:pointer;background-repeat:no-repeat;background:transparent;background-position:center;vertical-align:center;background-image:url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAYAAABXAvmHAAAABmJLR0QA/wD/AP+gvaeTAAAChUlEQVRoge2YT2sTQRjGf9Nmd6NFIlHx4EUQPbR48AOIVtFDUD9Bj6LFPwj2JngRBMFiBC9+Bc+CJ8GmQu9SAoIEL7UgDShEoWnavF7aZLvN7szszlbB/Z2WzOy8z5tn5mESKCgoKCj4n1F5LSwLwRUUrwHoq9tqev19HnVyaUCWqNIrfwGpbn/0k75/Wk132q5rjbleEIBe8DQkHuAw4xtP8ijl3AFZ9CcR9QkoRYa2QM6pCxvLLuu5d0BUnb3iAcZBvXRdzmkD0ghuAFcTplySheC6y5rOtpA08WkHy8AZTcUWv7pTqkbXRV13DrSD++jEAwinOBjcdVXWiQMjYlOHs1h148De2NThLFYzOxAXmw/mp/jR8QA4UulRf9iMvuokVrM7EBObXqk/ePa9fnQYHMVqpgaSYtP3ZPAcbiZC5lhN3YA08YHncePhbz3czAgFdXlHkFZHegc0semVhqL9eAcyx2qqBmSJKqhHSXN2O5DQAIDisXw4dDSNlnQOGMRmWLSnayBDrFo3IIv+JHBTNy+8bfxSwhkYLMwtafhnbfXYOxB/29yF8RkYkipWrRowuG0OsDoDQ6xj1bgBXWxGsTwDYUVWsWrugOltc5uwaM/kDOxgGatGd6EUt82sGN9WzRywv21mxThWtQ4k/EhP5Ntambcfj6OAa+e/c+LYus3rYHhb1YsyjM0or96cZHWtDMDX1QM8u/fZdomdWL2cNClxC9nEZpTOb2/ksyXaWI1twDY2o8zUVqhMbFKZ2GSmtpJ2GW2sxp4BaQRzwHz6yg4R5tTF7otRQ0lb6E5OcuwZi9eSz3+j+0h8A8IsitY+ahmNosWWmv3bMgoKCgoK/k3+AILkzValVvu5AAAAAElFTkSuQmCC');}\n\
.blink {animation:blink 2s steps(5, start) infinite; -webkit-animation:blink 1s steps(5, start) infinite;}\n\
@keyframes blink {to {visibility:hidden;}}\n\
@-webkit-keyframes blink {to {visibility:hidden;}}\n\
.confPopup {position:relative;opacity:0;display:none;-webkit-transition:opacity 400ms ease-in;-moz-transition:opacity 400ms ease-in;transition:opacity 400ms ease-in;}\n\
.confPopup:target{opacity:1;display:block;}\n\
.confPopup > div {width:750px;position:fixed;top:25px;left:25px;margin:10% auto;padding:5px 20px 13px 20px;border-radius:10px;background:#71a6fc;background:-moz-linear-gradient(#71a6fc, #fff);background:-webkit-linear-gradient(#71a6fc, #999);}\n\
.closeconfPopup {background:#606061;color:#FFFFFF;line-height:25px;position:absolute;right:-12px;text-align:center;top:-10px;width:24px;text-decoration:none;-webkit-border-radius:12px;-moz-box-shadow:1px 1px 3px #000;}\n\
.closeconfPopup:hover{background:#00d9ff;}\n\
</style>\n</head>\n");
  WEB_F("<body onload='init();'><div id='main'>\n");
  if(authorizedIP()){
    WEB_F("<div id='about' class='modal'><div class='modal-content'><span class='close' onClick='refresh();'>&times;</span><h2>About:</h2>\
This WiFi Water Meter is a connected device that allows you to control (from a home automation application like Domoticz or Jeedom) the \
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
    WEB_F("\" on: 192.168.4.1.<br><br>\n<table style='width:100%'>\n\
<th style='text-align:left;'><h3>Hostname</h3></th>\n\
<th style='text-align:center;display:inline-block;'><h3>NTP Server - TZone-daylight</h3></th>\n\
<th style='text-align:left;'><h3>ModemSleep</h3></th>\n\
<th style='text-align:center;'><h3>Restart</h3></th>\n\
<tr style='white-space:nowrap;'><td style='text-align:left;'>\n<form method='POST'>\n");
    sendHTML_inputText(F("hostname"), hostname, "style='width:80px'");
    sendHTML_button("", F("Submit"), F("onclick='submit();'"));
    WEB_F("</form>\n</td><td style='text-align:left;display:online-block;'>\n<form method='POST'>");
    sendHTML_inputText(F("ntpServer"), ntpServer, "style='width:200px'"); WEB_F("&nbsp;");
    sendHTML_inputNumber(F("localTimeZone"), String(localTimeZone, DEC), "min=-11 max=11 size=2 style='width:40px'");
    WEB_F("&nbsp;&nbsp;"); sendHTML_checkbox(F("daylight"), daylight, ""); WEB_F("&nbsp;&nbsp;");
    sendHTML_button("", F("Submit"), F("onclick='submit();'"));
    WEB_F("</form>\n</td><td style='text-align:center;'>\n<form method='POST'>");
    sendHTML_checkbox("lightSleepAllowed", isLightSleepAllowed(), F("onclick='lightSleepAllowed();'")); sendHTML_checkbox("lightSleep", true, F("style='display:none;'"));
    WEB_F("</form>\n</td><td style='text-align:center;'>\n<form method='POST'>");
    sendHTML_button(F("restart"), F("Save Data"), F("onclick='submit();'")); sendHTML_checkbox("reboot", true, "style='display:none;'");
    WEB_F("</form>\n</td></tr></table>\n<br><h3>Network connection [");
    WEB_S(WiFi.macAddress());
    WEB_F("]:</h3><table><tr>");
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
<h6><a href='update' onclick='javascript:event.target.port=80'>Firmware update</a> - <a href='https://github.com/peychart/WiFiWaterMeter'>Website here</a></h6>\n\
</div></div>\n");
//                             --------------------------------MAIN-------------------------------
  }WEB_F("<header>\n\
 <div style='text-align:right;white-space:nowrap;'><p><span class='close' onclick='showHelp();'>?&nbsp;</span></p></div>\n</header>\n\
\n\
<!HOME SECTION><section>\n<table id='counter' style='width:100%'><col width='175px'><col width='260px'><tr>\n<td>\n<h3>");
  WEB_S(counterName); WEB_F(": </h3>\n\
</td><td>\n<form id='0'>\n\
<canvas id='canvasOdometer' width='100' height='40'");
  if(authorizedIP())
    WEB_F(" onclick='initConfPopup(this);'");
  WEB_F("></canvas>\n\
</form></td><td>\n<form id='1'>\n\
<button id='leakStatusOk' name='status' class='safe' title='No leak detected.' style='display:none;'");
  if(authorizedIP())
    WEB_F(" onclick='initConfPopup(this);'");
  else WEB_F(" disabled");
  WEB_F("></button>\n\
<button id='leakStatusFail' name='status' class='warning blink' title='Warning: probable water leak!' style='display:inline-block;'");
  if(authorizedIP())
    WEB_F(" onclick='initConfPopup(this);'");
  else WEB_F(" disabled");
  WEB_F("></button>\n</form></td>\n</tr></table>\n");

  if(authorizedIP()){
    //MQTT Parameters:
    WEB_F("\n<!Parameters:>\n<div style='display:none;'>\n");
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
<table style='width:100%'>\n<col width=33%><col width=33%>\n\
<th id='label1' style='text-align:center;'><div style='display:inline-block;'>Counter Name</div><div style='display:none;'>Notification period</div></th>\n\
<th id='label2' style='text-align:center;'><div style='display:inline-block;'>Deciliter per pulse</div><div style='display:none;'>");
#ifdef MAXCONSUMPTIONTIME_MEASURE
    WEB_F("Maximum consumption delay");
#else
    WEB_F("Minimum non-consumption delay");
#endif
    WEB_F("</div></th>\n\
<th id='label3' style='text-align:center;'><div style='display:inline-block;'>Initial value</div><div style='display:none;'>Leak msg</div></th>\n\
<tr><td id='param1' style='text-align:center;'><div style='display:inline-block;'>");
    sendHTML_inputText(F("counterName"), counterName, F("style='width:120px;'"));
    WEB_F("</div><div style='display:none;'>");
    sendHTML_inputNumber(F("leakNotifDelay"), String(leakNotifDelay/3600000L, DEC), "min=" + String(MIN_LEAKNOTIF_DELAY/3600000L, DEC) + " max=" + String(MAX_LEAKNOTIF_DELAY/3600000L, DEC) + " style='width:50px;text-align:right;'");
    WEB_F("h</div></td>\n<td id='param2' align=center><div style='display:inline-block;'>");
    sendHTML_inputNumber(F("pulseValue"), String(pulseValue, DEC), F("min=1 max=100 style='width:50px;text-align:right;'"));
    WEB_F("dl/pulse</div><div style='display:none;'>");
    sendHTML_inputNumber(F("maxConsumTime"), String(maxConsumTime/60000L, DEC), "min=" + String(MIN_MAXCONSUM_DELAY/60000L, DEC) + " max=" + String(MAX_MAXCONSUM_DELAY/60000L, DEC) + " style='width:50px;text-align:right;'");
    WEB_F("mn</div></td>\n<td id='param3' align=center><div style='display:inline-block;'>");
    sendHTML_inputNumber(F("counterValue"), String(deciliterCounter/10.0, 1), F("min=0 max=999999999.9 style='width:85px;text-align:right;'"));
    WEB_F("liters</div><div style='display:none;'>");
    sendHTML_inputText(F("leakMsg"), leakMsg, F("style='width:210px;'"));
    WEB_F("</div></td></tr>\n</table>\n");

    WEB_F("<br><h3 title='Server settings'>MQTT parameters \n");
    //MQTT configuration:
    sendHTML_checkbox(F("mqttEnable"), false, "style='vertical-align:right;' onclick='refreshConfPopup();'");
    WEB_F("</h3>\n<div id='mqttParams'><p align=center title='Server settings'>Broker: ");
    sendHTML_inputText(F("mqttBroker"), mqttBroker, F("style='width:65%;'"));
    WEB_S(":");
    sendHTML_inputNumber(F("mqttPort"), String(mqttPort,DEC), F("min='0' max='-1' style='width:10%;'"));
    WEB_F("</p>\n<table style='width:100%'>\n<col width='42%'><col width='30%'><tr title='Server settings' style='white-space:nowrap;'><td>\nIdentification: ");
    sendHTML_inputText(F("mqttIdent"), mqttIdent, F("style='width:120px;'"));
    WEB_F("</td><td>\nUser: ");
    sendHTML_inputText(F("mqttUser"), mqttUser, F("style='width:120px;'"));
    WEB_F("</td><td>\nPassword: ");
    sendHTML_inputPwd(F("mqttPwd"), mqttPwd, F("style='width:75px;'"));
    WEB_F("</td></tr></table>\n<p align=center title='Server settings'>Topic: ");
    sendHTML_inputText(F("mqttTopic"), mqttQueue, F("style='width:80%;'"));
    WEB_F("</p><br>\n<table id='mqttRaws' title='notification settings' border='1' cellpadding='10' cellspacing='1' style='width:100%'>\n\
<col width='25%'><col width='25%'><col width='20%'><col width='25%'>\n\
<tr>\n<th>FieldName</th><th>Nature</th><th>Type</th><th>Value</th><th>");
    sendHTML_button("mqttPlus", "+", "title='Add a field name' style='background-color:rgba(0, 0, 0, 0);' onclick='mqttAddRaw();'");
    WEB_F("</th></tr>\n</table>\n</div></form></div></div>\n");
  }
  WEB_F("</section>\n\
\n<footer>\n\
<h6>(<div style='display:inline-block;'><span id='date'></span>V<span id='version'></span>,&nbsp;Uptime:&nbsp;<span id='uptime'></span>)</div></h6>\n\
</footer>\n\
\n\
<!FRAME /dev/null><iframe name='blankFrame' height='0' width='0' frameborder='0'></iframe>\n\n\
<script>\n\
var odometer;\n\
this.timer=0;\n\
function init(){try{odometer=new steelseries.Odometer('canvasOdometer', {'decimals':3});}catch(e){;};refresh(1);}\n\
function refresh(v=");
  WEB_S(String(WEB_REFRESH_DELAY, DEC));
  WEB_F("){var e=document.getElementById('about');\n\
 clearTimeout(this.timer);if(e)e.style.display='none';\n\
 if(v>0)this.timer=setTimeout(function(){RequestDevice('status');refresh();},v*1000);\n\
}\n\
function RequestDevice(url){\n\
 var request=new XMLHttpRequest(),requestURL=location.protocol+'//'+location.host+'/'+url;\n\
 request.open('POST',requestURL);request.responseType='json';request.send();\n\
 if(url=='config')\n\
      request.onload=function(){;};\n\
 else request.onload=function(){refreshDisplay(request.response);};\n\
}\n\
function refreshUptime(sec){var e=document.getElementById('uptime');\n\
  e.innerHTML=Math.round(sec/(24*3600));e.innerHTML+='d-';\n\
  e.innerHTML+=Math.round((sec%=24*3600)/3600);e.innerHTML+='h-';\n\
  e.innerHTML+=Math.round((sec%=3600)/60);e.innerHTML+='mn';\n\
}\n\
function refreshDisplay(param){var s='No NTP sync',v=param.index[0];\n\
 if(v>946684800){s=(new Date(v*1000)).toISOString();s=s.substring(0,s.indexOf('T')).split('-').join('/');}\n\
 document.getElementById('date').innerHTML=s + '&nbsp;-&nbsp;';\n\
 canvas=document.getElementById('canvasOdometer');v=param.index[1]*");
  WEB_S(String(UnitDisplay*1.0, 1));
  WEB_F(";\n\
 canvas.innerHTML=v+' m3';if(odometer!==undefined)odometer.setValue(v);else{\n\
  var ctx=canvas.getContext('2d');\n\
  canvas.width=300;ctx.font='35px Comic Sans MS';ctx.fillStyle='white';ctx.textAlign='center';\n\
  ctx.fillText(v+' m3', canvas.width/3, canvas.height/1.5);\n\
}if((s=document.getElementById('counterValue')))s.value=param.counterLiterValue;\n\
 document.getElementById('version').innerHTML=param.version;refreshUptime(Math.round(param.uptime/1000));\n\
 document.getElementById('leakStatusOk').style='display:'+(!param.leakStatus ?'inline-block;' :'none');\n\
 document.getElementById('leakStatusFail').style='display:'+(param.leakStatus ?'inline-block;' :'none');\n\
}\n\
function showHelp(){");
  if(!authorizedIP())
    WEB_F("window.location.href='https://github.com/peychart/WiFiWaterMeter';}\n");
  else{
    WEB_F("refresh(120);document.getElementById('about').style.display='block';}\n");
  WEB_F("function saveSSID(e){var f,s;\n\
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
function lightSleepAllowed(){\n\
 if("); WEB_S(COUNTERPIN==D0 ?"true" :"false"); WEB_F("){alert('Sorry: GPIO16 is already in use!');lightSleepAllowed.checked=false;}\n\
 submit();\n}\n\
//MQTT Forms:\n\
function setDisabled(v,b){for(var i=0;v[i];i++)v[i].disabled=b;}\n\
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
 i.appendChild(j=document.createElement('option'));j.value='0';j.innerHTML=(document.getElementById('plugNum').value=='0' ?'Counter-value' :'Warning-level');\n\
 if(document.getElementById('plugNum').value=='1') {i.appendChild(j=document.createElement('option'));j.value='1';j.innerHTML='Warning-message';}\n\
 i.appendChild(j=document.createElement('option'));j.value='2';j.innerHTML='Constant';\n\
 tr.appendChild(td=document.createElement('td'));td.style='text-align:center;';\n\
 td.appendChild(i=document.createElement('select'));i.id=i.name='mqttType'+n;i.setAttribute('onchange','refreshConfPopup();');i.style='width:80%;text-align:center;';\n\
 i.appendChild(j=document.createElement('option'));j.value='0';j.innerHTML='String';\n\
 i.appendChild(j=document.createElement('option'));j.value='1';j.innerHTML='Number';\n\
 tr.appendChild(td=document.createElement('td'));td.style='text-align:center;';\n\
 td.appendChild(i=document.createElement('b'));i.innerHTML='\"';\n\
 td.appendChild(i=document.createElement('input'));i.id=i.name='mqttValue'+n;i.value='0';i.type='text';i.style='width:60%;text-align:center;';\n\
 td.appendChild(i=document.createElement('b'));i.innerHTML='\"';\n\
 tr.appendChild(td=document.createElement('td'));td.style='text-align:center;';\n\
 td.appendChild(i=document.createElement('input'));i.id='mqttMinus'+n;i.type='button';i.value='-';i.style='background-color:rgba(0, 0, 0, 0);';i.setAttribute('onclick','mqttRawRemove(this);');\n\
}\n\
function checkConfPopup(){var r;\n\
 if(document.getElementById('counterName').value==='')return false;\n\
 if(document.getElementById('pulseValue').value==='')return false;\n\
 if(document.getElementById('counterValue').value==='')return false;\n\
 if(document.getElementById('leakNotifDelay').value==='')return false;\n\
 if(document.getElementById('maxConsumTime').value==='')return false;\n\
 if(document.getElementById('leakMsg').value==='')return false;\n\
 if(!document.getElementById('mqttEnable').checked)return true;\n\
 if(document.getElementById('mqttBroker').value==='')return false;\n\
 if(document.getElementById('mqttTopic').value==='')return false;\n\
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
   b[0].style.display=b[1].style.display=document.getElementById('mqttValue'+i).style.display=(document.getElementById('mqttNature'+i).value==='2'?'inline-block' :'none');\n\
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
}}\n");
  }WEB_F("</script>\n\
<script src='https://cdn.rawgit.com/HanSolo/SteelSeries-Canvas/master/tween-min.js'></script>\n\
<script src='https://cdn.rawgit.com/HanSolo/SteelSeries-Canvas/master/steelseries-min.js'></script>\n\
</div></body>\n</html>\n\n");
  ESPWebServer.sendContent("");
  ESPWebServer.client().stop();
}

void sendBlankHTML(){
  ESPWebServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  ESPWebServer.send(200, "text/html", F("<!DOCTYPE HTML>\n<html lang='us-US'>\n<head><meta charset='utf-8'/>\n<body>\n</body>\n</html>\n\n"));
  ESPWebServer.sendContent("");
  ESPWebServer.client().stop();
}

void shiftSSID(){
  for(ushort i(0); i<SSIDCount(); i++){
    if(!ssid[i].length() || !password[i].length()) ssid[i]=password[i]="";
    for(ushort n(SSIDCount()-i-1);!ssid[i].length() && n; n--){
      for(ushort j(i+1); j<SSIDCount(); j++){
        ssid[i]=ssid[j]; password[i]=password[j]; ssid[j]="";
} } } }

void writeConfig(){                                     //Save current config:
  if(!readConfig(false))
    return;
  if( !LittleFS.begin() ){
    DEBUG_print("Cannot open LittleFS!...\n");
    return;
  }File f=LittleFS.open("/config.txt", "w+");
  DEBUG_print("Writing LittleFS.\n");
  if(f){
    f.println(String(VERSION).substring(0, String(VERSION).indexOf(".")));
    f.println(hostname);                           //Save hostname
    shiftSSID(); for(ushort i(0); i<SSIDCount(); i++){  //Save SSIDs
      f.println(ssid[i]);
      f.println(password[i]);
    }f.println(counterName);
    f.println(deciliterCounter);
    f.println(pulseValue);
    f.println(leakNotifDelay);
    f.println(maxConsumTime);
    f.println(leakStatus);
    f.println(leakMsg);
    f.println(ntpServer);
    f.println(localTimeZone);
    f.println(daylight);
    f.println(isLightSleepAllowed());
    /*f.println(dailyData.size());
    for (std::map<ulong,ulong>::const_iterator it=dailyData.begin(); it!=dailyData.end(); it++){
      f.println(it->first);
      f.println(it->second);
    }*/
    //MQTT parameters:
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
    f.close(); LittleFS.end();
    DEBUG_print("LittleFS writed.\n");
} }

String readString(File f){ String ret=f.readStringUntil('\n'); ret.remove(ret.indexOf('\r')); return ret; }
inline bool getConf(std::vector<String>::iterator v, File& f, bool w=true){String r(readString(f).c_str());      if(r==*v) return false; if(w)*v=r; return true;}
inline bool getConf(std::vector<ushort>::iterator v, File& f, bool w=true){ushort r(atoi(readString(f).c_str()));if(r==*v) return false; if(w)*v=r; return true;}
inline bool getConf(String& v, File& f, bool w=true){String r(readString(f).c_str());       if(r==v) return false; if(w)v=r; return true;}
inline bool getConf(bool&   v, File& f, bool w=true){bool   r(atoi(readString(f).c_str())); if(r==v) return false; if(w)v=r; return true;}
inline bool getConf(ushort& v, File& f, bool w=true){ushort r((unsigned)atoi(readString(f).c_str())); if(r==v) return false; if(w)v=r; return true;}
inline bool getConf(short&  v, File& f, bool w=true){short  r(atoi(readString(f).c_str())); if(r==v) return false; if(w)v=r; return true;}
inline bool getConf(ulong&  v, File& f, bool w=true){ulong  r((unsigned)atol(readString(f).c_str())); if(r==v) return false; if(w)v=r; return true;}
bool readConfig(bool w){                                //Get config (return false if config is not modified):
  bool isNew(false);
  if( !LittleFS.begin() ){
    DEBUG_print("Cannot open LittleFS!...\n");
    return false;
  }File f(LittleFS.open("/config.txt", "r"));
  if(f && String(VERSION).substring(0, String(VERSION).indexOf("."))!=readString(f)){
    f.close();
    if(w) DEBUG_print("New configFile version...\n");
  }if(!f){
    if(w){    //Write default config:
#ifdef DEFAULT_MQTT_SERVER
      mqttBroker=DEFAULT_MQTT_SERVER; mqttPort=DEFAULT_MQTT_PORT;
      mqttIdent=DEFAULT_MQTT_IDENT; mqttUser=DEFAULT_MQTT_USER; mqttPwd=DEFAULT_MQTT_PWD;
      mqttQueue=DEFAULT_MQTT_QUEUE;
#endif
      LittleFS.format(); LittleFS.end(); writeConfig();
      DEBUG_print("LittleFS initialized.\n");
    }return true;
  }isNew|=getConf(hostname, f, w);
  for(ushort i(0); i<SSIDCount(); i++){
    isNew|=getConf(ssid[i], f, w);
    isNew|=getConf(password[i], f, w);
  }isNew|=getConf(counterName, f, w);
  isNew|=getConf(deciliterCounter, f, w);
  isNew|=getConf(pulseValue, f, w);
  isNew|=getConf(leakNotifDelay, f, w);
  isNew|=getConf(maxConsumTime, f, w);
  isNew|=getConf(leakStatus, f, w);
  isNew|=getConf(leakMsg, f, w);
  isNew|=getConf(ntpServer, f, w);
  isNew|=getConf(localTimeZone, f, w);
  isNew|=getConf(daylight, f, w);
  {bool b(isLightSleepAllowed()); isNew|=getConf(b, f, w); if(b) setLightSleep(); else unsetLightSleep();}
  /*ushort n=dailyData.size();
  isNew|=getConf(n, f); dailyData.erase( dailyData.begin(), dailyData.end() );
  for(ushort i(0); (!isNew||w) && i<n; i++){
    std::pair<ulong,ulong> data;
    isNew|=getConf(data.first, f, w);
    isNew|=getConf(data.second, f, w);
    dailyData.insert(data);
  }*/
  //MQTT parameters:
  isNew|=getConf(mqttBroker, f, w);
  isNew|=getConf(mqttPort, f, w);
  isNew|=getConf(mqttIdent, f, w);
  isNew|=getConf(mqttUser, f, w);
  isNew|=getConf(mqttPwd, f, w);
  isNew|=getConf(mqttQueue, f, w);
  for(ushort i(0); (!isNew||w) && i<2; i++){
    isNew|=getConf(mqttEnable[i], f, w);
    for(ushort j(0); (!isNew||w) && j<mqttEnable[i] && (!isNew||w); j++){
      isNew|=addMQTT(i, j);
      isNew|=getConf(mqttFieldName[i][j], f, w);
      isNew|=getConf(mqttNature[i][j], f, w);
      isNew|=getConf(mqttType[i][j], f, w);
      isNew|=getConf(mqttValue[i][j], f, w);
  } }
  f.close(); LittleFS.end();
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

inline void reboot(){
  pushData(true);
  writeConfig();
  ESP.restart();
}

inline void memoryTest(){
#ifdef MEMORYLEAKS      //oberved on DNS server (bind9/NTP) errors -> reboot each ~30mn
  ulong f=ESP.getFreeHeap();
  if(f<MEMORYLEAKS) reboot();
  DEBUG_print("FreeMem: " + String(f, DEC) + "\n");
#endif
}

inline bool connected() {return(WiFiAP || WIFI_STA_Connected());}

void WiFiDisconnect(){
  if(connected())
    DEBUG_print("Wifi disconnected!...\n");
  WiFi.softAPdisconnect(); WiFi.disconnect(); WiFiAP=false;
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin(); delay(1L);
  unsetDisconnectDelay();
}

inline void onWiFiConnect(){
  timeClient.forceUpdate();
  resetDisconnectDelay();
#ifdef DEBUG
  if(!WiFiAP){
    telnetServer.begin();
    telnetServer.setNoDelay(true);
  }
#endif
}

inline void ifConnected(){ // Every WIFISTADELAYRETRY
  memoryTest();

  if(isDisconnectDelay()){
    WiFiDisconnect();
  }else{
    MDNS.update();
    timeClient.update();

#ifdef DEBUG
    if(!WiFiAP){
      if(telnetServer.hasClient()){  //Telnet client connection:
        if (!telnetClient || !telnetClient.connected()){
          if(telnetClient){
            telnetClient.stop();
            DEBUG_print("Telnet Client Stop\n");
          }telnetClient=telnetServer.available();
          telnetClient.flush();
          DEBUG_print("Telnet client connected...\n");
    } } }
#endif
} }

bool WiFiConnect(){
#ifdef DEFAULTWIFIPASS
  WiFiDisconnect(); WiFi.forceSleepWake(); delay(1L);
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
      Serial_print("IP address: "); Serial_print(WiFi.localIP()); Serial_print(", dns: "); Serial_print(WiFi.dnsIP()); Serial_print("\n\n");
      onWiFiConnect();
      return true;
    }WiFi.disconnect();
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

void connectionMonitoring(){ //Test connexion/Check WiFi every mn:
#ifdef DEFAULTWIFIPASS
  static ulong next_connectionTest=0UL;
  if(isNow(next_connectionTest)){
    next_connectionTest = millis() + 60000UL;
    if( connected() ) ifConnected();
    else if(!isLightSleepAllowed() && nbWifiAttempts) WiFiConnect();
  }
#endif
}

String getConfig(String s)                                  {return "\""+s+"\"";}
template<typename T> String getConfig(T v)                  {return String(v);}
template<typename T> String getConfig(String n, T v)        {return getConfig(n)+":"+getConfig(v);}
String getStatus(){
  return(
    "{" + getConfig("version",           String(VERSION))
  + "," + getConfig("uptime",            millis())
  + "," + getConfig(String("index")) + ":[" + String(Now(), DEC) + "," + getM3Counter() + "]"
  + "," + getConfig("counterLiterValue", String(deciliterCounter/10.0,1))
  + "," + getConfig("leakStatus",        leakStatus)
  + "}"
  );
}

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

#define setMQTT_S(n,m) if(            ESPWebServer.arg(n)         !=m){m=     ESPWebServer.arg(n);         isNew=true;};
#define setMQTT_N(n,m) if((ulong)atol(ESPWebServer.arg(n).c_str())!=m){m=atol(ESPWebServer.arg(n).c_str());isNew=true;};
inline void check_leakNotifDelay(){
  leakNotifDelay*=3600000L;
  leakNotifDelay=min(leakNotifDelay, MAX_LEAKNOTIF_DELAY);
  leakNotifDelay=max(leakNotifDelay, MIN_LEAKNOTIF_DELAY);
}inline void check_maxConsumTime(){
  maxConsumTime*=60000L;
  maxConsumTime=min(maxConsumTime, MAX_MAXCONSUM_DELAY);
  maxConsumTime=max(maxConsumTime, MIN_MAXCONSUM_DELAY);
}bool handleSubmitMQTTConf(ushort n){
  bool isNew(false); String count(deciliterCounter/10.0, 1);
  setMQTT_S("counterName",       counterName    );
  setMQTT_S("counterValue",      count          ); deciliterCounter=(ulong)(atof(count.c_str())*10.0);
  setMQTT_N("pulseValue",        pulseValue     );
  setMQTT_N("leakNotifDelay",    leakNotifDelay ); check_leakNotifDelay();
  setMQTT_N("maxConsumTime",     maxConsumTime  ); check_maxConsumTime();
  setMQTT_S("leakMsg",           leakMsg        );
  if((mqttEnable[n]=ESPWebServer.hasArg("mqttEnable"))){ushort i;
    if(mqttClient.connected())   mqttClient.disconnect();
    setMQTT_S("mqttBroker",      mqttBroker);
    setMQTT_N("mqttPort",        mqttPort  );
    setMQTT_S("mqttIdent",       mqttIdent );
    setMQTT_S("mqttUser",        mqttUser  );
    setMQTT_S("mqttPwd",         mqttPwd   );
    setMQTT_S("mqttTopic",       mqttQueue );
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
  if((w=ESPWebServer.hasArg("hostname"))){                                                    //Set host name
    hostname=ESPWebServer.arg("hostname");
    reboot();
  }else if(ESPWebServer.hasArg("ntpServer") || ESPWebServer.hasArg("localTimeZone")){         //set NTP service
    if((w|=ESPWebServer.hasArg("ntpServer")))     ntpServer=ESPWebServer.arg("ntpServer");
    if((w|=ESPWebServer.hasArg("localTimeZone"))) localTimeZone=atoi(ESPWebServer.arg("localTimeZone").c_str());
    if((w|=(ESPWebServer.hasArg("daylight")!=daylight))) daylight=ESPWebServer.hasArg("daylight");
    reboot();
  }else if(ESPWebServer.hasArg("lightSleep")){                                                //set modem sleep
    if((w|=(ESPWebServer.hasArg("lightSleepAllowed")!=isLightSleepAllowed()))) {if (ESPWebServer.hasArg("lightSleepAllowed")) setLightSleep(); else unsetLightSleep();}
  }else if(ESPWebServer.hasArg("reboot")){
    reboot();
  }else if((w|=ESPWebServer.hasArg("password"))){                                             //set WiFi connections
    handleSubmitSSIDConf(); shiftSSID();
    if(WiFiAP && ssid[0].length()) WiFiConnect();
  }else if(ESPWebServer.hasArg("plugNum")){                                                   //set counter parameters
    w|=handleSubmitMQTTConf(atoi(ESPWebServer.arg("plugNum").c_str()));
  }if(w) writeConfig();
  if(blankPage)
       sendBlankHTML();
  else sendHTML();
}

void setIndex(){
  String v;
  v=counterName; v.toLowerCase();
  if ((v=ESPWebServer.arg(v))!=""){
    float val(atof(v.c_str()));
    if(val){
      deciliterCounter=val*1000L*UnitDisplay;
      DEBUG_print("HTTP request on counter(" + counterName + ") value: " + String(val, DEC) + "...\n");
} } }

bool mqttNotify(ushort n){
  if(!mqttEnable[n])
    return true;
  if(!connected() && !WiFiConnect()){
    DEBUG_print("MQTT notification not enabled (no WiFi connection)...\n");
    return false;
  }
  if(mqttBroker.length()){
    mqttClient.setServer(mqttBroker.c_str(), mqttPort);
    if(!mqttClient.connected())
      mqttClient.connect(mqttIdent.c_str(), mqttUser.c_str(), mqttPwd.c_str());
  }if(!mqttBroker.length() || !mqttClient.connected()){
    DEBUG_print("MQTT server \"" + mqttBroker + ":" + String(mqttPort,DEC) + "\" not found...\n");
    return false;
  }if(!timeClient.isTimeSet() && !n){
    DEBUG_print("Time is not set, MQTT request aborted...\n");
    return false;
  }
  
  String s="{";
  for(ushort i(0); i<mqttEnable[n]; i++){
    if(i) s+=",";
    s+="\n \"" + mqttFieldName[n][i] + "\": ";
    if(mqttType[n][i]==0) s+= "\"";   //type "String"
    if(mqttNature[n][i]==0){          //Counter-value or Warning-level
      if(n) s+=String(leakStatus, DEC);
      else  s+=String(deciliterCounter/10.0, 1);
    }else if(mqttNature[n][i]!=1)
          s+=leakMsg;                 //Warning-message
    else  s+=mqttValue[n][i];         //Constant
    if(mqttType[n][i]==0) s+= "\"";
  }if(s=="{"){
    DEBUG_print("Nothing to published to \"" + mqttBroker + "\"!\n");
    return false;
  }s+="\n}\n";
  mqttClient.publish(mqttQueue.c_str(), s.c_str());
  DEBUG_print((n ?"Leak control message published to \"" :"Counter Value published to \"") + mqttBroker + "\".\n");
  return true;
}inline bool mqttNotifyIndex  (void) {return(mqttNotify(0));}  //Volume notification
 inline bool mqttNotifyWarning(void) {return(mqttNotify(1));}  //Possible leak warning

void getData(std::map<ulong,ulong>& d){
  for(std::map<ulong,ulong>::const_iterator it=dailyData.begin(); it!=dailyData.end(); ){
    ESPWebServer.sendContent("\n  [");
    ESPWebServer.sendContent(String(it->first, DEC));
    ESPWebServer.sendContent(",");
    ESPWebServer.sendContent(getM3Counter(it->second));
    ESPWebServer.sendContent("]");
    if((++it)!=dailyData.end())
      ESPWebServer.sendContent(",");
} }

void getData(String fileName){
  File f;
DEBUG_print("Open: "); DEBUG_print(fileName); DEBUG_print("\n");
  if(LittleFS.begin() && (f=LittleFS.open(fileName, "r"))){
    for(String s=readString(f); s.length(); ){
      ESPWebServer.sendContent("\n  [");
      ESPWebServer.sendContent(s.substring(0,s.indexOf(",")));
      ESPWebServer.sendContent(",");
      ESPWebServer.sendContent(getM3Counter((ulong)atol(s.substring(s.indexOf(",")+1).c_str())));
      ESPWebServer.sendContent("]");
      if((s=readString(f)).length())
        ESPWebServer.sendContent(",");
    }f.close();
  }LittleFS.end();
}

void getData(bool current){
  ESPWebServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  ESPWebServer.send(200, "text/plain");
  ESPWebServer.sendContent(F("{\n \"counterName\":\""));
  ESPWebServer.sendContent(counterName);
  ESPWebServer.sendContent(F("\",\n \"records\":["));
  if(current)
        getData(String("dataStorage"));
  else  getData(dailyData);
  ESPWebServer.sendContent("\n ]\n}\n");
  ESPWebServer.sendContent("");
  ESPWebServer.client().stop();
}void getHistory()   {getData(true);}
void getDayRecords() {getData(false);}

bool deleteLittleFSDataFile(bool b, bool force=false){
  static bool  deleteDataFile=false;
  static ulong next_canDeleteFileStorage;
  if(b || force){
    deleteDataFile=(force ?b :true);
    next_canDeleteFileStorage = millis() + DELETEDATAFILE_DELAY;
  }else if(deleteDataFile && isNow(next_canDeleteFileStorage)){
    if(LittleFS.begin()){
      deleteDataFile=false;
      LittleFS.remove("dataStorage");
      LittleFS.end();
      DEBUG_print("Data file removed.\n");
    }else{
      next_canDeleteFileStorage = millis() + DELETEDATAFILE_DELAY;
      DEBUG_print("Waiting for history deletion...\n");
    } }
    return deleteDataFile;
} // C-- object... ;-)
void deleteDataFile(bool b=true)  {deleteLittleFSDataFile(b);}
bool isRemovingDataFile()         {return deleteLittleFSDataFile(false);};
void stopHistoryReset()           {deleteLittleFSDataFile(false, true);};

// *************************************** Measure **********************************************
bool reindexMap(ulong T, ulong m){
  if( timeClient.isTimeSet() ){
    std::map<ulong,ulong> buf;
    while(dailyData.size()){
      if( isSynchronizedTime(dailyData.begin()->first) )
        buf[dailyData.begin()->first] = dailyData.begin()->second;
      else if(dailyData.begin()->first < m)
        buf[MeasurementInterval( (T - (m - dailyData.begin()->first)) )] = dailyData.begin()->second;
      dailyData.erase(dailyData.begin()->first);
    }while(buf.size()) {dailyData[buf.begin()->first] = buf.begin()->second; buf.erase(buf.begin());} //Low memory!
    return dailyData.size();
  }return false;
}

void dataFileWrite(ulong t, ulong m){
  if(KEEP_HISTORY){
    DEBUG_print("Trying data write...\n");
    if( !isRemovingDataFile() && reindexMap(t,m) ){
      File f;
      if( LittleFS.begin() && (f=LittleFS.open("dataStorage", "a")) ){
        while( dailyData.size() && dailyData.begin()->first != t ){
          f.print(dailyData.begin()->first);
          f.print(",");
          f.print(dailyData.begin()->second);
          f.print("\n");
          dailyData.erase(dailyData.begin());
        }f.close(); LittleFS.end();
        writeConfig();
        DEBUG_print("LittleFS Data writed.\n");
      }else{
        DEBUG_print("Cannot open data file!...\n");
} } } }

void pushData(bool force){   //Do and Send the measurement each MEASUREMENT_INTERVAL_SEC :
  static ulong next_pushData(0UL), next_MeasurementPublication(MEASUREMENT_INTERVAL_SEC);
  const  ulong nextPushDelay(30000UL);
  if( force || isNow(next_pushData) ){
    ulong T(Now()), m(millis());
    next_pushData = m + nextPushDelay;
      dailyData[MeasurementInterval(T)] = deciliterCounter; // It's time to write index to memory...

    if( force || isNow(next_MeasurementPublication) ){
      next_MeasurementPublication=m + MEASUREMENT_INTERVAL_SEC;
      dataFileWrite(T, m); // It's time to write index to SD...
      mqttNotifyIndex();
} } }

//Gestion des switchs/Switchs management
void IRAM_ATTR counterInterrupt(){intr=true;}

void interruptTreatment(){
  if(intr){
    delay(DEBOUNCE_DELAY);
    if(!digitalRead(COUNTERPIN)){ //Counter++ on falling pin...
      deciliterCounter+=pulseValue;
      DEBUG_print("Counter: " + String(deciliterCounter, DEC) + "\n");
      next_leakCheck=millis() + maxConsumTime;
      intr=false;
} } }

#ifdef MAXCONSUMPTIONTIME_MEASURE
void leakChecker(){
  if(isNow(next_leakPublication)){  // ...in control period.
    if(leakStatus>0){
      leakStatus--;
      DEBUG_print("Leak notification!...\n");
      mqttNotifyWarning();
    }next_leakPublication=millis() + leakNotifDelay;
  }if(isNow(next_leakCheck)){           // Minimal fixing time reached...
    if(leakStatus < MAXLEAKDETECT) leakStatus++;
    mqttNotifyWarning();
    next_leakPublication=millis() + leakNotifDelay;
    next_leakCheck=millis() + maxConsumTime;
} }

#else
void leakChecker(){
  if(isNow(next_leakCheck)){           // Minimal fixing time reached...
    if(leakStatus>0){
      leakStatus--;
      mqttNotifyWarning();
      next_leakPublication=millis() + leakNotifDelay;
    }next_leakCheck=millis() + maxConsumTime;
  }else if(isNow(next_leakPublication)){  // ...in control period.
    if(leakStatus < MAXLEAKDETECT) leakStatus++;
    DEBUG_print("Leak notification!...\n");
    mqttNotifyWarning();
    next_leakPublication=millis() + leakNotifDelay;
} }
#endif

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
  WiFi.softAPdisconnect(); WiFiDisconnect();
  //Definition des URLs d'entree /Input URL definitions
  ESPWebServer.on("/",                 [](){handleRoot(); resetDisconnectDelay();  ESPWebServer.client().stop();});
  ESPWebServer.on("/status",           [](){setIndex();   resetDisconnectDelay();  ESPWebServer.send(200, "json/plain", getStatus());});
  ESPWebServer.on("/getCurrentIndex",  [](){setIndex();   resetDisconnectDelay();  ESPWebServer.send(200, "text/plain", "[" + String(Now(), DEC) + "," + getM3Counter() + "]");});
  ESPWebServer.on("/getData",          [](){if(authorizedIP()){getHistory();    resetDisconnectDelay();                                               }else ESPWebServer.send(403, "text/plain", "403: access denied");});
  ESPWebServer.on("/getCurrentRecords",[](){if(authorizedIP()){getDayRecords(); resetDisconnectDelay();                                               }else ESPWebServer.send(403, "text/plain", "403: access denied");});
  ESPWebServer.on("/resetHistory",     [](){if(authorizedIP()){getHistory(); deleteDataFile(); resetDisconnectDelay();                                }else ESPWebServer.send(403, "text/plain", "403: access denied");});
  ESPWebServer.on("/stopHistoryReset", [](){if(authorizedIP()){stopHistoryReset(); ESPWebServer.send(200, "text/plain", "Ok"); resetDisconnectDelay();}else ESPWebServer.send(403, "text/plain", "403: access denied");});
  ESPWebServer.on("/modemSleepAllowed",[](){if(authorizedIP()){setLightSleep();    ESPWebServer.send(200, "text/plain", "Ok"); resetDisconnectDelay();}else ESPWebServer.send(403, "text/plain", "403: access denied");});
  ESPWebServer.on("/modemSleepDenied", [](){if(authorizedIP()){unsetLightSleep();  ESPWebServer.send(200, "text/plain", "Ok"); resetDisconnectDelay();}else ESPWebServer.send(403, "text/plain", "403: access denied");});
  ESPWebServer.on("/restart",          [](){if(authorizedIP()){reboot();                                                                              }else ESPWebServer.send(403, "text/plain", "403: access denied");});
//ESPWebServer.on("/about",            [](){ ESPWebServer.send(200, "text/plain", getHelp()); });
  ESPWebServer.onNotFound(             [](){ESPWebServer.send(404, "text/plain", "404: Not found"); resetDisconnectDelay();});

  httpUpdater.setup(&ESPWebServer);  //Adds OnTheAir updates
  ESPWebServer.begin();              //Demarrage du serveur web /Web server start
  Serial_print("HTTP server started\n");

  MDNS.begin(hostname.c_str());
  MDNS.addService("http", "tcp", 80);

  if(ntpServer.length()){
    timeClient.setPoolServerName(ntpServer.c_str());
    timeClient.setUpdateInterval(NTP_UPDATE_INTERVAL_MS*1000UL);
    //timeClient.setTimeOffset(3600 * localTimeZone);
    dstRule.offset = 60 * (localTimeZone - daylight);
    stdRule.offset = 60 * localTimeZone;
    myTZ = new Timezone(dstRule, stdRule);
    //myTZ->setRules(dstRule, stdRule);
    timeClient.begin();
} }

// **************************************** LOOP *************************************************
void loop(){
  connectionMonitoring();
  ESPWebServer.handleClient(); delay(1L);

  interruptTreatment();                 //Gestion du compteur/Counter management
  pushData();                           //add data
  leakChecker();                        //Check for leaks...
 }
// ***********************************************************************************************
