char* WIFI_SSID = "xxx";                            // << kann bis zu 32 Zeichen haben
char* WIFI_PASSWORD = "xxx"; // << mindestens 8 Zeichen jedoch nicht länger als 64 Zeichen


//#define WIFI_SSID "xxx"                          // << kann bis zu 32 Zeichen haben
//#define WIFI_PASSWORD "xxx"                      // << mindestens 8 Zeichen jedoch nicht länger als 64 Zeichen

#define WIFIOFF_COUNT_VALUE 60                                //Counter wie lang WiFi ausgeschaltet bleibt bis ein neuer Verbindungsversuch gestartet wird
#define WIFICONNECTING_COUNT_VALUE 20                         //Counter wie lange WiFi versucht werden soll zu erreichen, bis erneut deaktiviert wird
#define HOSTNAME "ESP8266-Sromzaehler"

#define INFLUXDB_URL "http://192.168.0.xx:8086/write?db=DB&u=xxx&p=Password"
#define PAYLOAD_CACHE_SIZE 850
#define MEASUREMENT_COUNT_VALUE 60       // multiplikator von Heartbeat
#define HEARTBEATINTERVAL_VALUE 1000     //MilliSekunden
#define MAX_QUEUE_SIZE 1190
#define DB_NOT_REACHABLE_COUNT_MAX 3
#define HUMOFFSET -5
#define TEMPOFFSET -0.1
