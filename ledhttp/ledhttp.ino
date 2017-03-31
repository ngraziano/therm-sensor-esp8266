#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)

#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <ESP8266LLMNR.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <Adafruit_NeoPixel.h>

#include <Ticker.h>
Ticker ticker;



#define DEBUG_MSG(...) Serial.printf( __VA_ARGS__ )
//#define DEBUG_MSG(...)



#define PIXEL_PIN       5
#define BUTTON_PIN       4

// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS      50

// When we setup the NeoPixel library, we tell it how many pixels, and which pin to use to send signals.
// Note that for older NeoPixel strips you might need to change the third parameter--see the strandtest
// example for more information on possible values.
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIXEL_PIN, NEO_GRB + NEO_KHZ800);

ESP8266WebServer wwwserver(80);
ESP8266HTTPUpdateServer httpUpdater;

const char* host = "espled";
const char* update_path = "/firmware";
const char* update_username = "admin";
const char* update_password = "nico";

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

      //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;
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

  // stop blink
  ticker.detach();
  digitalWrite(BUILTIN_LED, HIGH);

  MDNS.begin(host);
  LLMNR.begin(host);
  
  pinMode(PIXEL_PIN, OUTPUT);
  pixels.begin();

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  
  httpUpdater.setup(&wwwserver, update_path, update_username, update_password);
  wwwserver.on("/led", HTTP_PUT, handlePut);
  wwwserver.begin();
  
  MDNS.addService("http", "tcp", 80);
  
  Serial.println("Setup finish");
  
}

void movePixel(int nbElem) {
  for(int i= NUMPIXELS-1; i >= nbElem ; i--) {
      uint32_t color = pixels.getPixelColor(i - nbElem);
      pixels.setPixelColor(i, color);
  }
}

void handlePut() {
  blinkLed();

  DEBUG_MSG("Handle PUT\n");
  
  DynamicJsonBuffer jsonBuffer;
  JsonArray& json = jsonBuffer.parseArray(wwwserver.arg("plain"));

  DEBUG_MSG("Handle message %s\n", wwwserver.arg("plain").c_str());
  
  int nbElem = json.size();
  DEBUG_MSG("Got %i colors\n",nbElem);
  
  movePixel(nbElem);

  for(int i = 0; i < nbElem; i++) {
    pixels.setPixelColor(i, pixels.Color(json[i]["red"],json[i]["green"],json[i]["blue"])); 
  }

  
  wwwserver.send(200, "application/json", "{\"status\": \"OK\"}");

  pixels.show();
}




int lastButtonValue;

void loop() {

  wwwserver.handleClient();
  int buttonValue = digitalRead(BUTTON_PIN);
  if(lastButtonValue != buttonValue)
  {
    lastButtonValue = buttonValue;
    if(buttonValue == LOW) {
      DEBUG_MSG("Button down\n");
      movePixel(1);
      pixels.setPixelColor(0, pixels.Color(255,0,0)); 
      pixels.show();
    }
  }
}
