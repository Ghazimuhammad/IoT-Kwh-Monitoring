#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <RTClib.h>
#include <time.h>

const char *ssid = "POCO M4 Pro";
const char *password = "asdfghjk";

const char *thingsboardHost = "thingsboard.cloud";
const char *accessToken = "FFlbLnNexI8rXymjj8JC";
const char *api_key = "AIzaSyCa6PwS0QWqFxSqh-U5DALklKg4HSeAgek";
const char *url_database = "https://kwh-meter-2db5c-default-rtdb.asia-southeast1.firebasedatabase.app/";

const int voltagePin = 34;
const int currentPin = 36;
const char *telemetryTopic = "v1/devices/me/telemetry";

WiFiClient espClient;

PubSubClient client(espClient);

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long PrevMillis = 0;
bool signupOK = false;

typedef struct Data{
  float current_rms, current_max, voltage_rms, voltage_max;
}SensorData;

SensorData data_sensor;

void setup() {
  Serial.begin(115200);

  pinMode(23, OUTPUT);
  pinMode(voltagePin, INPUT);
  pinMode(currentPin, INPUT);
  digitalWrite(23, LOW);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");

  client.setServer(thingsboardHost, 1883);
  client.setCallback(callback);

  init_firebase();
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  time_t now;
  while ((now = time(nullptr)) < 1000000) {
    delay(500);
    Serial.println("Waiting for time to be set...");
  }
  Serial.println("Time set successfully!");
}

float count;

void loop() {
  // Sample telemetry data
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  data_sensor = get_data();
  send_data(data_sensor);
  // Wait for a few seconds before publishing the next message
  if (Firebase.ready() && signupOK && (millis() - PrevMillis > 15000 || PrevMillis == 0)){
    publish_data("KwhMeter/Current RMS", data_sensor.current_rms);
    publish_data("KwhMeter/Current Max", data_sensor.current_max);
    publish_data("KwhMeter/Voltage RMS", data_sensor.voltage_rms);
    publish_data("KwhMeter/Voltage Max", data_sensor.voltage_max);
    publish_data("KwhMeter/Watt Daily", data_sensor.voltage_rms * data_sensor.current_rms * 0.016 / 1000 * 12);
    publish_data("KwhMeter/Watt Monthly", data_sensor.voltage_rms * data_sensor.current_rms * 0.016 / 1000 * 12 * 30);
  }
  delay(5000);
}

SensorData get_current(){
  SensorData data;
  float mVperAmp = 185;
  float ACSoffset = 2500; 
  float Voltage = 0;
  float amplitude_current;               
  float effective_value;  
  float sensor_max;
  sensor_max = analogRead(currentPin);
  Voltage = (sensor_max / 4095) * 5000; // Voltage ( mV )
  amplitude_current = ((Voltage - ACSoffset) / mVperAmp); 
  effective_value = amplitude_current / sqrt(2);
  data.current_rms = effective_value;
  data.current_max = amplitude_current;
  data.voltage_rms = 0;
  data.voltage_max = 0;
  return data;
}

SensorData get_voltage(){
  SensorData data;
  const float referenceVoltage = 220;  
  const int adcMaxValue = 4095;
  int sensorValue = analogRead(voltagePin);
  
  float voltage = map(sensorValue, 0, 4095, 0, referenceVoltage);
  float rmsVoltage = voltage / sqrt(2);
  
  static float maxVoltage = 0;
  if (voltage > maxVoltage) {
    maxVoltage = voltage;
  }
  data.voltage_rms = rmsVoltage;
  data.voltage_max = maxVoltage;
  data.current_rms = 0;
  data.current_max = 0;
  return data;
}

SensorData get_data(){
  SensorData data;
  data.current_rms = get_current().current_rms;
  data.current_max = get_current().current_max;
  data.voltage_rms = get_voltage().voltage_rms;
  data.voltage_max = get_voltage().voltage_max;
  return data;
}

void send_data(SensorData data){
  char payload[256];
  StaticJsonDocument<256> telemetryData;
  telemetryData["Current rms"] = data.current_rms;
  telemetryData["Current max"] = data.current_max;
  telemetryData["Voltage rms"] = data.voltage_rms;
  telemetryData["Voltage max"] = data.voltage_max;
  telemetryData["Watt"] = data.voltage_rms * data.current_rms * 0.016 / 1000;
  telemetryData["Watt Daily"] = data.voltage_rms * data.current_rms * 0.016 / 1000 * 12;
  telemetryData["Watt Monthly"] = data.voltage_rms * data.current_rms * 0.016 / 1000 * 12 * 30;
  serializeJson(telemetryData, payload);
  client.publish(telemetryTopic, payload);
}

void publish_data(const char *path, float data){
  struct tm timeinfo;
  DynamicJsonDocument Json(200);
  String date, Jsonstring;
  int year, month, day, hour, minute, second;
  if (getLocalTime(&timeinfo)) {
    // Extract and store values
    year = timeinfo.tm_year + 1900;  // Years since 1900
    month = timeinfo.tm_mon + 1;     // Months start from 0
    day = timeinfo.tm_mday;
    hour = timeinfo.tm_hour;
    minute = timeinfo.tm_min;
    second = timeinfo.tm_sec;
  }else {
    Serial.println("Failed to obtain time");
  }
  date = String(year) + "-" + String(month) + "-" + String(day) + " " + String(hour) + ":" + String(minute) + ":" + String(second);
  const char* new_path = (String(path) + "/" + date).c_str();
  if(Firebase.RTDB.setFloat(&fbdo, new_path, data)){
    Serial.println("PASSED");
    Serial.println("PATH: " + fbdo.dataPath());
    Serial.println("TYPE: " + fbdo.dataType());
  }
}

void init_firebase(){
  config.api_key = api_key;
  config.database_url = url_database;
    if(Firebase.signUp(&config, &auth, "", "")){
      Serial.println("ok");
      signupOK = true;
    }else{
      Serial.println("error");
    }
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

void callback(char* topic, byte* payload, unsigned int length) {
  int requestId;
  Serial.print("Message received on topic: ");
  Serial.println(topic);
  if (sscanf(topic, "v1/devices/me/rpc/request/%d", &requestId) == 1) {
        printf("Extracted number: %d\n", requestId);
  }

  if (requestId %2 == 0) {
    Serial.println("Device is ON");
    digitalWrite(23, LOW);
    // Add your code to handle ON state
  } else{
    Serial.println("Device is OFF");
    digitalWrite(23, HIGH);
    // Add your code to handle OFF state
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.println("Connecting to ThingsBoard MQTT...");
    if (client.connect("ESP32Client", accessToken, NULL)) {
      Serial.println("Connected to ThingsBoard MQTT");
      client.subscribe("v1/devices/me/rpc/request/+");
    } else {
      Serial.print("Failed, rc=");
      Serial.print(client.state());
      Serial.println(" Retrying in 5 seconds...");
      delay(5000);
    }
  }
}
