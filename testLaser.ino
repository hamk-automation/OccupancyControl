#include <EEPROM.h> //Store counter in EEPROM memory to avoid sudden reset
#include <Wire.h>// I2C is used to communicate with sensors
#include <VL53L0X.h>// Sensor Library
#include <Ticker.h>

#include <ESP8266WiFi.h>// WiFi Library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> 
#include <ESP8266HTTPClient.h>

#define DAQ_INTERVAL 5
#define timeLimit 3000 //3 seconds
#define upperLimit 1800
#define lowerLimit 200
#define countIn 0x01 //EEPROM addresses
#define countOut 0x09
#define sensor1Shutdown D6
#define sensor2Shutdown D5

VL53L0X sensor1;
VL53L0X sensor2;
Ticker DAQTimer;

int rangeLimit;
enum {phase0, phase1A, phase1B, phase1C, phase2A, phase2B, phase2C}; // counting phases of each sensor 
volatile int dir1,dir2;
volatile bool measurementFlag= true;

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, HIGH);
  Wire.begin();
  EEPROM.begin(128);
  sensorsInit(); 
  wifiInit();
  DAQTimer.attach(DAQ_INTERVAL, setMeasurementFlag);
}

void loop() {
  runDAQ(); //Run   
  
  if(measurementFlag){ 
    postData();
  }

  delay(10);
}

void runDAQ(){
  unsigned long last1, last2;
  bool sensor1state;
  bool sensor2state;

  int sensor1Distance = sensor1.readRangeContinuousMillimeters();
  int sensor2Distance = sensor2.readRangeContinuousMillimeters();
  
  if(dir1==phase0 && dir2==phase0){
    distanceSampling(sensor1Distance);
  }
  sensor1state=(sensor1Distance< rangeLimit && sensor1Distance > lowerLimit) ? LOW : HIGH;
  sensor2state=(sensor2Distance< rangeLimit && sensor2Distance > lowerLimit) ? LOW : HIGH;

 //.........................CountIn phases..................................//
  if(sensor1state == LOW && dir2==phase2A){
    dir1= phase1B;
    last1=millis();
  }
  if(dir1  == phase1B && sensor2state==LOW){
    dir1= phase1C;
  }
  if(dir1 == phase1C && sensor2state==HIGH){
    digitalWrite(LED_BUILTIN, LOW);
    eeWriteInt(countIn, eeGetInt(countIn)+1);
    dir1=phase1A;
    digitalWrite(LED_BUILTIN, HIGH);
  }
  if((millis()-last1>timeLimit)&& dir1==phase1B){ //timeout
    dir1=phase1A;
  }
//--------------------------CountOut phases---------------------------------//
  if(sensor2state == LOW && dir1==phase1A){
    dir2= phase2B;
    last2= millis();
  }
  if(dir2  == phase2B && sensor1state==LOW){
    dir2= phase2C;
  }
  if(dir2 == phase2C && sensor1state==HIGH){
    digitalWrite(LED_BUILTIN, LOW);
    eeWriteInt(countOut, eeGetInt(countOut)+1);
    dir2=phase2A;
    digitalWrite(LED_BUILTIN, HIGH);
  }
  if((millis()-last2>timeLimit) && dir2==phase2B){ //timeout
    dir2=phase2A;
  }
  
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
  Serial.print(eeGetInt(countIn));
  Serial.print("\t");
  Serial.print("Count Out: ");
  Serial.print(eeGetInt(countOut));
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
  WiFiManager wifiManager;
//  wifiManager.resetSettings();
  wifiManager.autoConnect("VictoryDemo");
  Serial.println("connected...yeey :)");
}

void postData(){
  HTTPClient http;
  http.setReuse(true);
  static char msg[50];
  static int port = 443;
  String fingerprint = "d0ef874071f9f864abf5c1dc6376b591ee989866";
  String host = "konttiserver.ddns.net";
  String urlAPI = "/api/v1/hamk/tung";
  int comeIn= eeGetInt(countIn);
  int comeOut= eeGetInt(countOut);

  snprintf(msg, 75, "{\"countIn\": %d, \"countOut\": %d,\"id\": \"hamk\"}", comeIn, comeOut);
  http.begin(host, port, urlAPI, fingerprint);
  http.addHeader("Content-Type", "application/json");
  int postcode = http.POST(msg);

  String payload = http.getString();
  http.end();
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
 if(millis()-last >= 5000){
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
//Since ESP8266 only stores EEPROM as a byte, we need to split value into low and high bits then store them
void eeWriteInt(int pos, int val) {
    byte* p = (byte*) &val;
    EEPROM.write(pos, *p);
    EEPROM.write(pos + 1, *(p + 1));
    EEPROM.write(pos + 2, *(p + 2));
    EEPROM.write(pos + 3, *(p + 3));
    EEPROM.commit();
}

int eeGetInt(int pos) {
    int val;
    byte* p = (byte*) &val;
    *p        = EEPROM.read(pos);
    *(p + 1)  = EEPROM.read(pos + 1);
    *(p + 2)  = EEPROM.read(pos + 2);
    *(p + 3)  = EEPROM.read(pos + 3);
    return val;
}

