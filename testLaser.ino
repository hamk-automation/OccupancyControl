#include <EEPROM.h> //Store counter in EEPROM memory to avoid sudden reset
#include <Wire.h>// I2C is used to communicate with sensors
#include <VL53L0X.h>// Sensor Library
#include <ESP8266WiFi.h>// WiFi Library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> 
#include <ESP8266HTTPClient.h>

#define timeLimit 3000 //3 seconds
#define upperLimit 1800
#define lowerLimit 200
#define countIn 0x01 //EEPROM addresses
#define countOut 0x09
#define sensor1Shutdown D5
#define sensor2Shutdown D6

VL53L0X sensor1;
VL53L0X sensor2;


int nowIn, nowOut, prevIn, prevOut, rangeLimit;
unsigned long last1,last2;

enum {phase0, phase1A, phase1B, phase1C, phase2A, phase2B, phase2C}; // counting phases of each sensor 

volatile int dir1,dir2;

void setup() {
  Serial.begin(115200);
  Wire.begin();
  EEPROM.begin(128);
  sensorsInit(); 
  wifiInit();
}

void loop() {
  nowIn= eeGetInt(countIn);
  nowOut= eeGetInt(countOut);
  
  runDAQ(); //Run   
  if(nowIn != prevIn || nowOut != prevOut){ //MQTT push 
    postData(nowIn,nowOut);
  }
  
  prevIn=nowIn;
  prevOut=nowOut;
  delay(10);
}

void runDAQ(){
  bool sensor1state;
  bool sensor2state;

  int sensor1Distance = sensor1.readRangeSingleMillimeters();
  int sensor2Distance = sensor2.readRangeSingleMillimeters();
  
  if(dir1==phase0 && dir2==phase0){
    distanceSampling(sensor1Distance);
  }

  
  if(sensor1Distance< rangeLimit && sensor1Distance > lowerLimit){
    sensor1state= LOW;
  }else{
    sensor1state= HIGH;
  }
  if(sensor2Distance<rangeLimit && sensor2Distance > lowerLimit){
    sensor2state= LOW;
  } else{
    sensor2state= HIGH;
  }

 //.........................CountIn phases..................................//

  if(sensor1state == LOW && dir2==phase2A){
    dir1= phase1B;
    last1=millis();
  }
  if(dir1  == phase1B && sensor2state==LOW){
    dir1= phase1C;
  }
  if(dir1 == phase1C && sensor2state==HIGH){
    eeWriteInt(countIn, eeGetInt(countIn)+1);
    dir1=phase1A;
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
    eeWriteInt(countOut, eeGetInt(countOut)+1);
    dir2=phase2A;
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

  sensor1.setSignalRateLimit(0.1);
  sensor1.setVcselPulsePeriod(VL53L0X::VcselPeriodPreRange, 18);
  sensor1.setVcselPulsePeriod(VL53L0X::VcselPeriodFinalRange, 14);

  sensor2.setSignalRateLimit(0.1);
  sensor2.setVcselPulsePeriod(VL53L0X::VcselPeriodPreRange, 18);
  sensor2.setVcselPulsePeriod(VL53L0X::VcselPeriodFinalRange, 14);

  sensor1.setMeasurementTimingBudget(20000);
  sensor2.setMeasurementTimingBudget(20000);
  dir1=phase0;
  dir2=phase0;
}
void wifiInit() {
  WiFiManager wifiManager;
//  wifiManager.resetSettings();
  wifiManager.autoConnect("VictoryDemo");
  Serial.println("connected...yeey :)");
}
void postData(int comeIn, int comeOut){
  
  HTTPClient http;
  static char msg[50];
  String fingerprint="d0ef874071f9f864abf5c1dc6376b591ee989866";

  snprintf(msg, 75, "{\"countIn\": %d, \"countOut\": %d,\"id\": \"hamk\"}", comeIn, comeOut);
  Serial.print("connecting to konttiserver.ddns.net");
  http.begin("konttiserver.ddns.net", 443, "/api/v1/hamk/tung",fingerprint);
  http.addHeader("Content-Type", "application/json");
  int postcode = http.POST(msg);
  Serial.println(postcode);
  String payload = http.getString();
  Serial.println(payload);
  http.end();
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
  if(i==0){
    rangeLimit= upperLimit;
  }else{
    rangeLimit= (distance/i)-300;
  }
  dir1= phase1A;
  dir2= phase2A;
  Serial.printf("Range now is: %d\n",rangeLimit);
  return;
 }
 delay(500);
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

