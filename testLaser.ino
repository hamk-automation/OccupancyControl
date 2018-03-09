#include <EEPROM.h> //Store counter in EEPROM memory to avoid sudden reset
#include <Wire.h>// I2C is used to communicate with sensors
#include <VL53L0X.h>// Sensor Library
#include <ESP8266WiFi.h>// WiFi Library
#include <PubSubClient.h>// MQTT is used to communicate with Server


#define timeLimit 3000 //3 seconds
#define rangeLimit 500 //50 cm
#define countIn 0x01
#define countOut 0x09

VL53L0X sensor1;
VL53L0X sensor2;
WiFiClient wifi_client;
PubSubClient mqtt_client(wifi_client);

const int sensor1Shutdown= D6;
const int sensor2Shutdown= D5;
int nowIn, nowOut, prevIn, prevOut;
unsigned long last1,last2;
char msg[50];
enum {phase1A, phase1B, phase1C, phase2A, phase2B, phase2C}; // counting phases of each sensor 
/* Explanation:
    At start, both sensors are at phase1A and phase 2A.
    when a person is comming IN, hits sensor1, then phase1B starts.
    After that, if the person hits sensor2, releases sensor1, then starts phase1C.
    Finally, if the person completely gets IN, both sensors are released, countIn +1, then sensor1 restarts back to phase1A
*/
volatile int dir1,dir2;

const char* wifi_ssid  = "0948ea";
const char* wifi_password  = "275132971";
const char* mqtt_server = "192.168.0.19";

void setup() {
  
  Serial.begin(115200);
  Wire.begin();
  EEPROM.begin(512);
  sensorsInit(); 

  dir1=phase1A;
  dir2=phase2A;
  wifiInit();
  mqtt_client.setServer(mqtt_server, 1883);
  mqtt_client.setCallback(callback);
  Serial.printf("Count In: %d\tCount Out: %d \n",eeGetInt(countIn), eeGetInt(countIn));
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
  if(eeGetInt(countIn)==-1 || eeGetInt(countOut)==-1){
    EEPROM.write(countOut, 0);
    EEPROM.write(countIn, 0);
    EEPROM.commit();
  }
}
void loop() {
  if (!mqtt_client.connected()) {
    reconnect();
  }
  mqtt_client.loop();
  
  nowIn= eeGetInt(countIn);
  nowOut= eeGetInt(countOut);
  
  runDAQ(); //Run 
  
  if(nowIn != prevIn || nowOut != prevOut){ //MQTT push
    Serial.printf("Count In: %d\tCount Out: %d \n",nowIn, nowOut);
    snprintf(msg, 75, "{\"countIn\": %d, \"countOut\": %d}", nowIn, nowOut);
    mqtt_client.publish("khoa", msg); 
  }
  prevIn=nowIn;
  prevOut=nowOut;
}

void runDAQ(){
  bool sensor1state;
  bool sensor2state;

  int sensor1Distance = sensor1.readRangeSingleMillimeters();
  int sensor2Distance = sensor2.readRangeSingleMillimeters();

  if(sensor1Distance< rangeLimit){
    sensor1state= LOW;
  } else {sensor1state=HIGH;}
  
  if(sensor2Distance<rangeLimit){
    sensor2state= LOW;
  } else {sensor2state=HIGH;}

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
  
//  Serial.print("Sensor1: ");
//  Serial.print(sensor1Distance);
//  Serial.print("\t");
//  Serial.print("Sensor2: ");
//  Serial.println(sensor2Distance);  
//  Serial.print("\t");
//  Serial.print("Count In: ");
//  Serial.print(countIn);
//  Serial.print("\t");
//  Serial.print("Count Out: ");
//  Serial.print(countOut);
//  Serial.print("\n");

}
void wifiInit() {
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(wifi_ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(WiFi.status());
    Serial.print(".");
    Serial.println("");
  }
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}
void reconnect() {
  while (!mqtt_client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP8266_HAMK_WeatherStation";
    clientId += String(random(0xffff), HEX);
    if (mqtt_client.connect(clientId.c_str())) {
      Serial.println("connected");
      mqtt_client.subscribe("clear");
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}
void callback(String topic, byte* payload, unsigned int length) {
//  Serial.printf("Topic is: %s\n",topic);
  if(topic == "clear"){
    Serial.println("Clearing");
    EEPROM.write(countOut, 0);
    EEPROM.write(countIn, 0);
    EEPROM.commit();
  }
}

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
