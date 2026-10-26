/*
  ESP32 Multi-Sensor Industrial Hazard Monitor
  --------------------------------------------
  Sensors : DHT11 (temperature/humidity), MQ gas sensor, SW-420 vibration
  Outputs : Traffic LED module, buzzer
  Alerts  : SMS via SIM800L, cloud logging via ThingSpeak (WiFi)

  Before uploading:
    1. Copy secrets.h.example to secrets.h
    2. Fill in your WiFi, ThingSpeak key and phone number in secrets.h
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include "secrets.h"   // WIFI_SSID, WIFI_PASS, TS_API_KEY, PHONE_NUMBER

// ---------- ThingSpeak ----------
const unsigned long UPLOAD_INTERVAL = 20000;   // ThingSpeak free: min 15 s

// ---------- GSM ----------
const unsigned long SMS_COOLDOWN = 300000;     // 5 minutes per alert level
#define SMS_ON_WARNING true                    // set false for DANGER-only SMS
#define GSM_RX  16   // ESP32 RX2 <- SIM800L TXD
#define GSM_TX  17   // ESP32 TX2 -> SIM800L RXD
#define GSM_RST 5
HardwareSerial gsm(2);
bool gsmReady = false;
unsigned long lastSMS[3] = {0, 0, 0};          // index 1 = warning, 2 = danger
bool smsSent[3] = {false, false, false};

// ---------- Pins ----------
#define DHT_PIN     4
#define DHT_TYPE    DHT11
#define MQ_PIN      34
#define VIB_PIN     27
#define BUZZER_PIN  26
#define LED_RED     25
#define LED_YELLOW  33
#define LED_GREEN   32

// ---------- Thresholds ----------
#define TEMP_LIMIT  40.0
#define HUM_LIMIT   85.0
#define VIB_LIMIT   5

// ---------- Gas auto-calibration ----------
#define WARMUP_SECONDS  120    // keep MQ in clean air during this time
#define GAS_MARGIN      400    // limit = clean-air baseline + this
#define GAS_HYSTERESIS  100    // alarm clears when gas < limit - this
#define GAS_CONFIRM     3      // consecutive readings above limit needed
int gasBaseline = 0;
int gasLimit = 1500;

DHT dht(DHT_PIN, DHT_TYPE);

volatile unsigned int vibCount = 0;
unsigned long lastRead = 0;
unsigned long lastUpload = 0;
const unsigned long INTERVAL = 2000;

// Buzzer: 0 = off, 1 = warning beep, 2 = danger beep
int buzzMode = 0;
bool beepState = false;
unsigned long lastBeep = 0;

// Worst values since last upload
float  upTemp = 0, upHum = 0;
int    upGas = 0;
unsigned int upVib = 0;
int    upStatus = 0;

void IRAM_ATTR vibISR() { vibCount++; }

void setLEDs(bool r, bool y, bool g) {
  digitalWrite(LED_RED, r);
  digitalWrite(LED_YELLOW, y);
  digitalWrite(LED_GREEN, g);
}

int readGas() {
  long sum = 0;
  for (int i = 0; i < 10; i++) { sum += analogRead(MQ_PIN); delay(5); }
  return sum / 10;
}

// Non-blocking buzzer patterns
void updateBuzzer() {
  unsigned long now = millis();
  if (buzzMode == 2) {                       // DANGER: fast beeping
    if (now - lastBeep >= 200) {
      lastBeep = now;
      beepState = !beepState;
      digitalWrite(BUZZER_PIN, beepState);
    }
  } else if (buzzMode == 1) {                // WARNING: short beep every 2 s
    digitalWrite(BUZZER_PIN, (now % 2000) < 150);
  } else {
    beepState = false;
    digitalWrite(BUZZER_PIN, LOW);
  }
}

// ================= GSM FUNCTIONS =================
bool waitFor(const char* expected, unsigned long timeout) {
  String resp = "";
  unsigned long start = millis();
  while (millis() - start < timeout) {
    while (gsm.available()) resp += (char)gsm.read();
    if (resp.indexOf(expected) >= 0) return true;
    delay(10);
  }
  Serial.print("GSM no match, got: ");
  Serial.println(resp);
  return false;
}

bool sendAT(const char* cmd, const char* expected, unsigned long timeout = 2000) {
  while (gsm.available()) gsm.read();
  gsm.println(cmd);
  return waitFor(expected, timeout);
}

void gsmInit() {
  pinMode(GSM_RST, OUTPUT);
  digitalWrite(GSM_RST, HIGH);
  gsm.begin(9600, SERIAL_8N1, GSM_RX, GSM_TX);
  delay(3000);

  Serial.println("Initializing SIM800L...");
  bool ok = false;
  for (int i = 0; i < 5 && !ok; i++) ok = sendAT("AT", "OK");
  if (!ok) {
    Serial.println("SIM800L not responding. Check wiring and power.");
    return;
  }

  sendAT("ATE0", "OK");        // echo off
  sendAT("AT+CMGF=1", "OK");   // SMS text mode

  bool reg = false;
  for (int i = 0; i < 15 && !reg; i++) {
    while (gsm.available()) gsm.read();
    gsm.println("AT+CREG?");
    String resp = "";
    unsigned long t = millis();
    while (millis() - t < 1000) {
      while (gsm.available()) resp += (char)gsm.read();
    }
    if (resp.indexOf(",1") >= 0 || resp.indexOf(",5") >= 0) reg = true;
    else delay(1000);
  }

  if (reg) {
    Serial.println("GSM network registered.");
    gsmReady = true;
  } else {
    Serial.println("GSM not registered. Check SIM, antenna, 2G coverage.");
  }
}

bool sendSMS(const String& text) {
  if (!gsmReady) {
    Serial.println("SMS skipped: GSM not ready.");
    return false;
  }

  while (gsm.available()) gsm.read();
  gsm.print("AT+CMGS=\"");
  gsm.print(PHONE_NUMBER);
  gsm.println("\"");

  if (!waitFor(">", 5000)) return false;

  gsm.print(text);
  gsm.write(26);               // Ctrl+Z sends the message

  return waitFor("+CMGS", 20000);
}

// level: 1 = WARNING, 2 = DANGER
void sendAlertSMS(int level, float t, float h, int g, unsigned int v, const String& cause) {
  if (level == 1 && !SMS_ON_WARNING) return;
  if (smsSent[level] && millis() - lastSMS[level] < SMS_COOLDOWN) return;

  String msg = (level == 2) ? "HAZARD DANGER! " : "HAZARD WARNING! ";
  msg += "Cause:" + cause + " ";
  msg += "Temp:" + String(t, 1) + "C ";
  msg += "Hum:" + String(h, 0) + "% ";
  msg += "Gas:" + String(g) + " ";
  msg += "Vib:" + String(v);

  Serial.println("Sending SMS...");
  if (sendSMS(msg)) {
    Serial.println("SMS sent.");
    lastSMS[level] = millis();
    smsSent[level] = true;
  } else {
    Serial.println("SMS failed.");
  }
}

// ================= WIFI / THINGSPEAK =================
void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nWiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\nWiFi failed (will retry later)");
  }
}

void uploadToThingSpeak() {
  connectWiFi();
  if (WiFi.status() != WL_CONNECTED) return;

  String url = "http://api.thingspeak.com/update?api_key=";
  url += TS_API_KEY;
  url += "&field1=" + String(upTemp, 1);
  url += "&field2=" + String(upHum, 1);
  url += "&field3=" + String(upGas);
  url += "&field4=" + String(upVib);
  url += "&field5=" + String(upStatus);

  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  String entry = http.getString();   // entry ID, or "0" if rejected
  http.end();

  if (code == 200 && entry != "0") {
    Serial.printf("ThingSpeak OK (entry %s)\n", entry.c_str());
  } else {
    Serial.printf("ThingSpeak FAILED (HTTP %d, response %s)\n", code, entry.c_str());
  }

  upTemp = 0; upHum = 0; upGas = 0; upVib = 0; upStatus = 0;
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  dht.begin();

  pinMode(VIB_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  analogReadResolution(12);
  attachInterrupt(digitalPinToInterrupt(VIB_PIN), vibISR, RISING);

  Serial.println("Industrial Hazard Monitor Started");

  // LED + buzzer self-test
  setLEDs(1, 0, 0); delay(500);
  setLEDs(0, 1, 0); delay(500);
  setLEDs(0, 0, 1); delay(500);
  setLEDs(0, 0, 0);
  digitalWrite(BUZZER_PIN, HIGH); delay(200);
  digitalWrite(BUZZER_PIN, LOW);

  gsmInit();
  connectWiFi();

  Serial.printf("MQ sensor warming up (%d s). Keep it in CLEAN AIR...\n", WARMUP_SECONDS);
  for (int i = 0; i < WARMUP_SECONDS; i++) {
    setLEDs(0, i % 2, 0);      // yellow blinks while warming up
    delay(1000);
  }

  // ---- Gas auto-calibration (5 s of clean-air samples) ----
  Serial.println("Calibrating gas baseline...");
  long s = 0;
  for (int i = 0; i < 50; i++) { s += analogRead(MQ_PIN); delay(100); }
  gasBaseline = s / 50;
  gasLimit = gasBaseline + GAS_MARGIN;
  if (gasLimit > 4000) {
    gasLimit = 4000;
    Serial.println("Warning: baseline very high, sensor may be saturated.");
  }
  Serial.printf("Gas baseline: %d  ->  Gas limit: %d\n", gasBaseline, gasLimit);

  vibCount = 0;
  lastUpload = millis();
}

// ================= LOOP =================
void loop() {
  static int  gasHighCount = 0;
  static bool gasAlarm = false;

  if (millis() - lastRead >= INTERVAL) {
    lastRead = millis();

    float temp = dht.readTemperature();
    float hum  = dht.readHumidity();
    int gas = readGas();

    noInterrupts();
    unsigned int vib = vibCount;
    vibCount = 0;
    interrupts();

    if (isnan(temp) || isnan(hum)) {
      Serial.println("DHT11 read failed!");
      return;
    }

    // ---- Gas alarm with confirmation + hysteresis ----
    if (!gasAlarm) {
      if (gas > gasLimit) gasHighCount++;
      else gasHighCount = 0;
      if (gasHighCount >= GAS_CONFIRM) gasAlarm = true;
    } else {
      if (gas < gasLimit - GAS_HYSTERESIS) {
        gasAlarm = false;
        gasHighCount = 0;
      }
    }

    Serial.println("-----------------------------");
    Serial.printf("Temperature : %.1f C\n", temp);
    Serial.printf("Humidity    : %.1f %%\n", hum);
    Serial.printf("Gas (raw)   : %d  (limit %d)\n", gas, gasLimit);
    Serial.printf("Vibration   : %u pulses\n", vib);

    bool tempAlert = temp > TEMP_LIMIT;
    bool humAlert  = hum > HUM_LIMIT;
    bool gasAlert  = gasAlarm;
    bool vibAlert  = vib > VIB_LIMIT;
    int score = tempAlert + humAlert + gasAlert + vibAlert;

    String cause = "";
    if (tempAlert) { cause += "Temp "; Serial.println("High Temperature!"); }
    if (humAlert)  { cause += "Humidity "; Serial.println("High Humidity!"); }
    if (gasAlert)  { cause += "Gas "; Serial.println("Gas Leak Detected!"); }
    if (vibAlert)  { cause += "Vibration "; Serial.println("Abnormal Vibration!"); }
    cause.trim();

    int status;
    if (score >= 2 || gasAlert) {          // gas leak alone = DANGER
      status = 2;
      buzzMode = 2;
      Serial.println("STATUS: DANGER");
      setLEDs(1, 0, 0);
      sendAlertSMS(2, temp, hum, gas, vib, cause);
    } else if (score == 1) {
      status = 1;
      buzzMode = 1;
      Serial.println("STATUS: WARNING");
      setLEDs(0, 1, 0);
      sendAlertSMS(1, temp, hum, gas, vib, cause);
    } else {
      status = 0;
      buzzMode = 0;
      Serial.println("STATUS: NORMAL");
      setLEDs(0, 0, 1);
    }

    // Keep the worst values for the next upload
    if (temp > upTemp) upTemp = temp;
    if (hum  > upHum)  upHum  = hum;
    if (gas  > upGas)  upGas  = gas;
    if (vib  > upVib)  upVib  = vib;
    if (status > upStatus) upStatus = status;
  }

  updateBuzzer();

  if (millis() - lastUpload >= UPLOAD_INTERVAL) {
    lastUpload = millis();
    uploadToThingSpeak();
  }
}
