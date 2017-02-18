

// you need to change
// #define MQTT_KEEPALIVE 1200
// #define MQTT_MAX_PACKET_SIZE 512

#define MAX_KEPT_VALUE 5

#define DEEPSLEEP 10
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
  byte nbCnxPb;
} stateData;

const byte SEND_DATA_THIS_LOOP = 1;

int stateDataOffset = 0;
int firstDataOffset = stateDataOffset + (sizeof(stateData) / 4);



IPAddress mqttServer(192, 168, 0, 249 );

WiFiClient client;
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

boolean waitWifi() {
  // wait wifi
  DEBUG_MSG("Connecting to %s\n", ssidName);
  uint8_t i = 0;
  while (WiFi.status() != WL_CONNECTED && i++ < 200) {
    // DEBUG_MSG("Wait for %s\n",ssidName);
    delay(100);
  }

  if (i >= 200) {
    DEBUG_MSG("Could not connect to %s\n", ssidName);
    return false;
  }
  return true;
}

boolean connectMqtt() {
  // connect MQTT
  DEBUG_MSG("try to connect to MQTT server\n");
  boolean isConnected = false;
  int i = 0;
  do {
    isConnected = mqtt.connect(mqttClientId, mqttUser, mqttPassword, "test/lostcom", 0, false, "yep");
    if (!isConnected)
      delay(50);
    i++;
  } while (!isConnected && i < 5);
  if (isConnected)
    DEBUG_MSG("Connected to MQTT server %i \n", i);
  return isConnected;
}

void makeFreeRoom() {
  if(stateData.nbvals < MaxNumberOfValue()) {
    DEBUG_MSG("No need to make free space\n");
    return;
  }
  DEBUG_MSG("Make free space\n");
  char buffer[(stateData.nbSensors * stateData.nbvals) * sizeof(float) + 4];
  float* temps = GetAlignedBuffer(buffer);
  ESP.rtcUserMemoryRead(firstDataOffset, (uint32_t*) temps, sizeof(float) * stateData.nbSensors * stateData.nbvals);
  //byte* newPtr = (byte*)temps;
  // forget older value sizeof(float) *
  temps += stateData.nbSensors;
  stateData.nbvals--;
  ESP.rtcUserMemoryWrite(firstDataOffset, (uint32_t*) temps, sizeof(float) * stateData.nbSensors * stateData.nbvals);
  stateData.stateFlags &= ~SEND_DATA_THIS_LOOP;
}

float* GetAlignedBuffer(char * buffer) {
  if ((int)buffer % 4 != 0 )
    DEBUG_MSG("Not alligned\n");
  return (float*)  (buffer +  ((4 - ((int)buffer % 4)) % 4));
}

int MaxNumberOfValue() {
  if(stateData.nbSensors == 0)
    return MAX_KEPT_VALUE;
  return (512 - firstDataOffset * 4) / (sizeof(float) *  stateData.nbSensors) - 4;  
}


void setup() {

#ifdef DEBUG_ESP_PORT
  DEBUG_ESP_PORT.begin(74880);
#endif

  DEBUG_MSG("\nGO\n");
  startConversion();

  bool firstStart = true;
  rst_info * rstinfo = ESP.getResetInfoPtr();
  if (rstinfo->reason == REASON_DEEP_SLEEP_AWAKE)
    firstStart = false;
  if (firstStart) {
    DEBUG_MSG("First start.\n");
    DEBUG_MSG("MQTT BUFFER %i\n", MQTT_MAX_PACKET_SIZE);
    // Uncomment only for first prog
    // Wifi is automatic connect next time.
    // WiFi.mode(WIFI_STA);
    // WiFi.begin(ssidName, ssidPassword, 11);

    stateData.stateFlags = SEND_DATA_THIS_LOOP;
    stateData.nbSensors = sensors.getDeviceCount();
    stateData.nbvals = 0;
    stateData.nbCnxPb = 0;
  }
  else {
    ESP.rtcUserMemoryRead(stateDataOffset, (uint32_t*) &stateData, sizeof(stateData) / 4);
  }

  if (stateData.stateFlags & SEND_DATA_THIS_LOOP) {
    boolean result = waitWifi();
    result = result && connectMqtt();
    if (!result) {
      makeFreeRoom();
      stateData.nbCnxPb++;
      // if connection fail SEND_DATA_THIS_LOOP is clear
      stateData.stateFlags &= ~SEND_DATA_THIS_LOOP;
    } else {
      stateData.nbCnxPb=0;
    }
  }

  waitEndOfConversion();

  DeviceAddress address;

  if (!(stateData.stateFlags & SEND_DATA_THIS_LOOP)) {
    DEBUG_MSG("Do not send this time.\n");
    DEBUG_MSG("Manage %i.\n", stateData.nbSensors);
    int i = 0;
    char buffer[stateData.nbSensors * sizeof(float) + 4];
    float* temps = GetAlignedBuffer(buffer);
    while (oneWire.search(address) && i < stateData.nbSensors) {
      temps[i] = sensors.getTempC(address);
      DEBUG_MSG("Read Temp for memory : %s \n", String(temps[i]).c_str());
      i++;
    }
    int dataOffset = firstDataOffset + sizeof(float) * stateData.nbvals * stateData.nbSensors / 4;
    ESP.rtcUserMemoryWrite(dataOffset, (uint32_t*) temps, sizeof(float) * stateData.nbSensors);
    stateData.nbvals++;
    DEBUG_MSG("Temp store in memory at %i for %i byte.\n", dataOffset, sizeof(float) * stateData.nbSensors);
  } else {
    DEBUG_MSG("Read Stored temps.\n");
    char buffer[(stateData.nbSensors * stateData.nbvals) * sizeof(float) + 4];
    float* temps = GetAlignedBuffer(buffer);

    DEBUG_MSG("Temp read from RTC memory at %i for %i byte.\n", firstDataOffset, sizeof(float) * stateData.nbSensors * stateData.nbvals);
    ESP.rtcUserMemoryRead(firstDataOffset, (uint32_t*) temps, sizeof(float) * stateData.nbSensors * stateData.nbvals);
    int totalRetry = 0;
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
      topic = "test/hist/" + String(addressString);
      unsigned long now = millis();
      long runTime = now - startConv;
      for (int numVal = 0; numVal < stateData.nbvals; numVal++) {
        value = "{\"t\":-";
        value += (stateData.nbvals - numVal) * DEEPSLEEP * 1000 + runTime;
        value += ",\"v\":";
        value += String(temps[numVal * stateData.nbSensors + i ]) + "}";
        
        if(!mqtt.publish(topic.c_str(), value.c_str(), false)) {
          mqtt.disconnect();
          connectMqtt();
          totalRetry++;
          now = millis();
          runTime = now - startConv;
        }
      }
      value = "{\"t\":-" + String(runTime) + ",\"v\":" + String(lastVal) + "}";
      if(!mqtt.publish(topic.c_str(), value.c_str(), false)) {
          mqtt.disconnect();
          connectMqtt();
          totalRetry++;
      }
      i++;
    }
    // Read voltage
    DEBUG_MSG("Read voltage :");
    String value = String(analogRead(A0) * (320.0 / 100.0 / 1024.0));
    DEBUG_MSG("%s\n", value.c_str());
    mqtt.publish("test/volt", value.c_str(), true);
    mqtt.publish("test/totalRetry", String(totalRetry).c_str(), true);
    mqtt.disconnect();
    // reset number of values
    stateData.nbvals = 0;
    stateData.nbSensors = sensors.getDeviceCount();
    stateData.stateFlags &= ~SEND_DATA_THIS_LOOP;

    // Give some time to send data.
    // and notify with a blink
    pinMode(BLUE_LED, OUTPUT);
    digitalWrite(BLUE_LED, LOW);
    delay(100);
    digitalWrite(BLUE_LED, HIGH);
    delay(400);
  }
  // little fash to show we are alive.
  pinMode(BLUE_LED, OUTPUT);
  digitalWrite(BLUE_LED, LOW);
  int maxNbVals = MaxNumberOfValue();
  if (maxNbVals > MAX_KEPT_VALUE)
    maxNbVals = MAX_KEPT_VALUE;
  DEBUG_MSG("Nbval %i/%i \n", stateData.nbvals, maxNbVals);
  if (stateData.nbvals >=  maxNbVals ) {
    DEBUG_MSG("Send next time.\n");
    stateData.stateFlags |= SEND_DATA_THIS_LOOP;
  }
  DEBUG_MSG("Store state.\n");
  ESP.rtcUserMemoryWrite(stateDataOffset, (uint32_t*) &stateData, sizeof(stateData));

  DEBUG_MSG("Set output Hight\n");
  // set to output and hight state
  pinMode(ONE_WIRE_BUS, OUTPUT);
  digitalWrite(ONE_WIRE_BUS, HIGH);

  digitalWrite(BLUE_LED, HIGH);
  DEBUG_MSG("Go to deep sleep.\n");

  // ajust delay to keep it at desired frequency.
  unsigned long now = millis();
  long runTime = now - startConv;
  // Check that runtime do not take to long
  if(runTime >= DEEPSLEEP * 1000) 
    runTime = (DEEPSLEEP - 1) * 1000;
    
  if (stateData.stateFlags & SEND_DATA_THIS_LOOP) {
    // if to much error sleep a long time.
    if(stateData.nbCnxPb > 20)
      ESP.deepSleep(10 * DEEPSLEEP * 1000000);
    else if(stateData.nbCnxPb > 0)
      ESP.deepSleep(DEEPSLEEP * 1000000 - runTime * 1000 );
    else // a test to limit consumtion
      ESP.deepSleep(DEEPSLEEP * 1000000 - runTime * 1000, RF_NO_CAL);
  } else {
    // DEEP SLEEP AND WAKE without WIFI
    ESP.deepSleep(DEEPSLEEP * 1000000 - runTime * 1000, RF_DISABLED);
  }
}

void loop() {
  // we should not come here
  ESP.deepSleep(DEEPSLEEP * 1000000);
}
