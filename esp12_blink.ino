#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <OneWire.h> 
#include <DallasTemperature.h>
#include "vars.h"

#define REDLED_PIN 13
#define BLUELED_PIN 14
#define GREENLED_PIN 16
#define TEMP_PIN 12
#define DELAY_POST_DATA 30000L         // delay between updates, in milliseconds
#define DELAY_PRINT 15000L              // delay between printing to the console, in milliseconds
#define DELAY_READ 5000L                // delay between reading the sensor(s), in milliseconds
#define DELAY_CONNECT_ATTEMPT 10000L    // delay between attempting wifi reconnect, in milliseconds
#define DELAY_BLINK 100L                // how long a led blinks, in milliseconds
#define MAX_TEMP_SENSOR_COUNT 5         // maximum of DS18B20 sensors we can connect
#define TEMP_DECIMALS 4                 // 4 decimals of output
#define TEMPERATURE_PRECISION 12        // 12 bits precision

OneWire oneWire(TEMP_PIN);
DallasTemperature sensors(&oneWire);

// **** WiFi *****
ESP8266WiFiMulti WiFiMulti;
unsigned long lastConnectAttempt = millis();
unsigned long lastPostData = millis();
unsigned long lastPrint = millis();
unsigned long lastRead = millis();
boolean startedRead = false;
boolean startedPrint = false;
boolean startedPostData = false;
unsigned long ldr = 0;
uint8_t sensorCount = 0;
DeviceAddress addresses[MAX_TEMP_SENSOR_COUNT];
float temperatures[MAX_TEMP_SENSOR_COUNT];
uint8_t reconnect;

void setup() {
  Serial.begin(115200);

  pinMode(A0, INPUT);
  pinMode(REDLED_PIN, OUTPUT);
  digitalWrite(REDLED_PIN, LOW);
  pinMode(BLUELED_PIN, OUTPUT);
  digitalWrite(BLUELED_PIN, LOW);
  pinMode(GREENLED_PIN, OUTPUT);
  digitalWrite(GREENLED_PIN, LOW);

  // initialize temp sensors
  initTempSensors();
  
  // init wifi
  initWifi();
}

void loop() {
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
        Serial.println("Resetting...");
        ESP.reset();
        return;
      }
    }
    
  } else if (status == WL_CONNECTED) {
    reconnect = 0;
    if (!startedRead && (millis() - lastRead) > DELAY_READ) {
      lastRead = millis();
      startedRead = true;
      
      digitalWrite(REDLED_PIN, HIGH);
    }
    yield();
    
    if (startedRead && (millis() - lastRead) > DELAY_BLINK) {
      lastRead = millis();
      startedRead = false;
      
      digitalWrite(REDLED_PIN, LOW);
      
      readTemperatures();
      ldr = analogRead(A0);
      
    }
    yield();
    
    if (!startedPrint && (millis() - lastPrint) > DELAY_PRINT) {
      lastPrint = millis();
      startedPrint = true;
      
      digitalWrite(BLUELED_PIN, HIGH);
      
      printTemperatures();
  
      Serial.print("LDR Reading: "); 
      Serial.println(ldr);
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
      char payload[2 + (sensorCount * 70) + 70];
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
}

char* preparePayload(char *content) {
  // start json array
  strcpy(content, "[");

  // loop sensors
  for (uint8_t i=0; i<sensorCount; i++) {
    char str_temp[8];
    dtostrf(temperatures[i], 6, TEMP_DECIMALS, str_temp);

    if (i > 0) {
      strcat(content, ",");
    }
    strcat(content, "{\"sensorId\": \"");
    strcat(content, deviceAddressToString(addresses[i]));
    strcat(content, "\", \"sensorValue\": ");
    strcat(content, str_temp);
    strcat(content, "}");
  }

  // add ldr
  char str_ldr[8];
  ltoa(ldr, str_ldr, 10);
  strcat(content, ",");
  strcat(content, "{\"sensorId\": \"");
  strcat(content, sensorId_Light);
  strcat(content, "\", \"sensorValue\": ");
  strcat(content, str_ldr);
  strcat(content, "}");
  strcat(content, "]");

  // return
  return content;
}

void sendData(char *data) {
  // prepare headers
  uint8_t contentLength = strlen(data) + 4;
  char str_contentLength[4];
  sprintf (str_contentLength, "%03i", contentLength);
  
  // send
  HTTPClient http;
  http.begin(server);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Content-Length", str_contentLength);
  int httpCode = http.POST(data);
  String payload = http.getString();                  //Get the response payload

  Serial.println(httpCode);   //Print HTTP return code
  Serial.println(payload);    //Print request response payload

  http.end();

  Serial.println("Sent to server...");
}

void readTemperatures() {
  // begin to scan for change in sensors
  sensors.begin();

  // get count
  uint8_t sensorCountNew = sensors.getDeviceCount();
  if (sensorCount != sensorCountNew) {
    // sensorCount changed
    Serial.print("Detected sensor count change - was ");
    Serial.print(sensorCount);
    Serial.print(" now ");
    Serial.println(sensorCountNew);
    sensorCount = sensorCountNew;

    if (sensorCount > 0) {
      // set resolution
      sensors.setResolution(TEMPERATURE_PRECISION);
  
      // get addresses
      for (uint8_t i=0; i<sensorCount; i++) {
        sensors.getAddress(addresses[i], i);
      }
    }
  }
  
  if (sensorCount > 0) {
    // request temperatures and store
    sensors.requestTemperatures();
    for (uint8_t i=0; i<sensorCount; i++) {
      temperatures[i] = sensors.getTempCByIndex(i);
    }
  }
}

void printTemperatures() {
  if (sensorCount <= 0) return;
  
  Serial.println("------------------------");
  for (uint8_t i=0; i<sensorCount; i++) {
    Serial.print(deviceAddressToString(addresses[i]));
    Serial.print(": ");
    Serial.println(temperatures[i], TEMP_DECIMALS);
  }
}

void initTempSensors() {
  // Start up the sensors
  Serial.println("Initializing sensors");
  sensors.begin();
  Serial.println("Locating devices...");
  Serial.print("Found ");
  Serial.print(sensors.getDeviceCount(), DEC);
  Serial.println(" devices.");
  
}

void initWifi() {
  
  // show MAC
  printMacAddress();
  
}

char* deviceAddressToString(DeviceAddress deviceAddress){
    static char return_me[18];
    static char *hex = "0123456789ABCDEF";
    uint8_t i, j;

    for (i=0, j=0; i<8; i++) 
    {
         return_me[j++] = hex[deviceAddress[i] / 16];
         return_me[j++] = hex[deviceAddress[i] & 15];
    }
    return_me[j] = '\0';
    
    return return_me;
}

void printMacAddress() {
  // get your MAC address
  byte mac[6];
  WiFi.macAddress(mac);
  
  // print MAC address
  char buf[20];
  sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X", mac[5], mac[4], mac[3], mac[2], mac[1], mac[0]);
  Serial.print("MAC address: ");
  Serial.println(buf);
}
