#include "vars.h"
#include "ethernet_config.h"
#include "wifi_config.h"
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
  #include <ESP8266WiFiMulti.h>
  #include <ESP8266HTTPClient.h>
#endif

#define VERSION_NUMBER "20190830T2300"
#define VERSION_LASTCHANGE "Add support for DS18B20 sensors to multiple pins"

#define WATCHDOG_PIN 13                 // pin where we connect to a 555 timer watch dog circuit
#define BLUELED_PIN 14
#define GREENLED_PIN 16
#define DELAY_CONNECT_ATTEMPT 10000L    // delay between attempting wifi reconnect or if no ethernet link, in milliseconds
#define DELAY_BLINK 200L                // how long a led blinks, in milliseconds
#define DELAY_PAT_WATCHDOG 200L         // how long a watchdog pat lasts, in milliseconds
#define MAX_TEMP_SENSOR_COUNT 10         // maximum of DS18B20 sensors we can connect
#define TEMP_DECIMALS 4                 // 4 decimals of output
#define HUM_DECIMALS 4                  // 4 decimals of output
#define TEMPERATURE_PRECISION 12        // 12 bits precision

// **** WiFi *****
#ifdef NETWORK_WIFI
  ESP8266WiFiMulti WiFiMulti;
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

// ds18b20
#ifdef SENSORTYPE_DS18B20
  uint8_t sensorCounts[sizeof(DS18B20_PINS)/sizeof(uint8_t)];
  DeviceAddress addresses[MAX_TEMP_SENSOR_COUNT];
  float temperatures[MAX_TEMP_SENSOR_COUNT];
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

void setup() {
  Serial.begin(115200);
  Serial.print("Version: ");
  Serial.println(VERSION_NUMBER);
  Serial.print("Last change: ");
  Serial.println(VERSION_LASTCHANGE);
  if (isProd) {
    Serial.println("Config: PRODUCTION"); 
    Serial.print("Server is: ");
    Serial.println(serverProd);
  } else {
    Serial.println("Config: TEST");
    Serial.print("Server is: ");
    Serial.println(serverTest);
  }
  printMacAddress();

  pinMode(WATCHDOG_PIN, INPUT); // set to high impedance
  digitalWrite(WATCHDOG_PIN, HIGH);
  pinMode(BLUELED_PIN, OUTPUT);
  digitalWrite(BLUELED_PIN, LOW);
  pinMode(GREENLED_PIN, OUTPUT);
  digitalWrite(GREENLED_PIN, LOW);

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
 * Get total DS18B20 sensor count across pins.
 */
uint8_t getDS18B20SensorCount() {
  uint8_t result = 0;
  for (uint8_t i=0; i<sizeof(DS18B20_PINS)/sizeof(uint8_t); i++) {
    result += sensorCounts[i];
  }
  return result;
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

bool isConnectedToNetwork() {
#ifdef NETWORK_WIFI
  // wifi
  wl_status_t status = WiFi.status();
  if (status != WL_CONNECTED && ((unsigned long)millis() - lastConnectAttempt > DELAY_CONNECT_ATTEMPT)) {
    // not connected - init wifi
    WiFi.mode(WIFI_STA);
    WiFiMulti.addAP(ssid1, pass1);
    if (strlen(ssid2) > 0) {
      WiFiMulti.addAP(ssid2, pass2);
    }
    Serial.println("Not connected attempting reconnect...");
    lastConnectAttempt = millis();
    if (WiFiMulti.run() != WL_CONNECTED) {
      reconnect++;
      Serial.print("Could not reconnect (attempt: ");
      Serial.print(reconnect);
      Serial.println(")...");
      if (reconnect >= 3) {
        Serial.println("Restarting...");
        ESP.restart();
        return false;
      }
    }
    
  } else if (status == WL_CONNECTED) {
    return true;
  }
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

    // we're connected
    Serial.print("Ethernet assigned IP by DHCP: ");
    Serial.println(Ethernet.localIP());
    
    // toggle flag
    didEthernetBegin = true;
  }
  
  // ensure continued DHCP lease
  if (Ethernet.maintain() != 0) {
    Serial.print("Received new DHCP address: ");
    Serial.println(Ethernet.localIP());
  }

  // return
  return true;
#endif
}

void loop() {
  if (!isConnectedToNetwork()) {
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
    char payload[128];
    strcpy(payload, "{\"msgtype\": \"control\", \"data\": {\"restart\": true, \"deviceId\": \"");
    strcat(payload, mac_addr);
    strcat(payload, "\"}}");
    
    // send payload
    sendData(payload);
  }
  yield();

  // reset reconnect count
  reconnect = 0;
  if (!startedRead && (millis() - lastRead) > DELAY_READ) {
    lastRead = millis();
    startedRead = true;

    // pat the watchdog
    pinMode(WATCHDOG_PIN, OUTPUT);
    digitalWrite(WATCHDOG_PIN, LOW);
  }
  yield();
  
  if (startedRead && (millis() - lastRead) > DELAY_PAT_WATCHDOG) {
    lastRead = millis();
    startedRead = false;

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
    
    // finish write and return to high impedance
    Serial.println("Patted watch dog...");
    digitalWrite(WATCHDOG_PIN, HIGH);
    pinMode(DELAY_PAT_WATCHDOG, INPUT);
  }
  yield();
  
  if (!startedPrint && (millis() - lastPrint) > DELAY_PRINT) {
    lastPrint = millis();
    startedPrint = true;
    
    digitalWrite(BLUELED_PIN, HIGH);

    // show data on console
    printData();
  }
  yield();
  
  if (startedPrint && (millis() - lastPrint) > DELAY_BLINK) {
    startedPrint = false;
    lastPrint = millis();
    
    digitalWrite(BLUELED_PIN, LOW);
  }
  yield();

  // post
  if (!startedPostData && (millis() - lastPostData) > DELAY_POST_DATA) {
    lastPostData = millis();
    startedPostData = true;
    
    digitalWrite(GREENLED_PIN, HIGH);

    // prepare post data
    uint8_t sensorCount = 0;
    #ifdef SENSORTYPE_DS18B20
    sensorCount += getDS18B20SensorCount();
    #endif
    #ifdef SENSORTYPE_DHT22
    sensorCount++;
    #endif
    #ifdef SENSORTYPE_LDR
    sensorCount++;
    #endif
    char payload[2 + (sensorCount * 70) + 70 + 40];
    preparePayload(payload);
    
    // send payload
    sendData(payload);
  }
  yield();

  if (startedPostData && (millis() - lastPostData) > DELAY_BLINK) {
    startedPostData = false;
    lastPostData = millis();
    
    digitalWrite(GREENLED_PIN, LOW);
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
}

char* preparePayload(char *content) {
  // get your MAC address
  char mac_addr[20];
  getMacAddressString(mac_addr);
  
  // start json array
  strcpy(content, "{\"msgtype\": \"data\", \"deviceId\": \"");
  strcat(content, mac_addr);
  strcat(content, "\", \"data\": [");
  boolean didAddSensors = false;

  // loop DS18B20 sensors
  #ifdef SENSORTYPE_DS18B20
  char str_temp[8];
  for (uint8_t i=0, k=getDS18B20SensorCount(); i<k; i++) {
    dtostrf(temperatures[i], 6, TEMP_DECIMALS, str_temp);

    if (i > 0 || didAddSensors) {
      strcat(content, ",");
    }
    strcat(content, "{\"sensorId\": \"");
    strcat(content, ds18b20AddressToString(addresses[i]));
    strcat(content, "\", \"sensorValue\": ");
    strcat(content, str_temp);
    strcat(content, "}");
    didAddSensors = true;
  }
  #endif

  #ifdef SENSORTYPE_DHT22
    // add dht22
    char str_dht_temp[8];
    char str_dht_hum[8];
    dtostrf(dht22_temp, 6, TEMP_DECIMALS, str_dht_temp);
    dtostrf(dht22_hum, 6, HUM_DECIMALS, str_dht_hum);
    if (didAddSensors) {
      strcat(content, ",");
    }
    strcat(content, "{\"sensorId\": \"");
    strcat(content, SENSORID_DHT22_TEMP);
    strcat(content, "\", \"sensorValue\": ");
    strcat(content, str_dht_temp);
    strcat(content, "}, {\"sensorId\": \"");
    strcat(content, SENSORID_DHT22_HUM);
    strcat(content, "\", \"sensorValue\": ");
    strcat(content, str_dht_hum);
    strcat(content, "}");
    didAddSensors = true;
  #endif

  #ifdef SENSORTYPE_LDR
    // add ldr
    char str_ldr[8];
    ltoa(ldr, str_ldr, 10);
    if (didAddSensors) {
      strcat(content, ",");
    }
    strcat(content, "{\"sensorId\": \"");
    strcat(content, SENSORID_LDR);
    strcat(content, "\", \"sensorValue\": ");
    strcat(content, str_ldr);
    strcat(content, "}");
    didAddSensors = true;
  }
  #endif

  // close payload
  strcat(content, "]}");

  // return
  return content;
}

void sendData(char *data) {
  // prepare headers
  uint16_t contentLength = strlen(data) + 4;
  char str_contentLength[4];
  sprintf (str_contentLength, "%03i", contentLength);
  
#ifdef NETWORK_WIFI
  // send
  HTTPClient http;
  char server[50];
  strcpy(server, "http://");
  if (isProd) {
    strcat(server, serverProd);
  } else {
    strcat(server, serverTest);
  }
  http.begin(server);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Content-Length", str_contentLength);
  http.addHeader("X-SensorCentral-Version", VERSION_NUMBER);
  http.addHeader("X-SensorCentral-LastChange", VERSION_LASTCHANGE);
  int httpCode = http.POST(data);
  String payload = http.getString();                  //Get the response payload

  Serial.println(httpCode);   //Print HTTP return code
  Serial.println(payload);    //Print request response payload

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
      sensors.setResolution(TEMPERATURE_PRECISION);
      
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
  uint8_t sensorCount = getDS18B20SensorCount();
  if (sensorCount > 0) {
    for (uint8_t i=0; i<sensorCount; i++) {
      Serial.print(ds18b20AddressToString(addresses[i]));
      Serial.print(": ");
      Serial.println(temperatures[i], TEMP_DECIMALS);
    }
  }
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
