/*
  =====================================================
  ESP32 Smart Pump Controller v10.0 — FINAL
  Jamia Millia Islamia University
  =====================================================
  SENSORS:
  - DHT11        → GPIO4  (10k resistor DATA→3.3V)
  - pH Sensor    → GPIO34 (analog AO + voltage divider)
  - MQ135        → GPIO27 (digital DO)
  - Soil Sensor  → GPIO26 (analog AO)
  - NPK Sensor   → GPIO16(RX2), GPIO17(TX2), GPIO5(RE/DE)
                   Power: 12V brown=VCC, black=GND
  - INA219       → GPIO21(SDA), GPIO22(SCL)
  - OLED SSD1306 → GPIO21(SDA), GPIO22(SCL)
  - Relay        → GPIO13

  RELAY TYPE: ACTIVE LOW
  - LOW  = Relay ON  = Pump runs
  - HIGH = Relay OFF = Pump stops

  PUMP LOGIC:
  - Soil moisture BELOW 30% → Pump ON
  - Soil moisture ABOVE 30% → Pump OFF

  NEW IN v10:
  - Voltage tracked when pump turns ON and OFF
  - Voltage @ pump ON and OFF shown in Serial Monitor
  - Voltage columns added to Excel CSV

  DATA LOGGING:
  - Serial CSV → run excel_logger.py on PC

  SOIL CALIBRATION:
  - First boot: press BOOT button to calibrate
  - Values saved to flash automatically
  - Set SKIP_CALIBRATION true after first calibration
  =====================================================
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_INA219.h>
#include <DHT.h>
#include <HardwareSerial.h>
#include <Preferences.h>

// =====================================================
// SOIL CALIBRATION
// =====================================================
#define SKIP_CALIBRATION  false
#define MANUAL_DRY_VAL    4095    // ← your dry RAW value
#define MANUAL_WET_VAL     800    // ← your wet RAW value

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
#define BOOT_BTN          0

// =====================================================
// SETTINGS
// =====================================================
#define PUMP_ON_THRESHOLD 30
#define MQ135_WARMUP_MS   180000

// =====================================================
// OBJECTS
// =====================================================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306  display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_INA219   ina219;
DHT               dht(DHTPIN, DHTTYPE);
HardwareSerial    mySerial(2);
Preferences       prefs;

// =====================================================
// NPK COMMANDS — ZTS-3002-TR-NPK-N01 @ 4800 baud
// =====================================================
byte requestN[]     = {0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x84, 0x0A};
byte requestP[]     = {0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0xD5, 0xCA};
byte requestK[]     = {0x01, 0x03, 0x00, 0x02, 0x00, 0x01, 0x25, 0xCA};
byte requestN_alt[] = {0x01, 0x03, 0x00, 0x1E, 0x00, 0x01, 0xE4, 0x0C};
byte requestP_alt[] = {0x01, 0x03, 0x00, 0x1F, 0x00, 0x01, 0xB5, 0xCC};
byte requestK_alt[] = {0x01, 0x03, 0x00, 0x20, 0x00, 0x01, 0x85, 0xC0};
byte response[7];

// =====================================================
// GLOBAL VARIABLES
// =====================================================
float temperature    = 0;
float humidity       = 0;
float phValue        = 0;
int   soilPercent    = 0;
int   soilRaw        = 0;
bool  airGood        = true;
bool  mq135Ready     = false;
bool  dhtOK          = false;
bool  pumpOn         = false;
bool  lastPumpState  = false;
bool  ina219OK       = false;
int   npkN           = -1;
int   npkP           = -1;
int   npkK           = -1;
float solarVoltage   = 0;
float solarCurrent   = 0;
float solarPower     = 0;
float voltageAtPumpOn  = 0;   // voltage recorded when pump turned ON
float voltageAtPumpOff = 0;   // voltage recorded when pump turned OFF

int   DRY_VAL        = 4095;
int   WET_VAL        = 800;

int           currentPage = 0;
unsigned long lastSwitch  = 0;
unsigned long lastDHTRead = 0;
#define PAGE_INTERVAL  3000
#define TOTAL_PAGES    4

// =====================================================
// PUMP CONTROL — ACTIVE LOW
// LOW  = Relay ON  = Pump runs
// HIGH = Relay OFF = Pump stops
// =====================================================
void controlPump() {
  bool shouldBeOn = (soilPercent < PUMP_ON_THRESHOLD);

  // Detect state change — record voltage at that moment
  if (shouldBeOn != lastPumpState) {
    if (shouldBeOn) {
      voltageAtPumpOn = solarVoltage;
      Serial.println("==============================");
      Serial.println(">>>      PUMP TURNED ON     <<<");
      Serial.print("Voltage when pump ON  : ");
      Serial.print(voltageAtPumpOn, 2); Serial.println(" V");
      Serial.println("==============================");
    } else {
      voltageAtPumpOff = solarVoltage;
      Serial.println("==============================");
      Serial.println(">>>      PUMP TURNED OFF    <<<");
      Serial.print("Voltage when pump OFF : ");
      Serial.print(voltageAtPumpOff, 2); Serial.println(" V");
      Serial.println("==============================");
    }
    lastPumpState = shouldBeOn;
  }

  if (shouldBeOn) {
    digitalWrite(RELAY_PIN, LOW);   // pump ON
    pumpOn = true;
  } else {
    digitalWrite(RELAY_PIN, HIGH);  // pump OFF
    pumpOn = false;
  }
}

// =====================================================
// SOIL CALIBRATION
// =====================================================
void waitForBoot(const char* msg1, const char* msg2) {
  Serial.println(msg1);
  Serial.println(msg2);
  Serial.println(">>> Press BOOT button on ESP32 <<<");

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);  display.println("-- CALIBRATION ------");
  display.setCursor(0, 12); display.println(msg1);
  display.setCursor(0, 22); display.println(msg2);
  display.setCursor(0, 38); display.println("Press BOOT button");
  display.setCursor(0, 50); display.println("when ready...");
  display.display();

  while (digitalRead(BOOT_BTN) == HIGH) delay(50);
  delay(300);
  while (digitalRead(BOOT_BTN) == LOW)  delay(50);
  delay(300);
}

void runCalibration() {
  Serial.println("==============================");
  Serial.println("   SOIL SENSOR CALIBRATION");
  Serial.println("==============================");
  Serial.println("Press BOOT button to begin...");

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);  display.println("SOIL CALIBRATION");
  display.setCursor(0, 14); display.println("2 steps needed:");
  display.setCursor(0, 26); display.println("1. Sensor in DRY AIR");
  display.setCursor(0, 38); display.println("2. Sensor in WATER");
  display.setCursor(0, 52); display.println("Press BOOT to start");
  display.display();

  while (digitalRead(BOOT_BTN) == HIGH) delay(50);
  delay(300);
  while (digitalRead(BOOT_BTN) == LOW)  delay(50);
  delay(300);

  // STEP 1 — DRY
  waitForBoot("STEP 1:", "Hold sensor in DRY AIR");
  delay(300);
  long sum = 0;
  for (int i = 0; i < 20; i++) { sum += analogRead(SOIL_PIN); delay(50); }
  int dryReading = sum / 20;
  Serial.print("DRY reading = "); Serial.println(dryReading);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);  display.println("DRY value recorded:");
  display.setTextSize(2);
  display.setCursor(20, 20); display.println(dryReading);
  display.setTextSize(1);
  display.setCursor(0, 52); display.println("Now step 2...");
  display.display();
  delay(2000);

  // STEP 2 — WET
  waitForBoot("STEP 2:", "Dip sensor in WATER");
  delay(300);
  sum = 0;
  for (int i = 0; i < 20; i++) { sum += analogRead(SOIL_PIN); delay(50); }
  int wetReading = sum / 20;
  Serial.print("WET reading = "); Serial.println(wetReading);

  prefs.begin("soil", false);
  prefs.putInt("dry", dryReading);
  prefs.putInt("wet", wetReading);
  prefs.end();

  DRY_VAL = dryReading;
  WET_VAL  = wetReading;

  Serial.println("==============================");
  Serial.println("CALIBRATION SAVED!");
  Serial.print("DRY_VAL = "); Serial.println(DRY_VAL);
  Serial.print("WET_VAL = "); Serial.println(WET_VAL);
  Serial.println("Now set SKIP_CALIBRATION true");
  Serial.println("and paste values in MANUAL_DRY/WET");
  Serial.println("==============================");

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);  display.println("CALIBRATION DONE!");
  display.setCursor(0, 14); display.print("DRY = "); display.println(DRY_VAL);
  display.setCursor(0, 26); display.print("WET = "); display.println(WET_VAL);
  display.setCursor(0, 40); display.println("Saved to memory.");
  display.setCursor(0, 52); display.println("Starting system...");
  display.display();
  delay(3000);
}

// =====================================================
// READ NPK
// =====================================================
int readNPK(byte* req) {
  while (mySerial.available()) mySerial.read();
  digitalWrite(RE_DE, HIGH); delay(10);
  mySerial.write(req, 8); mySerial.flush();
  digitalWrite(RE_DE, LOW);  delay(300);
  if (mySerial.available() >= 7) {
    for (int i = 0; i < 7; i++) response[i] = mySerial.read();
    if (response[0] == 0x01 && response[1] == 0x03)
      return (response[3] << 8) | response[4];
  }
  return -1;
}

// =====================================================
// READ DHT11
// =====================================================
void readDHT() {
  if (millis() - lastDHTRead < 2000) return;
  lastDHTRead = millis();
  float t = NAN, h = NAN;
  for (int i = 0; i < 3; i++) {
    t = dht.readTemperature();
    h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) break;
    delay(250);
  }
  if (!isnan(t) && !isnan(h) && t > -10 && t < 80 && h >= 0 && h <= 100) {
    temperature = t; humidity = h; dhtOK = true;
  } else {
    dhtOK = false;
  }
}

// =====================================================
// READ ALL SENSORS
// =====================================================
void readSensors() {
  readDHT();

  int phRaw = analogRead(PH_PIN);
  phValue   = 7.0 + ((2.5 - phRaw * (3.3 / 4095.0)) / 0.18);
  phValue   = constrain(phValue, 0.0, 14.0);

  if (millis() >= MQ135_WARMUP_MS) {
    mq135Ready = true;
    airGood    = digitalRead(MQ135_PIN);
  } else {
    mq135Ready = false;
  }

  soilRaw     = analogRead(SOIL_PIN);
  soilPercent = map(soilRaw, DRY_VAL, WET_VAL, 0, 100);
  soilPercent = constrain(soilPercent, 0, 100);

  npkN = readNPK(requestN);   delay(100);
  if (npkN == -1) { npkN = readNPK(requestN_alt); delay(100); }
  npkP = readNPK(requestP);   delay(100);
  if (npkP == -1) { npkP = readNPK(requestP_alt); delay(100); }
  npkK = readNPK(requestK);   delay(100);
  if (npkK == -1) { npkK = readNPK(requestK_alt); delay(100); }

  if (ina219OK) {
    solarVoltage = ina219.getBusVoltage_V();
    solarCurrent = max(0.0f, ina219.getCurrent_mA());
    solarPower   = ina219.getPower_mW();
  }
}

// =====================================================
// SOIL STATUS
// =====================================================
String soilStatus() {
  if      (soilPercent < 20) return "VERY DRY";
  else if (soilPercent < 30) return "DRY";
  else if (soilPercent < 60) return "MOIST";
  else if (soilPercent < 80) return "WET";
  else                        return "SATURATED";
}

// =====================================================
// OLED PAGE 1 — TEMP & HUMIDITY
// =====================================================
void showPage1() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0); display.println("-- TEMP & HUMIDITY --");
  if (dhtOK) {
    display.setTextSize(2);
    display.setCursor(0, 14); display.print(temperature, 1); display.print((char)247); display.println("C");
    display.setCursor(0, 36); display.print(humidity, 1); display.println("%RH");
  } else {
    display.setTextSize(1);
    display.setCursor(0, 16); display.println("DHT11 ERROR!");
    display.setCursor(0, 28); display.println("Add 10k resistor");
    display.setCursor(0, 38); display.println("DATA pin → 3.3V");
  }
  display.setTextSize(1);
  display.setCursor(0, 57);
  display.print("PUMP:"); display.print(pumpOn ? "ON  " : "OFF ");
  display.print("SOIL:"); display.print(soilPercent); display.print("%");
  display.display();
}

// =====================================================
// OLED PAGE 2 — SOIL & pH
// =====================================================
void showPage2() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0); display.println("-- SOIL & pH --------");
  display.setTextSize(3);
  display.setCursor(10, 12); display.print(soilPercent); display.println("%");
  display.setTextSize(1);
  display.setCursor(0, 42); display.print("STATUS: "); display.println(soilStatus());
  display.setCursor(0, 52);
  display.print("pH:"); display.print(phValue, 1);
  display.print("  PUMP:"); display.println(pumpOn ? "ON" : "OFF");
  display.display();
}

// =====================================================
// OLED PAGE 3 — NPK & AIR
// =====================================================
void showPage3() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0); display.println("-- NPK & AIR --------");
  if (npkN == -1 && npkP == -1 && npkK == -1) {
    display.setCursor(0, 14); display.println("NPK: NOT RESPONDING");
    display.setCursor(0, 26); display.println("Check 12V power &");
    display.setCursor(0, 36); display.println("RS485 wiring");
  } else {
    display.setCursor(0, 14); display.print("N: "); display.print(npkN == -1 ? 0 : npkN); display.println(" mg/kg");
    display.setCursor(0, 26); display.print("P: "); display.print(npkP == -1 ? 0 : npkP); display.println(" mg/kg");
    display.setCursor(0, 38); display.print("K: "); display.print(npkK == -1 ? 0 : npkK); display.println(" mg/kg");
  }
  display.setCursor(0, 52);
  display.print("Air: ");
  if      (!mq135Ready) display.println("WARMING UP...");
  else if (airGood)     display.println("GOOD");
  else                  display.println("POOR");
  display.display();
}

// =====================================================
// OLED PAGE 4 — SOLAR + VOLTAGE TRACKING
// =====================================================
void showPage4() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0); display.println("-- SOLAR & VOLTAGE --");
  if (!ina219OK) {
    display.setCursor(0, 16); display.println("INA219 NOT FOUND");
    display.setCursor(0, 28); display.println("SDA=21  SCL=22");
    display.setCursor(0, 38); display.println("VCC=3.3V Addr=0x40");
  } else {
    display.setCursor(0, 12); display.print("Now : "); display.print(solarVoltage, 2); display.print("V "); display.print(solarCurrent, 0); display.println("mA");
    display.setCursor(0, 24); display.print("Pwr : "); display.print(solarPower / 1000.0, 3); display.println(" W");
    display.setCursor(0, 36); display.print("V@ON : "); display.print(voltageAtPumpOn, 2);  display.println(" V");
    display.setCursor(0, 48); display.print("V@OFF: "); display.print(voltageAtPumpOff, 2); display.println(" V");
  }
  display.setCursor(0, 57);
  display.print("PUMP:"); display.print(pumpOn ? "ON  " : "OFF ");
  display.print("SOIL:"); display.print(soilPercent); display.print("%");
  display.display();
}

// =====================================================
// WARMUP SCREEN
// =====================================================
void showWarmup() {
  long remaining = (MQ135_WARMUP_MS - millis()) / 1000;
  if (remaining < 0) remaining = 0;
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);  display.println("- MQ135 WARMING UP --");
  display.setCursor(0, 12); display.print("Wait: "); display.print(remaining); display.println(" sec");
  display.setCursor(0, 24); display.print("Soil : "); display.print(soilPercent); display.println("%");
  display.setCursor(0, 34); display.print("Temp : ");
  dhtOK ? (display.print(temperature, 1), display.println("C")) : display.println("ERR");
  display.setCursor(0, 44); display.print("Solar: "); display.print(solarVoltage, 1); display.print("V ");
  display.print(solarCurrent, 0); display.println("mA");
  display.setCursor(0, 54); display.print("PUMP : "); display.println(pumpOn ? "ON" : "OFF");
  display.display();
}

// =====================================================
// CSV LINE FOR PYTHON LOGGER
// FORMAT: DATA,temp,hum,ph,soil,air,N,P,K,pump,
//         solarV,solarmA,solarmW,V@PumpON,V@PumpOFF
// =====================================================
void printCSVLine() {
  Serial.print("DATA,");
  Serial.print(dhtOK ? String(temperature, 1) : "ERR"); Serial.print(",");
  Serial.print(dhtOK ? String(humidity, 1)    : "ERR"); Serial.print(",");
  Serial.print(phValue, 2);    Serial.print(",");
  Serial.print(soilPercent);   Serial.print(",");
  Serial.print(mq135Ready ? (airGood ? "GOOD" : "POOR") : "WARMUP"); Serial.print(",");
  Serial.print(npkN);          Serial.print(",");
  Serial.print(npkP);          Serial.print(",");
  Serial.print(npkK);          Serial.print(",");
  Serial.print(pumpOn ? "ON" : "OFF"); Serial.print(",");
  Serial.print(solarVoltage, 2);    Serial.print(",");
  Serial.print(solarCurrent, 1);    Serial.print(",");
  Serial.print(solarPower, 1);      Serial.print(",");
  Serial.print(voltageAtPumpOn, 2); Serial.print(",");
  Serial.println(voltageAtPumpOff, 2);
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  Serial.println("==============================");
  Serial.println("  Smart Pump Controller v10.0 ");
  Serial.println("  FINAL — Voltage Tracking     ");
  Serial.println("==============================");

  // Relay OFF at startup — active LOW so HIGH = OFF
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);

  pinMode(BOOT_BTN, INPUT);
  dht.begin();
  pinMode(MQ135_PIN, INPUT);
  pinMode(RE_DE, OUTPUT);
  digitalWrite(RE_DE, LOW);
  mySerial.begin(4800, SERIAL_8N1, 16, 17);

  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED FAILED!"); while (true);
  }
  display.setTextColor(WHITE);

  ina219OK = ina219.begin();
  Serial.println(ina219OK ? "INA219: OK" : "INA219: NOT FOUND");

  // ---- SOIL CALIBRATION ----
  if (SKIP_CALIBRATION) {
    DRY_VAL = MANUAL_DRY_VAL;
    WET_VAL  = MANUAL_WET_VAL;
    Serial.print("Manual calibration — DRY="); Serial.print(DRY_VAL);
    Serial.print(" WET="); Serial.println(WET_VAL);
  } else {
    prefs.begin("soil", true);
    int savedDry = prefs.getInt("dry", -1);
    int savedWet = prefs.getInt("wet", -1);
    prefs.end();

    if (savedDry != -1 && savedWet != -1) {
      DRY_VAL = savedDry;
      WET_VAL  = savedWet;
      Serial.print("Loaded from flash — DRY="); Serial.print(DRY_VAL);
      Serial.print(" WET="); Serial.println(WET_VAL);

      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0, 0);  display.println("Calibration found:");
      display.setCursor(0, 14); display.print("DRY = "); display.println(DRY_VAL);
      display.setCursor(0, 26); display.print("WET = "); display.println(WET_VAL);
      display.setCursor(0, 40); display.println("Press BOOT to redo");
      display.setCursor(0, 52); display.println("or wait 5s...");
      display.display();

      unsigned long w = millis();
      bool redo = false;
      while (millis() - w < 5000) {
        if (digitalRead(BOOT_BTN) == LOW) { redo = true; break; }
        delay(100);
      }
      if (redo) runCalibration();
    } else {
      Serial.println("No calibration found — running calibration...");
      runCalibration();
    }
  }

  // Startup screen
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(5,  4); display.println("SMART IRRIGATION v10");
  display.setCursor(0, 16); display.println("Jamia Millia Islamia");
  display.setCursor(0, 28); display.print("DRY:"); display.print(DRY_VAL);
  display.print(" WET:"); display.println(WET_VAL);
  display.setCursor(0, 40); display.println("Relay: ACTIVE LOW");
  display.setCursor(0, 52); display.println("Voltage tracking ON");
  display.display();
  delay(3000);

  Serial.println("CSV_HEADER,DateTime,Temp_C,Humidity_%,pH,Soil_%,Air,N,P,K,Pump,Solar_V,Solar_mA,Solar_mW,Voltage_PumpON,Voltage_PumpOFF");
  Serial.println("READY");
}

// =====================================================
// MAIN LOOP
// =====================================================
void loop() {
  readSensors();
  controlPump();

  // Human readable serial
  Serial.println("------------------------------");
  Serial.print("Temp     : "); dhtOK ? Serial.println(String(temperature, 1) + " C") : Serial.println("ERROR");
  Serial.print("Humidity : "); dhtOK ? Serial.println(String(humidity, 1) + " %")    : Serial.println("ERROR");
  Serial.print("pH       : "); Serial.println(phValue, 2);
  Serial.print("Soil     : "); Serial.print(soilPercent); Serial.print("% (RAW:"); Serial.print(soilRaw); Serial.println(")");
  Serial.print("Air      : "); Serial.println(mq135Ready ? (airGood ? "GOOD" : "POOR") : "WARMING UP");
  Serial.print("N/P/K    : "); Serial.print(npkN); Serial.print("/"); Serial.print(npkP); Serial.print("/"); Serial.println(npkK);
  Serial.print("Pump     : "); Serial.println(pumpOn ? "ON" : "OFF");
  Serial.println("-- VOLTAGE -------------------");
  Serial.print("Voltage NOW       : "); Serial.print(solarVoltage, 2); Serial.println(" V");
  Serial.print("Current NOW       : "); Serial.print(solarCurrent, 1); Serial.println(" mA");
  Serial.print("Power NOW         : "); Serial.print(solarPower / 1000.0, 3); Serial.println(" W");
  Serial.print("Voltage @ PUMP ON : "); Serial.print(voltageAtPumpOn, 2);  Serial.println(" V");
  Serial.print("Voltage @ PUMP OFF: "); Serial.print(voltageAtPumpOff, 2); Serial.println(" V");

  printCSVLine();

  // OLED
  if (!mq135Ready) {
    showWarmup();
  } else {
    if (millis() - lastSwitch >= PAGE_INTERVAL) {
      lastSwitch  = millis();
      currentPage = (currentPage + 1) % TOTAL_PAGES;
    }
    switch (currentPage) {
      case 0: showPage1(); break;
      case 1: showPage2(); break;
      case 2: showPage3(); break;
      case 3: showPage4(); break;
    }
  }

  delay(500);
}