#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ── Config ──────────────────────────────────────────
const char* WIFI_SSID     = "Redmi Note 11S";
const char* WIFI_PASSWORD = "haifa29/12";
const char* MQTT_BROKER   = "172.25.27.81"; 
const int   MQTT_PORT     = 1883;
const char* DEVICE_ID     = "esp32-factory-01";
const int   SEND_INTERVAL = 5000;


#define PIN_MQ5      34  
#define PIN_ACS712   35  
#define PIN_VOLTAGE  32  


#define ACS712_SENSITIVITY 0.185  

#define ACS712_ZERO       2048   


#define VOLTAGE_RATIO  5.0 

const int BUFFER_SIZE = 20;
String    msgBuffer[BUFFER_SIZE];
int       bufHead = 0, bufCount = 0;

WiFiClient   wifiClient;
PubSubClient mqtt(wifiClient);


float readGasPPM() {
  int raw = analogRead(PIN_MQ5);
  
  float voltage = raw * (3.3 / 4095.0);
  float ppm = voltage * 1000.0; 
  return round(ppm * 10) / 10.0;
}


float readCurrent() {
  
  long sum = 0;
  for (int i = 0; i < 100; i++) {
    sum += analogRead(PIN_ACS712);
    delayMicroseconds(100);
  }
  float avg = sum / 100.0;
  float voltage_mv = avg * (3300.0 / 4095.0);
  float current = (voltage_mv - ACS712_ZERO) / ACS712_SENSITIVITY;
  return round(current * 1000) / 1000.0;
}


float readVoltage() {
  int raw = analogRead(PIN_VOLTAGE);
  float voltage_esp = raw * (3.3 / 4095.0);
  float voltage_real = voltage_esp * VOLTAGE_RATIO;
  return round(voltage_real * 100) / 100.0;
}


void connectWiFi() {
  Serial.print("Connecting WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(" OK  IP: " + WiFi.localIP().toString());
}


void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("MQTT connecting...");
    String clientId = String(DEVICE_ID) + "-" + String(millis());
    if (mqtt.connect(clientId.c_str())) {
      Serial.println(" connected !");
      mqtt.publish("factory/status", "{\"status\":\"online\"}");
      // flush buffer
      while (bufCount > 0) {
        mqtt.publish("factory/buffered", msgBuffer[bufHead % BUFFER_SIZE].c_str());
        bufHead = (bufHead + 1) % BUFFER_SIZE;
        bufCount--;
      }
    } else {
      Serial.println(" failed, retry in 3s");
      delay(3000);
    }
  }
}

void ensureConnected() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqtt.connected())             connectMQTT();
}

void bufferMsg(const char* msg) {
  int slot = (bufHead + bufCount) % BUFFER_SIZE;
  msgBuffer[slot] = String(msg);
  if (bufCount < BUFFER_SIZE) bufCount++;
  else bufHead = (bufHead + 1) % BUFFER_SIZE;
}


void publishGas(float ppm) {
  StaticJsonDocument<128> doc;  // réduit de 200 à 128
  doc["device"]  = DEVICE_ID;
  doc["ts"]      = millis();
  doc["gas_ppm"] = ppm;
  char buf[128];
  serializeJson(doc, buf);
  Serial.print("Gas MQTT: ");
  Serial.println(buf);  // affiche ce qui est envoyé
  bool ok = mqtt.publish("factory/gas", buf);
  Serial.println(ok ? "Gas envoyé OK" : "Gas ECHEC");
}

void publishPower(float current, float voltage) {
  StaticJsonDocument<200> doc;
  doc["device"]    = DEVICE_ID;
  doc["ts"]        = millis();
  doc["current_a"] = current;
  doc["voltage_v"] = voltage;
  doc["power_w"]   = round(current * voltage * 100) / 100.0;
  char buf[200];
  serializeJson(doc, buf);
  if (!mqtt.publish("factory/power", buf)) bufferMsg(buf);
}


void setup() {
  Serial.begin(115200);
  delay(1000);

  
  analogReadResolution(12);       
  analogSetAttenuation(ADC_11db); 

  Serial.println("=== Factory IoT Device ===");
  connectWiFi();
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  connectMQTT();

  
  Serial.println("Chauffe MQ-5 en cours (30 sec)...");
  delay(30000);
  Serial.println("Pret !");
}


void loop() {
  ensureConnected();
  mqtt.loop();

  float gas     = readGasPPM();
  float current = readCurrent();
  float voltage = readVoltage();

  
  Serial.println("──────────────────────");
  Serial.print("Gaz (MQ-5)  : "); Serial.print(gas);     Serial.println(" ppm");
  Serial.print("Courant     : "); Serial.print(current);  Serial.println(" A");
  Serial.print("Tension     : "); Serial.print(voltage);  Serial.println(" V");
  Serial.print("Puissance   : "); Serial.print(current * voltage); Serial.println(" W");

  
  publishGas(gas);
  publishPower(current, voltage);

  delay(SEND_INTERVAL);
}