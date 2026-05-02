#include <Wire.h>
#include <DHT.h>
#include <Adafruit_BMP085.h>

#ifndef LED_BUILTIN
  #define LED_BUILTIN 2
#endif

// ─── PINS ──────────────────────────────────────────────────
#define DHT_PIN       4
#define DHT_TYPE      DHT22
#define GAS_AO_PIN    34
#define GAS_DO_PIN    26

// ─── OBJETS ────────────────────────────────────────────────
DHT             dht(DHT_PIN, DHT_TYPE);
Adafruit_BMP085 bmp;

// ─── FLAGS ─────────────────────────────────────────────────
bool bmpOK = false;
bool dhtOK = false;

// ─── PARAMÈTRES SALLE BLANCHE ──────────────────────────────
// Valeurs cibles ISO pharma (audit usine pharmaceutique)
const float TEMP_TARGET   = 22.0;   // °C  consigne
const float TEMP_DRIFT    = 1.5;    // °C  variation max
const float HUM_TARGET    = 45.0;   // %   consigne
const float HUM_DRIFT     = 5.0;    // %   variation max
const float PRES_TARGET   = 1013.0; // hPa pression atmo
const float PRES_DRIFT    = 2.0;    // hPa variation
const int   GAS_CLEAN     = 520;    // ADC air propre salle blanche
const int   GAS_DRIFT     = 80;     // ADC variation normale

// ─── SIMULATION D'ANOMALIE ─────────────────────────────────
// Toutes les 60 secondes, injecte une anomalie pour tester
const int   ANOMALY_EVERY = 60;     // secondes
int         anomalyCounter = 0;
bool        inAnomaly = false;

// ─── COMPTEURS ─────────────────────────────────────────────
unsigned long lastRead    = 0;
unsigned long readCount   = 0;
const long    INTERVAL    = 2000;

// ─── SEED ALÉATOIRE RÉALISTE ───────────────────────────────
float frand(float min, float max) {
  return min + (float)random(0, 1000) / 1000.0f * (max - min);
}

// ─── SIMULATION VALEUR AVEC DÉRIVE LENTE ───────────────────
// Simule une dérive réaliste autour d'une valeur cible
float simulateSensor(float target, float drift, float& current) {
  // Dérive progressive vers la cible + bruit haute fréquence
  float noise    = frand(-drift * 0.3f, drift * 0.3f);
  float tendency = (target - current) * 0.1f;  // retour vers cible
  current += tendency + noise;
  current = constrain(current, target - drift * 2, target + drift * 2);
  return current;
}

// ─── ÉTAT COURANT DES CAPTEURS (pour dérive progressive) ───
float curTemp  = 22.0;
float curHum   = 45.0;
float curPres  = 1013.0;
float curGas   = 520.0;

// ═══════════════════════════════════════════════════════════
// SETUP
// ═══════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  randomSeed(analogRead(0) + analogRead(35) + micros());
  delay(500);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(GAS_DO_PIN, INPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  Wire.begin(21, 22);
  bmpOK = bmp.begin();
  dht.begin();
  delay(2000);
  float t = dht.readTemperature();
  dhtOK = !isnan(t);

  Serial.println();
  Serial.println("╔══════════════════════════════════════════════════════╗");
  Serial.println("║   SALLE BLANCHE PHARMACEUTIQUE — Monitoring IoT     ║");
  Serial.println("║   ESP32 WROOM-32  |  Norme ISO 14644 / GMP          ║");
  Serial.println("╚══════════════════════════════════════════════════════╝");
  Serial.println();
  Serial.printf("  BMP180  : %s\n", bmpOK ? "OK" : "Simulation activee");
  Serial.printf("  DHT22   : %s\n", dhtOK ? "OK" : "Simulation activee");
  Serial.println("  MQ Gaz  : Simulation activee");
  Serial.println();
  Serial.println("  Mode simulation salle blanche:");
  Serial.printf("  Temp cible  : %.1f +/- %.1f degC\n", TEMP_TARGET, TEMP_DRIFT);
  Serial.printf("  Hum cible   : %.1f +/- %.1f %%\n",  HUM_TARGET,  HUM_DRIFT);
  Serial.printf("  Pres cible  : %.1f hPa\n",           PRES_TARGET);
  Serial.println("  Anomalie    : injectee toutes les 60s");
  Serial.println();

  // ── En-tête CSV ──
  Serial.println("=== DEBUT DONNEES CSV ===");
  Serial.println("timestamp_ms,temperature_C,humidity_pct,pressure_hPa,gas_raw,gas_ppm,anomaly,source_temp,source_hum,source_pres");
}

// ═══════════════════════════════════════════════════════════
// LOOP
// ═══════════════════════════════════════════════════════════
void loop() {
  if (millis() - lastRead < INTERVAL) return;
  lastRead = millis();
  readCount++;
  anomalyCounter++;

  // ════ Décider si on injecte une anomalie ════
  // Toutes les 60s, anomalie pendant 5 lectures
  if (anomalyCounter >= ANOMALY_EVERY / (INTERVAL / 1000)) {
    anomalyCounter = 0;
    inAnomaly = true;
  }
  if (inAnomaly && readCount % 5 == 0) {
    inAnomaly = false;
  }

  // ════ Lecture BMP180 (ou simulation) ════
  float temperature, pressure, altitude;
  String srcPres;

  if (bmpOK) {
    temperature = bmp.readTemperature();
    pressure    = bmp.readPressure() / 100.0f;
    altitude    = bmp.readAltitude();
    srcPres     = "BMP180";
  } else {
    // Simulation avec dérive réaliste
    if (inAnomaly) {
      // Anomalie température : monte vers 26°C
      curTemp += frand(0.3f, 0.8f);
      curTemp  = min(curTemp, 27.0f);
    } else {
      simulateSensor(TEMP_TARGET, TEMP_DRIFT, curTemp);
    }
    temperature = curTemp;
    simulateSensor(PRES_TARGET, PRES_DRIFT, curPres);
    pressure  = curPres;
    altitude  = 44330.0f * (1.0f - pow(pressure / 1013.25f, 0.1903f));
    srcPres   = "SIM";
  }

  // ════ Lecture DHT22 (ou simulation) ════
  float humidity;
  String srcHum;

  if (dhtOK) {
    float h = dht.readHumidity();
    float t = dht.readTemperature();
    humidity    = isnan(h) ? curHum : h;
    temperature = isnan(t) ? temperature : t;
    srcHum      = "DHT22";
  } else {
    if (inAnomaly) {
      // Anomalie humidité : monte vers 65%
      curHum += frand(0.5f, 1.5f);
      curHum  = min(curHum, 68.0f);
    } else {
      simulateSensor(HUM_TARGET, HUM_DRIFT, curHum);
    }
    humidity = curHum;
    srcHum   = "SIM";
  }

  // ════ Lecture/simulation MQ gaz ════
  int gasRaw;
  float gasPPM;

  if (inAnomaly && anomalyCounter < 3) {
    // Pic de gaz simulé
    curGas = frand(2200, 3000);
  } else {
    simulateSensor((float)GAS_CLEAN, (float)GAS_DRIFT, curGas);
  }
  gasRaw = (int)curGas;
  // Conversion approx RAW → ppm CO2 (courbe MQ-135)
  gasPPM = 116.6f * pow((gasRaw / 4095.0f * 3.3f) / 1.65f, -2.769f);
  gasPPM = constrain(gasPPM, 350, 5000);

  bool  alarm    = (gasRaw >= 2000) || inAnomaly;
  bool  tempAlarm = (temperature > 25.0f || temperature < 19.0f);
  bool  humAlarm  = (humidity > 60.0f   || humidity < 30.0f);
  bool  anyAlarm  = alarm || tempAlarm || humAlarm;

  digitalWrite(LED_BUILTIN, anyAlarm ? HIGH : LOW);

  // ════ Affichage console lisible ════
  Serial.println("──────────────────────────────────────────────────────");
  Serial.printf("  #%04lu  |  t=%lums\n", readCount, millis());
  Serial.printf("  TEMP   : %6.2f °C     [cible: %.1f ± %.1f]  %s\n",
    temperature, TEMP_TARGET, TEMP_DRIFT, tempAlarm ? "!! HORS NORME !!" : "OK");
  Serial.printf("  HUM    : %6.2f %%     [cible: %.1f ± %.1f]  %s\n",
    humidity, HUM_TARGET, HUM_DRIFT, humAlarm ? "!! HORS NORME !!" : "OK");
  Serial.printf("  PRES   : %6.2f hPa   [cible: %.1f]          OK\n",
    pressure, PRES_TARGET);
  Serial.printf("  GAZ    : %4d raw  ~%.0f ppm  %s  %s\n",
    gasRaw, gasPPM, gasRaw < 800 ? "AIR PROPRE" : gasRaw < 2000 ? "TRACE" : "ALARME !!",
    inAnomaly ? "<< ANOMALIE INJECTEE >>" : "");
  if (anyAlarm) {
    Serial.println("  *** ALERTE SALLE BLANCHE — VERIFIER CONDITIONS ***");
  }

  // ════ Ligne CSV (copiable directement) ════
  Serial.printf("CSV,%lu,%.2f,%.2f,%.2f,%d,%.1f,%d,%s,%s,%s\n",
    millis(),
    temperature,
    humidity,
    pressure,
    gasRaw,
    gasPPM,
    anyAlarm ? 1 : 0,
    (bmpOK ? "BMP180" : "SIM"),
    srcHum.c_str(),
    srcPres.c_str()
  );
}