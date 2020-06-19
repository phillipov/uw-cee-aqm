// backend
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <NTPClient.h>
#include <ESP8266httpUpdate.h>
#include <ArduinoJson.h>
#include "server-secrets.h"

// hardware
#include <Wire.h>
#include <SoftwareSerial.h>

// sensors
#include <Plantower_PMS7003.h>
#include <Adafruit_BME280.h>
#include <RTClib.h>

static const char* VERSION = "0.0.1";
static const int MAX_PAYLOAD = 2048;

SoftwareSerial pmsSerial(13, 15, false);
Plantower_PMS7003 pms;
Adafruit_BME280 bme;
RTC_DS3231 rtc;

WiFiClient wifi;
WiFiManager wifiManager;
PubSubClient mqtt(wifi);

char sensorID[18];
DynamicJsonDocument jsonDoc(MAX_PAYLOAD);

bool mqtt_on = false;
bool pms_on = false;
bool bme_on = false;
bool rtc_on = false;

void setup() {
  setupGlobal();
  setupMQTT();
  setupOTA();
  setupPMS();
  setupBME();
  setupRTC();
  Serial.println("Setup complete.");
  Serial.println();
}

void loop() {
  collect();
  report();

  if(mqtt_on)
    mqtt.loop();
  delay(5000);
}

void collect()
{
  recordPMS();
  recordBME();
  recordRTC();
}

void report()
{
  jsonDoc["sensor_id"] = sensorID;
  
  if(mqtt_on)
  {
    char payload[MAX_PAYLOAD];
    serializeJson(jsonDoc, payload);
    
    mqtt.publish("sensors/data", payload);
  }
  else
  {
    serializeJsonPretty(jsonDoc, Serial);
  }
  jsonDoc.clear();
}

void otaCallback(char* topic, byte* payload, unsigned int length)
{
  // do not update if version is same
  if(memcmp(payload, VERSION, max(length, strlen(VERSION))) == 0)
    return;
  
  Serial.println("UPDATE RECIEVED. Updating...");
  ESPhttpUpdate.update(ServerSecrets::otaHost, ServerSecrets::otaPort, "/update.bin");
}

void setupGlobal()
{
  Serial.begin(9600);
  Serial.println();
  Wire.begin(4, 5);

  // set sensor ID to MAC address
  WiFi.macAddress().toCharArray(sensorID, 18);
  Serial.print(sensorID);
  Serial.print(" with version ");
  Serial.println(VERSION);
  
  WiFi.mode(WIFI_STA);
  wifiManager.setDebugOutput(false);
  wifiManager.autoConnect("AQM-AutoConnect");
}

void setupMQTT()
{
  mqtt.setBufferSize(MAX_PAYLOAD);
  mqtt.setServer(ServerSecrets::mqttHost, ServerSecrets::mqttPort);
  Serial.println("Connecting to MQTT broker...");
  
  while(!mqtt.connected())
  {
    mqtt.connect(sensorID, ServerSecrets::mqttUser, ServerSecrets::mqttPassw);
    if(mqtt.state() == 0)
    {
      mqtt_on = true;
      Serial.println("MQTT broker connected.");
      break;
    }
    else if(mqtt.state() > 0) // good connection, server refused
    {
      mqtt_on = false;
      Serial.print("MQTT broker connection failed, rc=");
      Serial.println(mqtt.state());
      break;
    }
    delay(200);
  }
}

void setupOTA()
{
  if(!mqtt_on)
    return;

  Serial.println("Setting up OTA updates...");
  mqtt.setCallback(otaCallback);
  while(!mqtt.subscribe("sensors/update"))
    delay(100);
  Serial.println("OTA updates setup.");
}

void setupPMS()
{
  pmsSerial.begin(9600);
  pms.init(&pmsSerial);
  pms_on = true;
}

void setupBME()
{
  bme_on = bme.begin(0x76);
  if(!bme_on)
  {
    Serial.println("BME offline.");
  }
}

void setupRTC()
{
  rtc_on = rtc.begin();
  if(!rtc_on)
  {
    Serial.println("RTC offline.");
  }
  else
  {
    Serial.println("Setting RTC time...");
    
    WiFiUDP ntpUDP;
    NTPClient timeClient(ntpUDP, "pool.ntp.org");
    timeClient.begin();
    
    if(timeClient.update())
    {
      DateTime stamp(timeClient.getEpochTime());
      rtc.adjust(stamp);
      
      rtc_on = true;
      Serial.println("RTC time set.");
    }
    else
    {
      rtc_on = false;
      Serial.println("Could not connect to NTP time server.");
    }
    timeClient.end();
  }
}

void recordPMS()
{
  if(!pms_on)
    return;
  
  while(true)
  {
    pms.updateFrame();
    
    if(pms.hasNewData())
    {
      jsonDoc["pm_1_0"] = pms.getPM_1_0();
      jsonDoc["pm_2_5"] = pms.getPM_2_5();
      jsonDoc["pm_10_0"] = pms.getPM_10_0();
      break;
    }
    yield();
  }
}

void recordBME()
{
  if(!bme_on)
    return;
  
  float temp = bme.readTemperature();
  float hum = bme.readHumidity();
  
  jsonDoc["temp"] = temp;
  jsonDoc["hum"] = hum;
}

void recordRTC()
{
  if(!rtc_on)
    return;
  
  DateTime now = rtc.now();
  jsonDoc["time"] = now.timestamp(DateTime::TIMESTAMP_FULL);
}
