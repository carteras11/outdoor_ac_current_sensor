# ESP32 HVAC Monitoring Project - Action Items

## **Phase 1: Environment Setup & Basic Understanding**

### **1.1 - Install Development Tools**
- [ ] Download and install **Visual Studio Code** (vscode.dev)
- [ ] Open VSCode, go to Extensions (Ctrl+Shift+X)
- [ ] Search for and install **PlatformIO IDE** extension
- [ ] Restart VSCode after installation
- [ ] Wait for PlatformIO to finish installing dependencies (check bottom toolbar)

### **1.2 - Create First Project**
- [ ] Click PlatformIO icon in left sidebar
- [ ] Click "New Project"
- [ ] Name: "hvac_monitor"
- [ ] Board: Search "NodeMCU-32S" or "ESP32 Dev Module"
- [ ] Framework: "Arduino"
- [ ] Click Finish (this creates project structure)
- [ ] Explore the project structure:
  - `src/main.cpp` - where your code goes
  - `platformio.ini` - configuration file
  - `lib/` - where libraries go

### **1.3 - Test Basic Hardware Connection**
- [ ] Connect ESP32 to computer via USB cable
- [ ] Check Device Manager (Windows) or `ls /dev/tty*` (Mac/Linux) to see if board detected
- [ ] In PlatformIO, click "Upload" button (right arrow icon in bottom toolbar)
- [ ] Verify it compiles and uploads (even with empty main.cpp)
- [ ] If upload fails, try different USB cable or install CP2102/CH340 drivers

---

## **Phase 2: First Code - LED Blink Test**

### **2.1 - Write Blink Program**
- [ ] Open `src/main.cpp`
- [ ] Delete any template code
- [ ] Copy this code:
```cpp
void setup() {
  pinMode(2, OUTPUT);  // GPIO2 usually has built-in LED
}

void loop() {
  digitalWrite(2, HIGH);
  delay(1000);
  digitalWrite(2, LOW);
  delay(1000);
}
```
- [ ] Click "Upload" (right arrow) in bottom toolbar
- [ ] Verify LED blinks on your ESP32 board
- [ ] **If no LED visible**: Try GPIO pin 2, 4, or 5 with external LED + resistor

### **2.2 - Add Serial Debug Output**
- [ ] Modify code to add serial output:
```cpp
void setup() {
  Serial.begin(115200);
  pinMode(2, OUTPUT);
}

void loop() {
  Serial.println("LED ON");
  digitalWrite(2, HIGH);
  delay(1000);
  
  Serial.println("LED OFF");
  digitalWrite(2, LOW);
  delay(1000);
}
```
- [ ] Upload code
- [ ] Click "Serial Monitor" icon (plug icon) in bottom toolbar
- [ ] Set baud rate to 115200
- [ ] Verify you see "LED ON" / "LED OFF" messages
- [ ] **Troubleshooting**: If garbage text, check baud rate matches (115200)

---

## **Phase 3: Research & Understand Core Concepts**

### **3.1 - Research Arduino Framework**
- [ ] Read: "What is Arduino Framework for ESP32" - search this
- [ ] Understand: `setup()` runs once, `loop()` runs repeatedly
- [ ] Understand: Arduino provides libraries so you don't write low-level protocol code
- [ ] Bookmark: [ESP32 Arduino Core Documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/)

### **3.2 - Research MQTT Protocol**
- [ ] Read: "What is MQTT protocol" (search this or read mqtt.org)
- [ ] Key concepts to understand:
  - **Broker**: Central server that routes messages (you'll run Mosquitto on Home Assistant)
  - **Publisher**: Device that sends data (your ESP32)
  - **Subscriber**: Device that receives data (Home Assistant)
  - **Topic**: Like a channel - "hvac/temperature/supply"
  - **QoS**: Quality of Service (use QoS 0 for sensor data - fire and forget)
- [ ] Watch: Search YouTube for "MQTT explained" (5-10 min video)
- [ ] Understand why MQTT for IoT: lightweight, publish/subscribe model, reliable

### **3.3 - Research Home Assistant MQTT Integration**
- [ ] Read Home Assistant docs: Search "Home Assistant MQTT"
- [ ] Understand: HA can auto-discover devices or you manually configure sensors
- [ ] Understand: You'll need Mosquitto broker add-on installed in HA
- [ ] Bookmark: [Home Assistant MQTT Integration Docs](https://www.home-assistant.io/integrations/mqtt/)

---

## **Phase 4: WiFi Connection**

### **4.1 - Add WiFi Library and Connect**
- [ ] Modify `main.cpp`:
```cpp
#include <WiFi.h>

const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";

void setup() {
  Serial.begin(115200);
  
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  delay(1000);
}
```
- [ ] Replace `YOUR_WIFI_NAME` and `YOUR_WIFI_PASSWORD` with your actual credentials
- [ ] Upload code
- [ ] Open Serial Monitor
- [ ] Verify you see "WiFi connected!" and an IP address
- [ ] **Troubleshooting**: If fails, check SSID/password, check 2.4GHz WiFi (ESP32 doesn't support 5GHz)

---

## **Phase 5: MQTT Setup & Testing**

### **5.1 - Install Mosquitto on Home Assistant**
- [ ] Open Home Assistant web interface
- [ ] Go to Settings → Add-ons → Add-on Store
- [ ] Search for "Mosquitto broker"
- [ ] Click Install
- [ ] After install, click Start
- [ ] Enable "Start on boot"
- [ ] Go to Configuration tab
- [ ] Add a user (or use default anonymous - less secure but easier for testing)
- [ ] Note the broker address (usually your Home Assistant IP, port 1883)

### **5.2 - Add MQTT Integration in Home Assistant**
- [ ] Settings → Devices & Services → Add Integration
- [ ] Search "MQTT"
- [ ] Configure:
  - Broker: localhost (or your HA IP)
  - Port: 1883
  - Username/password if you set one (or leave blank for anonymous)
- [ ] Click Submit
- [ ] Verify integration shows "Configured"

### **5.3 - Test MQTT from Computer (Optional but Helpful)**
- [ ] Install MQTT Explorer (free tool) on your computer
- [ ] Connect to your Home Assistant IP:1883
- [ ] Manually publish a test message to topic "test/topic"
- [ ] Verify you can see it in MQTT Explorer
- [ ] This confirms broker is working before testing with ESP32

### **5.4 - Add MQTT to ESP32 Code**
- [ ] Open `platformio.ini`
- [ ] Add this line under `[env:...]` section:
```ini
lib_deps = 
    knolleary/PubSubClient@^2.8
```
- [ ] Save file (PlatformIO will auto-install library)
- [ ] Modify `main.cpp`:
```cpp
#include <WiFi.h>
#include <PubSubClient.h>

const char* ssid = "YOUR_WIFI_NAME";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "YOUR_HOME_ASSISTANT_IP";  // e.g., "192.168.1.100"

WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(115200);
  
  // Connect WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  
  // Connect MQTT
  client.setServer(mqtt_server, 1883);
  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
    if (client.connect("ESP32_HVAC")) {  // Client ID
      Serial.println("MQTT connected");
    } else {
      Serial.print("Failed, rc=");
      Serial.println(client.state());
      delay(2000);
    }
  }
}

void loop() {
  if (!client.connected()) {
    // Reconnect if connection lost
    client.connect("ESP32_HVAC");
  }
  client.loop();  // Keep MQTT alive
  
  // Publish test message every 5 seconds
  static unsigned long lastMsg = 0;
  unsigned long now = millis();
  if (now - lastMsg > 5000) {
    lastMsg = now;
    
    char msg[50];
    snprintf(msg, 50, "Test message: %lu", now);
    client.publish("hvac/test", msg);
    Serial.println("Published test message");
  }
}
```
- [ ] Replace `YOUR_HOME_ASSISTANT_IP` with actual IP (check HA → Settings → System → Network)
- [ ] Upload code
- [ ] Open Serial Monitor
- [ ] Verify you see "MQTT connected" and "Published test message"
- [ ] Open MQTT Explorer and verify messages appearing on topic "hvac/test"
- [ ] **Troubleshooting**: If "Failed, rc=-2" = can't connect to broker (check IP/port)

### **5.5 - Add Sensor in Home Assistant**
- [ ] Open Home Assistant configuration.yaml (Settings → System → click three dots → "Edit in raw configuration editor" or use File Editor add-on)
- [ ] Add this:
```yaml
mqtt:
  sensor:
    - name: "HVAC Test"
      state_topic: "hvac/test"
```
- [ ] Save and restart Home Assistant (Developer Tools → YAML → Restart)
- [ ] Go to Developer Tools → States
- [ ] Search for "sensor.hvac_test"
- [ ] Verify you see your ESP32's test messages updating every 5 seconds

---

## **Phase 6: First Real Sensor - DS18B20 Temperature**

### **6.1 - Research DS18B20 and 1-Wire Protocol**
- [ ] Read: "DS18B20 datasheet" (search for PDF)
- [ ] Understand: 1-Wire protocol uses one data line for multiple sensors
- [ ] Understand: Each sensor has unique 64-bit address
- [ ] Understand: Need 4.7kΩ pullup resistor on data line
- [ ] Watch: Search YouTube "DS18B20 Arduino tutorial" (5-10 min)

### **6.2 - Wire Up One DS18B20**
- [ ] Identify DS18B20 pins:
  - GND (usually black wire)
  - VCC (usually red wire) → connect to 3.3V on ESP32
  - DATA (usually yellow/white wire) → connect to GPIO4 on ESP32
- [ ] Add 4.7kΩ resistor between DATA and VCC (pullup resistor)
- [ ] Double-check connections with multimeter if available
- [ ] **Do NOT connect to 5V** - ESP32 is 3.3V logic

### **6.3 - Install 1-Wire Libraries**
- [ ] Open `platformio.ini`
- [ ] Add to `lib_deps`:
```ini
lib_deps = 
    knolleary/PubSubClient@^2.8
    paulstoffregen/OneWire@^2.3.7
    milesburton/DallasTemperature@^3.11.0
```
- [ ] Save (PlatformIO auto-installs)

### **6.4 - Test DS18B20 Reading**
- [ ] Modify `main.cpp` to add temperature reading:
```cpp
#include <WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Previous WiFi/MQTT setup code...

// Temperature sensor setup
#define ONE_WIRE_BUS 4  // GPIO4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

void setup() {
  // ... previous WiFi/MQTT setup ...
  
  sensors.begin();
  Serial.print("Found ");
  Serial.print(sensors.getDeviceCount());
  Serial.println(" temperature sensors");
}

void loop() {
  // ... previous MQTT loop code ...
  
  // Read and publish temperature every 10 seconds
  static unsigned long lastTempRead = 0;
  unsigned long now = millis();
  if (now - lastTempRead > 10000) {
    lastTempRead = now;
    
    sensors.requestTemperatures();
    float tempC = sensors.getTempCByIndex(0);
    float tempF = sensors.getTempFByIndex(0);
    
    Serial.print("Temperature: ");
    Serial.print(tempF);
    Serial.println(" °F");
    
    char msg[50];
    snprintf(msg, 50, "%.2f", tempF);
    client.publish("hvac/temperature/test", msg);
  }
}
```
- [ ] Upload code
- [ ] Open Serial Monitor
- [ ] Verify you see temperature readings
- [ ] If shows -196.6°F or 85°C = sensor not connected properly, check wiring
- [ ] Touch sensor with fingers and verify temperature increases
- [ ] Check MQTT Explorer - verify messages on "hvac/temperature/test" topic

---

## **Phase 7: Multiple Temperature Sensors**

### **7.1 - Research Sensor Addressing**
- [ ] Understand: Each DS18B20 has unique 64-bit address
- [ ] Read DallasTemperature library docs about addressing
- [ ] Two methods: by index (0, 1, 2...) or by address (more reliable)

### **7.2 - Find Sensor Addresses**
- [ ] Connect all your DS18B20 sensors to same DATA line (parallel)
  - All GND together → ESP32 GND
  - All VCC together → ESP32 3.3V
  - All DATA together → ESP32 GPIO4 (with one 4.7kΩ pullup)
- [ ] Add this code to print addresses in `setup()`:
```cpp
void setup() {
  // ... previous setup ...
  
  sensors.begin();
  Serial.print("Found ");
  Serial.print(sensors.getDeviceCount());
  Serial.println(" sensors");
  
  // Print each sensor's address
  for (int i = 0; i < sensors.getDeviceCount(); i++) {
    DeviceAddress addr;
    sensors.getAddress(addr, i);
    Serial.print("Sensor ");
    Serial.print(i);
    Serial.print(" address: ");
    for (uint8_t j = 0; j < 8; j++) {
      if (addr[j] < 16) Serial.print("0");
      Serial.print(addr[j], HEX);
    }
    Serial.println();
  }
}
```
- [ ] Upload and check Serial Monitor
- [ ] Write down each sensor's address and label which physical sensor it is
- [ ] Test: Heat one sensor, verify correct address shows temp change

### **7.3 - Assign Sensor Locations**
- [ ] Define sensor addresses at top of code:
```cpp
DeviceAddress supplyAirSensor = {0x28, 0xFF, 0x..., 0x..., 0x..., 0x..., 0x..., 0x...};
DeviceAddress returnAirSensor = {0x28, 0xFF, 0x..., 0x..., 0x..., 0x..., 0x..., 0x...};
DeviceAddress outdoorSensor = {0x28, 0xFF, 0x..., 0x..., 0x..., 0x..., 0x..., 0x...};
// ... etc for each sensor
```
- [ ] Modify loop to read by address:
```cpp
float supplyTemp = sensors.getTempF(supplyAirSensor);
float returnTemp = sensors.getTempF(returnAirSensor);
float outdoorTemp = sensors.getTempF(outdoorSensor);
```
- [ ] Publish each to separate MQTT topic:
```cpp
client.publish("hvac/temperature/supply", ...);
client.publish("hvac/temperature/return", ...);
client.publish("hvac/temperature/outdoor", ...);
```

---

## **Phase 8: Physical Sensor Placement**

### **8.1 - Plan Sensor Locations**
- [ ] Identify where to place each sensor:
  - **Supply air**: At vent where cold air comes out
  - **Return air**: At return vent where air goes back to unit
  - **Outdoor ambient**: Outside window/balcony (weatherproof needed)
  - **Optional**: Outdoor unit coil (measure with IR thermometer first to verify location)
- [ ] Sketch a simple diagram of sensor placement
- [ ] Measure wire length needed for each location

### **8.2 - Extend Sensor Wires (if needed)**
- [ ] If sensors have short leads, plan wire extensions
- [ ] Use 3-conductor wire (or separate wires for GND/VCC/DATA)
- [ ] Keep extensions under 10-15 feet if possible (1-Wire has distance limits)
- [ ] Solder connections and heat-shrink or use screw terminals
- [ ] Test each sensor still reads correctly after extension

### **8.3 - Mount Sensors**
- [ ] **Supply/return vents**: Tape sensor near vent opening, don't block airflow
- [ ] **Outdoor**: Weatherproof enclosure if exposed to elements (small project box)
- [ ] **Outdoor unit**: Secure near coil but not touching refrigerant lines
- [ ] Use zip ties, tape, or mounting putty
- [ ] Ensure wires won't be pinched or damaged

---

## **Phase 9: Current/Power Measurement**

### **9.1 - Decide on Approach**
- [ ] **Option A**: PZEM-004T module (easier, plug-and-play, UART)
- [ ] **Option B**: SCT-013 CT clamp + signal conditioning (more DIY, analog)
- [ ] For first version, recommend PZEM-004T for simplicity
- [ ] Research: "PZEM-004T ESP32 tutorial"

### **9.2 - If Using PZEM-004T**
- [ ] Wire PZEM to ESP32:
  - PZEM RX → ESP32 TX (GPIO17)
  - PZEM TX → ESP32 RX (GPIO16)
  - PZEM GND → ESP32 GND
  - PZEM VCC → ESP32 5V or 3.3V (check PZEM spec)
- [ ] Clamp CT sensor around ONE wire at breaker (BLACK or RED, not both)
- [ ] **Safety**: Turn off breaker before opening panel, or have electrician help
- [ ] Install library in `platformio.ini`:
```ini
lib_deps = 
    ... previous libs ...
    mandulaj/PZEM-004T-v30@^1.1.2
```
- [ ] Add PZEM reading code:
```cpp
#include <PZEM004Tv30.h>

PZEM004Tv30 pzem(Serial2, 16, 17);  // RX=16, TX=17

void setup() {
  // ... previous setup ...
}

void loop() {
  // ... previous loop code ...
  
  // Read power every 5 seconds
  static unsigned long lastPowerRead = 0;
  if (millis() - lastPowerRead > 5000) {
    lastPowerRead = millis();
    
    float voltage = pzem.voltage();
    float current = pzem.current();
    float power = pzem.power();
    
    if (!isnan(power)) {
      Serial.print("Power: ");
      Serial.print(power);
      Serial.println(" W");
      
      char msg[50];
      snprintf(msg, 50, "%.2f", power);
      client.publish("hvac/power", msg);
    }
  }
}
```
- [ ] Upload and test
- [ ] Turn on AC and verify power reading increases
- [ ] Compare with Kill-A-Watt or utility meter if available

### **9.3 - If Using SCT-013 (Advanced)**
- [ ] Research: "SCT-013 ESP32 tutorial" and OpenEnergyMonitor schematics
- [ ] Build signal conditioning circuit (burden resistor, bias voltage, filtering)
- [ ] Connect to ADS1115 ADC
- [ ] Calibrate with known load
- [ ] This is more complex - consider starting with PZEM first

---

## **Phase 10: Humidity Sensor**

### **10.1 - Choose Humidity Sensor**
- [ ] **DHT22** (cheaper, ~$5) or **BME280** (better, ~$10, also temp/pressure)
- [ ] For this project, recommend BME280 (I2C, more accurate)

### **10.2 - Wire BME280**
- [ ] Connect to ESP32:
  - VCC → 3.3V
  - GND → GND
  - SCL → GPIO22 (default I2C clock)
  - SDA → GPIO21 (default I2C data)
- [ ] Can share I2C bus with other I2C devices (like ADS1115 if using)

### **10.3 - Add BME280 Code**
- [ ] Add library to `platformio.ini`:
```ini
lib_deps = 
    ... previous libs ...
    adafruit/Adafruit BME280 Library@^2.2.2
    adafruit/Adafruit Unified Sensor@^1.1.9
```
- [ ] Add to code:
```cpp
#include <Adafruit_BME280.h>

Adafruit_BME280 bme;

void setup() {
  // ... previous setup ...
  
  if (!bme.begin(0x76)) {  // or 0x77, check your module
    Serial.println("BME280 not found!");
  }
}

void loop() {
  // ... previous loop code ...
  
  float humidity = bme.readHumidity();
  float temp = bme.readTemperature();
  
  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");
  
  client.publish("hvac/humidity", ...);
}
```
- [ ] Upload and test
- [ ] Breathe on sensor and verify humidity increases

---

## **Phase 11: CO2 Sensor**

### **11.1 - Wire MH-Z19B**
- [ ] Connect to ESP32:
  - VCC → 5V (MH-Z19B needs 5V, not 3.3V)
  - GND → GND
  - TX → ESP32 RX (use another serial port, like GPIO14)
  - RX → ESP32 TX (like GPIO12)
- [ ] Note: This uses hardware serial, not I2C

### **11.2 - Add MH-Z19B Code**
- [ ] Install library:
```ini
lib_deps = 
    ... previous libs ...
    wifwaf/MH-Z19@^1.5.4
```
- [ ] Add code:
```cpp
#include <MHZ19.h>

MHZ19 mhz(&Serial1);  // Use Serial1

void setup() {
  // ... previous setup ...
  
  Serial1.begin(9600, SERIAL_8N1, 14, 12);  // RX=14, TX=12, baud=9600
  mhz.setAutoCalibration(false);  // Disable auto-calibration initially
}

void loop() {
  // ... previous loop code ...
  
  static unsigned long lastCO2Read = 0;
  if (millis() - lastCO2Read > 10000) {  // Every 10 sec
    lastCO2Read = millis();
    
    int co2 = mhz.getCO2();
    if (co2 > 0) {
      Serial.print("CO2: ");
      Serial.print(co2);
      Serial.println(" ppm");
      
      char msg[50];
      snprintf(msg, 50, "%d", co2);
      client.publish("hvac/co2", msg);
    }
  }
}
```
- [ ] Upload and test
- [ ] Normal indoor CO2: 400-800 ppm
- [ ] Breathe near sensor, should spike to 2000+ ppm

---

## **Phase 12: Home Assistant Integration & Dashboards**

### **12.1 - Add All Sensors to configuration.yaml**
- [ ] Edit configuration.yaml:
```yaml
mqtt:
  sensor:
    - name: "AC Supply Temperature"
      state_topic: "hvac/temperature/supply"
      unit_of_measurement: "°F"
      device_class: temperature
      
    - name: "AC Return Temperature"
      state_topic: "hvac/temperature/return"
      unit_of_measurement: "°F"
      device_class: temperature
      
    - name: "Outdoor Temperature"
      state_topic: "hvac/temperature/outdoor"
      unit_of_measurement: "°F"
      device_class: temperature
      
    - name: "AC Power Consumption"
      state_topic: "hvac/power"
      unit_of_measurement: "W"
      device_class: power
      
    - name: "Indoor Humidity"
      state_topic: "hvac/humidity"
      unit_of_measurement: "%"
      device_class: humidity
      
    - name: "Indoor CO2"
      state_topic: "hvac/co2"
      unit_of_measurement: "ppm"
```
- [ ] Save and restart Home Assistant
- [ ] Verify all sensors appear in Developer Tools → States

### **12.2 - Create HVAC Dashboard**
- [ ] Go to Overview (or create new dashboard)
- [ ] Click "Edit Dashboard" → "Add Card"
- [ ] Add **Entities Card** with all your HVAC sensors
- [ ] Add **History Graph Card** to show temperature trends over time
- [ ] Add **Gauge Card** for power consumption
- [ ] Organize cards into logical layout

### **12.3 - Set Up Long-Term Statistics**
- [ ] Home Assistant automatically records sensor history
- [ ] Go to History tab and verify data is being logged
- [ ] For longer retention, consider InfluxDB or MariaDB (advanced)

---

## **Phase 13: Data Analysis & Optimization**

### **13.1 - Collect Baseline Data**
- [ ] Let system run for at least 1 week
- [ ] Note different weather conditions
- [ ] Record when AC is on/off (compressor cycles)

### **13.2 - Calculate Useful Metrics**
- [ ] **Temperature differential**: Supply - Return (measures cooling performance)
- [ ] **EER estimate**: Cooling BTU/hr ÷ Power (watts)
  - Rough BTU/hr = CFM × 1.08 × ΔT (need airflow estimate)
- [ ] **Duty cycle**: % time compressor is running
- [ ] **Power vs outdoor temp**: Plot correlation

### **13.3 - Create Template Sensors in HA**
- [ ] Add to configuration.yaml:
```yaml
template:
  - sensor:
      - name: "AC Temperature Differential"
        unit_of_measurement: "°F"
        state: >
          {{ states('sensor.ac_return_temperature') | float - 
             states('sensor.ac_supply_temperature') | float }}
```
- [ ] Add calculated sensors to dashboard

### **13.4 - Identify Optimization Opportunities**
- [ ] Check if AC short-cycles (turns on/off rapidly - inefficient)
- [ ] Verify good temperature differential (should be 15-20°F when running)
- [ ] Compare power consumption to rated efficiency
- [ ] Check if EER degrades significantly in high heat

---

## **Phase 14: Code Cleanup & Documentation**

### **14.1 - Organize Code Structure**
- [ ] Split into multiple files:
  - `main.cpp` - setup and loop
  - `config.h` - WiFi credentials, MQTT server, sensor addresses
  - `sensors.h/cpp` - sensor reading functions
  - `mqtt.h/cpp` - MQTT publishing functions
- [ ] Add header guards to .h files
- [ ] Use meaningful variable names

### **14.2 - Add Comments**
- [ ] Comment each major section
- [ ] Explain non-obvious decisions (timing intervals, calculations)
- [ ] Document sensor wiring in header comment
- [ ] Add file header with project description

### **14.3 - Create README**
- [ ] Create `README.md` in project root
- [ ] Document:
  - Project purpose
  - Hardware list (with links/prices)
  - Wiring diagram (use Fritzing or simple sketch)
  - Software dependencies
  - How to compile and upload
  - Home Assistant configuration needed
  - Calculated metrics and what they mean
- [ ] Take photos of physical setup

### **14.4 - Version Control**
- [ ] Initialize git repository: `git init`
- [ ] Create `.gitignore` (don't commit WiFi passwords!)
- [ ] Commit code: `git add .` then `git commit -m "Initial HVAC monitor"`
- [ ] Create GitHub repository and push
- [ ] Add LICENSE file (MIT or similar)

---

## **Phase 15: Resume & Portfolio**

### **15.1 - Update Resume Projects Section**
- [ ] Add new project entry (use format we discussed earlier)
- [ ] Emphasize: custom C++ firmware, multiple protocols, real-world data collection
- [ ] Mention specific technologies: ESP32, 1-Wire, I2C, UART, MQTT, WiFi

### **15.2 - Prepare Interview Talking Points**
- [ ] Write down key decisions and why you made them:
  - Why Arduino framework vs ESP-IDF
  - Why MQTT vs HTTP REST API
  - How you handled sensor calibration
  - Challenges faced (timing, wiring, debugging)
- [ ] Calculate interesting findings:
  - AC efficiency at different outdoor temps
  - Cost per hour to run AC
  - How much temperature affects power consumption
- [ ] Practice explaining project in 2 minutes

### **15.3 - Create Portfolio/GitHub Link**
- [ ] Clean up GitHub repository
- [ ] Add detailed README with photos
- [ ] Add wiring diagram
- [ ] Include sample data/graphs from Home Assistant
- [ ] Add this link to resume
- [ ] Consider writing blog post about project (optional)

---

## **Troubleshooting Reference**

### **Common Issues:**
- **ESP32 won't connect to WiFi**: Check 2.4GHz vs 5GHz, check SSID/password
- **MQTT connection fails**: Check Home Assistant IP, check Mosquitto is running
- **Sensor reads -196°F**: Not connected, check wiring and pullup resistor
- **Multiple sensors all read same value**: Address conflict, use getAddress() to identify
- **Random crashes/reboots**: Power supply insufficient, use external 5V supply
- **Garbage in Serial Monitor**: Wrong baud rate (use 115200)
- **Library not found**: Check `platformio.ini` lib_deps, rebuild project

---

## **Estimated Timeline:**

- **Phase 1-2** (Setup & Blink): 1-2 hours
- **Phase 3** (Research): 2-3 hours
- **Phase 4-5** (WiFi & MQTT): 2-3 hours
- **Phase 6-7** (Temperature sensors): 3-4 hours
- **Phase 8** (Physical placement): 2-3 hours
- **Phase 9** (Power measurement): 2-4 hours
- **Phase 10-11** (Humidity & CO2): 2-3 hours
- **Phase 12** (Home Assistant setup): 1-2 hours
- **Phase 13** (Data analysis): Ongoing, 1 week+
- **Phase 14** (Documentation): 2-3 hours
- **Phase 15** (Resume): 1 hour

**Total active work: ~20-30 hours** spread over 2-3 weeks

---

**This gives you a complete roadmap. Start with Phase 1 and work through sequentially. Check off items as you complete them. Good luck!**
