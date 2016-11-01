
#define MQTT_KEEPALIVE 120

#define DEEPSLEEP 60
#define ONE_WIRE_BUS 12
#define BLUE_LED 2

/* Config.h contain :
  const char ssidName[] = "..........";
  const char ssidPassword[] = ".............";


  const char mqttServerName[] = "mqttserver.example.com";

  const char mqttClientId[] = "arduinoClient";
  const int  mqttPort = 8883;
  const char mqttUser[] = "username";
  const char mqttPassword[] = "password";


  IPAddress mqttServer(1,1,1,1);

*/

#include "config.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DallasTemperature.h>



#ifdef DEBUG_ESP_PORT
#define DEBUG_MSG(...) DEBUG_ESP_PORT.printf( __VA_ARGS__ )
#else
#define DEBUG_MSG(...)
#endif


IPAddress mqttServer(37, 187, 2, 99);

WiFiClientSecure client;
PubSubClient mqtt(mqttServer, mqttPort, client);

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

void setup() {

#ifdef DEBUG_ESP_PORT
  DEBUG_ESP_PORT.begin(74880);
#endif

  // Uncomment only for first prog
  // Wifi is automatic connect next time.
  // WiFi.begin(ssidName, ssidPassword, 11);

  DEBUG_MSG("\n");
  DEBUG_MSG("Request temp\n");
  sensors.begin();
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();

  unsigned long startConv = millis();
  int resolution = 12;
  unsigned long delayconv = 750 / (1 << (12 - resolution));

  // wait wifi
  DEBUG_MSG("Connecting to %s\n", ssidName);
  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 200) {
    // DEBUG_MSG("Wait for %s\n",ssidName);
    delay(100);
  }
  if (i >= 200) {
    DEBUG_MSG("Could not connect to %s\n", ssidName);
    // GO to deep sleep for 5 min before retry
    ESP.deepSleep(5 * 60 * 1000000);
  }

  // connect MQTT
  boolean isConnected = false;
  i = 0;
  do {
    isConnected = mqtt.connect(mqttClientId, mqttUser, mqttPassword, "test/lostcom", 0, false, "yep");
    i++;
  } while (!isConnected && i < 5);

  if (!isConnected) {
    DEBUG_MSG("Could not connect to MQTT server\n");
    // GO to deep sleep for 5 min before retry
    ESP.deepSleep(5 * 60 * 1000000);
  }
  
  DEBUG_MSG("Connected to MQTT server\n");
  // wait if sensor have not finish
  unsigned long now = millis();
  if (now - startConv < delayconv) {
    DEBUG_MSG("Wait convert\n");
    delay(delayconv - (now - startConv));
  }

  DeviceAddress address;
  while (oneWire.search(address))
  {
    DEBUG_MSG("Read Temp of :");
    char addressString[8 * 2 + 8];
    sprintf(addressString, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
            address[0],
            address[1],
            address[2],
            address[3],
            address[4],
            address[5],
            address[6],
            address[7]
           );
    DEBUG_MSG("%s\n", addressString);
    String topic = "test/" + String(addressString);
    String value = String(sensors.getTempC(address));
    mqtt.publish(topic.c_str(), value.c_str(), true);
  }

  // Read voltage
  DEBUG_MSG("Read voltage :");
  String value = String(analogRead(A0) * (320.0 / 100.0 / 1024.0));
  mqtt.publish("test/volt", value.c_str(), true);

  DEBUG_MSG("Set output Hight");
  // set to output and hight state 
  pinMode(ONE_WIRE_BUS, OUTPUT);
  digitalWrite(ONE_WIRE_BUS, HIGH);

  // Give some time to send data.
  // and notify with a blink
  pinMode(BLUE_LED, OUTPUT);
  digitalWrite(BLUE_LED, LOW);
  delay(250);
  digitalWrite(BLUE_LED, HIGH);
  DEBUG_MSG("Go to deep sleep.\n");
  ESP.deepSleep(DEEPSLEEP * 1000000);
}

void loop() {
  // we should not come here
  ESP.deepSleep(DEEPSLEEP * 1000000);
}
