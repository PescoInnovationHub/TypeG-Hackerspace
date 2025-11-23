#include <SdsDustSensor.h>

#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <MQUnifiedsensor.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include "BluetoothSerial.h" // <--- NEW: Bluetooth Library

// Check if Bluetooth configurations are enabled in the SDK
#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

// ============================================================================
// CONFIGURATION SECTION
// ============================================================================

// WiFi Configuration
const char* ssid = "NoizeNode";
const char* password = "astroturf";

// MQTT Configuration
const char* mqtt_server = "192.168.1.223";
const int mqtt_port = 1883;

// Device Information
const char* g_deviceModel = "ESP32Device";
const char* g_swVersion = "2.3-BT";
const char* g_manufacturer = "UserK";
String g_deviceName = "AirQuality";

// Bluetooth Object
BluetoothSerial SerialBT; // <--- NEW: Bluetooth Object

// Sensor Pin Configuration
#define LUX_PIN 32
#define PIR_PIN 2
#define DHTPIN 4
#define DHTTYPE DHT22
#define MQ135_PIN 33

// SDS011 UART2 pins
constexpr int SDS_RX = 16; // ESP32 RX2  <- SDS TX
constexpr int SDS_TX = 17; // ESP32 TX2  -> SDS RX

// NeoPixel Pin Configuration
#define PIN_ONE_NEO_PIXEL 15    // PM10
#define PIN_TWO_NEO_PIXEL 12    // NH4
#define PIN_THREE_NEO_PIXEL 5   // CO2
#define PIN_FOUR_NEO_PIXEL 23   // PM2.5
#define PIN_FIVE_NEO_PIXEL 22   // Temperature
#define NUM_PIXELS 10
#define BRIGHTNESS 50

// Sensor Range Configuration
#define MIN_TEMP 7
#define MAX_TEMP 40
#define MIN_HUM 0
#define MAX_HUM 100

#define MIN_PM25 0
#define MAX_PM25 80       // range "logico"
#define MAX_PM25_LED 300  // full bar at 300

#define MIN_PM10 0
#define MAX_PM10 100      // range "logico"
#define MAX_PM10_LED 300  // full bar at 300

#define MIN_CO2 0
#define MAX_CO2 50
#define MIN_NH4 0
#define MAX_NH4 40

// Timing Configuration
const long sensorReadInterval = 5000;      // 5 seconds for main loop
const long pmSensorInterval = 60000;       // 60 seconds between PM sensor cycles
const long pmWarmupTime = 30000;           // 30 seconds warm-up time for PM sensor
const long wifiReconnectInterval = 10000;  // 10 seconds between WiFi reconnect attempts
const long mqttReconnectInterval = 5000;   // 5 seconds between MQTT reconnect attempts
const int WDT_TIMEOUT = 30;                // 30 seconds watchdog timeout

// LED logging control
const bool LED_DEBUG = false;              // default FALSE -> only measurements printed

// Alert fade/blink settings
const unsigned long alertPeriodMs = 1500;  // 1.5s breathing cycle

// ============================================================================
// GLOBAL OBJECTS AND VARIABLES
// ============================================================================

WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHTPIN, DHTTYPE);
MQUnifiedsensor MQ135("ESP-32", 3.3, 12, MQ135_PIN, "MQ-135");

SdsDustSensor sds(Serial2);

// NeoPixel Strips
Adafruit_NeoPixel neoPixels[] = {
  Adafruit_NeoPixel(NUM_PIXELS, PIN_ONE_NEO_PIXEL, NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(NUM_PIXELS, PIN_TWO_NEO_PIXEL, NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(NUM_PIXELS, PIN_THREE_NEO_PIXEL, NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(NUM_PIXELS, PIN_FOUR_NEO_PIXEL, NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(NUM_PIXELS, PIN_FIVE_NEO_PIXEL, NEO_GRB + NEO_KHZ800)
};
const int NUM_NEO_STRIPS = 5;

// Sensor Data
float temperature = 0.0, humidity = 0.0;
float NH4 = 0.0, CO2 = 0.0;
float pm_25 = -99.9, pm_10 = -99.9;
int lightInit, lightVal;
int pirStateCurrent = LOW;
int pirStatePrevious = LOW;

// Timing Variables
unsigned long lastMsg = 0;
unsigned long lastWiFiReconnect = 0;
unsigned long lastMqttReconnect = 0;

// PM Sensor State Machine
enum PMSensorState {
  PM_IDLE,
  PM_WAKING,
  PM_WARMING_UP,
  PM_READING
};

PMSensorState pmState = PM_IDLE;
unsigned long pmStateStartTime = 0;
unsigned long pmLastReadTime = 0;

// MQTT Variables
String g_UniqueId;
String mqttStatusTopic;
bool initialized = false;
bool wifiConnected = false;
bool mqttDiscoverySent = false;

// JSON Payload
StaticJsonDocument<200> loop_payload;
String loop_strPayload;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

void setup_wifi();
void check_wifi_connection();
void setup_ota();
void setup_mq135();
void setup_neopixels();
void mqtt_receiver_callback(char* topic, byte* message, unsigned int length);
void reconnect_mqtt();
void mqtt_home_assistant_discovery();
void get_mq_data();
void dht_routine();
void handle_pm_sensor();
void set_ux();

// Bluetooth Functions
void handle_bluetooth();
void bt_print_menu();
void bt_print_values();
void bt_print_network();

int map_val_to_led_index(float val, float val_min, float val_max, int led_min = 1, int led_max = 10);
uint32_t set_color(int num_pix, bool temp = false, bool hum = false);

// Alert helpers
float alert_factor();
uint32_t scale_color(uint32_t c, float f);

// ============================================================================
// SETUP FUNCTION
// ============================================================================

void setup() {
  Serial.begin(115200);
  
  // Delay to prevent missing boot logs
  delay(2000); 

  Serial.println("\n\n======================================");
  Serial.println("Air Quality Monitor Starting...");
  Serial.println("======================================");

  // Initialize Bluetooth
  String btName = g_deviceName + "_BT";
  if(!SerialBT.begin(btName)){
    Serial.println("An error occurred initializing Bluetooth");
  } else {
    Serial.println("Bluetooth Initialized! Device Name: " + btName);
  }

  setup_wifi();
  setup_neopixels();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqtt_receiver_callback);
  client.setBufferSize(1024);

  pinMode(PIR_PIN, INPUT);
  dht.begin();
  lightInit = analogRead(LUX_PIN);

  setup_mq135();

  // SDS011 PM Sensor (UART2)
  Serial2.begin(9600, SERIAL_8N1, SDS_RX, SDS_TX);
  sds.begin();
  Serial.println("SDS011 Firmware: " + sds.queryFirmwareVersion().toString());
  Serial.println("SDS011 Mode: " + sds.setQueryReportingMode().toString());

  // First PM read asap
  pmLastReadTime = millis() - pmSensorInterval;

  setup_ota();

  // Watchdog Timer
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);

  // Print Device Info
  Serial.println("\n----------------------------------------------");
  Serial.print("MODEL: "); Serial.println(g_deviceModel);
  Serial.print("DEVICE: "); Serial.println(g_deviceName);
  Serial.print("SW Rev: "); Serial.println(g_swVersion);
  Serial.print("Unique ID: "); Serial.println(g_UniqueId);
  Serial.println("----------------------------------------------\n");

  // Quick NeoPixel test
  for (int i = 0; i < NUM_NEO_STRIPS; i++) {
    for (int pixel = 0; pixel < NUM_PIXELS; pixel++) {
      uint32_t colors[] = {
        neoPixels[i].Color(200, 0, 0),
        neoPixels[i].Color(200, 200, 0),
        neoPixels[i].Color(200, 200, 233),
        neoPixels[i].Color(0, 0, 233),
        neoPixels[i].Color(0, 200, 0)
      };
      neoPixels[i].setPixelColor(pixel, colors[i]);
    }
    neoPixels[i].show();
  }
  delay(100);

  initialized = true;
  Serial.println("INIT SYSTEM COMPLETE!\n");
}

// ============================================================================
// SETUP HELPERS
// ============================================================================

void setup_wifi() {
  Serial.println();
  Serial.print("Starting WiFi connection to ");
  Serial.println(ssid);

  byte mac[6];
  WiFi.macAddress(mac);
  g_UniqueId = String(mac[0], HEX) + String(mac[1], HEX) + String(mac[2], HEX) +
               String(mac[3], HEX) + String(mac[4], HEX) + String(mac[5], HEX);

  mqttStatusTopic = "ha-newera/sensor/ESP32Device" + g_deviceName + "/state";

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.println("WiFi connection initiated (non-blocking)");
}

void check_wifi_connection() {
  unsigned long now = millis();

  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiConnected) {
      wifiConnected = true;
      Serial.println("\nWiFi connected!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      mqttDiscoverySent = false;
    }
  } else {
    if (wifiConnected) {
      wifiConnected = false;
      Serial.println("WiFi disconnected!");
    }

    if (now - lastWiFiReconnect > wifiReconnectInterval) {
      lastWiFiReconnect = now;
      Serial.print(".");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
  }
}

void setup_ota() {
  ArduinoOTA.setHostname(g_deviceName.c_str());

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    Serial.println("Start updating " + type);
  });

  ArduinoOTA.onEnd([]() { Serial.println("\nEnd"); });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  Serial.println("OTA ready");
}

void setup_mq135() {
  MQ135.setRegressionMethod(1);
  MQ135.setA(110.47);
  MQ135.setB(-2.862);
  MQ135.init();

  Serial.print("Calibrating MQ-135 please wait");
  float calcR0 = 0;
  for (int i = 1; i <= 10; i++) {
    MQ135.update();
    calcR0 += MQ135.calibrate(3.6);
    Serial.print(".");
  }
  MQ135.setR0(calcR0 / 10);
  Serial.println(" done!");

  if (isinf(calcR0) || calcR0 == 0) {
    Serial.println("Warning: MQ-135 connection issue");
    while (1);
  }
  MQ135.serialDebug(true);
}

void setup_neopixels() {
  for (int i = 0; i < NUM_NEO_STRIPS; i++) {
    neoPixels[i].begin();
    neoPixels[i].clear();
    neoPixels[i].show();
  }
}

// ============================================================================
// BLUETOOTH HELPERS
// ============================================================================

void bt_print_menu() {
  SerialBT.println("\n--- AIR QUALITY MENU ---");
  SerialBT.println("[v] View Sensor Values");
  SerialBT.println("[n] Network Status");
  SerialBT.println("[r] Reboot Device");
  SerialBT.println("[h] Help");
  SerialBT.println("------------------------");
}

void bt_print_values() {
  SerialBT.println("\n--- LIVE READINGS ---");
  SerialBT.print("Temp:  "); SerialBT.print(temperature); SerialBT.println(" C");
  SerialBT.print("Hum:   "); SerialBT.print(humidity); SerialBT.println(" %");
  SerialBT.print("CO2:   "); SerialBT.print(CO2); SerialBT.println(" ppm");
  SerialBT.print("NH4:   "); SerialBT.print(NH4); SerialBT.println(" ppm");
  SerialBT.print("PM2.5: "); SerialBT.println(pm_25);
  SerialBT.print("PM10:  "); SerialBT.println(pm_10);
  SerialBT.print("Lux:   "); SerialBT.println(lightVal);
  SerialBT.print("PIR:   "); SerialBT.println(pirStateCurrent == HIGH ? "Motion" : "Clear");
  SerialBT.println("---------------------");
}

void bt_print_network() {
  SerialBT.println("\n--- NETWORK INFO ---");
  SerialBT.print("WiFi: "); 
  SerialBT.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  
  if (WiFi.status() == WL_CONNECTED) {
    SerialBT.print("IP: "); SerialBT.println(WiFi.localIP());
    SerialBT.print("RSSI: "); SerialBT.println(WiFi.RSSI());
  }
  
  SerialBT.print("MQTT: ");
  SerialBT.println(client.connected() ? "Connected" : "Disconnected");
  SerialBT.println("--------------------");
}

void handle_bluetooth() {
  if (SerialBT.available()) {
    char cmd = SerialBT.read();
    // Consume any extra newline characters
    delay(2);
    while(SerialBT.available()) SerialBT.read();

    switch (cmd) {
      case 'v': case 'V':
        bt_print_values();
        break;
      case 'n': case 'N':
        bt_print_network();
        break;
      case 'r': case 'R':
        SerialBT.println("Rebooting device...");
        delay(500);
        ESP.restart();
        break;
      case 'h': case 'H':
      default:
        bt_print_menu();
        break;
    }
  }
}

// ============================================================================
// MQTT
// ============================================================================

void mqtt_receiver_callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");

  String messageTemp;
  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

  if (String(topic) == String("homeassistant/status")) {
    if (messageTemp == "online") mqtt_home_assistant_discovery();
  }
}

void reconnect_mqtt() {
  unsigned long now = millis();

  if (!wifiConnected) return;
  if (now - lastMqttReconnect < mqttReconnectInterval) return;

  lastMqttReconnect = now;

  if (!client.connected()) {
    Serial.print("Attempting MQTT connection...");

    if (client.connect(g_deviceName.c_str())) {
      Serial.println("connected");
      client.subscribe("homeassistant/status");

      if (!mqttDiscoverySent) {
        mqtt_home_assistant_discovery();
        mqttDiscoverySent = true;
      }
    } else {
      Serial.print("failed, rc=");
      Serial.println(client.state());
    }
  }
}

void mqtt_home_assistant_discovery() {
  Serial.println("Sending Home Assistant MQTT Discovery...");
  // Add your discovery logic here if needed, otherwise handled by HA or custom function
}

// ============================================================================
// SENSOR READING
// ============================================================================

void get_mq_data() {
  MQ135.setA(110.47);
  MQ135.setB(-2.862);
  MQ135.update();
  CO2 = MQ135.readSensor();
  Serial.println("CO2: " + String(CO2) + " PPM");

  MQ135.setA(102.2);
  MQ135.setB(-2.473);
  MQ135.update();
  NH4 = MQ135.readSensor();
  Serial.println("NH4: " + String(NH4) + " PPM");
}

void dht_routine() {
  float cur_humidity = dht.readHumidity();
  float cur_temperature = dht.readTemperature();

  if (isnan(cur_humidity) || isnan(cur_temperature)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    dht.begin();
    return;
  }

  temperature = cur_temperature;
  humidity = cur_humidity;

  Serial.print(F("Humidity: "));
  Serial.print(humidity);
  Serial.print(F("%  Temperature: "));
  Serial.print(temperature);
  Serial.println(F("°C"));
}

void handle_pm_sensor() {
  unsigned long now = millis();

  switch (pmState) {

    case PM_IDLE:
      if (now - pmLastReadTime >= pmSensorInterval) {
        Serial.println("PM Sensor: Starting wake-up sequence...");
        sds.wakeup();
        pmState = PM_WAKING;
        pmStateStartTime = now;
      }
      break;

    case PM_WAKING:
      if (now - pmStateStartTime >= 1000) {
        Serial.println("PM Sensor: Warming up (30 seconds)...");
        pmState = PM_WARMING_UP;
        pmStateStartTime = now;
      }
      break;

    case PM_WARMING_UP:
      if (now - pmStateStartTime >= pmWarmupTime) {
        Serial.println("PM Sensor: Warm-up complete, reading data...");
        pmState = PM_READING;
      }
      break;

    case PM_READING: {
        PmResult pm = sds.queryPm();
        if (pm.isOk()) {
          pm_25 = pm.pm25;
          pm_10 = pm.pm10;
          Serial.println(pm.toString());
        } else {
          Serial.print("PM Sensor: Could not read - ");
          Serial.println(pm.statusToString());
        }

        sds.sleep();
        pmLastReadTime = now;
        pmState = PM_IDLE;
        Serial.println("PM Sensor: Will read again in 60 seconds\n");
      }
      break;

    default:
      pmState = PM_IDLE;
      break;
  }
}

// ============================================================================
// ALERT HELPERS
// ============================================================================

float alert_factor() {
  float phase = (millis() % alertPeriodMs) / (float)alertPeriodMs; // 0..1
  float tri = (phase < 0.5f) ? (phase * 2.0f) : (2.0f - phase * 2.0f); // 0..1..0
  return tri * tri; // ease-in/out
}

uint32_t scale_color(uint32_t c, float f) {
  uint8_t r = (c >> 16) & 0xFF;
  uint8_t g = (c >> 8) & 0xFF;
  uint8_t b = c & 0xFF;
  r = (uint8_t)(r * f);
  g = (uint8_t)(g * f);
  b = (uint8_t)(b * f);
  return Adafruit_NeoPixel::Color(r, g, b);
}

// ============================================================================
// NEOPIXEL UX
// ============================================================================

int map_val_to_led_index(float val, float val_min, float val_max, int led_min, int led_max) {
  if (val > val_max) val = val_max;
  if (val < val_min) val = val_min;

  float mappedValue = (val - val_min) / (val_max - val_min) * (led_max - led_min) + led_min;
  if (mappedValue < led_min) mappedValue = led_min;
  if (mappedValue > led_max) mappedValue = led_max;

  return int(mappedValue);
}

uint32_t set_color(int num_pix, bool temp, bool hum) {
  if (num_pix < 3) {
    if (temp) return Adafruit_NeoPixel::Color(10, 10, 200);
    else if (hum) return Adafruit_NeoPixel::Color(200, 50, 50);
    else return Adafruit_NeoPixel::Color(50, 200, 50);
  }
  if (num_pix < 5) {
    if (temp) return Adafruit_NeoPixel::Color(50, 50, 200);
    else if (hum) return Adafruit_NeoPixel::Color(200, 50, 50);
    else return Adafruit_NeoPixel::Color(200, 50, 50);
  }
  else if (num_pix < 8) {
    return Adafruit_NeoPixel::Color(50, 50, 200);
  }
  else {
    if (hum) return Adafruit_NeoPixel::Color(10, 10, 200);
    else return Adafruit_NeoPixel::Color(250, 50, 10);
  }
}

void set_ux() {
  for (int i = 0; i < NUM_NEO_STRIPS; i++) neoPixels[i].clear();
  int num_pix;

  const uint32_t ORANGE = Adafruit_NeoPixel::Color(255, 120, 0);
  const uint32_t RED    = Adafruit_NeoPixel::Color(255, 0, 0);

  // Strip 0: PM10
  if (pm_10 > 0) {
    if (pm_10 >= 300 && pm_10 <= 500) {
      float f = alert_factor();
      uint32_t col = scale_color(ORANGE, f);
      for (int p = 0; p < NUM_PIXELS; p++) neoPixels[0].setPixelColor(p, col);

    } else if (pm_10 >= 501 && pm_10 <= 700) {
      float f = alert_factor();
      uint32_t col = scale_color(RED, f);
      for (int p = 0; p < NUM_PIXELS; p++) neoPixels[0].setPixelColor(p, col);

    } else if (pm_10 > 700) {
      for (int p = 0; p < NUM_PIXELS; p++) neoPixels[0].setPixelColor(p, RED);

    } else {
      num_pix = map_val_to_led_index(pm_10, MIN_PM10, MAX_PM10_LED);
      if (LED_DEBUG) { Serial.print("PM10 LED: "); Serial.println(num_pix); }
      for (int p = 0; p < num_pix; p++) neoPixels[0].setPixelColor(p, set_color(num_pix));
    }
    neoPixels[0].setBrightness(BRIGHTNESS);
  }

  // Strip 1: NH4
  if (NH4 > 0) {
    num_pix = map_val_to_led_index(NH4, MIN_NH4, MAX_NH4);
    if (LED_DEBUG) { Serial.print("NH4 LED: "); Serial.println(num_pix); }
    for (int p = 0; p < num_pix; p++) neoPixels[1].setPixelColor(p, set_color(num_pix));
    neoPixels[1].setBrightness(BRIGHTNESS);
  }

  // Strip 2: CO2
  if (CO2 > 0) {
    num_pix = map_val_to_led_index(CO2, MIN_CO2, MAX_CO2);
    if (LED_DEBUG) { Serial.print("CO2 LED: "); Serial.println(num_pix); }
    for (int p = 0; p < num_pix; p++) neoPixels[2].setPixelColor(p, set_color(num_pix));
    neoPixels[2].setBrightness(BRIGHTNESS);
  }

  // Strip 3: PM2.5
  if (pm_25 > 0) {
    if (pm_25 >= 300 && pm_25 <= 500) {
      float f = alert_factor();
      uint32_t col = scale_color(ORANGE, f);
      for (int p = 0; p < NUM_PIXELS; p++) neoPixels[3].setPixelColor(p, col);

    } else if (pm_25 >= 501 && pm_25 <= 700) {
      float f = alert_factor();
      uint32_t col = scale_color(RED, f);
      for (int p = 0; p < NUM_PIXELS; p++) neoPixels[3].setPixelColor(p, col);

    } else if (pm_25 > 700) {
      for (int p = 0; p < NUM_PIXELS; p++) neoPixels[3].setPixelColor(p, RED);

    } else {
      num_pix = map_val_to_led_index(pm_25, MIN_PM25, MAX_PM25_LED);
      if (LED_DEBUG) { Serial.print("PM2.5 LED: "); Serial.println(num_pix); }
      for (int p = 0; p < num_pix; p++) neoPixels[3].setPixelColor(p, set_color(num_pix));
    }
    neoPixels[3].setBrightness(BRIGHTNESS);
  }

  // Strip 4: Temperature
  if (temperature > 0) {
    num_pix = map_val_to_led_index(temperature, MIN_TEMP, MAX_TEMP);
    if (LED_DEBUG) { Serial.print("Temp LED: "); Serial.println(num_pix); }
    for (int p = 0; p < num_pix; p++) neoPixels[4].setPixelColor(p, set_color(num_pix, true, false));
    neoPixels[4].setBrightness(BRIGHTNESS);
  }

  for (int i = 0; i < NUM_NEO_STRIPS; i++) neoPixels[i].show();
  delay(100);
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  esp_task_wdt_reset();

  // Check for Bluetooth commands continuously (non-blocking)
  handle_bluetooth();

  if (wifiConnected) ArduinoOTA.handle();
  check_wifi_connection();

  if (wifiConnected) {
    if (client.connected()) client.loop();
    else reconnect_mqtt();
  }

  handle_pm_sensor();

  unsigned long now = millis();
  if (now - lastMsg > sensorReadInterval) {
    lastMsg = now;
    loop_payload.clear();

    // PIR
    pirStatePrevious = pirStateCurrent;
    pirStateCurrent = digitalRead(PIR_PIN);

    if (pirStatePrevious == LOW && pirStateCurrent == HIGH) {
      Serial.println("Motion detected!");
      loop_payload["pir"] = "ON";
    } else if (pirStatePrevious == HIGH && pirStateCurrent == LOW) {
      Serial.println("Motion stopped!");
      loop_payload["pir"] = "OFF";
    } else {
      loop_payload["pir"] = (pirStateCurrent == HIGH) ? "ON" : "OFF";
    }

    // LUX
    lightVal = analogRead(LUX_PIN);
    Serial.print("LUX: ");
    Serial.println(lightVal);
    loop_payload["lux"] = lightVal;

    // MQ135
    get_mq_data();
    loop_payload["nh4_ppm"] = NH4;
    loop_payload["co2_ppm"] = CO2;

    // DHT22
    dht_routine();
    loop_payload["temp"] = temperature;
    loop_payload["hum"] = humidity;

    // PM
    loop_payload["pm25_ppm"] = pm_25;
    loop_payload["pm10_ppm"] = pm_10;

    // UX
    set_ux();

    // Publish MQTT
    if (wifiConnected && client.connected()) {
      loop_strPayload.clear();
      serializeJson(loop_payload, loop_strPayload);
      client.publish(mqttStatusTopic.c_str(), loop_strPayload.c_str());
      Serial.println("MQTT: Data published!");
      Serial.println();
    } else {
      Serial.println("WiFi/MQTT not connected - sensors continue operating locally");
    }
  }
}