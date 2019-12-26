// test or prod
const boolean isProd = false;

// servers
const char serverProd[] = "desolate-meadow-68880.herokuapp.com";
const char serverTest[] = "boiling-dusk-12267.herokuapp.com";

// delays
#define DELAY_POST_DATA 120000L          // delay between updates, in milliseconds
#define DELAY_PRINT 10000L              // delay between printing to the console, in milliseconds
#define DELAY_READ 10000L                // delay between reading the sensor(s), in milliseconds

// enable DS18B20 on pin 9
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
