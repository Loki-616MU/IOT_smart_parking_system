/*
 * ================================================================
 *  SMART PARKING SYSTEM v4.0 — Arduino Mega Sensor Hub
 * ================================================================
 *  Role: All sensor I/O, LCD display, buzzer/LED alerts
 *  Sends JSON sensor data to ESP32 via Serial1 @ 115200
 *  Receives commands from ESP32 on Serial1
 * ================================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <math.h>
#include "RTClib.h"

// ─── Pin Definitions ───────────────────────────────────────
#define TOTAL_SLOTS   4

const int IR_PINS[TOTAL_SLOTS] = {22, 23, 24, 25};  // Slot 1-4
#define GAS_SENSOR    A0
#define BUZZER_PIN    8
#define LED_RED       9
#define LED_GREEN     10

// ─── MQ-2 Calibration ─────────────────────────────────────
#define GAS_SAMPLES       20
#define GAS_WARNING_PPM   400.0
#define GAS_DANGER_PPM    1000.0
float MQ2_Ro            = 10.0;
float MQ2_Rl            = 1.0;
float MQ2_CleanAirRatio = 9.83;
bool  calibrated        = false;

// ─── State ─────────────────────────────────────────────────
bool  slotOccupied[TOTAL_SLOTS] = {false};
int   debounceCount[TOTAL_SLOTS] = {0};
float gasPPM    = 0;
int   gasRaw    = 0;
char  gasStatus[10] = "Normal";
bool  gasDanger  = false;
bool  gasWarning = false;

// ─── RTC & Billing State ───────────────────────────────────
RTC_DS1307 rtc;
DateTime entryTimes[TOTAL_SLOTS];
uint32_t parkedMinutes[TOTAL_SLOTS] = {0};
bool     rtcFound = false;

// ─── Timing ────────────────────────────────────────────────
unsigned long lastSend  = 0;
unsigned long lastLCD   = 0;
unsigned long lastBuzz  = 0;
bool showGasOnLCD = false;
bool buzzState    = false;

// ─── LCD + Custom Chars ────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);

byte carIcon[8]     = {0,0b01110,0b11111,0b11111,0b01010,0,0,0};
byte checkIcon[8]   = {0,0b00001,0b00011,0b10110,0b11100,0b01000,0,0};
byte warnIcon[8]    = {0b00100,0b00100,0b01110,0b01010,0b11111,0b11011,0b11111,0b01110};
byte gasIcon[8]     = {0b00100,0b01010,0b00100,0b01110,0b10001,0b10001,0b01110,0};

// ─── MQ-2 Functions ────────────────────────────────────────

float getMQResistance() {
  long sum = 0;
  for (int i = 0; i < GAS_SAMPLES; i++) {
    sum += analogRead(GAS_SENSOR);
    delay(2);
  }
  float v = (float)sum / GAS_SAMPLES;
  if (v < 1) v = 1;
  if (v >= 1023) v = 1022;   // Mega 10-bit ADC
  return ((1023.0 * MQ2_Rl) / v) - MQ2_Rl;
}

void calibrateGasSensor() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Calibrating MQ2");
  lcd.setCursor(0, 1);
  lcd.print("Warming up...");
  Serial.println(F("[INFO] MQ-2 calibration starting..."));

  // Warm-up with convergence detection
  float prev = 0;
  int stableCount = 0;
  for (int i = 0; i < 30; i++) {
    float r = getMQResistance();
    if (i > 10 && prev > 0 && fabs(r - prev) < (prev * 0.05)) {
      stableCount++;
      if (stableCount >= 5) break;
    } else {
      stableCount = 0;
    }
    prev = r;
    delay(500);
    lcd.setCursor(15, 1);
    lcd.print(i);
  }

  // Take final baseline
  float val = 0;
  for (int i = 0; i < 10; i++) {
    val += getMQResistance();
    delay(100);
  }
  MQ2_Ro = (val / 10.0) / MQ2_CleanAirRatio;
  if (MQ2_Ro < 0.1) MQ2_Ro = 0.1;

  calibrated = true;
  Serial.print(F("[OK] Calibrated. Ro = "));
  Serial.println(MQ2_Ro);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Calibrated!");
  lcd.setCursor(0, 1);
  lcd.print("Ro=");
  lcd.print(MQ2_Ro, 2);
  delay(1500);
}

// ─── Serial TX ─────────────────────────────────────────────

void sendSensorData() {
  JsonDocument doc;
  doc["t"] = "S";

  JsonArray slots = doc["slots"].to<JsonArray>();
  for (int i = 0; i < TOTAL_SLOTS; i++) {
    slots.add(slotOccupied[i] ? 1 : 0);
  }

  doc["ppm"] = (int)(gasPPM * 10) / 10.0;  // 1 decimal
  doc["raw"] = gasRaw;
  doc["gas"] = gasStatus;
  doc["cal"] = !calibrated;

  JsonArray bill = doc["bill"].to<JsonArray>();
  for (int i = 0; i < TOTAL_SLOTS; i++) {
    bill.add(parkedMinutes[i]);
  }

  // Add current time if RTC is working
  if (rtcFound) {
    DateTime now = rtc.now();
    char timeBuf[10];
    snprintf(timeBuf, 10, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    doc["time"] = timeBuf;
  }

  String out;
  serializeJson(doc, out);
  Serial1.println(out);
}

// ─── Serial RX ─────────────────────────────────────────────

void processCommand(const String& line) {
  JsonDocument doc;
  if (deserializeJson(doc, line) != DeserializationError::Ok) return;

  const char* cmd = doc["cmd"];
  if (!cmd) return;

  if (strcmp(cmd, "lcd") == 0) {
    const char* l1 = doc["line1"];
    const char* l2 = doc["line2"];
    if (l1) { lcd.setCursor(0, 0); lcd.print(l1); }
    if (l2) { lcd.setCursor(0, 1); lcd.print(l2); }
  }
}

// ─── Setup ─────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);    // USB debug
  Serial1.begin(115200);   // To ESP32

  // IR sensor pins
  for (int i = 0; i < TOTAL_SLOTS; i++) {
    pinMode(IR_PINS[i], INPUT_PULLUP);
  }
  pinMode(GAS_SENSOR, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);

  // LCD init
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, carIcon);
  lcd.createChar(1, checkIcon);
  lcd.createChar(2, warnIcon);
  lcd.createChar(3, gasIcon);

  lcd.setCursor(0, 0);
  lcd.print("Smart Parking");
  lcd.setCursor(0, 1);
  lcd.print("v4.0  Mega Hub");
  delay(1500);

  // RTC init
  if (!rtc.begin()) {
    Serial.println(F("[ERR] Couldn't find RTC"));
    rtcFound = false;
  } else {
    rtcFound = true;
    if (!rtc.isrunning()) {
      Serial.println(F("[WARN] RTC is NOT running, adjusting to compile time..."));
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
  }

  // Calibrate gas sensor with warm-up detection
  calibrateGasSensor();

  Serial.println(F("[OK] Mega Sensor Hub ready"));
}

// ─── Main Loop (non-blocking) ──────────────────────────────

void loop() {
  // ── 1. Read IR sensors with debouncing ──
  int avail = 0;
  bool changed = false;
  for (int i = 0; i < TOTAL_SLOTS; i++) {
    bool cur = (digitalRead(IR_PINS[i]) == LOW);
    if (cur != slotOccupied[i]) {
      if (++debounceCount[i] >= 3) {
        slotOccupied[i] = cur;
        changed = true;
        debounceCount[i] = 0;

        // Billing: Record entry time on occupation
        if (slotOccupied[i] && rtcFound) {
          entryTimes[i] = rtc.now();
        } else {
          parkedMinutes[i] = 0; // Reset on exit
        }
      }
    } else {
      debounceCount[i] = 0;
    }

    // Update duration if occupied
    if (slotOccupied[i] && rtcFound) {
      DateTime now = rtc.now();
      if (now.unixtime() > entryTimes[i].unixtime()) {
        parkedMinutes[i] = (now.unixtime() - entryTimes[i].unixtime()) / 60;
      }
    }
    if (!slotOccupied[i]) avail++;
  }

  // ── 2. Read gas sensor ──
  if (calibrated) {
    float rs = getMQResistance();
    float ppm = 116.6 * pow(rs / MQ2_Ro, -2.769);
    gasPPM = (0.3 * ppm) + (0.7 * gasPPM);
    gasRaw = analogRead(GAS_SENSOR);

    if (gasPPM > GAS_DANGER_PPM) {
      strcpy(gasStatus, "Danger");
      gasDanger = true; gasWarning = false;
    } else if (gasPPM > GAS_WARNING_PPM) {
      strcpy(gasStatus, "Warning");
      gasDanger = false; gasWarning = true;
    } else {
      strcpy(gasStatus, "Normal");
      gasDanger = false; gasWarning = false;
    }
  }

  // ── 3. Buzzer & LEDs ──
  if (gasDanger) {
    digitalWrite(LED_GREEN, LOW);
    digitalWrite(LED_RED, HIGH);
    if (millis() - lastBuzz > 200) {
      lastBuzz = millis();
      buzzState = !buzzState;
      digitalWrite(BUZZER_PIN, buzzState);
    }
  } else if (gasWarning) {
    digitalWrite(LED_GREEN, HIGH);
    digitalWrite(LED_RED, HIGH);
    if (millis() - lastBuzz > 800) {
      lastBuzz = millis();
      buzzState = !buzzState;
      digitalWrite(BUZZER_PIN, buzzState);
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
    digitalWrite(LED_GREEN, avail > 0 ? HIGH : LOW);
    digitalWrite(LED_RED, avail == 0 ? HIGH : LOW);
  }

  // ── 4. LCD (alternating every 3s) ──
  if (millis() - lastLCD > 3000) {
    lastLCD = millis();
    showGasOnLCD = !showGasOnLCD;
  }

  lcd.setCursor(0, 0);
  if (showGasOnLCD || gasDanger) {
    char buf[17];
    snprintf(buf, 17, "Gas:%4dppm      ", (int)gasPPM);
    lcd.print(buf);
    lcd.setCursor(0, 1);
    snprintf(buf, 17, "%-16s", gasStatus);
    lcd.print(buf);
  } else if (!showGasOnLCD && rtcFound) {
    // Screen: Time & Billing
    DateTime now = rtc.now();
    char buf[17];
    snprintf(buf, 17, "Time: %02d:%02d   ", now.hour(), now.minute());
    lcd.print(buf);
    lcd.setCursor(0, 1);
    
    // Show duration of the most recently changed occupied slot or total summary
    int activeSlot = -1;
    for(int i=0; i<TOTAL_SLOTS; i++) { if(slotOccupied[i]) activeSlot = i; }
    
    if (activeSlot != -1) {
      snprintf(buf, 17, "S%d Parked: %dm  ", activeSlot+1, (int)parkedMinutes[activeSlot]);
    } else {
      snprintf(buf, 17, "Lot Empty       ");
    }
    lcd.print(buf);
  } else {
    char buf[17];
    snprintf(buf, 17, "1:%c 2:%c 3:%c 4:%c",
      slotOccupied[0] ? 'X' : 'O',
      slotOccupied[1] ? 'X' : 'O',
      slotOccupied[2] ? 'X' : 'O',
      slotOccupied[3] ? 'X' : 'O');
    lcd.print(buf);
    lcd.setCursor(0, 1);
    snprintf(buf, 17, "Free:%d  Occ:%d  ", avail, TOTAL_SLOTS - avail);
    lcd.print(buf);
  }

  // ── 5. Send data to ESP32 ──
  if (changed || millis() - lastSend > 500) {
    lastSend = millis();
    sendSensorData();
  }

  // ── 6. Receive commands from ESP32 ──
  if (Serial1.available()) {
    String cmd = Serial1.readStringUntil('\n');
    processCommand(cmd);
  }

  delay(100);  // ~10Hz loop
}
