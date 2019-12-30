#include "vars.h"
#include "network_config.h"
#include <ArduinoJson.h>
#include <EEPROM.h>
#ifdef SENSORTYPE_DS18B20
  #include <Adafruit_Sensor.h>
  #include <OneWire.h> 
  #include <DallasTemperature.h>
#endif
#ifdef SENSORTYPE_DHT22
  #include <DHT.h>
  #include <DHT_U.h>
#endif
#ifdef NETWORK_ETHERNET
  #include <SPI.h>
  #include <Ethernet.h>
#endif
#ifdef NETWORK_WIFI
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
  #include <ESP8266WebServer.h>
#endif

#define VERSION_NUMBER "20191230T1230"
#define VERSION_LASTCHANGE "Better web ui for config"

//#define PIN_WATCHDOG 13                 // pin where we connect to a 555 timer watch dog circuit
//#define PIN_PRINT_LED 14
//#define PIN_HTTP_LED 16
#define DELAY_CONNECT_ATTEMPT 10000L    // delay between attempting wifi reconnect or if no ethernet link, in milliseconds
#define DELAY_TURNOFF_AP 300000L        // delay after restart before turning off access point, in milliseconds
#define DELAY_BLINK 200L                // how long a led blinks, in milliseconds
#define DELAY_PAT_WATCHDOG 200L         // how long a watchdog pat lasts, in milliseconds
#define MAX_DS18B20_SENSORS 10          // maximum of DS18B20 sensors we can connect
#define TEMP_DECIMALS 4                 // 4 decimals of output
#define HUM_DECIMALS 4                  // 4 decimals of output

// define struct to hold general config
struct {
  char endpoint[64] = "boiling-dusk-12267.herokuapp.com";
  unsigned long delayPrint = 10000L;
  unsigned long delayPoll = 10000L;
  unsigned long delayPost = 120000L;
} configuration;

// **** WiFi *****
#ifdef NETWORK_WIFI
  ESP8266WebServer server(80);

  // define struct to hold wifi configuration
  struct { 
    char ssid[20] = "";
    char password[20] = "";
  } wifi_data;
#endif
#ifdef NETWORK_ETHERNET
  EthernetClient client;
  bool didEthernetBegin = false;
#endif

unsigned long lastConnectAttempt = millis();
unsigned long lastPostData = millis();
unsigned long lastPrint = millis();
unsigned long lastRead = millis();
boolean startedRead = false;
boolean startedPrint = false;
boolean startedPostData = false;
boolean justReset = true;
uint8_t reconnect;
uint8_t sensorCount = 0;

// ds18b20
#ifdef SENSORTYPE_DS18B20
  uint8_t sensorCounts[sizeof(DS18B20_PINS)/sizeof(uint8_t)];
  DeviceAddress addresses[MAX_DS18B20_SENSORS];
  float temperatures[MAX_DS18B20_SENSORS];
#endif

// ldr
#ifdef SENSORTYPE_LDR
  unsigned long ldr = 0;
#endif

// dht22
#ifdef SENSORTYPE_DHT22
  DHT_Unified dht(DHT_PIN, DHT_TYPE);
  
  float dht22_temp;
  float dht22_hum;
#endif

/**
 * Get Mac address to use.
 */
void getMacAddress(byte *mac) {
  #ifdef NETWORK_WIFI
    WiFi.macAddress(mac);
  #endif
  #ifdef NETWORK_ETHERNET
    mac = ethernetMac;
  #endif
}

/**
 * Convert mac address to a char buffer.
 */
void getMacAddressString(char *buffer) {
  byte mac[6];
  getMacAddress(mac);
  sprintf(buffer, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * Convert mac address to a char buffer.
 */
void getMacAddressStringNoColon(char *buffer) {
  byte mac[6];
  getMacAddress(mac);
  sprintf(buffer, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/**
 * Print MAC address to serial console
 */
void printMacAddress() {
  // print MAC address
  char buf[20];
  getMacAddressString(buf);
  Serial.print("MAC address: ");
  Serial.println(buf);
}

// *** WEB SERVER
void initWebserver() {
  server.on("/", HTTP_GET, webHandle_GetRoot);
  server.on("/data.html", HTTP_GET, webHandle_GetData);
  server.on("/sensorconfig.html", HTTP_GET, webHandle_GetSensorConfig);
  server.on("/wificonfig.html", HTTP_GET, webHandle_GetWifiConfig);
  server.on("/wifi", HTTP_POST, webHandle_PostWifiForm);
  server.on("/styles.css", HTTP_GET, webHandle_GetStyles);
  server.onNotFound(webHandle_NotFound);  
  
}
void webHandle_GetRoot() {
  char response[600];
  strcpy(response, "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"initial-scale=1.0\"><title>SensorCentral</title><link rel=\"stylesheet\" href=\"./styles.css\"></head><body><div class=\"position title\">Menu</div><div class=\"position menuitem\"><a href=\"./data.html\">Data</a></div><div class=\"position menuitem\"><a href=\"./sensorconfig.html\">Sensor Config.</a></div><div class=\"position menuitem\"><a href=\"./wificonfig.html\">Wi-Fi Config.</a></div>");
  strcat(response, "<div class=\"position footer right\">");
  strcat(response, VERSION_NUMBER);
  strcat(response, "<br/>");
  strcat(response, VERSION_LASTCHANGE);
  strcat(response, "</div>");
  strcat(response, "</body></html>");
  server.send(200, "text/html", response);
}

void webHandle_GetData() {
  char str_dht_temp[8];
  char str_dht_hum[8];
  dtostrf(dht22_temp, 6, TEMP_DECIMALS, str_dht_temp);
  dtostrf(dht22_hum, 6, HUM_DECIMALS, str_dht_hum);
  
  char response[400];
  strcpy(response, "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"initial-scale=1.0\"><title>SensorCentral</title><link rel=\"stylesheet\" href=\"./styles.css\"></head><body><div class=\"position\"><a href=\"./\">Back</a></div><div class=\"position title\">Data</div><div class=\"position menuitem\">");
#ifdef SENSORTYPE_DS18B20
  sensorCount = getDS18B20SensorCount();
  if (sensorCount > 0) {
    for (uint8_t i=0; i<sensorCount; i++) {
      strcat(response, ds18b20AddressToString(addresses[i]));
      strcat(response, ": ");
      strcat(response, temperatures[i]);
      strcat(response, "<br/>");
    }
  } else {
    strcat(response, "No DS18B20 sensors found on bus");
  }
#endif
#ifdef SENSORTYPE_DHT22
  strcat(response, "Temperature: ");
  strcat(response, str_dht_temp);
  strcat(response, "&deg;C<br/>Humidity: ");
  strcat(response, str_dht_hum);
#endif
  strcat(response, "%</div></body></html>");
  server.send(200, "text/html", response);
}

void webHandle_GetSensorConfig() {
  
}

void webHandle_GetWifiConfig() {
  char response[1024];
  strcpy(response, "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"initial-scale=1.0\"><title>SensorCentral</title><link rel=\"stylesheet\" href=\"./styles.css\"></head><body><div class=\"position\"><a href=\"./\">Back</a></div><div class=\"position title\">Wi-Fi Config.</div><div class=\"position menuitem\">");
  strcat(response, "<p>");
  strcat(response, "Current SSID: "); strcat(response, wifi_data.ssid); strcat(response, "<br/>");
  strcat(response, "Current Password: "); strncat(response, wifi_data.password, 4); strcat(response, "****<br/>");
  strcat(response, "Status: "); strcat(response, WiFi.status() == WL_CONNECTED ? "Connected" : "NOT connected");
  strcat(response, "</p>");
  strcat(response, "<form method=\"post\" action=\"/wifi\">");
  strcat(response, "<table border=\"0\">");
  strcat(response, "<tr><td align=\"left\">SSID</td><td><input type=\"text\" name=\"ssid\" autocomplete=\"off\"></input></td></tr>");
  strcat(response, "<tr><td>Password</td><td><input type=\"text\" name=\"password\" autocomplete=\"off\"></input></td></tr>");
  strcat(response, "<tr><td colspan=\"2\" align=\"right\"><input type=\"submit\"></input></td></tr>");
  strcat(response, "</table>");
  strcat(response, "</div></body></html>");
  server.send(200, "text/html", response);
}

void webHandle_GetStyles() {
  char response[400];
  strcpy(response, "* {font-size: 14pt;}");
  strcat(response, "a {font-weight: bold;}");
  strcat(response, "table {margin-left:auto;margin-right:auto;}");
  strcat(response, ".position {width: 60%; margin-bottom: 10px; position: relative; margin-left: auto; margin-right: auto;}");
  strcat(response, ".title {text-align: center; font-weight: bold; font-size: 20pt;}");
  strcat(response, ".right {text-align: right;}");
  strcat(response, ".footer {font-size: 10pt; font-style: italic;}");
  strcat(response, ".menuitem {text-align: center; background-color: #efefef; cursor: pointer; border: 1px solid black;}");
  server.send(200, "text/css", response);
}

void webHandle_PostWifiForm() {
  if (!server.hasArg("ssid") || !server.hasArg("password") || server.arg("ssid") == NULL || server.arg("password") == NULL) {
    server.send(417, "text/plain", "417: Invalid Request");
    return;
  }

  // save to eeprom
  server.arg("ssid").toCharArray(wifi_data.ssid, 20);
  server.arg("password").toCharArray(wifi_data.password, 20);
  EEPROM.put(0, wifi_data);
  EEPROM.commit();

  // send response
  server.send(200, "text/html", "<html><body><h1>Wi-Fi</h1><div>SSID: " + server.arg("ssid") + "</div><div>Password: " + server.arg("password") + "</div></body></html>");

  // restart esp
  ESP.restart();
}

void webHandle_NotFound(){
  server.send(404, "text/plain", "404: Not found");
}

void initNetworking() {
#ifdef NETWORK_WIFI
  // read config from eeprom
  EEPROM.begin(512);
  EEPROM.get(0, wifi_data);
  Serial.println("Read data from EEPROM - ssid: " + String(wifi_data.ssid) + ", password: ****");
  
  // start AP
  char ssid[32];
  char mac_addr[18];
  getMacAddressStringNoColon(mac_addr);
  strcpy(ssid, "SensorCentral-");
  strcat(ssid, mac_addr);
  WiFi.softAP(ssid, "");
  Serial.print("Started AP on IP: ");
  Serial.println(WiFi.softAPIP());

  // start web server
  initWebserver();
  server.begin();
  Serial.println("Started web server on port 80");

  // attempt to start wifi if we have config
  WiFi.begin(wifi_data.ssid, wifi_data.password);
  Serial.print("Establishing WiFi connection");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    server.handleClient();
    Serial.print(".");
  }
  Serial.print("\n");
  Serial.print("WiFi connection established - IP address: ");
  Serial.println(WiFi.localIP());

#endif
#ifdef NETWORK_ETHERNET
  // ensure ethernet library is initialized
  if (!didEthernetBegin) {
    Serial.println("Initializing ethernet library");
    if (Ethernet.begin(ethernetMac) == 0) {
      Serial.println("Failed to configure Ethernet using DHCP...");
      if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
        while (true) {
          delay(1); // do nothing, no point running without Ethernet hardware
        }
      }
      if (Ethernet.linkStatus() == LinkOFF) {
        Serial.println("Ethernet cable is not connected...");
        delay(DELAY_CONNECT_ATTEMPT);
        return false;
      }
    }
    // give the Ethernet shield a second to initialize:
    delay(1000);
    
    // toggle flag
    didEthernetBegin = true;
  }
  
  // ensure continued DHCP lease
  if (Ethernet.maintain() != 0) {
    Serial.print("Received new DHCP address: ");
    Serial.println(Ethernet.localIP());
  }
#endif
}

void sendData(char* data) {
  // prepare headers
  uint16_t contentLength = strlen(data) + 4;
  char str_contentLength[4];
  sprintf (str_contentLength, "%03i", contentLength);
  
#ifdef NETWORK_WIFI
  // send
  HTTPClient http;
  char server[50];
  strcpy(server, "http://");
  strcat(server, configuration.endpoint);
  http.begin(server);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Content-Length", str_contentLength);
  http.addHeader("X-SensorCentral-Version", VERSION_NUMBER);
  http.addHeader("X-SensorCentral-LastChange", VERSION_LASTCHANGE);
  int httpCode = http.POST(data);
  String payload = http.getString();                  //Get the response payload
  
  http.end();
#endif

#ifdef NETWORK_ETHERNET
  const char *server = isProd ? serverProd : serverTest;
  if (client.connect(server, 80)) {
    // post data
    client.println("POST / HTTP/1.0");
    client.print  ("Host: "); client.println(server);
    client.println("Content-Type: application/json");
    client.print  ("Content-Length: "); client.println(str_contentLength);
    client.print  ("X-SensorCentral-Version: "); client.println(VERSION_NUMBER);
    client.print  ("X-SensorCentral-LastChange: "); client.println(VERSION_LASTCHANGE);
    client.println("Connection: close");
    client.println();
    client.println(data);
    client.println();
    client.flush();

    int len = client.available();
    if (len > 0) {
      byte buffer[80];
      if (len > 80) len = 80;
      client.read(buffer, len);
      Serial.write(buffer, len); // show in the serial monitor (slows some boards)
      
    }
  } else {
    // if you didn't get a connection to the server:
    Serial.println("connection failed");
  }
  client.stop();
#endif

  // done
  Serial.println("Sent to server...");
}

void setup() {
  Serial.begin(115200);
  Serial.print("Version: ");
  Serial.println(VERSION_NUMBER);
  Serial.print("Last change: ");
  Serial.println(VERSION_LASTCHANGE);
  Serial.print("Server is: ");
  Serial.println(configuration.endpoint);
  printMacAddress();

  // init networking
  initNetworking();

  // init pins
#ifdef PIN_WATCHDOG
  pinMode(PIN_WATCHDOG, INPUT); // set to high impedance
  digitalWrite(PIN_WATCHDOG, HIGH);
#endif
#ifdef PIN_PRINT_LED
  pinMode(PIN_PRINT_LED, OUTPUT);
  digitalWrite(PIN_PRINT_LED, LOW);
#endif
#ifdef PIN_HTTP_LED
  pinMode(PIN_HTTP_LED, OUTPUT);
  digitalWrite(PIN_HTTP_LED, LOW);
#endif

  yield();
#ifdef SENSORTYPE_DS18B20
  // initialize DS18B20 temp sensors
  initSensor_DS18B20();
  yield();
#endif

#ifdef SENSORTYPE_DHT22
  // initialize DHT22 temp sensor
  initSensor_DHT22();
  yield();
#endif

#ifdef SENSORTYPE_LDR
  initSensor_LDR();
  yield();
#endif
}

bool isConnectedToNetwork() {
#ifdef NETWORK_WIFI
  // wifi
  if (WiFi.status() != WL_CONNECTED) {
    initNetworking();
  }
  return true;
#endif

#ifdef NETWORK_ETHERNET
  // ensure continued DHCP lease
  if (Ethernet.maintain() != 0) {
    Serial.print("Received new DHCP address: ");
    Serial.println(Ethernet.localIP());
  }
  // return
  return true;
#endif
}

void debugWebServer(char* buffer) {
#ifdef WEBSERVER_DEBUG
  Serial.print("WEBSERVER_DEBUG: ");
  Serial.println(buffer);
#endif
}

void loop() {
#ifdef NETWORK_WIFI
  // disable AP 
  if (WiFi.softAPIP() && millis() > DELAY_TURNOFF_AP) {
    // diable AP
    Serial.println("Disabling AP...");
    WiFi.softAPdisconnect(false);
    WiFi.enableAP(false);
  }
#endif

  if (!isConnectedToNetwork()) {
    Serial.println("No network connected...");
    return;
  }
  
  if (justReset) {
    // this is the first run - tell web server we restarted
    yield();
    justReset = false;
    
    // get your MAC address
    char mac_addr[20];
    getMacAddressString(mac_addr);

    // build payload
    StaticJsonDocument<256> doc;
    doc["msgtype"] = "control";
    JsonObject jsonData = doc.createNestedObject("data"); 
    jsonData["restart"] = true;
    jsonData["deviceId"].set(mac_addr);

    // serialize
    uint8_t jsonLength = measureJson(doc) + 1; 
    char jsonBuffer[jsonLength];
    serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));
    
    // send payload
    sendData(jsonBuffer);
    yield();
  }
  

  // reset reconnect count
  reconnect = 0;

#ifdef NETWORK_WIFI
  // handle incoming request to web server
  server.handleClient();

#endif


  // read from sensor(s)
  if (!startedRead && (millis() - lastRead) > configuration.delayPoll) {
    lastRead = millis();
    startedRead = true;

    // read ds18b20 data
    #ifdef SENSORTYPE_DS18B20
      readData_DS18B20();
    #endif

    #ifdef SENSORTYPE_DHT22
      readData_DHT22();
    #endif

    // read ldr
    #ifdef SENSORTYPE_LDR
      readData_LDR();
    #endif

    #ifdef PIN_WATCHDOG
    // pat the watchdog
    pinMode(PIN_WATCHDOG, OUTPUT);
    digitalWrite(PIN_WATCHDOG, LOW);
    #endif
  }
  yield();
  
  if (startedRead && (millis() - lastRead) > DELAY_PAT_WATCHDOG) {
    lastRead = millis();
    startedRead = false;

    #ifdef PIN_WATCHDOG
    // finish write and return to high impedance
    Serial.println("Patted watch dog...");
    digitalWrite(PIN_WATCHDOG, HIGH);
    pinMode(PIN_WATCHDOG, INPUT);
    #endif
  }
  yield();
  
  if (!startedPrint && (millis() - lastPrint) > configuration.delayPrint) {
    lastPrint = millis();
    startedPrint = true;

    #ifdef PIN_PRINT_LED
    digitalWrite(PIN_PRINT_LED, HIGH);
    #endif

    // show data on console
    printData();
  }
  yield();
  
  if (startedPrint && (millis() - lastPrint) > DELAY_BLINK) {
    startedPrint = false;
    lastPrint = millis();

    #ifdef PIN_PRINT_LED
    digitalWrite(PIN_PRINT_LED, LOW);
    #endif
  }
  yield();

  // post
  if (!startedPostData && (millis() - lastPostData) > configuration.delayPost) {
    lastPostData = millis();
    startedPostData = true;
    
    #ifdef PIN_HTTP_LED
    digitalWrite(PIN_HTTP_LED, HIGH);
    #endif
    
    // prepare post data
    char* jsonData = preparePayload();
    
    // send payload
    sendData(jsonData);
  }
  yield();

  if (startedPostData && (millis() - lastPostData) > DELAY_BLINK) {
    startedPostData = false;
    lastPostData = millis();
    #ifdef PIN_HTTP_LED
    digitalWrite(PIN_HTTP_LED, LOW);
    #endif
  }
  yield();
}

/**
 * Prints the data to the serial console.
 */
void printData() {
  Serial.println("------------------------");
  
  #ifdef SENSORTYPE_DS18B20
    printData_DS18B20();
  #endif

  #ifdef SENSORTYPE_DHT22
      printData_DHT22();
  #endif

  #ifdef SENSORTYPE_LDR
     printData_LDR();
  #endif
    
  Serial.println("Printed available data");
}

char* preparePayload() {
  // create document
  StaticJsonDocument<1024> doc;
  
  // get your MAC address
  char mac_addr[20];
  getMacAddressString(mac_addr);

  // build payload
  doc["msgtype"] = "data";
  doc["deviceId"].set(mac_addr);
  JsonArray jsonData = doc.createNestedArray("data");
  
  // loop DS18B20 sensors
  #ifdef SENSORTYPE_DS18B20
  char str_temp[8];
  for (uint8_t i=0, k=getDS18B20SensorCount(); i<k; i++) {
    dtostrf(temperatures[i], 6, TEMP_DECIMALS, str_temp);
    JsonObject jsonSensorData = jsonData.createNestedObject();
    jsonSensorData["sensorId"] = ds18b20AddressToString(addresses[i]);
    jsonSensorData["sensorValue"].set(str_temp);
  }
  #endif

  #ifdef SENSORTYPE_DHT22
    // add dht22
    char str_dht_temp[8];
    char str_dht_hum[8];
    dtostrf(dht22_temp, 6, TEMP_DECIMALS, str_dht_temp);
    dtostrf(dht22_hum, 6, HUM_DECIMALS, str_dht_hum);

    JsonObject jsonSensorData = jsonData.createNestedObject();
    jsonSensorData["sensorId"] = SENSORID_DHT22_TEMP;
    jsonSensorData["sensorValue"].set(str_dht_temp);
    jsonSensorData = jsonData.createNestedObject();
    jsonSensorData["sensorId"] = SENSORID_DHT22_HUM;
    jsonSensorData["sensorValue"].set(str_dht_hum);
  #endif

  #ifdef SENSORTYPE_LDR
    // add ldr
    char str_ldr[8];
    ltoa(ldr, str_ldr, 10);
    JsonObject jsonSensorData = jsonData.createNestedObject();
    jsonSensorData["sensorId"] = SENSORID_LDR
    jsonSensorData["sensorValue"].set(str_ldr);
  }
  #endif

  // serialize
  uint16_t jsonLength = measureJson(doc) + 1; // add space for 0-termination
  char jsonBuffer[jsonLength];
  serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));
  return jsonBuffer;
}

// ******************** DS18B20
#ifdef SENSORTYPE_DS18B20
void initSensor_DS18B20() {
  // Start up the sensors
  Serial.println("Initializing DS18B20 sensors");
  uint8_t sensorCountTotal = 0;
  
  for (uint8_t i=0; i<sizeof(DS18B20_PINS)/sizeof(uint8_t); i++) {
    OneWire oneWire(DS18B20_PINS[i]);
    DallasTemperature sensors(&oneWire);
    sensors.begin();
    Serial.print("Locating DS18B20 sensors on pin <");
    Serial.print(DS18B20_PINS[i]);
    Serial.print(">. Found ");
    Serial.print(sensors.getDeviceCount(), DEC);
    Serial.println(" DS18B20 sensors.");
    sensorCountTotal += sensors.getDeviceCount();
  }
  Serial.print("Found <");
  Serial.print(sensorCountTotal);
  Serial.println("> DS18B20 sensors");
}

/**
 * Get total DS18B20 sensor count across pins.
 */
uint8_t getDS18B20SensorCount() {
  uint8_t result = 0;
  for (uint8_t i=0; i<sizeof(DS18B20_PINS)/sizeof(uint8_t); i++) {
    result += sensorCounts[i];
  }
  return result;
}

char* ds18b20AddressToString(DeviceAddress deviceAddress) {
    static char return_me[18];
    static char *hex = "0123456789ABCDEF";
    uint8_t i, j;

    for (i=0, j=0; i<8; i++) {
         return_me[j++] = hex[deviceAddress[i] / 16];
         return_me[j++] = hex[deviceAddress[i] & 15];
    }
    return_me[j] = '\0';
    
    return return_me;
}

void readData_DS18B20() {
  uint8_t indexTempAddress = 0;
  for (uint8_t i=0; i<sizeof(DS18B20_PINS)/sizeof(uint8_t); i++) {
    OneWire oneWire(DS18B20_PINS[i]);
    DallasTemperature sensors(&oneWire);
    
    // begin to scan for change in sensors
    sensors.begin();
  
    // get count
    uint8_t sensorCount = sensorCounts[i];
    uint8_t sensorCountNew = sensors.getDeviceCount();
    if (sensorCount != sensorCountNew) {
      // sensorCount changed
      Serial.print("Detected DS18B20 sensor count change on pin <");
      Serial.print(DS18B20_PINS[i]);
      Serial.print("> - was ");
      Serial.print(sensorCount);
      Serial.print(" now ");
      Serial.println(sensorCountNew);
      sensorCount = sensorCountNew;
      sensorCounts[i] = sensorCountNew;
    }

    // read temperaturs
    if (sensorCount > 0) {
      // set resolution
      sensors.setResolution(DS18B20_TEMP_PRECISION);
      
      // request temperatures and store
      sensors.requestTemperatures();
    
      // get addresses
      for (uint8_t j=0; j<sensorCount; j++) {
        sensors.getAddress(addresses[indexTempAddress], j);
        temperatures[indexTempAddress] = sensors.getTempCByIndex(j);
        indexTempAddress++;
      }
    }
  }
}

void printData_DS18B20() {
  sensorCount = getDS18B20SensorCount();
  if (sensorCount > 0) {
    for (uint8_t i=0; i<sensorCount; i++) {
      Serial.print(ds18b20AddressToString(addresses[i]));
      Serial.print(": ");
      Serial.println(temperatures[i], TEMP_DECIMALS);
    }
  } else {
    Serial.println("No DS18B20 sensors found on bus");
  }
#endif

// ******************** DHT22
#ifdef SENSORTYPE_DHT22
void initSensor_DHT22() {
  // Start up the sensors
  Serial.println("Initializing DHT22 sensor");
  dht.begin();
}

void readData_DHT22() {
  sensorCount = 1;
  sensors_event_t event_temp;
  dht.temperature().getEvent(&event_temp);
  dht22_temp = event_temp.temperature;

  sensors_event_t event_hum;
  dht.humidity().getEvent(&event_hum);
  dht22_hum = event_hum.relative_humidity;
}

void printData_DHT22() {
  Serial.print(SENSORID_DHT22_TEMP);
  Serial.print(": ");
  Serial.println(dht22_temp);
  Serial.print(SENSORID_DHT22_HUM);
  Serial.print(": ");
  Serial.println(dht22_hum);
}
#endif

// ******************** LDR
#ifdef SENSORTYPE_LDR
void initSensor_LDR() {
  // Start up the sensors
  Serial.println("Setting pinMode to INPUT for LDR sensors");
  pinMode(LDR_PIN, INPUT);
}

void readData_LDR() {
  sensorCount = 1;
  ldr = analogRead(LDR_PIN);
}

void printData_LDR() {
  Serial.print("LDR Reading: "); 
  Serial.println(ldr);
}
#endif
