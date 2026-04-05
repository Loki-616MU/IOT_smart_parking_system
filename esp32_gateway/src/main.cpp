/*
 * ================================================================
 *  SMART PARKING SYSTEM v4.0 — ESP32 Network Gateway
 * ================================================================
 *  Role: WiFi, WebSocket dashboard, REST API, inter-MCU coordinator
 *  Receives sensor data from Mega on Serial2 (GPIO16/17) @ 115200
 *  Receives gate events from Uno on Serial1 (GPIO4/5) @ 9600
 *  Zero I2C, zero sensors — pure network gateway
 * ================================================================
 */

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <esp_timer.h>

// ─── WiFi Credentials (CHANGE THESE) ───────────────────────
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// ─── UART Pin Mapping ──────────────────────────────────────
#define MEGA_RX  16   // ESP32 RX2 ← Mega TX1 (via voltage divider!)
#define MEGA_TX  17   // ESP32 TX2 → Mega RX1
#define UNO_RX   4    // ESP32 RX1 ← Uno TX  (via voltage divider!)
#define UNO_TX   5    // ESP32 TX1 → Uno RX

#define TOTAL_SLOTS 4

// ─── System State (POD struct for safe copying) ────────────
struct SystemState {
  bool  slotOccupied[TOTAL_SLOTS];
  int   availableSlots;
  float gasPPM;
  int   gasRaw;
  char  gasStatus[10];
  bool  calibrating;
  char  gateStatus[10];
  int   vehicleCount;
  uint32_t parkedMinutes[TOTAL_SLOTS];
  char  rtcTime[10];
  bool  megaOnline;
  bool  unoOnline;
  unsigned long megaLastSeen;
  unsigned long unoLastSeen;
} state;

SemaphoreHandle_t stateMutex;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// ─── Dashboard HTML (PROGMEM) ──────────────────────────────
const char DASHBOARD_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Smart Parking Dashboard</title>
<link href="https://fonts.googleapis.com/css2?family=Inter:wght@400;600;900&display=swap" rel="stylesheet">
<style>
:root{--bg:#060a13;--glass:rgba(255,255,255,0.03);--border:rgba(255,255,255,0.07);--text:#e2e8f0;--dim:#64748b;--green:#10b981;--red:#ef4444;--yellow:#f59e0b;--blue:#3b82f6;--purple:#a78bfa;--radius:16px}
*{box-sizing:border-box;margin:0}
body{font-family:'Inter',sans-serif;background:var(--bg);color:var(--text);padding:16px;display:flex;justify-content:center}
.c{max-width:860px;width:100%}
.hdr{text-align:center;padding:20px;background:var(--glass);border:1px solid var(--border);border-radius:var(--radius);margin-bottom:16px}
.hdr h1{font-size:1.5rem;background:linear-gradient(45deg,#818cf8,#f472b6);-webkit-background-clip:text;-webkit-text-fill-color:transparent}
.hdr p{font-size:.75rem;color:var(--dim);margin-top:4px}
.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin-bottom:16px}
.cd{background:var(--glass);border:1px solid var(--border);border-radius:12px;padding:14px;text-align:center}
.v{font-size:1.6rem;font-weight:900}.lb{font-size:.55rem;color:var(--dim);text-transform:uppercase;margin-top:4px;font-weight:700}
.gas{background:var(--glass);border:1px solid var(--border);border-radius:var(--radius);padding:18px;margin-bottom:16px}
.gb{height:6px;background:rgba(255,255,255,.05);border-radius:3px;margin:10px 0;overflow:hidden}
.gf{height:100%;transition:width .5s}
.slots{display:grid;grid-template-columns:1fr 1fr;gap:10px;margin-bottom:16px}
.sl{background:var(--glass);border:1px solid var(--border);border-radius:var(--radius);padding:14px;display:flex;align-items:center;gap:14px}
.sl.occ{border-color:var(--red)}.sl.avl{border-color:var(--green)}
.si{font-size:1.8rem;width:44px;height:44px;display:flex;align-items:center;justify-content:center;background:rgba(255,255,255,.03);border-radius:10px}
.gate{background:var(--glass);border:1px solid var(--border);border-radius:var(--radius);padding:18px;margin-bottom:16px;display:flex;justify-content:space-between;align-items:center}
.gate button{padding:8px 20px;border:none;border-radius:8px;font-weight:900;cursor:pointer;font-size:.8rem;transition:transform .1s}
.gate button:active{transform:scale(.95)}
.nodes{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.nd{background:var(--glass);border:1px solid var(--border);border-radius:12px;padding:12px;display:flex;align-items:center;gap:10px}
.dot{width:10px;height:10px;border-radius:50%}
.conn{position:fixed;top:10px;right:10px;font-size:.6rem;padding:4px 10px;border-radius:12px;font-weight:700;z-index:100}
.ov{position:fixed;inset:0;background:var(--bg);display:flex;flex-direction:column;align-items:center;justify-content:center;z-index:1000;transition:opacity .6s}
.sp{width:36px;height:36px;border:3px solid rgba(255,255,255,.1);border-top-color:var(--blue);border-radius:50%;animation:r 1s linear infinite;margin-bottom:12px}
@keyframes r{to{transform:rotate(360deg)}}
</style></head><body>
<div id="ld" class="ov"><div class="sp"></div><div style="font-weight:700">Connecting & Calibrating...</div></div>
<div id="tag" class="conn" style="background:rgba(239,68,68,.2);color:var(--red)">OFFLINE</div>
<div class="c">
<div class="hdr"><h1>🅿️ Smart Parking System</h1><p>v4.0 Multi-MCU Edition — ESP32 + Mega + Uno</p></div>
<div class="stats">
  <div class="cd"><div class="v" id="av" style="color:var(--green)">-</div><div class="lb">Available</div></div>
  <div class="cd"><div class="v" id="oc" style="color:var(--red)">-</div><div class="lb">Occupied</div></div>
  <div class="cd"><div class="v" id="up" style="color:var(--blue);font-size:1.2rem">--</div><div class="lb">Uptime</div></div>
</div>
<div class="gas">
  <div style="display:flex;justify-content:space-between;font-size:.7rem;font-weight:700;color:var(--dim);text-transform:uppercase"><span>Air Quality</span><span id="gs">Normal</span></div>
  <div style="margin-top:8px;font-size:2.2rem;font-weight:900" id="gv">0<span style="font-size:.7rem;color:var(--dim);margin-left:8px">PPM</span></div>
  <div class="gb"><div id="gbar" class="gf" style="width:0%;background:var(--green)"></div></div>
</div>
<div class="slots" id="sg"></div>
<div class="gate" id="gp">
  <div><div style="font-size:.65rem;font-weight:700;color:var(--dim);text-transform:uppercase">Gate Barrier</div>
  <div style="font-weight:900;font-size:1.1rem;margin-top:4px" id="gt">Closed</div>
  <div style="font-size:.7rem;color:var(--dim);margin-top:2px">Vehicles: <span id="vc" style="font-weight:900;color:var(--text)">0</span></div></div>
  <button id="gb" onclick="gateCmd()" style="background:var(--green);color:#000">Open Gate</button>
</div>
<div class="nodes">
  <div class="nd"><div class="dot" id="md" style="background:var(--red)"></div><div><div style="font-size:.6rem;color:var(--dim);font-weight:700">MEGA</div><div style="font-weight:900;font-size:.8rem" id="ms">Offline</div></div></div>
  <div class="nd"><div class="dot" id="ud" style="background:var(--red)"></div><div><div style="font-size:.6rem;color:var(--dim);font-weight:700">UNO</div><div style="font-weight:900;font-size:.8rem" id="us">Offline</div></div></div>
</div>
</div>
<script>
let ws,gateOpen=false;
function init(){
  ws=new WebSocket('ws://'+location.hostname+'/ws');
  ws.onopen=()=>{document.getElementById('tag').style.background='rgba(16,185,129,.2)';document.getElementById('tag').style.color='#10b981';document.getElementById('tag').innerText='LIVE'};
  ws.onclose=()=>setTimeout(init,2000);
  ws.onmessage=e=>{
    const d=JSON.parse(e.data);
    if(d.calibrating){return}
    document.getElementById('ld').style.opacity='0';setTimeout(()=>document.getElementById('ld').style.display='none',600);
    document.getElementById('av').innerText=d.available;
    document.getElementById('oc').innerText=d.occupied;
    document.getElementById('up').innerText=d.uptime||'--';
    document.getElementById('gv').firstChild.textContent=Math.round(d.gasPPM||0);
    let s=d.gasStatus||'Normal',c='var(--green)',w=Math.min((d.gasPPM||0)/10,100)+'%';
    if(s=='Warning'){c='var(--yellow)'}else if(s=='Danger'){c='var(--red)'}
    document.getElementById('gs').innerText=s;document.getElementById('gs').style.color=c;
    document.getElementById('gbar').style.width=w;document.getElementById('gbar').style.background=c;
    let h='';(d.slots||[]).forEach(s=>{
      let cl=s.occupied?'occ':'avl',t=s.occupied?'Occupied':'Available',ic=s.occupied?'🚗':'🅿️',co=s.occupied?'var(--red)':'var(--green)';
      h+='<div class="sl '+cl+'"><div class="si">'+ic+'</div><div><div style="font-size:.65rem;color:var(--dim);font-weight:800;text-transform:uppercase">Slot '+s.id+'</div><div style="font-weight:900;color:'+co+'">'+t+'</div></div></div>';
    });
    document.getElementById('sg').innerHTML=h;
    let go=(d.gate||'closed')!='closed';gateOpen=go;
    document.getElementById('gt').innerText=go?'Open':'Closed';document.getElementById('gt').style.color=go?'var(--green)':'var(--text)';
    document.getElementById('vc').innerText=d.vehicleCount||0;
    let btn=document.getElementById('gb');btn.innerText=go?'Close Gate':'Open Gate';btn.style.background=go?'var(--red)':'var(--green)';
    document.getElementById('md').style.background=d.megaOnline?'var(--green)':'var(--red)';document.getElementById('ms').innerText=d.megaOnline?'Online':'Offline';
    document.getElementById('ud').style.background=d.unoOnline?'var(--green)':'var(--red)';document.getElementById('us').innerText=d.unoOnline?'Online':'Offline';
  };
}
function gateCmd(){if(ws&&ws.readyState===1)ws.send(JSON.stringify({cmd:'gate',action:gateOpen?'close':'open'}))}
init();
</script></body></html>
)rawliteral";

// ─── Build JSON from State ─────────────────────────────────

String buildStateJson() {
  // Fast copy under lock
  SystemState local;
  xSemaphoreTake(stateMutex, portMAX_DELAY);
  memcpy(&local, &state, sizeof(SystemState));
  xSemaphoreGive(stateMutex);

  // Build JSON outside lock (no blocking)
  JsonDocument doc;
  doc["calibrating"] = local.calibrating;
  doc["available"]   = local.availableSlots;
  doc["occupied"]    = TOTAL_SLOTS - local.availableSlots;
  doc["total"]       = TOTAL_SLOTS;

  // 64-bit uptime — no rollover for 584,942 years
  uint64_t sec = esp_timer_get_time() / 1000000ULL;
  char ut[20];
  snprintf(ut, sizeof(ut), "%lldh %lldm %llds",
    (long long)(sec / 3600),
    (long long)((sec % 3600) / 60),
    (long long)(sec % 60));
  doc["uptime"] = ut;

  doc["gasPPM"]    = local.gasPPM;
  doc["gasLevel"]  = local.gasRaw;
  doc["gasStatus"] = local.gasStatus;
  doc["gate"]      = local.gateStatus;
  doc["vehicleCount"] = local.vehicleCount;
  doc["megaOnline"] = local.megaOnline;
  doc["unoOnline"]  = local.unoOnline;
  doc["buzzer"]     = (strcmp(local.gasStatus, "Normal") != 0);
  doc["ledRed"]     = (local.availableSlots == 0 || strcmp(local.gasStatus, "Normal") != 0);
  doc["ledGreen"]   = (local.availableSlots > 0 && strcmp(local.gasStatus, "Normal") == 0);
  doc["rtcTime"]    = local.rtcTime;

  JsonArray bill = doc["bill"].to<JsonArray>();
  for (int i = 0; i < TOTAL_SLOTS; i++) {
    bill.add(local.parkedMinutes[i]);
  }

  JsonArray slots = doc["slots"].to<JsonArray>();
  for (int i = 0; i < TOTAL_SLOTS; i++) {
    JsonObject sl = slots.add<JsonObject>();
    sl["id"] = i + 1;
    sl["occupied"] = local.slotOccupied[i];
  }

  String out;
  serializeJson(doc, out);
  return out;
}

void notifyClients() {
  if (ws.count() == 0) return;
  ws.textAll(buildStateJson());
}

// ─── FreeRTOS Task: Read Mega Serial2 ──────────────────────

void megaReaderTask(void* p) {
  String buf = "";
  for (;;) {
    while (Serial2.available()) {
      char c = Serial2.read();
      if (c == '\n') {
        JsonDocument doc;
        if (deserializeJson(doc, buf) == DeserializationError::Ok && doc["t"] == "S") {
          xSemaphoreTake(stateMutex, portMAX_DELAY);

          state.calibrating = doc["cal"] | false;
          JsonArray slots = doc["slots"];
          int avail = 0;
          for (int i = 0; i < TOTAL_SLOTS && i < (int)slots.size(); i++) {
            state.slotOccupied[i] = (slots[i].as<int>() == 1);
            if (!state.slotOccupied[i]) avail++;
          }
          state.availableSlots = avail;
          state.gasPPM = doc["ppm"] | 0.0f;
          state.gasRaw = doc["raw"] | 0;
          strlcpy(state.gasStatus, doc["gas"] | "Normal", sizeof(state.gasStatus));
          strlcpy(state.rtcTime, doc["time"] | "--:--:--", sizeof(state.rtcTime));
          
          JsonArray bill = doc["bill"];
          for (int i = 0; i < TOTAL_SLOTS && i < (int)bill.size(); i++) {
            state.parkedMinutes[i] = bill[i];
          }

          state.megaOnline = true;
          state.megaLastSeen = millis();

          xSemaphoreGive(stateMutex);

          // Forward availability to Uno for gate decisions
          JsonDocument cmd;
          cmd["cmd"] = "avail";
          cmd["n"] = avail;
          String out;
          serializeJson(cmd, out);
          Serial1.println(out);

          notifyClients();
        }
        buf = "";
      } else if (buf.length() < 300) {
        buf += c;
      }
    }
    vTaskDelay(10);
  }
}

// ─── FreeRTOS Task: Read Uno Serial1 ───────────────────────

void unoReaderTask(void* p) {
  String buf = "";
  for (;;) {
    while (Serial1.available()) {
      char c = Serial1.read();
      if (c == '\n') {
        JsonDocument doc;
        if (deserializeJson(doc, buf) == DeserializationError::Ok && doc["t"] == "G") {
          xSemaphoreTake(stateMutex, portMAX_DELAY);
          strlcpy(state.gateStatus, doc["gate"] | "closed", sizeof(state.gateStatus));
          state.vehicleCount = doc["count"] | 0;
          state.unoOnline = true;
          state.unoLastSeen = millis();
          xSemaphoreGive(stateMutex);

          notifyClients();
        }
        buf = "";
      } else if (buf.length() < 200) {
        buf += c;
      }
    }
    vTaskDelay(10);
  }
}

// ─── FreeRTOS Task: Node Health Monitor ────────────────────

void healthTask(void* p) {
  for (;;) {
    xSemaphoreTake(stateMutex, portMAX_DELAY);
    unsigned long now = millis();
    if (now - state.megaLastSeen > 5000) state.megaOnline = false;
    if (now - state.unoLastSeen > 5000)  state.unoOnline = false;
    xSemaphoreGive(stateMutex);
    vTaskDelay(2000);
  }
}

// ─── Setup ─────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);  // USB debug
  Serial2.begin(115200, SERIAL_8N1, MEGA_RX, MEGA_TX);
  Serial1.begin(9600, SERIAL_8N1, UNO_RX, UNO_TX);

  stateMutex = xSemaphoreCreateMutex();

  // Init state
  memset(&state, 0, sizeof(state));
  state.calibrating = true;
  strlcpy(state.gasStatus, "Normal", sizeof(state.gasStatus));
  strlcpy(state.gateStatus, "closed", sizeof(state.gateStatus));

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[INFO] Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[OK] WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());

  // HTTP routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send_P(200, "text/html", DASHBOARD_HTML);
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* r) {
    r->send(200, "application/json", buildStateJson());
  });

  // WebSocket
  ws.onEvent([](AsyncWebSocket* s, AsyncWebSocketClient* c,
                AwsEventType t, void* a, uint8_t* d, size_t l) {
    if (t == WS_EVT_CONNECT) {
      notifyClients();
    }
    if (t == WS_EVT_DATA) {
      JsonDocument doc;
      if (deserializeJson(doc, d, l) == DeserializationError::Ok) {
        if (doc["cmd"] == "gate") {
          String out;
          serializeJson(doc, out);
          Serial1.println(out);  // Forward to Uno
        }
      }
    }
  });

  server.addHandler(&ws);
  server.begin();

  // FreeRTOS tasks
  xTaskCreatePinnedToCore(megaReaderTask, "MegaRdr", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(unoReaderTask,  "UnoRdr",  4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(healthTask,     "Health",  2048, NULL, 1, NULL, 0);

  Serial.println("[OK] ESP32 Gateway started. Waiting for nodes...");
}

// ─── Loop ──────────────────────────────────────────────────

void loop() {
  ws.cleanupClients();
  static unsigned long lb = 0;
  if (millis() - lb > 3000) {
    lb = millis();
    notifyClients();
  }
  vTaskDelay(1000);
}
