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

static const char* HARDWARE_VERSION = "0.0.2";
static const char* SOFTWARE_VERSION = "0.0.3";
static const int MAX_PAYLOAD = 2048;

static const int PIN_SDA = 4;
static const int PIN_SCL = 5;
static const int PIN_PM_RX = 12;
static const int PIN_PM_TX = 13;

static const int LOOP_DELAY = 30000;

SoftwareSerial pmsSerial(PIN_PM_RX, PIN_PM_TX, false);
Plantower_PMS7003 pms;
Adafruit_BME280 bme;
RTC_DS3231 rtc;

WiFiClient wifi;
WiFiManager wifiManager;
PubSubClient mqtt(wifi);

char sensorID[18];
DynamicJsonDocument jsonDoc(MAX_PAYLOAD);

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

  if(!mqtt.loop()) // maintains MQTT stuff
    mqttReconnect();
  
  delay(LOOP_DELAY);
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
  char payload[MAX_PAYLOAD];
  serializeJson(jsonDoc, payload);
  
  while(!mqtt.publish("sensors/data", payload, false))
    mqttReconnect();
  jsonDoc.clear();
}

void otaCallback(char* topic, byte* payload, unsigned int length)
{
  StaticJsonDocument<MAX_PAYLOAD> payload_doc;
  deserializeJson(payload_doc, payload, length);

  const char* remote_hw_v = payload_doc["hw"];
  const char* remote_sw_v = payload_doc["sw"];

  // do not update if hardware version is different
  if(!(memcmp(remote_hw_v, HARDWARE_VERSION, max(length, strlen(HARDWARE_VERSION))) == 0))
  {
    return;
  }
  
  // do not update if software version is same
  if(memcmp(remote_sw_v, SOFTWARE_VERSION, max(length, strlen(SOFTWARE_VERSION))) == 0)
  {
    return;
  }
  
  Serial.println("New firmware version found. Updating...");
  
  t_httpUpdate_return result;
  for(int i = 0; i < 3; i++)
  {
    result = ESPhttpUpdate.update(  ServerSecrets::otaHost,
                                    ServerSecrets::otaPort,
                                    "/update.bin");
    
    if(result != HTTP_UPDATE_FAILED)
      break;
  }
  
  if(result == HTTP_UPDATE_FAILED)
    Serial.println("Update failed");
  else if(result == HTTP_UPDATE_NO_UPDATES)
    Serial.println("Server reported no updates available");
}

void setupGlobal()
{
  Serial.begin(9600);
  Serial.println();
  Wire.begin(PIN_SDA, PIN_SCL);

  // set sensor ID to MAC address
  WiFi.macAddress().toCharArray(sensorID, 18);
  Serial.print(sensorID);
  Serial.print(" hardware version ");
  Serial.print(HARDWARE_VERSION);
  Serial.print(", software version ");
  Serial.println(SOFTWARE_VERSION);
  
  WiFi.mode(WIFI_STA);
  wifiManager.setDebugOutput(false);
  wifiManager.autoConnect("AQM-AutoConnect");
}

void setupMQTT()
{
  mqtt.setBufferSize(MAX_PAYLOAD);
  mqtt.setServer(ServerSecrets::mqttHost, ServerSecrets::mqttPort);
  
  Serial.println("Connecting to MQTT broker...");
  mqttReconnect();
  Serial.println("MQTT broker connected.");
}

void mqttReconnect()
{
  while(!mqtt.connected())
  {
    mqtt.connect(sensorID, ServerSecrets::mqttUser, ServerSecrets::mqttPassw);
    
    if(mqtt.state() == 0)
      break;
    else if(mqtt.state() > 0) // good connection, server refused
      delay(30000); // wait 30 seconds before retrying
    // loop again if code < 0
    
    delay(200);
  }
  yield();
}

void setupOTA()
{
  Serial.println("Setting up OTA updates...");
  mqtt.setCallback(otaCallback);
  
  while(!mqtt.subscribe("sensors/update"))
    mqttReconnect();
  
  Serial.println("OTA updates setup.");
}

void setupPMS()
{
  Serial.println("Setting up PM sensor...");
  pmsSerial.begin(9600);
  pms.init(&pmsSerial);
  pms_on = true;

  while(true)
  {
    pms.updateFrame();
    
    if(pms.hasNewData())
    {
      break;
    }
    yield();
  }
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
    NTPClient timeClient(ntpUDP, "time.nist.gov");
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
