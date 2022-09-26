
/*
   (c) Stefan Meier, 2022-09-26
   Code for ESP8266 ESP01 to read serial SmartMeterLanguage from MT691 over optical interface
   Based on examples taken from various ESP8266 tutorials.
  

*/

//V2 CRC implementiert
//V3 Anpassungen StringNamen etc.
//V4 Scaler für Watt einbauen
//V5 an mich anpassen
//V6 Website auf LittleFS
//V7 Admin Infos einbauen
//V8 Serial Read umgebaut
//V9 Linked List / InfluxDB
//V10 Durchschnittswert Watt
//V11 CRC Fehlerquote
//V12 Einspeisung erfassen

#include "customs.h"

#include <LittleFS.h>
#include <uptime_formatter.h>
#include <ESP8266WebServer.h>
#include <asyncHTTPrequest.h>
#include <ArduinoOTA.h>
#include <QList.h>

#include <SoftwareSerial.h>

#define MYPORT_TX 5  // D1
#define MYPORT_RX 4  // D2

SoftwareSerial myPort;





const String DEVICENAME       = "ESP8266_Stromzaehler";

const String smlBegin         = "1b1b1b1b01010101";
const String smlEnd           = "1b1b1b1b1a";
const String searchStr_kWh    = "77070100010800ff";
const String searchStr_Watt   = "77070100100700ff";
const String searchStr_TL     = "77070100100700ff0101621b52005"; // Type-Length-Value
const String searchStr_na     = "77070100010800ff6500"; // Interessante Daten 
String smlTemp = "ff";
String smlMsg  = "";

float energy_counter            = 0.0;
int   energy_consumption        = 0;
int   type_length_value         = 0;
int   crc_bytes                 = 0;

bool end_SML = false;
bool end_CRC = false;
bool CRC_OK  = false;

unsigned long HeartbeatMillis = 0;                               // will store last time Heartbeat was running
const long Heartbeatinterval = HEARTBEATINTERVAL_VALUE;          // interval at which heart beats (milliseconds)

uint16_t MinFreeHeap     = 50000;
int MaxHeapFragmentation = 0;

int Measurement_quantity = 1;
int Measurement_ok = 0;
float Measurement_prozent = 0;

// Define data record
struct Measurement {
  float energy_counter;
  int energy_consumption;
  unsigned long measurementtime;
};

QList<Measurement> queue;                                  // Queue for caching Data
int httpResponseCode = 0;
int Measurement_count = MEASUREMENT_COUNT_VALUE;
int DB_not_reachable_count = 0;
char payload[PAYLOAD_CACHE_SIZE];                          //cache für HTTP zu InfluxDB
int average_consumption = 0;
int average_consumption_counter = 0;

WiFiEventHandler wifiConnectHandler;
WiFiEventHandler wifiDisconnectHandler;
bool WiFiconnecting = false;                               // Schalter für Wifi reconnect
int WiFiconnecting_count = WIFICONNECTING_COUNT_VALUE;     // Counter für WiFi connection
bool WiFiconnected = false;                                // Schalter für Wifi verbunden
bool WiFiOff = false;                                      // Schalter für Wifi OFF
int WiFiOff_count = WIFIOFF_COUNT_VALUE;                   // Counter wie lange WiFi Off bleiben soll



//------------------------------------------
//HTTP
ESP8266WebServer server(80);

// ****************************************************************

// lesbare Anzeige der Speichergrößen
const String formatBytes(size_t const& bytes) {
  return bytes < 1024 ? static_cast<String>(bytes) + " Byte" : bytes < 1048576 ? static_cast<String>(bytes / 1024.0) + " KB" : static_cast<String>(bytes / 1048576.0) + " MB";
};

// ****************************************************************

// generate sketchname for homepage
String sketchName() {
  char file[sizeof(__FILE__)] = __FILE__;
  char * pos = strrchr(file, '.'); *pos = '\0';
  return file;
};

// ****************************************************************

// Calc average value
int average_calc( int raw_value, int &buff, int factor)
{
  int av ;
  buff = buff - buff / factor;
  buff = buff + raw_value;
  av = buff / factor;

  return av;

}


// ****************************************************************

String bytetoHEX(byte onebyte) {
  String str = "";
  if (onebyte < 16) str += String(0, HEX);
  str += String(onebyte, HEX);
  return str;
}  // end bytetoHEX

// ****************************************************************

//https://stackoverflow.com/questions/58768018/what-is-the-difference-between-crc-16-ccitt-false-and-crc-16-x-25
uint16_t crc16x25(String hexStr) {
  uint16_t wCrc = 0xffff;
  char* pData;

  char byteArray[hexStr.length() / 2];

  uint16_t j = 0;
  for (uint16_t i = 0; i < hexStr.length(); i += 2) {
    String hexByte = String(hexStr[i]) + String(hexStr[i + 1]);
    //Serial.print(hexByte);
    uint8_t b = strtol(hexByte.c_str(), NULL, 16);
    byteArray[j] = (char)b;
    j++;
  }
  pData = byteArray;
  uint16_t i = sizeof(byteArray);
  while (i--) {
    wCrc ^= *(unsigned char *)pData++ << 0;
    for (j = 0; j < 8; j++)
      wCrc = wCrc & 0x0001 ? (wCrc >> 1) ^ 0x8408 : wCrc >> 1;
  }
  return wCrc ^ 0xffff;
}   // end crc16x25


// ****************************************************************

void handleSMLMsg() {

  String msg = "";

  msg += "<head>\n";
  msg += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
  msg += "<meta charset=\"UTF-8\">\n";
  msg += "<title>Stromzähler</title>\n";
  msg += "<style>\n";
  msg += "<data name=\"Energy_feed\" value=\"" + String(energy_consumption, 1) + "\" valueunit=\"W\"/>\n";
  msg += "<data name=\"smlMsg\" value=\"" + smlMsg + "\" valueunit=\"text\"/>\n";

  uint16_t i = smlMsg.length();
  smlMsg = smlMsg.substring(0, i - 4); // cut last 4 chars (2 last bytes for CRC15X25)
  i = crc16x25(smlMsg);
  msg += "<data name=\"CRC\" value=\"" + String(i, HEX) + "\" valueunit=\"text\"/>\n";

  msg += "</MyHome>";

  server.send(200, "text/plain", msg);       //Response to the HTTP request
}  // end handleSMLMsg()

// ****************************************************************

void handleNotFound() {
  String msg = "File Not Found\n\n";
  msg += "URI: ";
  msg += server.uri();
  msg += "\nMethod: ";
  msg += (server.method() == HTTP_GET) ? "GET" : "POST";
  msg += "\nArguments: ";
  msg += server.args();
  msg += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    msg += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/html", msg);
}  // end handleNotFound

// ****************************************************************

void onWifiConnect(const WiFiEventStationModeGotIP& event) {
  Serial.println(F("Connected to Wi-Fi."));
  WiFiconnected = true;
  WiFiconnecting_count = 0;
  WiFiOff_count = 0;
}

// ****************************************************************

void onWifiDisconnect(const WiFiEventStationModeDisconnected& event) {
  WiFiconnected = false;
  Serial.println(F("Disconnected from Wi-Fi."));
}

// ****************************************************************

void Connect() {
  //  WiFi.mode(WIFI_OFF);
  Serial.setDebugOutput(true);
  WiFi.persistent(false);  // https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/generic-class.html#mode
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(HOSTNAME);
  WiFi.begin("WIFI_SSID", "WIFI_PASSWORD");
  WiFiOff = false;
}  // end Connect

//***********************************************************************


void setupOTA() {
  ArduinoOTA.setHostname(DEVICENAME.c_str());

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
}

//***********************************************************************

void crcCheck_smlMsg () {
  String crcByte1 = "";
  String crcByte2 = "";
  String crcSum = "";

  uint16_t a = smlMsg.length();
  crcByte1 = smlMsg.substring(a - 4,  a - 2);
  //  Serial.println("CRCByte1 = " + crcByte1);
  uint16_t b = smlMsg.length();
  crcByte2 = smlMsg.substring(b - 2,  b - 0);
  //  Serial.println("CRCByte2 = " + crcByte2);
  crcByte2.concat(crcByte1);
  //  Serial.println("CRCBytes = " + crcByte2);

  uint16_t i = smlMsg.length();
  smlMsg = smlMsg.substring(0, i - 4); // cut last 4 chars (2 last bytes for CRC16X25)
  i = crc16x25(smlMsg);
  crcSum = String(i, HEX);
  //  Serial.println("CRC = " + crcSum);

  if (crcByte2 == crcSum) {
    CRC_OK = true;
    //  Serial.println("CRC Check OK");
  }

}   //  end crcCheck_smlMsg

//***********************************************************************

void parse_smlMsg() {
  int64_t value       = 0;
  int8_t  value_8Bit  = 0;
  int16_t value_16Bit = 0;
  String hexStr = "";

  // search counter value (kWh)
  String searchStr = searchStr_kWh;
  uint16_t pos = smlMsg.indexOf(searchStr);
  if (pos > 0) {
    pos = pos + searchStr.length() + 22;        // skip additional 10 Bytes = 20 Char!
    hexStr = smlMsg.substring(pos,  pos + 8);   // hexStr is 8 Byte = 16 Char
    value = strtoull(hexStr.c_str(), NULL, 16);
    energy_counter = (float)value / 10000;
  } else {
    energy_counter = 0.0;
  }

  //  search type_length_value for Watt.  https://www.msxfaq.de/sonst/bastelbude/smartmeter_d0_sml_protokoll.htm#tl_feld
  searchStr = searchStr_TL;
  pos = smlMsg.indexOf(searchStr);
  if (pos > 0) {
    pos = pos + searchStr.length();
    hexStr  = smlMsg.substring(pos,  pos + 1);
    type_length_value = hexStr.toInt();
    //  Serial.println("TL = " + String(type_length_value));
  } else {
    type_length_value = 3;
  }

  //  search for power feed or other informations.  http://www.schatenseite.de/2016/05/30/smart-message-language-stromzahler-auslesen/
  searchStr = searchStr_na;
  pos = smlMsg.indexOf(searchStr);
  if (pos > 0) {
    pos = pos + searchStr.length();
    hexStr  = smlMsg.substring(pos,  pos + 4);
    Serial.println("Interesting Bytes = " + String(hexStr));
  }


  // search energy consumption with Int16 value
  if (type_length_value == 3) {
    searchStr = searchStr_Watt;
    pos = smlMsg.indexOf(searchStr);
    if (pos > 0) {
      pos = pos + searchStr.length() + 14;
      hexStr = smlMsg.substring(pos,  pos + 4);
      value_16Bit = strtoull(hexStr.c_str(), NULL, 16);
      energy_consumption = value_16Bit;
     // energy_consumption = strtoull(hexStr.c_str(), NULL, 16);
     // energy_consumption = (float)value;
    } else {
      energy_consumption = 0;
    }
  }

  // search energy consumption with Int8 value
  if (type_length_value == 2) {
    searchStr = searchStr_Watt;
    pos = smlMsg.indexOf(searchStr);
    if (pos > 0) {
      pos = pos + searchStr.length() + 14;
      hexStr = smlMsg.substring(pos,  pos + 2);
      value_8Bit = strtoull(hexStr.c_str(), NULL, 16);
      energy_consumption = value_8Bit;
    //  energy_consumption = strtoull(hexStr.c_str(), NULL, 16);
    //  energy_consumption = (float)value;
    } else {
      energy_consumption = 0;
    }
  }

}   // end parse_smlMsg

// ****************************************************************

void send_measurements_to_website() {
  String uptime = uptime_formatter::getUptime();
  const String freeheap = formatBytes(ESP.getFreeHeap());
  const String lowestfreeheap = formatBytes(MinFreeHeap);
  int heapfragmentation = ESP.getHeapFragmentation();
  char JSONString[220];// sendbuffer dimensionieren
  // creat JSON String as Array formated like:["20.0","572.6","24.0"]
  sprintf(JSONString, "[\"%5.3f\",\"%5i\",\"%s\",\"%s\",\"%s\",\"%i\",\"%i\",\"%i\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"]", energy_counter, energy_consumption, uptime_formatter::getUptime().c_str(), freeheap.c_str(), lowestfreeheap.c_str(), heapfragmentation, MaxHeapFragmentation, WiFi.RSSI(), WiFi.BSSIDstr().c_str(), sketchName().c_str(), __DATE__, __TIME__, ESP.getResetReason().c_str());
  server.send(200, "application/json", JSONString);
  //  Serial.println(JSONString);
  //  Serial.println(strlen(JSONString));
}

// ****************************************************************

void writeSensorMeasure(struct Measurement *m) {
  long int t1 = millis();
  time_t T;
  time(&T);
  unsigned long seconds = (unsigned long) T;
  m->energy_counter = energy_counter;
  m->energy_consumption = energy_consumption;
  m->measurementtime = T;
  Serial.print(F("Recording Enery Counter: "));
  Serial.print(m->energy_counter);
  Serial.println();
  Serial.print(F("Recording Enery Consumption: "));
  Serial.print(m->energy_consumption);
  Serial.println();
  Serial.print(F("Records in cache: "));
  Serial.println(queue.size() + 1);
  long int t2 = millis();
  Serial.print(F("Time taken by writeSensorMeasure: ")); Serial.print(t2 - t1); Serial.println(F(" milliseconds"));
}

// ****************************************************************

void transferData() {
  long int t1 = millis();
  Serial.println("Transfering data to InfluxDB");
  int endIndex = queue.size() - 1;
  int startIndex = endIndex - 9; // Bei Änderungen Payload Cache anpassen
  if (startIndex < 0) {
    startIndex = 0;
  }
  WiFiClient client; //https://forum.arduino.cc/t/absturze-beim-senden-und-empfangen-von-http-requestes/859254/29
  asyncHTTPrequest  http;
  // http.setDebug(true);
  http.setTimeout(1);
  http.open("POST", INFLUXDB_URL);                                // Daten zur Influx DB
  http.setReqHeader("Content-type", "text/plain");
  char *pos = payload;
  for (int i = endIndex; i >= startIndex; i--) {
    struct Measurement m = queue[i];
    pos += sprintf(pos, "Strom Zaehler=%f,Verbrauch=%i %ld000000000\n", m.energy_counter, m.energy_consumption, m.measurementtime);
  }
  //  Serial.println(payload); //takes to much time!!! up to 80ms
  http.send((uint8_t *)payload, strlen(payload));
  int c = 0;
  while (http.readyState() != 4) {
    ++c;
    delay(1);
  }
  Serial.print(F("Time taken by waiting for readyState: ")); Serial.print(c); Serial.println(F(" milliseconds"));
  if (http.responseHTTPcode() == 204) {
    // clear the transferred items from the queue
    for (int i = endIndex; i >= startIndex; i--) {
      queue.clear(i);
    }
    DB_not_reachable_count = 0;
    Serial.println(F("Data successfully sent to DB"));
    Serial.print(F("Records in cache: ")); Serial.println(queue.size());
  }
  else {
    ++DB_not_reachable_count;
    if (DB_not_reachable_count == DB_NOT_REACHABLE_COUNT_MAX) {
      Connect();
      // WiFi.mode(WIFI_OFF);
      DB_not_reachable_count = 0;
    }
    Serial.println(F("DB not reachable. Retry...next Time."));
    Serial.print(F("Records in cache: "));
    Serial.println(queue.size());
  }
  long int t2 = millis();
  Serial.print(F("Time taken by transferData: ")); Serial.print(t2 - t1); Serial.println(F(" milliseconds"));
}



// ****************************************************************

void setup() {
  Serial.begin(115200);
  wifiConnectHandler = WiFi.onStationModeGotIP(onWifiConnect);
  wifiDisconnectHandler = WiFi.onStationModeDisconnected(onWifiDisconnect);
  Connect();

  myPort.begin(9600, SWSERIAL_8N1, MYPORT_RX, MYPORT_TX, false);
  if (myPort) { // If the object did not initialize, then its configuration is invalid
    Serial.println("SoftwareSerial port initialized.");
  } else {
    Serial.println("Invalid SoftwareSerial pin configuration, check config!");
  }
  Serial.println();

  if (!LittleFS.begin()) {
    Serial.println("An Error has occurred while mounting LittleFS");
  } else {
    Serial.println("LittleFS initialized.");
  }
  Serial.println();

  server.serveStatic("/", LittleFS, "/index.html");
  server.serveStatic("/style.css", LittleFS, "/style.css", "text/css");
  server.serveStatic("/stromzaehler.png", LittleFS, "/stromzaehler.png");
  server.on("/get_measurements", send_measurements_to_website);
  server.on("/admin", handleSMLMsg);
  server.onNotFound( handleNotFound );
  server.begin();                                       //Start the server
  Serial.println("Server is now listening ...");
  Serial.println();

  setupOTA();

}  // end setup

// ****************************************************************

void loop() {
  long int t1 = millis();
  uint8_t inByte;     // Chache for RX
  time_t now;
  time(&now);

if (myPort.available() == 0){
  //------------------------------------------
  //WebServer & OTA
  server.handleClient();    // Handling of incoming requests
  ArduinoOTA.handle();
}
  //------------------------------------------
  //RX-Data
  if (myPort.available() > 0 && end_SML == false) {
    int i = smlTemp.indexOf(smlBegin);
    if (i > 0) {
      // Serial.println();
      // Serial.println(smlBegin + " Begin gefunden!");
      smlTemp = smlTemp.substring(i, i + smlBegin.length());   // start to record a new temporary SML message, starting with smlBegin
    }
    inByte = myPort.read(); // read serial buffer into array
    smlTemp += bytetoHEX(inByte);
    // Serial.print(bytetoHEX(inByte) + " ");
    i = smlTemp.indexOf(smlEnd);
    if (i > 0) {                     // end of temporary SML message reached and complete now
      //    Serial.println();
      //    Serial.println(smlEnd + " End gefunden!");
      long int t2 = millis();
      //    Serial.println("RX - Loop = " + String(t2 - t1) + " ms");
      end_SML = true;
      crc_bytes = 3;
    }
  }
  if (myPort.available() > 0 && end_SML == true && crc_bytes >= 0) {
    inByte = myPort.read(); // read serial buffer into array
    smlTemp += bytetoHEX(inByte);
    //  Serial.println(bytetoHEX(inByte) + "CRC");
    crc_bytes--;
    if (crc_bytes == 0) {
      //  Serial.println();
      //  Serial.println("CRC Gelesen");
      end_SML = false;
      end_CRC = true;
      ++Measurement_quantity;
    }
  }

  //------------------------------------------
  //CRC-Check
  if (end_CRC == true) {
    end_CRC = false;
    smlMsg = smlTemp;
    // Serial.println();
    // Serial.println(smlMsg);
    // reset to 'ff' otherwise 'int i = smlTemp.indexOf(smlBegin)' doesn´t work
    crcCheck_smlMsg();
    if (CRC_OK == true) {
      parse_smlMsg();
      Serial.println("Zaehlerstand = " + String(energy_counter, 3) + " kWh");
      Serial.println("AktVerbrauch = " + String(energy_consumption) + " W");
      CRC_OK = false;
      ++Measurement_ok;
    }
    else {
      Serial.println("CRC Error in smlMsg");
      //Serial.println(smlTemp);
    }
    smlTemp = "ff";
  }






  //------------------------------------------
  //Heart Beat
  long int currentMillis  = millis();

  if (currentMillis - HeartbeatMillis >= Heartbeatinterval) {
    long int t1 = millis();
    HeartbeatMillis = currentMillis;
    --Measurement_count;

    if (myPort.available() == 0) {
      //      Serial.print(F("WiFiconnected: "));
      //      Serial.println(WiFiconnected);
      //      Serial.println(WiFi.localIP());
      //      Serial.print(F("WiFi-OFF: "));
      //      Serial.println(WiFiOff);
      //      Serial.print(F("WiFi-OFF_count: "));
      //      Serial.println(WiFiOff_count);
      //      Serial.print(F("WiFi-Connecting_count: "));
      //      Serial.println(WiFiconnecting_count);
      //      Serial.print(F("Measurement_count: "));
      //      Serial.println(Measurement_count);
      //      Serial.print(F("DB_not_reachable_count: "));
      //      Serial.println(DB_not_reachable_count);
      //      Serial.print(F("Unix-Time: "));
      //      Serial.println(now);
      //      Serial.print(F("Records in cache: "));
      //      Serial.println(queue.size());
      Measurement_prozent = Measurement_ok * 100 / Measurement_quantity;
      Serial.print(F("ohne CRC Fehler %: "));
      Serial.println(Measurement_prozent);
    }

    if (Measurement_count <= 0 && myPort.available() == 0 && energy_consumption <= 60000) {
      Measurement_count = MEASUREMENT_COUNT_VALUE;
      if (now >= 1000000000 && queue.size() < MAX_QUEUE_SIZE)  {          //Wenn UnixTime aktuell  &&  Queue kleiner als XXX && Humidity größer als 0 (Fehlmessung)
        struct Measurement m;
        writeSensorMeasure(&m);
        queue.push_back(m);
      }
    }

    if (WiFiconnected == false && WiFiOff == false && WiFiconnecting_count == 0) {
      Serial.println(F("Switching WiFi Off, no WiFi available"));
      // WiFi.mode(WIFI_OFF); // unklar was besser ist WiFi.mode(WIFI_OFF) oder WiFi.disconnect(true)
      WiFi.disconnect(true); //https://arduino-esp8266.readthedocs.io/en/latest/esp8266wifi/station-class.html?highlight=WiFi.disconnect()#disconnect
      WiFiOff = true;
      WiFiOff_count = WIFIOFF_COUNT_VALUE;
    }
    if (WiFiOff == true) {
      --WiFiOff_count;
    }
    if (WiFiOff == true && WiFiOff_count == 0) {
      WiFiconnecting = true;
    }
    if (WiFiOff == true && WiFiconnecting == true)  {
      Serial.println(F("Try to reconnect"));
      WiFiconnecting = false;
      Connect();
      WiFiconnecting_count = WIFICONNECTING_COUNT_VALUE;
    }
    if (WiFiconnected == false && WiFiconnecting_count > 0)  {
      --WiFiconnecting_count;
    }

    if (WiFiconnected == true && myPort.available() == 0) {
      if (queue.size() >= 1 && Measurement_count > 3 && Measurement_count != MEASUREMENT_COUNT_VALUE) { //Kein Transfer wenn in diesem Loop gemessen wird--> Loop unter 50ms halten.
        transferData ();
      }
    }
    long int t2 = millis();
    Serial.print(F("Time taken by Heartbeat: ")); Serial.print(t2 - t1); Serial.println(F(" milliseconds"));
    Serial.println(F("___________________________"));

    if (ESP.getFreeHeap() < MinFreeHeap) {
      MinFreeHeap = ESP.getFreeHeap();
    }
    if (ESP.getHeapFragmentation() > MaxHeapFragmentation) {
      MaxHeapFragmentation = ESP.getHeapFragmentation();
    }
    if (Measurement_quantity >= 86400) {
      Measurement_quantity = 1;
      Measurement_ok = 0;
      Measurement_prozent = 0;
    }
  }

}  // end loop
