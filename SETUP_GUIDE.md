# Smart Parking System v4.0 — Setup & Upload Guide

## 📁 Project Structure
```
c:\IOT\
├── esp32_gateway/          ← ESP32: WiFi, dashboard, API
│   ├── platformio.ini
│   └── src/main.cpp
├── mega_sensors/           ← Mega: Sensors, LCD, buzzer
│   ├── platformio.ini
│   └── src/main.cpp
├── uno_gate/               ← Uno: Servo gate, entry/exit
│   ├── platformio.ini
│   └── src/main.cpp
├── shared/protocol.h       ← Communication reference
├── dashboard_preview/      ← Demo dashboard (no hardware)
├── README.md
└── SETUP_GUIDE.md          ← This file
```

---

## 🛠️ Step 1: Install PlatformIO (Recommended)

1. Install **VS Code** or **Cursor IDE**
2. Install the **PlatformIO IDE** extension
3. Each board folder is an independent PlatformIO project

### OR: Arduino IDE

1. Download **Arduino IDE 2.x** from https://www.arduino.cc/en/software
2. Add ESP32 board support (Preferences → Additional Board URLs):
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. Install libraries: `LiquidCrystal_I2C`, `ArduinoJson`, `ESPAsyncWebServer`

---

## 🔌 Step 2: Wiring All Three Boards

### Arduino Mega — Sensor Hub

| Component | Mega Pin | Notes |
|-----------|----------|-------|
| IR Slot 1 | D22 | Digital, LOW = car detected |
| IR Slot 2 | D23 | Digital, LOW = car detected |
| IR Slot 3 | D24 | Digital, LOW = car detected |
| IR Slot 4 | D25 | Digital, LOW = car detected |
| MQ-2 AO | A0 | Analog output |
| MQ-2 VCC | 5V | Heater needs 5V |
| MQ-2 GND | GND | |
| LCD SDA | D20 | I2C Data |
| LCD SCL | D21 | I2C Clock |
| LCD VCC | 5V | Backpack needs 5V |
| LCD GND | GND | |
| Buzzer (+) | D8 | Active buzzer, 5V |
| Red LED | D9 | Through 220Ω to GND |
| Green LED | D10 | Through 220Ω to GND |
| IR VCC (all) | 5V | |
| IR GND (all) | GND | |

**Mega → ESP32 Serial Connection:**

| Mega Pin | | ESP32 Pin | Notes |
|----------|---|-----------|-------|
| D18 (TX1) | → via voltage divider → | GPIO16 (RX2) | ⚠️ 5V→3.3V required |
| D19 (RX1) | ← direct ← | GPIO17 (TX2) | 3.3V is valid HIGH for Mega |
| GND | — | GND | Common ground |

### Arduino Uno — Gate Controller

| Component | Uno Pin | Notes |
|-----------|---------|-------|
| HC-SR04 Trig | D6 | Digital output |
| HC-SR04 Echo | D7 | Digital input |
| Exit IR Sensor | D3 | Digital, LOW = vehicle |
| Servo Signal | D9 | PWM, SG90 servo |
| Servo VCC | 5V | Can share with board |
| Servo GND | GND | |
| Red Traffic LED | D4 | Through 220Ω to GND |
| Green Traffic LED | D5 | Through 220Ω to GND |
| Entry/Exit IR VCC | 5V | |
| Entry/Exit IR GND | GND | |

**Uno → ESP32 Serial Connection:**

| Uno Pin | | ESP32 Pin | Notes |
|---------|---|-----------|-------|
| D1 (TX) | → via voltage divider → | GPIO4 (RX1) | ⚠️ 5V→3.3V required |
| D0 (RX) | ← direct ← | GPIO5 (TX1) | 3.3V is valid HIGH for Uno |
| GND | — | GND | Common ground |

> **⚠️ Important:** Disconnect ESP32 wire from Uno RX (D0) before uploading firmware to the Uno!

### ESP32 — Network Gateway (no sensors attached)

| Function | ESP32 Pin | Connected To |
|----------|-----------|-------------|
| Mega Serial RX | GPIO16 | Mega D18 (TX1) via voltage divider |
| Mega Serial TX | GPIO17 | Mega D19 (RX1) direct |
| Uno Serial RX | GPIO4 | Uno D1 (TX) via voltage divider |
| Uno Serial TX | GPIO5 | Uno D0 (RX) direct |

### ⚡ Voltage Divider Wiring (REQUIRED × 2)

ESP32 GPIO pins are NOT 5V tolerant! Build this circuit on each Arduino TX → ESP32 RX line:

```
Arduino TX (5V) ──── [1KΩ] ────┬──── ESP32 RX (3.3V safe)
                                │
                              [2KΩ]
                                │
                               GND
```

Output voltage: 5V × 2K/(1K+2K) = 3.33V ✓

---

## ⚙️ Step 3: Configure WiFi

Edit `esp32_gateway/src/main.cpp`:
```cpp
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
```

> **Note:** ESP32 only supports **2.4GHz WiFi** (not 5GHz).

---

## 🚀 Step 4: Upload Firmware (order matters!)

### 1. Arduino Mega (upload first — no dependencies)
```bash
cd c:\IOT\mega_sensors
pio run -t upload
pio device monitor         # Should show sensor readings
```

### 2. Arduino Uno (disconnect ESP32 wire from D0 first!)
```bash
cd c:\IOT\uno_gate
pio run -t upload
```
> Reconnect ESP32 wire to Uno D0 after upload.

### 3. ESP32 (upload last)
```bash
cd c:\IOT\esp32_gateway
pio run -t upload
pio device monitor         # Shows WiFi IP + node status
```

---

## 📱 Step 5: Access the Dashboard

1. Open Serial Monitor for ESP32 (115200 baud)
2. Wait for WiFi connection:
   ```
   [OK] WiFi connected! IP: 192.168.1.42
   [OK] ESP32 Gateway started. Waiting for nodes...
   ```
3. Open that IP in any browser on the same WiFi network
4. Dashboard shows real-time parking, gas, gate, and node health! 🎉

### JSON API
```
http://<ESP32-IP>/api/status
```

---

## 🧪 Testing Without Hardware

Open `dashboard_preview/index.html` in your browser:
- Click parking slots to toggle occupied/available
- Drag the gas slider to simulate gas levels
- Click the gate button to toggle open/close

---

## 🔬 MQ-2 Gas Sensor Notes

The firmware includes automatic calibration with warm-up detection:

1. **First boot:** The Mega calibrates the MQ-2 automatically during startup (~15 seconds)
2. **Convergence detection:** Calibration samples resistance values and waits until 5 consecutive readings are within 5% of each other
3. **Warm-up handling:** Unlike v3, calibration won't lock on a cold sensor
4. **For best accuracy:** Let the MQ-2 heater run for 24-48 hours on first use

> PPM values are approximate. For precise measurements, adjust `MQ2_Rl` and `MQ2_CleanAirRatio` in the Mega firmware based on your specific MQ-2 module's datasheet.

---

## 🔍 Troubleshooting

| Problem | Solution |
|---------|----------|
| LCD shows nothing | Try `0x3F` instead of `0x27` in Mega firmware |
| WiFi won't connect | Check credentials, ensure 2.4GHz network |
| ESP32 upload fails | Hold BOOT button during upload |
| Uno upload fails | Disconnect ESP32 TX wire from Uno D0 |
| Dashboard shows nodes offline | Check Serial wiring + voltage dividers |
| Mega node offline | Check D18/D19 wiring to ESP32 GPIO16/17 |
| Uno node offline | Check D0/D1 wiring to ESP32 GPIO4/5 |
| Gas readings erratic | Allow 20+ seconds for MQ-2 warm-up |
| Gate won't open | Check entry IR sensor + available slots > 0 |
| Servo jitters | Add capacitor (100μF) across servo power |
| Random crashes | Verify voltage dividers — 5V on ESP32 RX = damage |

---

## 🔄 Communication Protocol

All inter-board communication uses **JSON-over-Serial** with newline termination:

**Mega → ESP32** (every 500ms):
```json
{"t":"S","slots":[0,1,0,1],"ppm":234.5,"raw":512,"gas":"Normal","cal":false}
```

**Uno → ESP32** (on gate events + every 3s):
```json
{"t":"G","gate":"open","count":15}
```

**ESP32 → Uno** (availability updates + commands):
```json
{"cmd":"avail","n":3}
{"cmd":"gate","action":"open"}
```

See `shared/protocol.h` for all message type and field definitions.
