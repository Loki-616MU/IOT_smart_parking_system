/*
 * ================================================================
 *  SMART PARKING SYSTEM v4.0 — Shared Communication Protocol
 * ================================================================
 *  JSON-over-Serial protocol used between all three MCUs.
 *
 *  MEGA → ESP32 (Serial1 @ 115200, every 500ms):
 *    {"t":"S","slots":[0,1,0,1],"ppm":234.5,"raw":512,"gas":"Normal","cal":false}
 *
 *  UNO → ESP32 (Serial @ 9600, on events):
 *    {"t":"G","gate":"open","count":15}
 *
 *  ESP32 → UNO (command):
 *    {"cmd":"avail","n":3}
 *    {"cmd":"gate","action":"open"}
 *
 *  ESP32 → MEGA (command):
 *    {"cmd":"lcd","line1":"WiFi OK","line2":"192.168.1.42"}
 *
 *  WIRING (5V → 3.3V voltage divider required on Arduino TX → ESP32 RX):
 *    Arduino TX ──[1KΩ]──┬── ESP32 RX
 *                        [2KΩ]
 *                        GND
 * ================================================================
 */

#ifndef SMART_PARKING_PROTOCOL_H
#define SMART_PARKING_PROTOCOL_H

// ─── Message Types ─────────────────────────────
#define MSG_TYPE_SENSOR   "S"   // Mega → ESP32
#define MSG_TYPE_GATE     "G"   // Uno  → ESP32

// ─── Commands ──────────────────────────────────
#define CMD_AVAILABILITY  "avail"   // ESP32 → Uno
#define CMD_GATE_CONTROL  "gate"    // ESP32 → Uno / Dashboard → ESP32
#define CMD_LCD_OVERRIDE  "lcd"     // ESP32 → Mega

// ─── System Limits ─────────────────────────────
#define TOTAL_SLOTS       4
#define BAUD_MEGA         115200
#define BAUD_UNO          9600

// ─── Gas Thresholds (PPM) ──────────────────────
#define GAS_WARNING_PPM   400.0
#define GAS_DANGER_PPM    1000.0

// ─── Gate Timing ───────────────────────────────
#define GATE_OPEN_ANGLE   90
#define GATE_CLOSED_ANGLE 0
#define GATE_HOLD_MS      4000   // Keep gate open after vehicle passes

#endif
