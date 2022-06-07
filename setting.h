//Ajust the following:

//#define DEBUG

#define VERSION                "1.1.2"              //Change first number to reset current config on the next boot...
#define DEFAULTHOSTNAME        "ESP8266"
#define DEFAULTWIFIPASS        "defaultPassword"
#define WIFISTADELAYRETRY       30000UL
#define MAXWIFIRETRY            2
#define WIFIAPDELAYRETRY        300000UL
#define MEMORYLEAKS             5000L
#define SSIDCount()             3
#define WEB_REFRESH_PERIOD      20                  //(s)

#define DEFAULTTIMEZONE         -10
#define DEFAULTNTPSERVER       "pool.ntp.org"
#define NTP_UPDATE_INTERVAL_MS  1800                //(s)
#define DEFAULTDAYLIGHT         false
TimeChangeRule dstRule = {"CEST", Last, Sun, Mar, 1, (DEFAULTTIMEZONE - 1) * 60};
TimeChangeRule stdRule = {"CET",  Last, Sun, Oct, 1,  DEFAULTTIMEZONE * 60};

#define MEASUREMENT_INTERVAL    3600UL              //(s) index reading interval
#define AWAKEDELAY              MINIMUM_DELAY       //can be set in (s) : Delay before deep sleep after each measuremen...
                                                    //Allow WEB access when sleep option on,
                                                    // each http request renews the time (nota: the WEB interface refreshes every WEB_REFRESH_PERIOD s).
#define COUNTERPIN              D5
#define DEBOUNCE_DELAY          50UL                //(ms)
#define PULSE_VALUE             10UL                //Deciliter per pulse...
#define UNIT_DISPLAY            1L                  // 1L ->unit=m3, 1000L->unit=l (API REST only)
#define KEEP_HISTORY            true                // keep measurements on SD
#define DELETEDATAFILE_DELAY    600000UL            //(ms)

#define EXCLUDED_IPV4_FROM_TUNE 192,168,0,253       //Update requests from this (IP & MASK) (HAProxy server?) are prohibited...
#define EXCLUDED_MASK_FROM_TUNE 255,255,255,255     //Mask to exclude prohibited IPs (warning: ',' not '.')

//Default values (editable in the web interface):
//#define MAXCONSUMPTIONTIME_MEASURE
#define WATERLEAK_MESSAGE      "Warning: probable water leak!"
#define MAXLEAKDETECT           4                   //Warning level...

#define DEFAULT_MQTT_SERVER    "mosquitto.home.lan"
#define DEFAULT_MQTT_PORT       1883
#define DEFAULT_MQTT_IDENT     ""
#define DEFAULT_MQTT_USER      ""
#define DEFAULT_MQTT_PWD       ""
#define DEFAULT_MQTT_QUEUE     "domoticz/in"

#define BACKGROUND_IMAGE       "https://static.mycity.travel/manage/uploads/7/36/12705/989bd67a1aad43055bd0322e9694f8dd8fab2b43_1080.jpg"
