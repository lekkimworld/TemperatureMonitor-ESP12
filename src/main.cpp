#include <Arduino.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include <Adafruit_Sensor.h>
#include <OneWire.h> 
#include <DallasTemperature.h>
#include <DHT.h>
#include <DHT_U.h>
#include "vars.h"
#ifdef NETWORK_ETHERNET
  #include <SPI.h>
  #include <Ethernet.h>
#endif
#ifdef NETWORK_WIFI
  #include <ESP8266WiFi.h>
  #include <ESP8266HTTPClient.h>
  #include <ESP8266WebServer.h>
#endif

#define VERSION_NUMBER "20221208T1130"
#define VERSION_LASTCHANGE "Extend ssid size"

//#define PIN_WATCHDOG 13                 // pin where we connect to a 555 timer watch dog circuit
//#define PIN_PRINT_LED 14
//#define PIN_HTTP_LED 16
#define DS18B20_TEMP_PRECISION 12        // 12 bits precision
#define DHT_TYPE DHT22
#define DELAY_CONNECT_ATTEMPT 10000L    // delay between attempting wifi reconnect or if no ethernet link, in milliseconds
#define DELAY_TURNOFF_AP 300000L        // delay after restart before turning off access point, in milliseconds
#define DELAY_BLINK 200L                // how long a led blinks, in milliseconds
#define DELAY_PAT_WATCHDOG 200L         // how long a watchdog pat lasts, in milliseconds
#define DEFAULT_DELAY_PRINT 10000L      // 
#define DEFAULT_DELAY_POLL 10000L       // 
#define DEFAULT_DELAY_POST 120000L      // 
#define MAX_SENSORS 10                  // maximum number of sensors we can connect
#define TEMP_DECIMALS 4                 // 4 decimals of output
#define HUM_DECIMALS 4                  // 4 decimals of output

// define struct to hold general config
#define CONFIGURATION_VERSION 4
struct {
  uint8_t version = CONFIGURATION_VERSION;
  char endpoint[64] = "";
  char jwt[650] = "";
  char sensorType[36] = "";
  unsigned long delayPrint = 0L;
  unsigned long delayPoll = 0L;
  unsigned long delayPost = 0L;
} configuration;

// **** network *****
#ifdef NETWORK_WIFI
  ESP8266WebServer server(80);

  // define struct to hold wifi configuration
  struct { 
    char ssid[32] = "";
    char password[20] = "";
    bool keep_ap_on = false;
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
int lastHttpResponseCode = 0;
char lastHttpResponse[2048] = ""; 

// sensor data
uint8_t sensorPins[] = {14};
uint8_t sensorsPerPin[sizeof(sensorPins)/sizeof(uint8_t)]; // array with number of sensors per pin
float sensorSamples[MAX_SENSORS]; // the samples coming of the actual sensors
DeviceAddress sensorAddresses[MAX_SENSORS]; // if using DS18B20 we need the address of each sensor
char sensorIds[MAX_SENSORS][36];

bool isSensorTypeDS18B20() {
  return strcmp(configuration.sensorType, "DS18B20") == 0;
}
bool isSensorTypeDHT22() {
  return strcmp(configuration.sensorType, "DHT22") == 0;
}
bool isSensorTypeBINARY() {
  return strcmp(configuration.sensorType, "BINARY") == 0;
}

uint8_t getSensorCount() {
  uint8_t result = 0;
  for (uint8_t i=0; i<sizeof(sensorsPerPin)/sizeof(uint8_t); i++) {
    result += sensorsPerPin[i];
  }
  return result;
}

bool hasWebEndpoint() {
  return strcmp(configuration.endpoint, "") != 0;
}

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

void buildNetworkName(char* buffer) {
  char mac_addr[18];
  getMacAddressStringNoColon(mac_addr);
  strcpy(buffer, "SensorCentral-");
  strcat(buffer, mac_addr);
}

/**
 * Convert IP address to a char buffer.
 */
IPAddress* getIpAddress() {
  #ifdef NETWORK_WIFI
    IPAddress ip = WiFi.localIP();
  #endif
  #ifdef NETWORK_ETHERNET
    IPAddress ip = Ethernet.localIP();
  #endif
  return &ip;
}

/**
 * Convert IP address to a char buffer.
 */
void getIpAddressString(char *buffer) {
  IPAddress ip;
  #ifdef NETWORK_WIFI
    ip = WiFi.localIP();
  #endif
  #ifdef NETWORK_ETHERNET
    ip = Ethernet.localIP();
  #endif
  strcpy(buffer, ip.toString().c_str());
}

/**
 * Print IP address to serial console
 */
void printIpAddress() {
  #ifdef NETWORK_WIFI
    Serial.println(WiFi.localIP());
  #endif
  #ifdef NETWORK_ETHERNET
    Serial.println(Ethernet.localIP());
  #endif
}


// *** WEB SERVER
void webHeader(char* buffer, bool back, const char* title) {
  strcpy(buffer, "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"initial-scale=1.0\"><title>SensorCentral</title><link rel=\"stylesheet\" href=\"./styles.css\"></head><body>");
  if (back) strcat(buffer, "<div class=\"position\"><a href=\"./\">Back</a></div>");
  strcat(buffer, "<div class=\"position title\">");
  strcat(buffer, title);
  strcat(buffer, "</div>");
}

void webRestarting(char* buffer) {
  webHeader(buffer, false, "Restarting");
  strcat(buffer, "</body></html>");
}

void webHandle_GetRoot() {
  char response[1024];
  webHeader(response, false, "Menu");
  strcat(response, "<div class=\"position menuitem height30\"><a href=\"./data.html\">Data</a></div><div class=\"position menuitem height30\"><a href=\"./sensorconfig.html\">Device/Sensor Config.</a></div><div class=\"position menuitem height30\"><a href=\"./wificonfig.html\">Wi-Fi Config.</a></div><div class=\"position menuitem height30\"><a href=\"./httpstatus.html\">HTTP status</a></div>");
  strcat(response, "<div class=\"position footer right\">");
  strcat(response, VERSION_NUMBER);
  strcat(response, "<br/>");
  strcat(response, VERSION_LASTCHANGE);
  strcat(response, "</div>");
  strcat(response, "</body></html>");
  server.send(200, "text/html", response);
}

void webHandle_GetHttpStatus() {
  char str_httpcode[8];
  sprintf(str_httpcode, "%d", lastHttpResponseCode);
  
  char response[600];
  webHeader(response, true, "HTTP Status");
  strcat(response, "<div class=\"position menuitem\">");
  strcat(response, "HTTP Code: "); strcat(response, str_httpcode); strcat(response, "<br/>");
  strcat(response, "HTTP Response: <br/>"); strcat(response, lastHttpResponse); strcat(response, "<br/>");
  strcat(response, "</div>");
  strcat(response, "</body></html>");
  server.send(200, "text/html", response);
}



void webHandle_GetData() {
  char str_temp[8];
  char str_hum[8];
  uint8_t sensorCount = getSensorCount();
  
  char response[400 + sensorCount * 100];
  webHeader(response, true, "Data");
  strcat(response, "<div class=\"position menuitem\">");

  if (isSensorTypeDS18B20()) {
    if (sensorCount > 0) {
      for (uint8_t i=0; i<sensorCount; i++) {
        dtostrf(sensorSamples[i], 6, TEMP_DECIMALS, str_temp);
        
        strcat(response, sensorIds[i]);
        strcat(response, ": ");
        strcat(response, str_temp);
        strcat(response, "<br/>");
      }
    } else {
      strcat(response, "No DS18B20 sensors found on bus");
    }
  } else if (isSensorTypeDHT22()) {
    dtostrf(sensorSamples[0], 6, TEMP_DECIMALS, str_temp);
    dtostrf(sensorSamples[1], 6, HUM_DECIMALS, str_hum);
    
    strcat(response, "Temperature: ");
    strcat(response, str_temp);
    strcat(response, "&deg;C<br/>Humidity: ");
    strcat(response, str_hum);
    strcat(response, "%");
  } else if (isSensorTypeBINARY()) {
    strcat(response, "Binary sensor: ON");
  }

  strcat(response, "</div></body></html>");
  server.send(200, "text/html", response);
}

void webHandle_GetSensorConfig() {
  char str_deviceid[36];
  getMacAddressString(str_deviceid);
  char str_delay_print[12];
  sprintf(str_delay_print, "%lu", configuration.delayPrint);
  char str_delay_poll[12];
  sprintf(str_delay_poll, "%lu", configuration.delayPoll);
  char str_delay_post[12];
  sprintf(str_delay_post, "%lu", configuration.delayPost);

  // create buffer
  char response[2048];

  // show current response
  webHeader(response, true, "Device/Sensor Config.");
  strcat(response, "<div class=\"position menuitem\">");
  strcat(response, "<p>");
  strcat(response, "Device ID: "); strcat(response, str_deviceid); strcat(response, "<br/>");
  strcat(response, "Current delay print: "); strcat(response, str_delay_print); strcat(response, "ms<br/>");
  strcat(response, "Current delay poll: "); strcat(response, str_delay_poll); strcat(response, "ms<br/>");
  strcat(response, "Current delay post: "); strcat(response, str_delay_post);  strcat(response, "ms<br/>");
  strcat(response, "Current endpoint: "); 
  if (strcmp(configuration.endpoint, "") == 0) {
    strcat(response, "&lt;none configured&gt;"); 
  } else {
    strcat(response, configuration.endpoint); 
  }
  strcat(response, "<br/>");
  strcat(response, "Current JWT: "); 
  if (strcmp(configuration.jwt, "") == 0) {
    strcat(response, "&lt;none configured&gt;"); 
  } else {
    strncat(response, configuration.jwt, 15); 
    strcat(response, "..."); 
  }
  strcat(response, "<br/>");
  strcat(response, "Current sensor type: "); strcat(response, configuration.sensorType);  strcat(response, "<br/>");
  strcat(response, "</p>");

  // add form
  strcat(response, "<form method=\"post\" action=\"/sensor\">");
  strcat(response, "<table border=\"0\">");
  strcat(response, "<tr><td align=\"left\">Delay, print</td><td><input type=\"text\" name=\"print\" autocomplete=\"off\"></input></td></tr>");
  strcat(response, "<tr><td align=\"left\">Delay, poll</td><td><input type=\"text\" name=\"poll\" autocomplete=\"off\"></input></td></tr>");
  strcat(response, "<tr><td align=\"left\">Delay, post</td><td><input type=\"text\" name=\"post\" autocomplete=\"off\"></input></td></tr>");
  strcat(response, "<tr><td align=\"left\">Endpoint</td><td><input type=\"text\" name=\"endpoint\" autocomplete=\"off\"></input></td></tr>");
  strcat(response, "<tr><td align=\"left\">JWT</td><td><input type=\"text\" name=\"jwt\" autocomplete=\"off\"></input></td></tr>");
  strcat(response, "<tr><td align=\"left\">Sensor type</td><td><select name=\"sensortype\"><option>DS18B20</option><option>DHT22</option><option>BINARY</option></select></td></tr>");
  strcat(response, "<tr><td colspan=\"2\" align=\"right\"><input type=\"submit\"></input></td></tr>");
  strcat(response, "</table>");

  // close page
  strcat(response, "</div></body></html>");
  server.send(200, "text/html", response);
}

void webHandle_PostSensorForm() {
  bool didUpdate = false;
  Serial.println("Received POST for sensor config");

  if (server.arg("print").length() > 0) {
    unsigned long larg = atol(server.arg("print").c_str());
    if (larg != 0) {
      configuration.delayPrint = larg;
      didUpdate = true;
      Serial.print("Delay print: ");
      Serial.println(larg);
    }
  }
  if (server.arg("poll").length() > 0) {
    unsigned long larg = atol(server.arg("poll").c_str());
    if (larg != 0) {
      configuration.delayPoll = larg;
      didUpdate = true;
      Serial.print("Delay poll: ");
      Serial.println(larg);
    }
  }
  if (server.arg("post").length() > 0) {
    unsigned long larg = atol(server.arg("post").c_str());
    if (larg != 0) {
      configuration.delayPost = larg;
      didUpdate = true;
      Serial.print("Delay post: ");
      Serial.println(larg);
    }
  }
  if (server.arg("endpoint").length() > 0) {
    strcpy(configuration.endpoint, server.arg("endpoint").c_str());
    didUpdate = true;
    Serial.print("Endpoint: ");
    Serial.println(configuration.endpoint);
  }
  if (server.arg("jwt").length() > 0) {
    strcpy(configuration.jwt, server.arg("jwt").c_str());
    didUpdate = true;
    Serial.print("JWT: ");
    Serial.println(configuration.jwt);
  }
  if (server.arg("sensortype").length() > 0) {
    strcpy(configuration.sensorType, server.arg("sensortype").c_str());
    didUpdate = true;
    Serial.print("Sensor type: ");
    Serial.println(configuration.sensorType);
  }

  if (didUpdate) {
    // save to eeprom
    EEPROM.put(0, configuration);
    EEPROM.commit();

    // send response
    char response[400];
    webRestarting(response);
    server.send(200, "text/html", response);
    yield();

    // restart esp
    ESP.restart();
  }
}

void webHandle_GetWifiConfig() {
  char response[1024];
  webHeader(response, true, "Wi-Fi Config.");
  strcat(response, "<div class=\"position menuitem\">");
  strcat(response, "<p>");
  strcat(response, "Current SSID: "); strcat(response, wifi_data.ssid); strcat(response, "<br/>");
  strcat(response, "Current Password: "); strncat(response, wifi_data.password, 4); strcat(response, "****<br/>");
  strcat(response, "Keep AP on: "); strcat(response, wifi_data.keep_ap_on ? "Yes" : "No"); strcat(response, "<br/>");
  strcat(response, "Status: "); strcat(response, WiFi.status() == WL_CONNECTED ? "Connected" : "NOT connected");
  strcat(response, "</p>");
  strcat(response, "<form method=\"post\" action=\"/wifi\">");
  strcat(response, "<table border=\"0\">");
  strcat(response, "<tr><td align=\"left\">SSID</td><td><input type=\"text\" name=\"ssid\" autocomplete=\"off\"></input></td></tr>");
  strcat(response, "<tr><td align=\"left\">Password</td><td><input type=\"text\" name=\"password\" autocomplete=\"off\"></input></td></tr>");
  strcat(response, "<tr><td align=\"left\">Keep AP on</td><td><input type=\"checkbox\" name=\"keep_ap_on\" value=\"1\"></input></td></tr>");
  strcat(response, "<tr><td colspan=\"2\" align=\"right\"><input type=\"submit\"></input></td></tr>");
  strcat(response, "</table>");
  strcat(response, "</div></body></html>");
  server.send(200, "text/html", response);
}

void webHandle_PostWifiForm() {
  Serial.println("Received POST with wifi data");
  if (!server.hasArg("ssid") || !server.hasArg("password") || server.arg("ssid") == NULL || server.arg("password") == NULL) {
    server.send(417, "text/plain", "417: Invalid Request");
    return;
  }

  // log
  Serial.print("SSID: ");
  Serial.println(server.arg("ssid"));
  Serial.print("Password: ");
  Serial.println(server.arg("password"));
  Serial.print("Keep AP on: ");
  Serial.println(server.arg("keep_ap_on"));

  // save to eeprom
  server.arg("ssid").toCharArray(wifi_data.ssid, 32);
  server.arg("password").toCharArray(wifi_data.password, 20);
  wifi_data.keep_ap_on = (server.arg("keep_ap_on") && server.arg("keep_ap_on").charAt(0) == '1');
  EEPROM.put(sizeof configuration, wifi_data);
  EEPROM.commit();

  // send response
  char response[400];
  webRestarting(response);
  server.send(200, "text/html", response);
  delay(200);

  // restart esp
  ESP.restart();
}

void webHandle_GetStyles() {
  char response[500];
  strcpy(response, "* {font-size: 14pt;}");
  strcat(response, "a {font-weight: bold;}");
  strcat(response, "table {margin-left:auto;margin-right:auto;}");
  strcat(response, ".position {width: 60%; margin-bottom: 10px; position: relative; margin-left: auto; margin-right: auto;}");
  strcat(response, ".title {text-align: center; font-weight: bold; font-size: 20pt;}");
  strcat(response, ".right {text-align: right;}");
  strcat(response, ".footer {font-size: 10pt; font-style: italic;}");
  strcat(response, ".menuitem {text-align: center; background-color: #efefef; cursor: pointer; border: 1px solid black;}");
  strcat(response, ".height30 {height: 30px;}");
  server.send(200, "text/css", response);
}

void webHandle_NotFound(){
  server.send(404, "text/plain", "404: Not found");
}

void initWebserver() {
  server.on("/", HTTP_GET, webHandle_GetRoot);
  server.on("/data.html", HTTP_GET, webHandle_GetData);
  server.on("/sensorconfig.html", HTTP_GET, webHandle_GetSensorConfig);
  server.on("/sensor", HTTP_POST, webHandle_PostSensorForm);
  server.on("/wificonfig.html", HTTP_GET, webHandle_GetWifiConfig);
  server.on("/wifi", HTTP_POST, webHandle_PostWifiForm);
  server.on("/httpstatus.html", HTTP_GET, webHandle_GetHttpStatus);
  server.on("/styles.css", HTTP_GET, webHandle_GetStyles);
  server.onNotFound(webHandle_NotFound);  
  
}

void initNetworking() {
#ifdef NETWORK_WIFI
  // read wifi config from eeprom
  EEPROM.get(sizeof configuration, wifi_data);
  Serial.println("Read data from EEPROM - ssid: " + String(wifi_data.ssid) + ", password: ****");
  
  // start AP
  char ssid[32];
  buildNetworkName(ssid);
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
    yield();
  }
  Serial.print("\n");
  Serial.print("WiFi connection established - IP address: ");

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
  }
#endif
  char ip[16];
  getIpAddressString(ip);
  Serial.println(ip);
}

void sendData(char* data) {
  // prepare headers
  uint16_t contentLength = strlen(data) + 4;
  char str_contentLength[5];
  sprintf (str_contentLength, "%4i", contentLength);
  
#ifdef NETWORK_WIFI
  // send
  yield();
  HTTPClient http;
  char server[70];
  strcpy(server, "http://");
  strcat(server, configuration.endpoint);
  http.begin(server);
  http.addHeader("Content-Type", "application/json");
  if (strcmp(configuration.jwt, "") != 0) {
    char auth_header[600];
    strcpy(auth_header, "Bearer ");
    strcat(auth_header, configuration.jwt);
    http.addHeader("Authorization", auth_header);
  }
  http.addHeader("Content-Length", str_contentLength);
  http.addHeader("X-SensorCentral-Version", VERSION_NUMBER);
  http.addHeader("X-SensorCentral-LastChange", VERSION_LASTCHANGE);
  yield();

  // post data and show respponse
  lastHttpResponseCode = http.POST(data);
  http.getString().toCharArray(lastHttpResponse, 2048, 0);
  Serial.print("Received response code: "); Serial.println(lastHttpResponseCode);
  Serial.print("Received payload: "); Serial.println(lastHttpResponse);
  yield();
  
  // end http
  http.end();
#endif

#ifdef NETWORK_ETHERNET
  const char *server = isProd ? serverProd : serverTest;
  if (client.connect(server, 80)) {
    // post data
    client.println("POST / HTTP/1.0");
    client.print  ("Host: "); client.println(server);
    client.println("Content-Type: application/json");
    if (strcmp(configuration.jwt, "") != 0) {
      client.print("Authorization: Bearer "); client.println(configuration.jwt);
    }
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
  yield();
}

bool isConnectedToNetwork() {
#ifdef NETWORK_WIFI
  // wifi
  if (WiFi.status() != WL_CONNECTED) {
    initNetworking();
  }
  yield();
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

char* preparePayload() {
  // create document
  StaticJsonDocument<2048> doc;
  
  // get your IP and MAC address
  char mac_addr[20];
  getMacAddressString(mac_addr);
  char ip_addr[16];
  getIpAddressString(ip_addr);

  // build payload
  doc["msgtype"] = "data";
  doc["deviceId"].set(mac_addr);
  JsonObject deviceData = doc.createNestedObject("deviceData");
  deviceData["ip"] = ip_addr;
  JsonArray jsonData = doc.createNestedArray("data");

  // loop sensors and add data
  for (uint8_t i=0, k=getSensorCount(); i<k; i++) {
    JsonObject jsonSensorData = jsonData.createNestedObject();
    jsonSensorData["sensorId"] = sensorIds[i];
    jsonSensorData["sensorValue"].set(sensorSamples[i]);
  }

  // serialize
  //uint16_t jsonLength = measureJson(doc) + 1; // add space for 0-termination
  char jsonBuffer[2048];
  serializeJson(doc, jsonBuffer, sizeof(jsonBuffer));
  return jsonBuffer;
}

// ******************** DS18B20
void initSensor_DS18B20() {
  // Start up the sensors
  Serial.println("Initializing DS18B20 sensors");
  uint8_t count = 0;
  
  for (uint8_t i=0; i<sizeof(sensorPins)/sizeof(uint8_t); i++) {
    OneWire oneWire(sensorPins[i]);
    DallasTemperature sensors(&oneWire);
    sensors.begin();
    Serial.print("Locating DS18B20 sensors on pin <");
    Serial.print(sensorPins[i]);
    Serial.print(">. Found ");
    Serial.print(sensors.getDeviceCount(), DEC);
    Serial.println(" DS18B20 sensors.");
    sensorsPerPin[i] = sensors.getDeviceCount();
    count += sensorsPerPin[i];
  }
  Serial.print("Found <");
  Serial.print(count);
  Serial.println("> DS18B20 sensors");
}



char* ds18b20AddressToString(DeviceAddress deviceAddress) {
    static char return_me[18];
    static char hex[] = "0123456789ABCDEF";
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

  // process each pin in turn
  for (uint8_t i=0; i<sizeof(sensorPins)/sizeof(uint8_t); i++) {
    OneWire oneWire(sensorPins[i]);
    DallasTemperature sensors(&oneWire);
    yield();
    
    // begin to scan for change in sensors
    sensors.begin();
    sensors.setResolution(DS18B20_TEMP_PRECISION);
    yield();
    
    // get count
    uint8_t sensorCount = sensorsPerPin[i];
    uint8_t sensorCountNew = sensors.getDeviceCount();
    if (sensorCount != sensorCountNew) {
      // sensorCount changed
      Serial.print("Detected DS18B20 sensor count change on pin <");
      Serial.print(sensorPins[i]);
      Serial.print("> - was ");
      Serial.print(sensorCount);
      Serial.print(" now ");
      Serial.println(sensorCountNew);
      sensorCount = sensorCountNew;
      sensorsPerPin[i] = sensorCountNew;
    }

    // read temperatures
    if (sensorCount > 0) {
      // request temperatures and store
      sensors.requestTemperatures();
      yield();
      
      // get addresses
      for (uint8_t j=0; j<sensorsPerPin[i]; j++) {
        sensors.getAddress(sensorAddresses[indexTempAddress], j);
        strcpy(sensorIds[indexTempAddress], ds18b20AddressToString(sensorAddresses[indexTempAddress]));
        sensorSamples[indexTempAddress] = sensors.getTempCByIndex(j);
        indexTempAddress++;
        yield();
      }
    }
  }
}

void printData_DS18B20() {
  uint8_t sensorCount = getSensorCount();
  if (sensorCount > 0) {
    for (uint8_t i=0; i<sensorCount; i++) {
      Serial.print(sensorIds[i]);
      Serial.print(": ");
      Serial.println(sensorSamples[i], TEMP_DECIMALS);
    }
  } else {
    Serial.println("No DS18B20 sensors found on bus");
  }
}


// ******************** DHT22
void initSensor_DHT22() {
  // Start up the sensors
  sensorsPerPin[0] = 2;
  getMacAddressStringNoColon(sensorIds[0]);
  strcat(sensorIds[0], "_temp");
  getMacAddressStringNoColon(sensorIds[1]);
  strcat(sensorIds[1], "_hum");
  Serial.println("Initializing DHT22 sensor");
  Serial.print("Temperature ID <");
  Serial.print(sensorIds[0]);
  Serial.println(">");
  Serial.print("Humidity ID <");
  Serial.print(sensorIds[1]);
  Serial.println(">");
}

void readData_DHT22() {
  // init
  DHT dht(sensorPins[0], DHT_TYPE);
  dht.begin();

  // read temperature
  sensorSamples[0] = dht.readTemperature();

  // read humidity
  sensorSamples[1] = dht.readHumidity();
}

void printData_DHT22() {
  Serial.println("Printing data from DHT22 sensor");
  Serial.print(sensorIds[0]);
  Serial.print(": ");
  Serial.print(sensorSamples[0]);
  Serial.println(" (temperature)");
  Serial.print(sensorIds[1]);
  Serial.print(": ");
  Serial.print(sensorSamples[1]);
  Serial.println(" (humidity)");
}

// ******************** BINARY
void initSensor_BINARY() {
  // Start up the sensors
  sensorsPerPin[0] = 1;
  getMacAddressStringNoColon(sensorIds[0]);
  strcat(sensorIds[0], "_binary");
  Serial.println("Initializing BINARY sensor");
  Serial.print("ID <");
  Serial.print(sensorIds[0]);
  Serial.println(">");
}
void printData_BINARY() {
  Serial.print("Printing data from BINARY sensor: ");
  Serial.println("ON");
}
void readData_BINARY() {
  sensorSamples[0] = 1;
}

/** 
 *  ********************************************
 *  COMMON
 *  ********************************************
 */

/**
 * Prints the data to the serial console.
 */
void printData() {
  Serial.println("------------------------");
  
  if (isSensorTypeDS18B20()) {
    printData_DS18B20();
  } else if (isSensorTypeDHT22()) {
    printData_DHT22();
  } else if (isSensorTypeBINARY()) {
    printData_BINARY();
  }
    
  Serial.println("Printed available data");
}


/** 
 *  ********************************************
 *  SETUP
 *  ********************************************
 */
void setup() {
  Serial.begin(115200);
  Serial.print("Version: ");
  Serial.println(VERSION_NUMBER);
  Serial.print("Last change: ");
  Serial.println(VERSION_LASTCHANGE);
  printMacAddress();

  // init config
  EEPROM.begin(sizeof configuration + sizeof wifi_data + 10);
  EEPROM.get(0, configuration);
  
  if (configuration.version != CONFIGURATION_VERSION) {
    Serial.println("Setting standard configuration");
    configuration.version = CONFIGURATION_VERSION;
    strcpy(configuration.endpoint, "");
    strcpy(configuration.jwt, "");
    strcpy(configuration.sensorType, "");
    configuration.delayPrint = DEFAULT_DELAY_PRINT;
    configuration.delayPoll = DEFAULT_DELAY_POLL;
    configuration.delayPost = DEFAULT_DELAY_POST;
    EEPROM.put(0, configuration);

    strcpy(wifi_data.ssid, "");
    strcpy(wifi_data.password, "");
    wifi_data.keep_ap_on = false;
    EEPROM.put(sizeof configuration, wifi_data);
    
    EEPROM.commit();
    yield();
  }
  Serial.print("Read data from EEPROM - endpoint <");
  Serial.print(configuration.endpoint);
  Serial.println(">");
  Serial.print("Read data from EEPROM - JWT <");
  Serial.print(configuration.jwt);
  Serial.println(">");
  Serial.print("Read data from EEPROM - delay print <");
  Serial.print(configuration.delayPrint);
  Serial.print("> delay poll <");
  Serial.print(configuration.delayPoll);
  Serial.print("> delay post <");
  Serial.print(configuration.delayPost);
  Serial.println(">");
  
  if (isSensorTypeDS18B20()) {
    Serial.print("Using DS18B20 on pins <");
    for (uint8_t i=0; i<sizeof(sensorPins) / sizeof(uint8_t); i++) {
      if (i > 0) Serial.print(", ");
      Serial.print(sensorPins[i]);
    }
    Serial.println(">");
  } else if (isSensorTypeDHT22()) {
    Serial.print("Using DHT22 on pin <");
    Serial.print(sensorPins[0]);
    Serial.println(">");
  } else if (isSensorTypeBINARY()) {
    Serial.print("Binary sensor - no pin");
  } else {
    Serial.println("Undefined sensor type set...");
  }
  
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
  if (isSensorTypeDS18B20()) {
    // initialize DS18B20 temp sensors
    initSensor_DS18B20();
  } else if (isSensorTypeDHT22()) {
    // initialize DHT22 temp sensor
    initSensor_DHT22();
  } else if (isSensorTypeBINARY()) {
    // init binary sensor
    initSensor_BINARY();
  }
  yield();
}


/** 
 *  ********************************************
 *  LOOP
 *  ********************************************
 */
void loop() {
#ifdef NETWORK_WIFI
  // disable AP 
  if ((WiFi.softAPIP() && millis() > DELAY_TURNOFF_AP) && wifi_data.keep_ap_on == false) {
    // diable AP
    Serial.println("Disabling AP...");
    WiFi.softAPdisconnect(false);
    WiFi.enableAP(false);
  }
#endif

#ifdef NETWORK_WIFI
  // handle incoming request to web server
  server.handleClient();
#endif

  if (!isConnectedToNetwork()) {
    Serial.println("No network connected...");
    return;
  }
  
  if (justReset && hasWebEndpoint()) {
    // this is the first run - tell web server we restarted
    yield();
    justReset = false;
    
    // get your MAC address and IP
    char mac_addr[20];
    getMacAddressString(mac_addr);
    char ip[16];
    getIpAddressString(ip);

    // build payload
    StaticJsonDocument<256> doc;
    doc["msgtype"] = "control";
    doc["deviceId"].set(mac_addr);
    JsonObject jsonData = doc.createNestedObject("data"); 
    jsonData["restart"] = true;
    jsonData["ip"].set(ip);
    
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

  // read from sensor(s)
  if (!startedRead && (millis() - lastRead) > configuration.delayPoll) {
    lastRead = millis();
    startedRead = true;

    // read ds18b20 data
    if (isSensorTypeDS18B20()) {
      readData_DS18B20();
    } else if (isSensorTypeDHT22()) {
      readData_DHT22();
    } else if (isSensorTypeBINARY()) {
      readData_BINARY();
    }

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
  if (!startedPostData && hasWebEndpoint() && ((millis() - lastPostData) > configuration.delayPost)) {
    lastPostData = millis();
    startedPostData = true;
    
    #ifdef PIN_HTTP_LED
    digitalWrite(PIN_HTTP_LED, HIGH);
    #endif
    
    // prepare post data
    char* jsonData = preparePayload();
    yield();
    
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
