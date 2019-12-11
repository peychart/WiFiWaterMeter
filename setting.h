//Ajust the following:

#define VERSION                "1.0.1"              //Change first number to reset current config on the next boot...
#define DEFAULTHOSTNAME        "ESP8266"
#define DEFAULTWIFIPASS        "defaultPassword"
#define WIFISTADELAYRETRY       30000UL
#define MAXWIFIRETRY            2
#define WIFIAPDELAYRETRY        300000UL
//#define MEMORYLEAKS           10000L
#define SSIDCount()             3
#define REFRESH_PERIOD          20                  //(s)

#define DEFAULTTIMEZONE         -10
#define DEFAULTNTPSERVER        "fr.pool.ntp.org"
#define DEFAULTDAYLIGHT         false
#define NTP_INTERVAL            3600                //(s)

#define AWAKETIME               600UL               //(s) Before next deep sleep...

#define COUNTERPIN              D5
#define DEBOUNCE_DELAY          50UL                //(ms)
#define MULTIPLIER              5UL                 //Deciliter per pulse...
#define UNIT_DISPLAY            10L                 // 1L ->unit=Liter, 1000L->unit=m3
#define DELETEDATAFILE_DELAY    600000UL            //(ms)

#define EXCLUDED_IPV4_FROM_TUNE 192,168,0,249       //Update requests from this (IP & MASK) (HAProxy server?) are prohibited...
#define EXCLUDED_MASK_FROM_TUNE 255,255,255,255     //Mask to exclude prohibited IPs (warning: ',' not '.')

//Default values (editable in the web interface):
//#define MAXCONSUMPTIONTIME_MEASURE
#define WATERLEAK_MESSAGE      "Warning: probable water leak!"
#define MAXLEAKDETECT           4

#define DEFAULT_MQTT_SERVER    "mosquitto.home.lan"
#define DEFAULT_MQTT_PORT       1883
#define DEFAULT_MQTT_IDENT     ""
#define DEFAULT_MQTT_USER      ""
#define DEFAULT_MQTT_PWD       ""
#define DEFAULT_MQTT_QUEUE     "domoticz/in"

#define BACKGROUND_IMAGE       "https://static.mycity.travel/manage/uploads/7/36/12705/989bd67a1aad43055bd0322e9694f8dd8fab2b43_1080.jpg"

//#define DEBUG
