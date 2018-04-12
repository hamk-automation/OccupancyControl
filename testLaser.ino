#include <FS.h>

#include <EEPROM.h> //Store counter in EEPROM memory to avoid sudden reset
#include <Wire.h>// I2C is used to communicate with sensors
#include <VL53L0X.h>// Sensor Library
#include <Ticker.h>

#include <ESP8266WiFi.h>// WiFi Library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> 
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>

#define DAQ_INTERVAL 60 //5 minutes
#define timeLimit 3000 //3 seconds
#define upperLimit 1800
#define lowerLimit 200

#define sensor1Shutdown D6
#define sensor2Shutdown D5

VL53L0X sensor1;
VL53L0X sensor2;
Ticker DAQTimer;

int rangeLimit;
enum {phase0, phase1A, phase1B, phase1C, phase1D, phase1E, phase2A, phase2B, phase2C, phase2D, phase2E}; // counting phases of each sensor 
volatile int dir1,dir2;
volatile int countIn, countOut;
volatile bool measurementFlag= true;
char sensorID[34] = "";
bool shouldSaveConfig = false;

void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, HIGH);
  Wire.begin();
  wifiInit();
  sensorsInit(); 
  DAQTimer.attach(DAQ_INTERVAL, setMeasurementFlag);
}

void loop() {
  runDAQ(); //Run   
  int nowIn= countIn;
  int nowOut= countOut;
  static int prevIn;
  static int prevOut;
//  if(measurementFlag){ //Ticker m
//    postData();
//  }

  if(nowIn!=prevIn || nowOut!=prevOut){
    postData();
  }
  
  prevIn= nowIn;
  prevOut=nowOut;

  yield();
}

void runDAQ(){
  unsigned long last1, last2;
  static bool sensor1state=HIGH;
  static bool sensor2state=HIGH;

  int sensor1Distance = sensor1.readRangeContinuousMillimeters();
  int sensor2Distance = sensor2.readRangeContinuousMillimeters();
  
  if(dir1==phase0 && dir2==phase0){
    distanceSampling(sensor1Distance);
  }

  
  
  sensor1state=(sensor1Distance< rangeLimit && sensor1Distance > 100) ? LOW : HIGH;
  sensor2state=(sensor2Distance< rangeLimit && sensor2Distance > 100) ? LOW : HIGH;

 //.........................CountIn phases..................................//
  if((sensor1state == LOW && dir2==phase2A && sensor2state==HIGH && dir1==phase1A) || (sensor1state==LOW && sensor2state==LOW && sensor1Distance < sensor2Distance && dir2==phase2A && dir1==phase1A)){
    dir1= phase1B;
    last1=millis();
    Serial.println("phase 1B begins");
  }
  
  if(dir1  == phase1B && sensor2state==LOW && sensor1state==LOW){
    dir1= phase1C;
    Serial.println("phase 1C begins");
  }
  
  if(dir1 == phase1C && sensor2state==HIGH && sensor1state==LOW){
    dir1=phase1A;
    countOut++;
    Serial.println("Reset to phase1A from false");
  }
  if(dir1 == phase1C && sensor2state==HIGH && sensor1state == HIGH){
    countIn++;
    dir1=phase1A;
    Serial.println("Reset to phase 1A");
  }
//  if((millis()-last1>timeLimit)&& dir1==phase1B){ //timeout
//    dir1=phase1A;
//  }
//--------------------------CountOut phases---------------------------------//
  if(sensor2state == LOW && dir1==phase1A && sensor1state == HIGH && dir2==phase2A){
    dir2= phase2B;
    last2= millis();
    Serial.println("phase 2B begins");
  }
  if(dir2  == phase2B && sensor1state==LOW && sensor2state== LOW){
    dir2= phase2C;
    Serial.println("phase 2C begins");
  }
  if(dir2== phase2C && sensor1state==HIGH && sensor2state==LOW){
    dir2=phase2A;
    countIn++;
    Serial.println("Reset to phase2A from false");
  }
  if(dir2 == phase2C && sensor1state==HIGH && sensor2state==HIGH){
    countOut++;
    dir2=phase2A;
    Serial.println("Reset to phase2A");
  }
//  if((millis()-last2>timeLimit) && dir2==phase2B){ //timeout
//    dir2=phase2A;
//  }
  
  if (sensor1.timeoutOccurred() ||sensor2.timeoutOccurred()) {
    delay(1000);
    ESP.restart();
  }

  Serial.print("Sensor1: ");
  Serial.print(sensor1Distance);
  Serial.print("\t");
  Serial.print("Sensor2: ");
  Serial.print(sensor2Distance);  
  Serial.print("\t");
  Serial.print("Count In: ");
  Serial.print(countIn);
  Serial.print("\t");
  Serial.print("Count Out: ");
  Serial.print(countOut);
  Serial.print("\n");
}

void sensorsInit(){
  pinMode(sensor2Shutdown, OUTPUT);
  pinMode(sensor1Shutdown, OUTPUT);

  delay(10);
  pinMode(sensor1Shutdown, INPUT);
  sensor1.setAddress(41);
  
  delay(10);
  
  pinMode(sensor2Shutdown, INPUT);
  sensor2.setAddress(50);
  
  sensor1.init();
  sensor2.init();

  sensor1.setTimeout(500);
  sensor2.setTimeout(500);

  sensor1.startContinuous();
  sensor2.startContinuous();

  dir1=phase0;
  dir2=phase0;
}

void wifiInit() {
  Serial.println("mounting FS...");
//  SPIFFS.format();
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
          strcpy(sensorID, json["sensorID"]);

        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  
  WiFiManagerParameter custom_sensorID("sensorID", "Location", sensorID, 32);
  WiFiManager wifiManager;
//  wifiManager.resetSettings();

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.addParameter(&custom_sensorID);
  
  wifiManager.setTimeout(120);
  wifiManager.setConnectTimeout(30);


  if (!wifiManager.autoConnect(String(ESP.getChipId()).c_str())) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.restart();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");  

  //save the custom parameters to FS
  if (shouldSaveConfig) {
  strcpy(sensorID, custom_sensorID.getValue());
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["sensorID"] = sensorID;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());
}

void postData(){
  digitalWrite(LED_BUILTIN, LOW);
  HTTPClient http;
  http.setReuse(true);
  static char msg[50];
  static int port = 443;
  String fingerprint = "d0ef874071f9f864abf5c1dc6376b591ee989866";
  String host = "konttiserver.ddns.net";
  String urlAPI = "/api/v1/hamk/tung";

  snprintf(msg, 75, "{\"countIn\": %lu, \"countOut\": %lu,\"id\": \"%s\"}", countIn, countOut,sensorID);
  Serial.println(msg);
  http.begin(host, port, urlAPI, fingerprint);
  http.addHeader("Content-Type", "application/json");
  int postcode = http.POST(msg);

  String payload = http.getString();
  http.end();
  Serial.println("Sent");
  digitalWrite(LED_BUILTIN, HIGH);
  measurementFlag = false;
}

void distanceSampling(int val){
 static int i=0;
 static unsigned long last=millis();
 static unsigned long int distance=0;
 Serial.printf("i is: %d \t distance is: %d\n", i,distance);
 if(val <upperLimit && val >lowerLimit){
    distance+= val;
    i++;
 }
 if(millis()-last >= 6000){
  rangeLimit = (i == 0) ? upperLimit : (distance / i) - lowerLimit;
  dir1= phase1A;
  dir2= phase2A;
  Serial.printf("Range now is: %d\n",rangeLimit);
  digitalWrite(LED_BUILTIN, HIGH);
  return;
 }
 delay(500);
}

void setMeasurementFlag() {
  measurementFlag = true;
}


