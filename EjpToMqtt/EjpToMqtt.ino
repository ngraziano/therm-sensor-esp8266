#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <ESP8266LLMNR.h>
#include <ESP8266mDNS.h>

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ESP8266HTTPUpdateServer.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>
#include <LibTeleinfo.h>
#include <Ticker.h>

Ticker ticker;
Ticker tickerTIC;



#define BLUE_LED 2
#define TIC_LED 5

#ifdef DEBUG_ESP_PORT
#define DEBUG_MSG(...) DEBUG_ESP_PORT.printf( __VA_ARGS__ )
#else
#define DEBUG_MSG(...)
#endif

const char* host = "esp-tic";
const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = "nico";

TInfo          tinfo; // Teleinfo object

// Web server
ESP8266WebServer httpServer(80);
// Update Server
ESP8266HTTPUpdateServer httpUpdater;


void buildinLedTick()
{
  //toggle state
  int state = digitalRead(BLUE_LED);  // get the current state of GPIO1 pin
  digitalWrite(BLUE_LED, !state);     // set pin to the opposite state
}

void blinkBuiltInLed()
{
  //show led
  digitalWrite(BLUE_LED, LOW);
  // hide after 0.3
  ticker.once(0.3,buildinLedTick);
}

void TICTick()
{
  //toggle state
  int state = digitalRead(TIC_LED);  // get the current state of GPIO1 pin
  digitalWrite(TIC_LED, !state);     // set pin to the opposite state
}


void blinkTICLed()
{
  //show led
  digitalWrite(TIC_LED, HIGH);
  // hide after 0.3
  tickerTIC.once(0.3,TICTick);
}



char mqtt_server[40];
char mqtt_port[6] = "1883";
char mqtt_user[30];
char mqtt_password[30];

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

WiFiClient espClient;
PubSubClient client(espClient);


void setup() {


  
  // put your setup code here, to run once:
  Serial.begin(74880);
  
  // Activate Builtin LED 
  pinMode(BLUE_LED, OUTPUT);
  // start ticker with 0.6 because we start in AP mode and try to connect
  ticker.attach(0.6, buildinLedTick);
  
  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(mqtt_user, json["mqtt_user"]);
          strcpy(mqtt_password, json["mqtt_password"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }



  //WiFiManager
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 5);
  WiFiManagerParameter custom_mqtt_user("user", "mqtt user", mqtt_user, 30);
  WiFiManagerParameter custom_mqtt_password("passwor", "mqtt passwod", mqtt_password, 30);

  
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  //add all your parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_mqtt_user);
  wifiManager.addParameter(&custom_mqtt_password);
  
  wifiManager.setTimeout(300);

  //reset saved settings
  //wifiManager.resetSettings();
  
  //set custom ip for portal
  //wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if(!wifiManager.autoConnect("AutoConnectAP")) {
    //reset and try again.
    ESP.restart();  
  }
  
  
  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(mqtt_user, custom_mqtt_user.getValue());
  strcpy(mqtt_password, custom_mqtt_password.getValue());

  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["mqtt_user"] = mqtt_user;
    json["mqtt_password"] = mqtt_password;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save

  }

  LLMNR.begin(host);
  // MDNS
  MDNS.begin(host);

  // MQTT config
  int mqtt_port_number = atoi(mqtt_port);
  client.setServer(mqtt_server, mqtt_port_number);
  client.setCallback(callback);
  
  // Update Server
  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);
  
  // Last blink for builtin led
  ticker.detach();
  blinkBuiltInLed();
  // One blink for TIC LED
  pinMode(TIC_LED, OUTPUT);
  blinkTICLed();

   // Init teleinfo
  tinfo.init();
  // Attacher les callback dont nous avons besoin
  // pour cette demo, toutes
  //tinfo.attachADPS(ADPSCallback);
  tinfo.attachData(DataCallback);
  tinfo.attachNewFrame(NewFrame);
  //tinfo.attachUpdatedFrame(UpdatedFrame);
  blinkTICLed();

  connectMqtt();
  // Serial mode pour teleinfo
  Serial.begin(1200, SERIAL_7E1);

}

/* ======================================================================
Function: NewFrame 
Purpose : callback when we received a complete teleinfo frame
Input   : linked list pointer on the concerned data
Output  : - 
Comments: -
====================================================================== */
void NewFrame(ValueList * me)
{
  blinkTICLed();
}

/* ======================================================================
Function: DataCallback 
Purpose : callback when we detected new or modified data received
Input   : linked list pointer on the concerned data
          current flags value
Output  : - 
Comments: -
====================================================================== */
void DataCallback(ValueList * me, uint8_t  flags)
{
  


//  if (flags & TINFO_FLAGS_ADDED) 
//    Serial.print(F("NEW -> "));

//  if (flags & TINFO_FLAGS_UPDATED)
//    Serial.print(F("MAJ -> "));

  // connect if necessary and return on fail.
  if(!client.connected() && !connectMqtt())
      return;
  blinkBuiltInLed();

  // Build topic
  String topic = "cpi/" + String(me->name);
  client.publish(topic.c_str(), me->value, true);
  
  // Display values
  // Serial.print(me->name);
  // Serial.print("=");
  // Serial.println(me->value);
}

void callback(char* topic, byte* payload, unsigned int length) {

  blinkBuiltInLed();
  String payloadString;
  for (int i = 0; i < length; i++) {
    payloadString+=(char)payload[i];
  }

  if(payloadString == "RESET") {
    client.publish("cpi/connect", "RESET", false);
    ESP.restart();
  }

}

boolean connectMqtt() {
  // connect MQTT
  DEBUG_MSG("try to connect to MQTT server\n");
  boolean isConnected = false;
  isConnected = client.connect("ESP-CPI", mqtt_user, mqtt_password, "cpi/lostcom", 0, false, "yep");
  if (isConnected) {
    DEBUG_MSG("Connected to MQTT server \n");
    client.publish("cpi/connect", "CONNECT", false);
    client.subscribe("cpi/cmd");    
  }
  else
  {
    delay(1000);
  }
  return isConnected;
}


void loop() {

  static char c;

  // On a reçu un caractère ?
  if ( Serial.available() )
  {
    c = Serial.read() ;
    //Serial.print(c & 0x7F);

    // Gerer
    tinfo.process(c);
  }

  // handle MQTT
  client.loop();
  // handle WEB 
  httpServer.handleClient();


}
