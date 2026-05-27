/*
  =====================================================
  ESP32 Smart Pump Controller v7.0
  Jamia Millia Islamia University
  =====================================================
  SENSORS:
  - DHT11        → GPIO4
  - pH Sensor    → GPIO34 (analog)
  - MQ135        → GPIO27 (digital)
  - Soil Sensor  → GPIO26 (analog AO)
  - NPK Sensor   → GPIO16(RX2), GPIO17(TX2), GPIO5(RE/DE)
  - INA219       → GPIO21(SDA), GPIO22(SCL)
  - OLED SSD1306 → GPIO21(SDA), GPIO22(SCL)
  - Relay        → GPIO13

  WiFi: ESP32 creates its own Access Point
  - Connect phone/laptop to WiFi: "SmartFarm"
  - Password: "12345678"
  - Open browser: http://192.168.4.1
  - Dashboard auto-refreshes every 3 seconds

  PUMP LOGIC:
  - Soil moisture BELOW 30% → Pump ON
  - Soil moisture ABOVE 30% → Pump OFF
  =====================================================
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_INA219.h>
#include <DHT.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <WebServer.h>

// =====================================================
// WiFi ACCESS POINT SETTINGS
// =====================================================
const char* AP_SSID     = "SmartFarm";       // WiFi name
const char* AP_PASSWORD = "12345678";         // WiFi password (min 8 chars)
// Connect to this WiFi then open: http://192.168.4.1

// =====================================================
// PIN DEFINITIONS
// =====================================================
#define DHTPIN            4
#define DHTTYPE           DHT11
#define PH_PIN            34
#define MQ135_PIN         27
#define SOIL_PIN          26
#define RE_DE             5
#define RELAY_PIN         13

// =====================================================
// SETTINGS
// =====================================================
#define PUMP_ON_THRESHOLD 30
#define DRY_VAL           4095
#define WET_VAL           800
#define MQ135_WARMUP_MS   180000

// =====================================================
// OBJECTS
// =====================================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306  display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_INA219   ina219;
DHT               dht(DHTPIN, DHTTYPE);
HardwareSerial    mySerial(2);
WebServer         server(80);

// =====================================================
// NPK COMMANDS — ZTS-3002-TR-NPK-N01 (4800 baud)
// =====================================================
byte requestN[]     = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};
byte requestP[]     = {0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0xD5, 0xCA};
byte requestK[]     = {0x01, 0x03, 0x00, 0x02, 0x00, 0x01, 0x25, 0xCA};
byte requestN_alt[] = {0x01, 0x03, 0x00, 0x1E, 0x00, 0x01, 0xE4, 0x0C};
byte requestP_alt[] = {0x01, 0x03, 0x00, 0x1F, 0x00, 0x01, 0xB5, 0xCC};
byte requestK_alt[] = {0x01, 0x03, 0x00, 0x20, 0x00, 0x01, 0x85, 0xC0};
byte response[7];

// =====================================================
// GLOBAL SENSOR DATA
// =====================================================
float temperature  = 0;
float humidity     = 0;
float phValue      = 0;
int   soilPercent  = 0;
bool  airGood      = true;
bool  mq135Ready   = false;
bool  dhtOK        = false;
bool  pumpOn       = false;
bool  ina219OK     = false;
int   npkN         = -1;
int   npkP         = -1;
int   npkK         = -1;
float solarVoltage = 0;
float solarCurrent = 0;
float solarPower   = 0;

int           currentPage = 0;
unsigned long lastSwitch  = 0;
unsigned long lastDHTRead = 0;
#define PAGE_INTERVAL 3000

// =====================================================
// HTML DASHBOARD
// =====================================================
String buildHTML() {
  String html = R"rawhtml(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<meta http-equiv="refresh" content="3">
<title>SmartFarm Dashboard</title>
<style>
  @import url('https://fonts.googleapis.com/css2?family=Share+Tech+Mono&family=Exo+2:wght@300;600;800&display=swap');
  :root {
    --bg:      #0a0f0d;
    --panel:   #111a15;
    --border:  #1e3a28;
    --accent:  #00e676;
    --accent2: #69f0ae;
    --warn:    #ff5252;
    --solar:   #ffab40;
    --text:    #e0f2e9;
    --muted:   #4a7a5a;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    background: var(--bg);
    color: var(--text);
    font-family: 'Exo 2', sans-serif;
    min-height: 100vh;
    padding: 16px;
  }
  body::before {
    content: '';
    position: fixed; inset: 0;
    background: radial-gradient(ellipse at 20% 20%, #00261580 0%, transparent 60%),
                radial-gradient(ellipse at 80% 80%, #00140a80 0%, transparent 60%);
    pointer-events: none;
    z-index: 0;
  }
  .container { position: relative; z-index: 1; max-width: 700px; margin: 0 auto; }
  header {
    text-align: center;
    padding: 20px 0 24px;
    border-bottom: 1px solid var(--border);
    margin-bottom: 20px;
  }
  .logo {
    font-size: 11px;
    letter-spacing: 4px;
    color: var(--muted);
    text-transform: uppercase;
    margin-bottom: 6px;
    font-family: 'Share Tech Mono', monospace;
  }
  h1 {
    font-size: 28px;
    font-weight: 800;
    color: var(--accent);
    letter-spacing: -0.5px;
    text-shadow: 0 0 30px #00e67640;
  }
  .status-bar {
    display: flex;
    justify-content: center;
    align-items: center;
    gap: 6px;
    margin-top: 8px;
    font-family: 'Share Tech Mono', monospace;
    font-size: 11px;
    color: var(--muted);
  }
  .dot {
    width: 7px; height: 7px;
    border-radius: 50%;
    background: var(--accent);
    animation: pulse 2s infinite;
  }
  @keyframes pulse {
    0%,100% { opacity: 1; box-shadow: 0 0 0 0 #00e67660; }
    50%      { opacity: .7; box-shadow: 0 0 0 5px #00e67600; }
  }
  .grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 12px;
    margin-bottom: 12px;
  }
  .grid-3 {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    gap: 12px;
    margin-bottom: 12px;
  }
  .card {
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 16px;
    position: relative;
    overflow: hidden;
    transition: border-color .2s;
  }
  .card::before {
    content: '';
    position: absolute;
    top: 0; left: 0; right: 0;
    height: 2px;
    background: linear-gradient(90deg, transparent, var(--accent), transparent);
    opacity: 0.5;
  }
  .card.warn::before  { background: linear-gradient(90deg, transparent, var(--warn), transparent); }
  .card.solar::before { background: linear-gradient(90deg, transparent, var(--solar), transparent); }
  .card-label {
    font-size: 10px;
    letter-spacing: 2px;
    text-transform: uppercase;
    color: var(--muted);
    font-family: 'Share Tech Mono', monospace;
    margin-bottom: 8px;
  }
  .card-value {
    font-size: 36px;
    font-weight: 800;
    color: var(--accent);
    line-height: 1;
    letter-spacing: -1px;
  }
  .card-value.warn  { color: var(--warn); }
  .card-value.solar { color: var(--solar); }
  .card-value.small { font-size: 24px; }
  .card-unit {
    font-size: 13px;
    color: var(--muted);
    margin-top: 4px;
    font-family: 'Share Tech Mono', monospace;
  }
  .card-sub {
    font-size: 11px;
    color: var(--muted);
    margin-top: 6px;
    font-family: 'Share Tech Mono', monospace;
  }
  .pump-card {
    grid-column: span 2;
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 18px 20px;
  }
  .pump-label { font-size: 12px; letter-spacing: 3px; text-transform: uppercase; color: var(--muted); font-family: 'Share Tech Mono', monospace; }
  .pump-status {
    font-size: 28px;
    font-weight: 800;
    letter-spacing: 2px;
  }
  .pump-on  { color: var(--accent); text-shadow: 0 0 20px #00e67680; }
  .pump-off { color: var(--muted); }
  .pump-reason { font-size: 11px; color: var(--muted); margin-top: 4px; font-family: 'Share Tech Mono', monospace; }
  .section-title {
    font-size: 10px;
    letter-spacing: 3px;
    text-transform: uppercase;
    color: var(--muted);
    font-family: 'Share Tech Mono', monospace;
    margin: 16px 0 10px;
    padding-left: 4px;
    border-left: 2px solid var(--accent);
    padding-left: 8px;
  }
  .npk-row {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    gap: 12px;
  }
  .npk-card {
    background: var(--panel);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 14px;
    text-align: center;
  }
  .npk-letter {
    font-size: 28px;
    font-weight: 800;
    color: var(--accent2);
    line-height: 1;
  }
  .npk-val {
    font-size: 20px;
    font-weight: 600;
    color: var(--text);
    margin-top: 4px;
    font-family: 'Share Tech Mono', monospace;
  }
  .npk-unit {
    font-size: 10px;
    color: var(--muted);
    font-family: 'Share Tech Mono', monospace;
  }
  .solar-row {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    gap: 12px;
  }
  .solar-card {
    background: var(--panel);
    border: 1px solid #3a2800;
    border-radius: 12px;
    padding: 14px;
    text-align: center;
  }
  .solar-card::before {
    content: '';
    position: absolute;
    top: 0; left: 0; right: 0;
    height: 2px;
    background: linear-gradient(90deg, transparent, var(--solar), transparent);
    opacity: .5;
  }
  .solar-card { position: relative; overflow: hidden; }
  .solar-icon { font-size: 20px; margin-bottom: 4px; }
  .solar-val  { font-size: 22px; font-weight: 700; color: var(--solar); font-family: 'Share Tech Mono', monospace; }
  .solar-unit { font-size: 10px; color: var(--muted); font-family: 'Share Tech Mono', monospace; }
  .bar-wrap {
    background: #0d1f14;
    border-radius: 4px;
    height: 6px;
    margin-top: 10px;
    overflow: hidden;
  }
  .bar-fill {
    height: 100%;
    border-radius: 4px;
    background: linear-gradient(90deg, var(--accent), var(--accent2));
    transition: width .5s ease;
  }
  .bar-fill.warn { background: linear-gradient(90deg, #ff5252, #ff8a65); }
  .bar-fill.solar { background: linear-gradient(90deg, var(--solar), #fff176); }
  footer {
    text-align: center;
    padding: 20px 0 10px;
    font-size: 10px;
    color: var(--muted);
    font-family: 'Share Tech Mono', monospace;
    letter-spacing: 1px;
    border-top: 1px solid var(--border);
    margin-top: 20px;
  }
  .badge {
    display: inline-block;
    padding: 2px 8px;
    border-radius: 4px;
    font-size: 10px;
    font-family: 'Share Tech Mono', monospace;
    letter-spacing: 1px;
    font-weight: 600;
  }
  .badge-ok   { background: #00261580; color: var(--accent); border: 1px solid #00e67630; }
  .badge-warn { background: #3d000080; color: var(--warn);   border: 1px solid #ff525230; }
  .badge-idle { background: #1a1a2080; color: var(--muted);  border: 1px solid #33335030; }
</style>
</head>
<body>
<div class="container">

  <header>
    <div class="logo">Jamia Millia Islamia University</div>
    <h1>SmartFarm Dashboard</h1>
    <div class="status-bar">
      <div class="dot"></div>
      LIVE &nbsp;·&nbsp; AUTO-REFRESH 3s &nbsp;·&nbsp; ESP32 AP MODE
    </div>
  </header>

  <!-- PUMP STATUS -->
  <div class="section-title">Pump Control</div>
  <div class="card pump-card )rawhtml";

  html += pumpOn ? "" : "";
  html += R"rawhtml(">
    <div>
      <div class="pump-label">Water Pump</div>
      <div class="pump-status )rawhtml";
  html += pumpOn ? "pump-on\">⬤ RUNNING" : "pump-off\">◯ STANDBY";
  html += R"rawhtml(</div>
      <div class="pump-reason">)rawhtml";
  if (pumpOn)
    html += "Soil moisture below " + String(PUMP_ON_THRESHOLD) + "% threshold";
  else
    html += "Soil moisture sufficient (&ge;" + String(PUMP_ON_THRESHOLD) + "%)";
  html += R"rawhtml(</div>
    </div>
    <div>
      <span class="badge )rawhtml";
  html += soilPercent < PUMP_ON_THRESHOLD ? "badge-warn\">DRY" : "badge-ok\">MOIST";
  html += R"rawhtml(</span>
    </div>
  </div>

  <!-- ENVIRONMENT -->
  <div class="section-title">Environment</div>
  <div class="grid">

    <div class="card)rawhtml";
  html += dhtOK ? "" : " warn";
  html += R"rawhtml(">
      <div class="card-label">Temperature</div>
      <div class="card-value)rawhtml";
  html += dhtOK ? "" : " warn";
  html += "\">";
  html += dhtOK ? String(temperature, 1) : "ERR";
  html += R"rawhtml(</div>
      <div class="card-unit">°C &nbsp; Celsius</div>)rawhtml";
  if (!dhtOK) html += "<div class=\"card-sub\">Add 10k resistor DATA→3.3V</div>";
  html += R"rawhtml(
    </div>

    <div class="card)rawhtml";
  html += dhtOK ? "" : " warn";
  html += R"rawhtml(">
      <div class="card-label">Humidity</div>
      <div class="card-value)rawhtml";
  html += dhtOK ? "" : " warn";
  html += "\">";
  html += dhtOK ? String(humidity, 1) : "ERR";
  html += R"rawhtml(</div>
      <div class="card-unit">% &nbsp; Relative Humidity</div>
      <div class="bar-wrap"><div class="bar-fill" style="width:)rawhtml";
  html += dhtOK ? String(humidity, 0) : "0";
  html += R"rawhtml(%"></div></div>
    </div>

    <div class="card">
      <div class="card-label">Soil Moisture</div>
      <div class="card-value">)rawhtml";
  html += String(soilPercent);
  html += R"rawhtml(</div>
      <div class="card-unit">% &nbsp; )rawhtml";
  if      (soilPercent < 20) html += "VERY DRY";
  else if (soilPercent < 30) html += "DRY";
  else if (soilPercent < 60) html += "MOIST";
  else if (soilPercent < 80) html += "WET";
  else                        html += "SATURATED";
  html += R"rawhtml(</div>
      <div class="bar-wrap"><div class="bar-fill)rawhtml";
  html += soilPercent < 30 ? " warn" : "";
  html += "\" style=\"width:" + String(soilPercent) + "%\"></div></div>";
  html += R"rawhtml(
    </div>

    <div class="card">
      <div class="card-label">pH Value</div>
      <div class="card-value small)rawhtml";
  html += (phValue < 5.5 || phValue > 7.5) ? " warn" : "";
  html += "\">" + String(phValue, 2);
  html += R"rawhtml(</div>
      <div class="card-unit">)rawhtml";
  if      (phValue < 5.5) html += "⚠ ACIDIC";
  else if (phValue > 7.5) html += "⚠ ALKALINE";
  else                    html += "✓ NORMAL RANGE";
  html += R"rawhtml(</div>
      <div class="card-sub">Optimal: pH 5.5 – 7.5</div>
    </div>

  </div>

  <!-- AIR QUALITY -->
  <div class="grid" style="grid-template-columns:1fr;">
    <div class="card)rawhtml";
  html += (!mq135Ready || !airGood) ? " warn" : "";
  html += R"rawhtml(">
      <div class="card-label">Air Quality (MQ135)</div>
      <div class="card-value small)rawhtml";
  html += (!mq135Ready || !airGood) ? " warn" : "";
  html += "\">";
  if      (!mq135Ready) html += "WARMING UP";
  else if (airGood)     html += "GOOD";
  else                  html += "POOR";
  html += R"rawhtml(</div>
      <div class="card-unit">)rawhtml";
  if (!mq135Ready) html += "Sensor needs 3 min warmup after power on";
  else if (airGood) html += "Air quality is acceptable";
  else              html += "⚠ Poor air quality detected";
  html += R"rawhtml(</div>
    </div>
  </div>

  <!-- NPK -->
  <div class="section-title">Soil Nutrients — NPK</div>
  <div class="npk-row">
    <div class="npk-card">
      <div class="npk-letter">N</div>
      <div class="npk-val">)rawhtml";
  html += npkN == -1 ? "--" : String(npkN);
  html += R"rawhtml(</div>
      <div class="npk-unit">Nitrogen mg/kg</div>
    </div>
    <div class="npk-card">
      <div class="npk-letter">P</div>
      <div class="npk-val">)rawhtml";
  html += npkP == -1 ? "--" : String(npkP);
  html += R"rawhtml(</div>
      <div class="npk-unit">Phosphorus mg/kg</div>
    </div>
    <div class="npk-card">
      <div class="npk-letter">K</div>
      <div class="npk-val">)rawhtml";
  html += npkK == -1 ? "--" : String(npkK);
  html += R"rawhtml(</div>
      <div class="npk-unit">Potassium mg/kg</div>
    </div>
  </div>
  )rawhtml";
  if (npkN == -1) html += "<div style='font-size:11px;color:#ff5252;font-family:monospace;margin-top:6px;padding-left:4px;'>⚠ NPK not responding — check 12V power & RS485 wiring</div>";

  // SOLAR
  html += R"rawhtml(
  <div class="section-title">Solar Panel — INA219 Monitor</div>
  <div class="solar-row">
    <div class="solar-card">
      <div class="solar-icon">☀</div>
      <div class="solar-val">)rawhtml";
  html += ina219OK ? String(solarVoltage, 2) : "--";
  html += R"rawhtml(</div>
      <div class="solar-unit">VOLTAGE (V)</div>
    </div>
    <div class="solar-card">
      <div class="solar-icon">⚡</div>
      <div class="solar-val">)rawhtml";
  html += ina219OK ? String(solarCurrent, 1) : "--";
  html += R"rawhtml(</div>
      <div class="solar-unit">CURRENT (mA)</div>
    </div>
    <div class="solar-card">
      <div class="solar-icon">◈</div>
      <div class="solar-val">)rawhtml";
  html += ina219OK ? String(solarPower / 1000.0, 2) : "--";
  html += R"rawhtml(</div>
      <div class="solar-unit">POWER (W)</div>
    </div>
  </div>)rawhtml";
  if (!ina219OK) html += "<div style='font-size:11px;color:#ff5252;font-family:monospace;margin-top:6px;padding-left:4px;'>⚠ INA219 not found — check I2C wiring (SDA=21, SCL=22)</div>";

  html += R"rawhtml(

  <footer>
    SmartFarm v7.0 &nbsp;·&nbsp; ESP32 Access Point &nbsp;·&nbsp; 192.168.4.1<br>
    Jamia Millia Islamia University &nbsp;·&nbsp; Auto-refresh every 3 seconds
  </footer>

</div>
</body>
</html>)rawhtml";
  return html;
}

// =====================================================
// WEB SERVER HANDLER
// =====================================================
void handleRoot() {
  server.send(200, "text/html", buildHTML());
}

void handleData() {
  // JSON endpoint for raw data
  String json = "{";
  json += "\"temp\":"    + (dhtOK ? String(temperature,1) : String("null")) + ",";
  json += "\"hum\":"     + (dhtOK ? String(humidity,1)    : String("null")) + ",";
  json += "\"ph\":"      + String(phValue,2)      + ",";
  json += "\"soil\":"    + String(soilPercent)     + ",";
  json += "\"air\":\""  + String(mq135Ready ? (airGood ? "GOOD" : "POOR") : "WARMUP") + "\",";
  json += "\"N\":"       + String(npkN)            + ",";
  json += "\"P\":"       + String(npkP)            + ",";
  json += "\"K\":"       + String(npkK)            + ",";
  json += "\"pump\":\""  + String(pumpOn ? "ON" : "OFF") + "\",";
  json += "\"solarV\":"  + String(solarVoltage,2)  + ",";
  json += "\"solarmA\":" + String(solarCurrent,1)  + ",";
  json += "\"solarmW\":" + String(solarPower,1)    + "}";
  server.send(200, "application/json", json);
}

// =====================================================
// NPK READ
// =====================================================
int readNPK(byte* request) {
  while (mySerial.available()) mySerial.read();
  digitalWrite(RE_DE, HIGH); delay(10);
  mySerial.write(request, 8); mySerial.flush();
  digitalWrite(RE_DE, LOW);  delay(300);
  if (mySerial.available() >= 7) {
    for (int i = 0; i < 7; i++) response[i] = mySerial.read();
    if (response[0] == 0x01 && response[1] == 0x03)
      return (response[3] << 8) | response[4];
  }
  return -1;
}

// =====================================================
// READ DHT
// =====================================================
void readDHT() {
  if (millis() - lastDHTRead < 2000) return;
  lastDHTRead = millis();
  float t = NAN, h = NAN;
  for (int i = 0; i < 3; i++) {
    t = dht.readTemperature(); h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) break;
    delay(250);
  }
  if (!isnan(t) && !isnan(h) && t > -10 && t < 80 && h >= 0 && h <= 100) {
    temperature = t; humidity = h; dhtOK = true;
  } else { dhtOK = false; }
}

// =====================================================
// READ ALL SENSORS
// =====================================================
void readSensors() {
  readDHT();

  int phRaw = analogRead(PH_PIN);
  phValue   = 7.0 + ((2.5 - phRaw * (3.3 / 4095.0)) / 0.18);
  phValue   = constrain(phValue, 0.0, 14.0);

  if (millis() >= MQ135_WARMUP_MS) { mq135Ready = true; airGood = digitalRead(MQ135_PIN); }
  else mq135Ready = false;

  int soilRaw = analogRead(SOIL_PIN);
  soilPercent = constrain(map(soilRaw, DRY_VAL, WET_VAL, 0, 100), 0, 100);

  npkN = readNPK(requestN); delay(100);
  if (npkN == -1) { npkN = readNPK(requestN_alt); delay(100); }
  npkP = readNPK(requestP); delay(100);
  if (npkP == -1) { npkP = readNPK(requestP_alt); delay(100); }
  npkK = readNPK(requestK); delay(100);
  if (npkK == -1) { npkK = readNPK(requestK_alt); delay(100); }

  if (ina219OK) {
    solarVoltage = ina219.getBusVoltage_V();
    solarCurrent = max(0.0f, ina219.getCurrent_mA());
    solarPower   = ina219.getPower_mW();
  }
}

// =====================================================
// PUMP CONTROL
// =====================================================
void controlPump() {
  if (soilPercent < PUMP_ON_THRESHOLD) { digitalWrite(RELAY_PIN, LOW);  pumpOn = true;  }
  else                                  { digitalWrite(RELAY_PIN, HIGH); pumpOn = false; }
}

// =====================================================
// OLED PAGES
// =====================================================
void showOLED() {
  display.clearDisplay();
  display.setTextSize(1);

  if (currentPage == 0) {
    display.setCursor(0,0);  display.println("-- TEMP & HUMIDITY --");
    if (dhtOK) {
      display.setTextSize(2);
      display.setCursor(0,14); display.print(temperature,1); display.print((char)247); display.println("C");
      display.setCursor(0,36); display.print(humidity,1); display.println("%");
    } else {
      display.setCursor(0,20); display.println("DHT11 ERROR");
      display.setCursor(0,32); display.println("Add 10k resistor");
    }
  } else if (currentPage == 1) {
    display.setCursor(0,0); display.println("-- SOIL & pH --------");
    display.setTextSize(2);
    display.setCursor(0,14); display.print("S:"); display.print(soilPercent); display.println("%");
    display.setCursor(0,36); display.print("pH:"); display.println(phValue,1);
  } else if (currentPage == 2) {
    display.setCursor(0,0); display.println("-- NPK SENSOR -------");
    display.setTextSize(2);
    display.setCursor(0,14); display.print("N:"); display.println(npkN==-1?0:npkN);
    display.setCursor(0,32); display.print("P:"); display.println(npkP==-1?0:npkP);
    display.setCursor(0,50); display.print("K:"); display.println(npkK==-1?0:npkK);
  } else if (currentPage == 3) {
    display.setCursor(0,0); display.println("-- SOLAR & PUMP -----");
    display.setTextSize(1);
    display.setCursor(0,14); display.print("Solar: "); display.print(solarVoltage,1); display.print("V ");
    display.print(solarCurrent,0); display.println("mA");
    display.setCursor(0,26); display.print("Power: "); display.print(solarPower/1000.0,2); display.println("W");
    display.setCursor(0,38); display.print("Soil : "); display.print(soilPercent); display.println("%");
    display.setTextSize(2);
    display.setCursor(0,50); display.print("PUMP:"); display.println(pumpOn?"ON":"OFF");
  } else if (currentPage == 4) {
    display.setCursor(0,0); display.println("-- WiFi DASHBOARD ---");
    display.setTextSize(1);
    display.setCursor(0,14); display.println("Connect to WiFi:");
    display.setCursor(0,24); display.println("SmartFarm");
    display.setCursor(0,36); display.println("Open browser:");
    display.setTextSize(1);
    display.setCursor(0,48); display.println("http://192.168.4.1");
  }

  // Bottom pump bar (except WiFi page)
  if (currentPage != 2) {
    display.setTextSize(1);
    display.setCursor(0,57);
    display.print("PUMP:"); display.print(pumpOn?"ON  ":"OFF ");
    display.print("SOIL:"); display.print(soilPercent); display.print("%");
  }
  display.display();
}

// =====================================================
// CSV FOR PYTHON LOGGER
// =====================================================
void printCSVLine() {
  Serial.print("DATA,");
  Serial.print(dhtOK?String(temperature,1):"ERR"); Serial.print(",");
  Serial.print(dhtOK?String(humidity,1):"ERR");    Serial.print(",");
  Serial.print(phValue,2);    Serial.print(",");
  Serial.print(soilPercent);  Serial.print(",");
  Serial.print(mq135Ready?(airGood?"GOOD":"POOR"):"WARMUP"); Serial.print(",");
  Serial.print(npkN); Serial.print(",");
  Serial.print(npkP); Serial.print(",");
  Serial.print(npkK); Serial.print(",");
  Serial.print(pumpOn?"ON":"OFF"); Serial.print(",");
  Serial.print(solarVoltage,2); Serial.print(",");
  Serial.print(solarCurrent,1); Serial.print(",");
  Serial.println(solarPower,1);
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  Serial.println("==============================");
  Serial.println("  Smart Pump Controller v7.0  ");
  Serial.println("==============================");

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
  dht.begin();
  pinMode(MQ135_PIN, INPUT);
  pinMode(RE_DE, OUTPUT);
  digitalWrite(RE_DE, LOW);
  mySerial.begin(4800, SERIAL_8N1, 16, 17);

  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { Serial.println("OLED failed!"); while(true); }
  display.setTextColor(WHITE);

  ina219OK = ina219.begin();
  Serial.println(ina219OK ? "INA219: OK" : "INA219: NOT FOUND");

  // Start WiFi Access Point
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP: "); Serial.println(IP);
  Serial.print("WiFi Name: "); Serial.println(AP_SSID);
  Serial.println("Open browser: http://192.168.4.1");

  // Web routes
  server.on("/",     handleRoot);
  server.on("/data", handleData);
  server.begin();
  Serial.println("Web server started");

  // Startup OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(5,  4); display.println("SMART IRRIGATION v7");
  display.setCursor(0, 16); display.println("Jamia Millia Islamia");
  display.setCursor(0, 28); display.println("WiFi: SmartFarm");
  display.setCursor(0, 40); display.println("http://192.168.4.1");
  display.setCursor(20,52); display.println("Starting...");
  display.display();
  delay(3000);

  Serial.println("CSV_HEADER,DateTime,Temp,Humidity,pH,Soil%,Air,N,P,K,Pump,SolarV,SolarmA,SolarmW");
  Serial.println("READY");
}

// =====================================================
// MAIN LOOP
// =====================================================
void loop() {
  server.handleClient();   // handle web requests

  readSensors();
  controlPump();

  Serial.println("------------------------------");
  Serial.print("Temp    : "); dhtOK?Serial.println(String(temperature,1)+" C"):Serial.println("ERROR");
  Serial.print("Humidity: "); dhtOK?Serial.println(String(humidity,1)+" %"):Serial.println("ERROR");
  Serial.print("pH      : "); Serial.println(phValue,2);
  Serial.print("Soil    : "); Serial.print(soilPercent); Serial.println("%");
  Serial.print("Air     : "); Serial.println(mq135Ready?(airGood?"GOOD":"POOR"):"WARMUP");
  Serial.print("N/P/K   : "); Serial.print(npkN); Serial.print("/"); Serial.print(npkP); Serial.print("/"); Serial.println(npkK);
  Serial.print("Pump    : "); Serial.println(pumpOn?"ON":"OFF");
  Serial.print("Solar   : "); Serial.print(solarVoltage,2); Serial.print("V "); Serial.print(solarCurrent,1); Serial.println("mA");
  Serial.print("WiFi IP : http://192.168.4.1");

  printCSVLine();

  if (millis() - lastSwitch >= PAGE_INTERVAL) {
    lastSwitch  = millis();
    currentPage = (currentPage + 1) % 5;
  }
  showOLED();

  delay(500);
}
