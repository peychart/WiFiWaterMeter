//Ajust the following:

unsigned short ResetConfig =  2;     //Change this value to reset current config on the next boot...
#define DEFAULTHOSTNAME      "ESP8266"
#define DEFAULTWIFIPASS      "defaultPassword"
#define WIFISTADELAYRETRY     30000L
#define MAXWIFIRETRY          2
#define WIFIAPDELAYRETRY      300000L
//#define MEMORYLEAKS           10000L
#define SSIDCount()           3

#define TIMEZONE              -10

#define COUNTERPIN            D5
#define DEBOUNCE_DELAY        50L
#define MULTIPLIER            5L     //Deciliter per pulse...

//Default values (editable in the web interface):
#define MAXCONSUM_TIME        600000L
#define LEAKNOTIF_PERIOD      2*3600000L
#define WATERLEAK_MESSAGE    "Warning: probable water leak!"

#define DEFAULT_MQTT_SERVER  "mosquitto.home.lan"
#define DEFAULT_MQTT_PORT     1883
#define DEFAULT_MQTT_IDENT   ""
#define DEFAULT_MQTT_USER    ""
#define DEFAULT_MQTT_PWD     ""
#define DEFAULT_MQTT_QUEUE   "domoticz/in"

#define DEBUG
