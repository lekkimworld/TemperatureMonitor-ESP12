# esp12-sensors-test
Project to test running my required temperature sensors (ds18b20) and a light photo-resistor off an ESP-12 instead of a full Arduino

Remember to add vars.h to keep variables such as SSID of wi-fi plus passcode. The file also keeps the 
sensorId transmitted for the light sensor as well as the webservice to post to. Below is an example file.
```
const char ssid1[]   = "wifi-ssid1";
const char pass1[]   = "foo";
const char ssid2[]   = "wifi-ssid2";
const char pass2[]   = "bar";
const char server[] = "http://boiling-tree-34283.herokuapp.com";
const char sensorId_Light[] = "outside_light";
```
