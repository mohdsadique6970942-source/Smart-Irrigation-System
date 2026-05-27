# Smart-Irrigation-System 🌱

ESP32-based IoT Smart Irrigation System for real-time monitoring of soil moisture, temperature, humidity, soil pH, and solar panel voltage generation. Implements fuzzy logic for automatic irrigation control and wireless monitoring through ESP32 Wi-Fi for smart farming applications.

---

## 🧰 Hardware Components

- ESP32
- DHT11 Sensor
- RC-A-4079 Soil Moisture Sensor
- INA219 Voltage/Current Sensor
- pH Sensor
- Relay Module
- Water Pump
- OLED Display
- Buck Converter
- Solar Panel
- 12V Battery

---

## 🔌 Pin Connections

### 🔹 DHT11 Sensor
- VCC → 3.3V
- GND → GND
- DATA → GPIO 4

### 🔹 Soil Moisture Sensor
- VCC → 3.3V
- GND → GND
- AO → GPIO 34

### 🔹 pH Sensor
- VCC → 5V
- GND → GND
- AO → GPIO 35

### 🔹 INA219 Sensor
- VCC → 3.3V
- GND → GND
- SDA → GPIO 21
- SCL → GPIO 22

### 🔹 OLED Display
- VCC → 3.3V
- GND → GND
- SDA → GPIO 21
- SCL → GPIO 22

### 🔹 Relay Module
- IN → GPIO 26
- VCC → 5V
- GND → GND

---

## 🌾 Applications

- Smart Agriculture
- Automated Irrigation Systems
- Precision Farming
- Water Management
- IoT-based Monitoring Systems

---

## 📘 Learning Outcomes

- ESP32 Programming
- IoT System Development
- Sensor Interfacing & Calibration
- Fuzzy Logic Control
- Wireless Data Monitoring
- Embedded System Design
- Smart Farming Automation
