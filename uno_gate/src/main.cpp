/*
 * ================================================================
 *  SMART PARKING SYSTEM v4.0 — Arduino Uno Gate Controller
 * ================================================================
 *  Role: Servo barrier gate, entry/exit detection, traffic LEDs
 *  Sends gate events to ESP32 via Hardware Serial @ 9600
 *  Receives availability updates and commands from ESP32
 *
 *  NOTE: Hardware Serial (D0/D1) is shared with USB.
 *        Disconnect ESP32 TX wire from Uno RX (D0) during upload.
 * ================================================================
 */

#include <Arduino.h>
#include <Servo.h>
#include <ArduinoJson.h>

// ─── Pin Definitions ───────────────────────────────────────
#define TRIG_PIN        6    // HC-SR04 Trig
#define ECHO_PIN        7    // HC-SR04 Echo
#define EXIT_IR_PIN     3    // Exit sensor (INT1)
#define SERVO_PIN       9    // Gate servo PWM
#define TRAFFIC_RED     4    // Red traffic LED
#define TRAFFIC_GREEN   5    // Green traffic LED

// ─── Gate Configuration ────────────────────────────────────
#define GATE_OPEN_ANGLE   90
#define GATE_CLOSED_ANGLE 0
#define GATE_HOLD_MS      4000   // ms to keep gate open after trigger
#define DISTANCE_THRESHOLD 10    // cm threshold for entry detection

// ─── Gate State Machine ────────────────────────────────────
enum GateState {
  GATE_CLOSED,
  GATE_OPENING,
  GATE_OPEN_HOLD,
  GATE_CLOSING
};

// ─── Variables ─────────────────────────────────────────────
Servo gateServo;

GateState gateState = GATE_CLOSED;
unsigned long gateTimer     = 0;
int   availableSlots        = 0;
int   vehicleCount          = 0;
bool  entryTriggered        = false;
bool  exitTriggered         = false;
bool  lastEntryState        = false;
bool  lastExitState         = false;
unsigned long lastEntryDebounce = 0;
unsigned long lastExitDebounce  = 0;
unsigned long lastStatusSend    = 0;
String serialBuffer = "";

// ─── Send Gate Status ──────────────────────────────────────

// ─── Ultrasonic Sensor ─────────────────────────────────────

float getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000); // 30ms timeout
  if (duration == 0) return 999.0;
  return duration * 0.034 / 2.0;
}

void sendGateStatus() {
  JsonDocument doc;
  doc["t"]     = "G";
  doc["gate"]  = (gateState == GATE_CLOSED || gateState == GATE_CLOSING) ? "closed" : "open";
  doc["count"] = vehicleCount;

  String out;
  serializeJson(doc, out);
  Serial.println(out);
}

// ─── Update Traffic LEDs ───────────────────────────────────

void updateTrafficLEDs() {
  if (availableSlots <= 0) {
    // Lot full — red
    digitalWrite(TRAFFIC_RED, HIGH);
    digitalWrite(TRAFFIC_GREEN, LOW);
  } else if (gateState == GATE_OPEN_HOLD || gateState == GATE_OPENING) {
    // Gate open — green
    digitalWrite(TRAFFIC_RED, LOW);
    digitalWrite(TRAFFIC_GREEN, HIGH);
  } else {
    // Available but gate closed — green steady
    digitalWrite(TRAFFIC_RED, LOW);
    digitalWrite(TRAFFIC_GREEN, HIGH);
  }
}

// ─── Process ESP32 Commands ────────────────────────────────

void processCommand(const String& line) {
  JsonDocument doc;
  if (deserializeJson(doc, line) != DeserializationError::Ok) return;

  // Availability update from ESP32
  if (doc["cmd"] == "avail") {
    availableSlots = doc["n"] | 0;
    updateTrafficLEDs();
  }

  // Manual gate command from dashboard
  if (doc["cmd"] == "gate") {
    const char* action = doc["action"];
    if (action) {
      if (strcmp(action, "open") == 0 && gateState == GATE_CLOSED) {
        gateState = GATE_OPENING;
      } else if (strcmp(action, "close") == 0 &&
                 (gateState == GATE_OPEN_HOLD || gateState == GATE_OPENING)) {
        gateState = GATE_CLOSING;
      }
    }
  }
}

// ─── Setup ─────────────────────────────────────────────────

void setup() {
  Serial.begin(9600);   // Hardware Serial to ESP32

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(EXIT_IR_PIN, INPUT_PULLUP);
  pinMode(TRAFFIC_RED, OUTPUT);
  pinMode(TRAFFIC_GREEN, OUTPUT);

  gateServo.attach(SERVO_PIN);
  gateServo.write(GATE_CLOSED_ANGLE);

  digitalWrite(TRAFFIC_RED, LOW);
  digitalWrite(TRAFFIC_GREEN, HIGH);

  // Send initial status
  delay(500);
  sendGateStatus();
}

// ─── Main Loop ─────────────────────────────────────────────

void loop() {
  unsigned long now = millis();

  // ── 1. Read Entry Ultrasonic (debounced) ──
  bool entryRaw = (getDistance() < DISTANCE_THRESHOLD);
  if (entryRaw != lastEntryState) {
    lastEntryDebounce = now;
    lastEntryState = entryRaw;
  }
  if ((now - lastEntryDebounce > 50) && entryRaw && !entryTriggered) {
    entryTriggered = true;
    // Vehicle at entry gate
    if (availableSlots > 0 && gateState == GATE_CLOSED) {
      gateState = GATE_OPENING;
      vehicleCount++;
    }
  }
  if (!entryRaw) entryTriggered = false;

  // ── 2. Read Exit IR (debounced) ──
  bool exitRaw = (digitalRead(EXIT_IR_PIN) == LOW);
  if (exitRaw != lastExitState) {
    lastExitDebounce = now;
    lastExitState = exitRaw;
  }
  if ((now - lastExitDebounce > 50) && exitRaw && !exitTriggered) {
    exitTriggered = true;
    // Vehicle at exit gate
    if (gateState == GATE_CLOSED) {
      gateState = GATE_OPENING;
      if (vehicleCount > 0) vehicleCount--;
    }
  }
  if (!exitRaw) exitTriggered = false;

  // ── 3. Gate State Machine ──
  switch (gateState) {
    case GATE_CLOSED:
      gateServo.write(GATE_CLOSED_ANGLE);
      break;

    case GATE_OPENING:
      gateServo.write(GATE_OPEN_ANGLE);
      gateTimer = now;
      gateState = GATE_OPEN_HOLD;
      sendGateStatus();
      break;

    case GATE_OPEN_HOLD:
      // Keep open, wait for timeout
      if (now - gateTimer > GATE_HOLD_MS) {
        // Safety: don't close if sensor still blocked
        if (getDistance() > DISTANCE_THRESHOLD && digitalRead(EXIT_IR_PIN) == HIGH) {
          gateState = GATE_CLOSING;
        } else {
          gateTimer = now;  // Extend hold
        }
      }
      break;

    case GATE_CLOSING:
      gateServo.write(GATE_CLOSED_ANGLE);
      gateState = GATE_CLOSED;
      sendGateStatus();
      break;
  }

  // ── 4. Traffic LEDs ──
  updateTrafficLEDs();

  // ── 5. Periodic status ──
  if (now - lastStatusSend > 3000) {
    lastStatusSend = now;
    sendGateStatus();
  }

  // ── 6. Receive commands from ESP32 ──
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      processCommand(serialBuffer);
      serialBuffer = "";
    } else {
      serialBuffer += c;
    }
  }

  delay(50);  // ~20Hz loop
}
