// debug
#define WEBSERVER_DEBUG

// enable DS18B20 on pin 8 and 9
//#define SENSORTYPE_DS18B20
const uint8_t DS18B20_PINS[] = {8, 9};
#define DS18B20_TEMP_PRECISION 12        // 12 bits precision

// enable DHT22 on pin 4 with specified deviceID's
#define SENSORTYPE_DHT22
#define DHT_PIN 14
#define DHT_TYPE DHT22
#define SENSORID_DHT22_TEMP "pcb1_dht_temp"
#define SENSORID_DHT22_HUM "pcb1_dht_hum"

// option to enable LDR on pin A0
//#define SENSORTYPE_LDR
//#define SENSORID_LDR "outside_light"
//#define LDR_PIN A0
