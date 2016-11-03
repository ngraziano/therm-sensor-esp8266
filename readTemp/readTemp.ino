
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

extern "C" {
#include "user_interface.h"

extern struct rst_info resetInfo;
}


#ifdef DEBUG_ESP_PORT
#define DEBUG_MSG(...) DEBUG_ESP_PORT.printf( __VA_ARGS__ )
#else
#define DEBUG_MSG(...)
#endif


// RTCMermoy
// maximum size of 512 bytes
// size of bucket is 4 byte
struct {
  byte stateFlags;
  byte nbSensors;
  byte nbvals;
  byte padding[1];
} stateData;

const byte SEND_DATA_THIS_LOOP = 1;

int firstDataOffset = sizeof(stateData);



IPAddress mqttServer(37, 187, 2, 99);

WiFiClientSecure client;
PubSubClient mqtt(mqttServer, mqttPort, client);

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);


unsigned long startConv;
unsigned long delayconv;
void startConversion() {
  DEBUG_MSG("Request temp\n");
  sensors.begin();
  sensors.setWaitForConversion(false);
  sensors.requestTemperatures();

  startConv = millis();
  int resolution = 12;
  delayconv = 750 / (1 << (12 - resolution));
}

void waitEndOfConversion() {
  // wait if sensor have not finish
  unsigned long now = millis();
  if (now - startConv < delayconv) {
    DEBUG_MSG("Wait convert\n");
    delay(delayconv - (now - startConv));
  }

}

void waitWifi() {
  // wait wifi
  DEBUG_MSG("Connecting to %s\n", ssidName);
  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 200) {
    // DEBUG_MSG("Wait for %s\n",ssidName);
    delay(100);
  }
  if (i >= 200) {
    DEBUG_MSG("Could not connect to %s\n", ssidName);
    // GO to deep sleep before retry
    ESP.deepSleep(DEEPSLEEP * 1000000);
  }

}

void connectMqtt() {
  // connect MQTT
  boolean isConnected = false;
  int i = 0;
  do {
    isConnected = mqtt.connect(mqttClientId, mqttUser, mqttPassword, "test/lostcom", 0, false, "yep");
    i++;
  } while (!isConnected && i < 5);

  if (!isConnected) {
    DEBUG_MSG("Could not connect to MQTT server\n");
    // GO to deep sleep before retry
    ESP.deepSleep(DEEPSLEEP * 1000000);
  }
  DEBUG_MSG("Connected to MQTT server\n");
}



void setup() {

#ifdef DEBUG_ESP_PORT
  DEBUG_ESP_PORT.begin(74880);
#endif

  // Uncomment only for first prog
  // Wifi is automatic connect next time.
  // WiFi.begin(ssidName, ssidPassword, 11);

  DEBUG_MSG("\n");
  startConversion();

  bool firstStart = true;
  rst_info * rstinfo = ESP.getResetInfoPtr();
  if(rstinfo->reason == REASON_DEEP_SLEEP_AWAKE)
    firstStart = false;
  if (firstStart) {
    stateData.stateFlags = 0;
    stateData.nbSensors = sensors.getDeviceCount();
    stateData.nbvals = 0;
  }
  else {
    ESP.rtcUserMemoryRead(0, (uint32_t*) &stateData, sizeof(stateData));
  }

  if (stateData.stateFlags & SEND_DATA_THIS_LOOP) {
    waitWifi();
    connectMqtt();
  }

  waitEndOfConversion();

  DeviceAddress address;

  if (stateData.stateFlags & SEND_DATA_THIS_LOOP) {
    DEBUG_MSG("Do not send this time.");
    int i = 0;
    std::unique_ptr<float[]> temps(new float[stateData.nbSensors]);
    while (oneWire.search(address) && i < stateData.nbSensors) {
      DEBUG_MSG("Read Temp for memory");
      temps[i] = sensors.getTempC(address);
      i++;
    }
    int dataOffset = firstDataOffset + sizeof(float) * stateData.nbvals;
    ESP.rtcUserMemoryWrite(dataOffset, (uint32_t*) temps.get(), sizeof(float) * stateData.nbSensors );
    stateData.nbvals++;
    DEBUG_MSG("Temp store in memory.");
  } else {
    DEBUG_MSG("Read Stored temps.");
    std::unique_ptr<float[]> temps(new float[stateData.nbSensors * stateData.nbvals]);
    ESP.rtcUserMemoryRead(firstDataOffset, (uint32_t*) temps.get(), sizeof(float) * stateData.nbSensors * stateData.nbvals);
    int i = 0;
    while (oneWire.search(address) && i < stateData.nbSensors)
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
      float lastVal = sensors.getTempC(address);
      String value = String(lastVal);
      mqtt.publish(topic.c_str(), value.c_str(), true);
      value = "[";
      for (int numVal = 0; numVal < stateData.nbvals; numVal++) {
        value += String(temps[numVal * stateData.nbSensors + i ]) + ",";
      }
      value += String(lastVal) + "]";
      topic = "test/hist/" + String(addressString);
      mqtt.publish(topic.c_str(), value.c_str(), true);
      i++;
    }
    // Read voltage
    DEBUG_MSG("Read voltage :");
    String value = String(analogRead(A0) * (320.0 / 100.0 / 1024.0));
    mqtt.publish("test/volt", value.c_str(), true);

    // reset number of values
    stateData.nbvals = 0;
    stateData.stateFlags &= ~SEND_DATA_THIS_LOOP;

    // Give some time to send data.
    // and notify with a blink
    pinMode(BLUE_LED, OUTPUT);
    digitalWrite(BLUE_LED, LOW);
    delay(250);
    digitalWrite(BLUE_LED, HIGH);
  }
  // little fash to show we are alive.
  pinMode(BLUE_LED, OUTPUT);
  digitalWrite(BLUE_LED, LOW);
  if(stateData.nbvals > (512 - firstDataOffset) / (sizeof(float) *  stateData.nbSensors) - 1) {
    DEBUG_MSG("Store state send next time.");
    stateData.stateFlags |= SEND_DATA_THIS_LOOP;
  }
  DEBUG_MSG("Store state.");
  ESP.rtcUserMemoryWrite(0, (uint32_t*) &stateData, sizeof(stateData));

  DEBUG_MSG("Set output Hight");
  // set to output and hight state
  pinMode(ONE_WIRE_BUS, OUTPUT);
  digitalWrite(ONE_WIRE_BUS, HIGH);

  digitalWrite(BLUE_LED, HIGH);
  DEBUG_MSG("Go to deep sleep.\n");

  if(stateData.stateFlags & SEND_DATA_THIS_LOOP)
    ESP.deepSleep(DEEPSLEEP * 1000000, RF_NO_CAL);
  else
    // DEEP SLEEP AND WAKE without WIFI
    ESP.deepSleep(DEEPSLEEP * 1000000, RF_DISABLED);
}

void loop() {
  // we should not come here
  ESP.deepSleep(DEEPSLEEP * 1000000);
}
