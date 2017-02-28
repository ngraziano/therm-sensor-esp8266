#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <PubSubClient.h>
#include <Adafruit_NeoPixel.h>

#include <Ticker.h>
Ticker ticker;



#define DEBUG_MSG(...) Serial.printf( __VA_ARGS__ )
//#define DEBUG_MSG(...)


#define CPI_LED 5
#define PIXEL_PIN       5

// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS      50

// When we setup the NeoPixel library, we tell it how many pixels, and which pin to use to send signals.
// Note that for older NeoPixel strips you might need to change the third parameter--see the strandtest
// example for more information on possible values.
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIXEL_PIN, NEO_GRB + NEO_KHZ800);



char mqtt_server[40];
char mqtt_port[6] = "1883";
char mqtt_user[30];
char mqtt_password[30];

//flag for saving data
bool shouldSaveConfig = false;
long lastMsg = 0;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

WiFiClient espClient;
PubSubClient client(espClient);

void tick()
{
  //toggle state
  int state = digitalRead(BUILTIN_LED);  // get the current state of GPIO1 pin
  digitalWrite(BUILTIN_LED, !state);     // set pin to the opposite state
}

void blinkLed()
{
  //show led
  digitalWrite(BUILTIN_LED, LOW);
  // hide after 0.6
  ticker.once(0.2,tick);
}


void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  //set led pin as output
  pinMode(BUILTIN_LED, OUTPUT);
  // start ticker with 0.6 because we start in AP mode and try to connect
  ticker.attach(0.6, tick);
  
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
    

    //reset saved settings
    //wifiManager.resetSettings();
    
    //set custom ip for portal
    //wifiManager.setAPStaticIPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

    //fetches ssid and pass from eeprom and tries to connect
    //if it does not connect it starts an access point with the specified name
    //here  "AutoConnectAP"
    //and goes into a blocking loop awaiting configuration
    wifiManager.autoConnect("AutoConnectAP");
    //or use this for auto generated name ESP + ChipID
    //wifiManager.autoConnect();

    
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

  int mqtt_port_number = atoi(mqtt_port);
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  ticker.detach();

  digitalWrite(BUILTIN_LED, HIGH);
  
  pinMode(PIXEL_PIN, OUTPUT);
  pixels.begin();

  Serial.println("Setup finish");
  
}


void callback(char* topic, byte* payload, unsigned int length) {
  blinkLed();
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  String payloadString;
  for (int i = 0; i < length; i++) {
    payloadString+=(char)payload[i];
    //Serial.print((char)payload[i]);
  }
  Serial.print(payloadString);
  Serial.println();

  

  DynamicJsonBuffer jsonBuffer;
  JsonArray& json = jsonBuffer.parseArray(payloadString);
  //json.printTo(Serial);

  int nbElem = json.size();

  int NBLed = 50;

  for(int i= NBLed-1; i >= nbElem ; i--) {

      uint32_t color = pixels.getPixelColor(i - nbElem);
      pixels.setPixelColor(i, color);
  }

  for(int i = 0; i < nbElem; i++) {
    pixels.setPixelColor(i, pixels.Color(json[i]["red"],json[i]["green"],json[i]["blue"])); 
  }
  

  pixels.show();
}

boolean connectMqtt() {
  // connect MQTT
  DEBUG_MSG("try to connect to MQTT server\n");
  boolean isConnected = false;
  isConnected = client.connect("ESP-LED", mqtt_user, mqtt_password, "cpi/lostcom", 0, false, "yep");
  if (isConnected) {
    DEBUG_MSG("Connected to MQTT server \n");
    client.subscribe("leds/cmd");    
  }
  else
  {
    delay(1000);
  }
  return isConnected;
}




void loop() {

  // connect if necessary and return on fail.
  if(!client.connected() && !connectMqtt())
      return;
      
  client.loop(); 

  long now = millis();
  if (now - lastMsg > 2000) {
    lastMsg = now;
    client.publish("leds/coucou", "coucou", false);
  }
  
}
