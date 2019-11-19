//Ajust the following:

unsigned short ResetConfig =    1;                  //Change this value to reset current config on the next boot...
#define DEFAULTHOSTNAME        "ESP8266"
#define DEFAULTWIFIPASS        "defaultPassword"
#define WIFISTADELAYRETRY       30000UL
#define MAXWIFIRETRY            2
#define WIFIAPDELAYRETRY        300000UL
//#define MEMORYLEAKS           10000L
#define SSIDCount()             3

#define TIMEZONE                -10
#define NTPSERVER              "fr.pool.ntp.org"
#define NTP_INTERVAL            3600

#define COUNTERPIN              D5
#define DEBOUNCE_DELAY          50UL
#define MULTIPLIER              5UL                 //Deciliter per pulse...
#define UNIT_DISPLAY            10L                 // 1L ->unit=Liter, 1000L->unit=m3
#define DELETEDATAFILE_DELAY    600000UL

#define EXCLUDED_IPV4_FROM_TUNE 192,168,0,100       //Update requests from this (IP & MASK) (HAProxy server?) are prohibited...
#define EXCLUDED_MASK_FROM_TUNE 255,255,255,255     //Mask to exclude prohibited IPs (warning: ',' not '.')

//Default values (editable in the web interface):
#define WATERLEAK_MESSAGE      "Warning: probable water leak!"
#define LEAKDETECTLIMITE        3

#define DEFAULT_MQTT_SERVER    "mosquitto.home.lan"
#define DEFAULT_MQTT_PORT       1883
#define DEFAULT_MQTT_IDENT     ""
#define DEFAULT_MQTT_USER      ""
#define DEFAULT_MQTT_PWD       ""
#define DEFAULT_MQTT_QUEUE     "domoticz/in"

#define DEBUG
