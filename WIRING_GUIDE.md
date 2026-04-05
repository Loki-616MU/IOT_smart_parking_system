# 🔌 Beginner's Hardware Wiring Guide

Welcome! Wiring three microcontrollers together might look terrifying, but if you do it **step-by-step**, it’s actually very straightforward. 

This guide assumes you are using **one main power supply** (like a 5V 3A adapter or a robust power bank) to power the entire system.

> [!WARNING]
> **Golden Rule of Wiring:** NEVER plug your main power supply into the wall while you are connecting wires. Complete ALL wiring first, double-check it, and only then apply power!

---

## ⚡ Phase 1: The Power Rail (Ground and 5V)

Because you are using a single power supply, you need to create a **Power Rail** on your breadboard. A breadboard usually has red (+) and blue (-) lines running down the sides.

1. Connect your Power Supply **5V line** to the **Red line** on the breadboard.
2. Connect your Power Supply **GND line** to the **Blue line** on the breadboard.
3. Run a wire from the breadboard **Red line (5V)** to the **VIN pin** (or 5V pin) on the Mega, Uno, and ESP32. *(Note: Check your specific ESP32 model. Most have a `5V` or `VIN` pin near the USB port that safely powers the board).*
4. Run a wire from the breadboard **Blue line (GND)** to a **GND pin** on the Mega, Uno, and ESP32.

> [!IMPORTANT]
> **Common Beginner Pitfall:** Why isn't my board turning on, or why are sensor readings jumping wildly? 
> **Solution:** If the `GND` across all three boards isn't physically connected to each other, they miscommunicate. The breadboard blue line guarantees they share the exact same `GND`.

---

## 🧠 Phase 2: Arduino Mega (Sensor Hub)

The Mega handles the parking slots, the gas sensor, and local alerts.

### 1. Parking Slots (IR Obstacle Sensors)
You have 4 IR sensors. Each sensor has 3 pins: `VCC`, `GND`, and `OUT`.
* Connect all 4 `GND` pins to the breadboard **Blue line**.
* Connect all 4 `VCC` pins to the breadboard **Red line**.
* Connect `OUT` of Sensor 1 to **Mega D22**.
* Connect `OUT` of Sensor 2 to **Mega D23**.
* Connect `OUT` of Sensor 3 to **Mega D24**.
* Connect `OUT` of Sensor 4 to **Mega D25**.

### 2. MQ-2 Gas Sensor
* Connect `GND` to **Blue line** and `VCC` to **Red line**.
* Connect the **Analog Output (A0)** pin of the sensor to the **A0** pin on the Mega.
> [!CAUTION]
> **MQ-2 Pitfall:** This sensor uses a tiny internal heater that draws a lot of power. If it feels warm to the touch, that is normal! If your Mega suddenly resets when the MQ-2 turns on, your power supply is too weak.

### 3. I2C LCD Display (16x2)
The LCD uses 4 wires:
* `GND` to **Blue line**, `VCC` to **Red line**.
* `SDA` to **Mega D20 (SDA)**.
* `SCL` to **Mega D21 (SCL)**.
> **LCD Pitfall:** Does your screen just show bright glowing squares? Take a small Philips screwdriver and twist the blue potentiometer screw on the back of the LCD until text appears.

### 4. Buzzer and LEDs
* **Red LED:** Long leg -> 220Ω Resistor -> **Mega D9**. Short leg -> GND.
* **Green LED:** Long leg -> 220Ω Resistor -> **Mega D10**. Short leg -> GND.
* **Active Buzzer:** Positive (+) leg -> **Mega D8**. Negative (-) leg -> GND.

---

## 🚪 Phase 3: Arduino Uno (Gate Controller)

The Uno manages the physical entrance to the parking lot.

### 1. Ultrasonic Sensor (Entrance)
The HC-SR04 has 4 pins.
* `VCC` to **Red line**, `GND` to **Blue line**.
* `Trig` to **Uno D6**.
* `Echo` to **Uno D7**.
> **Pitfall:** If the gate never opens or the Serial Monitor says distance is "999.0", you probably swapped Trig and Echo wires. Check them again!

### 2. IR Sensor (Exit)
* `VCC` and `GND` to the breadboard rails.
* `OUT` to **Uno D3**.

### 3. Servo Motor (Barrier Gate)
Servos have 3 wires (usually Brown, Red, and Orange/Yellow).
* **Brown wire (GND)** to **Blue line**.
* **Red wire (VCC)** to **Red line**.
* **Orange/Yellow wire (Signal)** to **Uno D9**.
> [!WARNING] 
> **Servo Pitfall:** NEVER plug the Servo's Red wire directly into the Uno's 5V pin. Servos spike in power draw when moving and will crash the Uno. Always plug it directly into the Breadboard's Red line!

### 4. Traffic LEDs
* **Red LED:** Long leg -> 220Ω Resistor -> **Uno D4**. Short leg -> GND.
* **Green LED:** Long leg -> 220Ω Resistor -> **Uno D5**. Short leg -> GND.

---

## 📡 Phase 4: Inter-MCU Communication (The Boss Fight)

This is the hardest part. The Mega and Uno need to send text messages to the ESP32 telling it what is going on.

Arduinos talk at **5-Volts**. The ESP32 listens at **3.3-Volts**. If you plug a 5V wire directly into the ESP32, you will burn the ESP32 chip forever. We must fix this using a **Voltage Divider**.

### Building the Voltage Divider
You need this setup **twice** (once for the Mega -> ESP32, and once for the Uno -> ESP32).
You will need a **1KΩ resistor** and a **2KΩ resistor**.

**How to wire it:**
1. Stick the **1KΩ resistor** and the **2KΩ resistor** into the breadboard so they touch at a single row (let's call this the `Middle Row`).
2. Connect the other end of the **2KΩ resistor** directly to **GND** (Blue line). 

**Connecting the Mega:**
3. Run a wire from **Mega D18 (TX1)** to the free end of the **1KΩ resistor**.
4. Run a wire from the `Middle Row` to **ESP32 GPIO16 (RX2)**.
5. Run a wire straight from **Mega D19 (RX1)** to **ESP32 GPIO17 (TX2)** (No resistors needed here, 3.3V is safe for the Mega).

**Connecting the Uno:**
6. Build a second voltage divider just like above.
7. Run a wire from **Uno D1 (TX)** to the free end of the new **1KΩ resistor**.
8. Run a wire from the new `Middle Row` to **ESP32 GPIO4 (RX1)**.
9. Run a wire straight from **Uno D0 (RX)** to **ESP32 GPIO5 (TX1)**.

> [!CAUTION]
> **Uno Programming Pitfall:** When you upload code from your PC to the Arduino Uno, the Uno uses `D0` and `D1` to talk to your PC over USB. If the ESP32 is plugged into them at the same time, the upload will **fail**. Simply unplug the wire inside `Uno D0` while you are uploading code, and plug it back in after it's done.

---

## 🎉 Final Checks

1. Are all three boards plugged into the exact same **Ground (Blue line)**?
2. Are the 1K/2K resistors correctly dropping the voltage on the TX pins?
3. Turn on the power supply. Do you see lights on all three boards? 

If everything lights up, wait about 15 seconds for the Mega to calibrate the gas sensor, and then check your phone/PC dashboard. You've officially wired a distributed IoT system!
