/*
 * SMART GARDEN - ESP32 + DHT11 + 2 soil sensors + relay + Firebase RTDB
 *
 * Firebase project:
 *   tuoicayiot-cc958
 *
 * Database structure:
 *   /status
 *     soil1, soil2, soilAvg          (% moisture)
 *     soilRaw1, soilRaw2             (ADC)
 *     temperature, humidity
 *     pumpState                      "IDLE" / "WATERING"
 *     updatedAt                      (millis)
 *
 *   /control
 *     threshold                      (10..90 %)
 *     manualPump                     (true/false)
 *
 *   /alerts/latest
 *     type, message, timestamp
 *
 * Libraries:
 *   - DHT sensor library by Adafruit
 *   - Firebase Arduino Client Library for ESP8266 and ESP32 by Mobizt
 *   - ESP32 board package
 *
 * IMPORTANT:
 *   1) Enable Authentication -> Sign-in method -> Anonymous.
 *   2) Database rules can use the supplied database.rules.json.
 *   3) Fill WIFI_SSID and WIFI_PASSWORD below. ESP32 now keeps retrying Wi-Fi before Firebase setup.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <Firebase_ESP_Client.h>
#include "DHT.h"
#include <addons/TokenHelper.h>
#include <addons/RTDBHelper.h>

// ========================= WIFI / FIREBASE =========================
#define WIFI_SSID     "YOUR_WIFI_NAME"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

#define API_KEY "AIzaSyCIGqAb3SKXkoUX3cid8v_HJdd8U-C2UW-M"
#define DATABASE_URL "https://tuoicayiot-cc958-default-rtdb.firebaseio.com/"

// ============================ HARDWARE =============================
constexpr uint8_t PIN_DHT    = 4;
constexpr uint8_t PIN_RELAY  = 25;
constexpr uint8_t PIN_SOIL_1 = 32;
constexpr uint8_t PIN_SOIL_2 = 33;

#define DHTTYPE DHT11
DHT dht(PIN_DHT, DHTTYPE);

constexpr int RELAY_ON  = HIGH;
constexpr int RELAY_OFF = LOW;

// Soil sensor calibration used to convert ADC -> moisture percentage.
// Change these two values after measuring your own sensor.
constexpr int SOIL_WET_RAW = 1200; // 100% moisture
constexpr int SOIL_DRY_RAW = 3300; // 0% moisture

constexpr unsigned long SENSOR_INTERVAL_MS = 2000;
constexpr unsigned long WATERING_CHECK_MS  = 1000;
constexpr unsigned long MAX_WATER_SAFETY_MS = 60000;
constexpr unsigned long CONTROL_INTERVAL_MS = 1500;
constexpr int SOIL_SAMPLES = 10;
constexpr int DHT_MAX_FAILS_WARN = 5;

// ========================== FIREBASE ===============================
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

bool signupOK = false;
unsigned long lastSensorRead = 0;
unsigned long lastWaterCheck = 0;
unsigned long lastControlRead = 0;
unsigned long stateChangedAt = 0;

enum SystemState { ST_MONITOR, ST_WATERING };
SystemState state = ST_MONITOR;

int thresholdPercent = 40;
bool manualPump = false;

float lastValidTemp = NAN;
float lastValidHumidity = NAN;
bool dhtEverValid = false;
uint16_t dhtConsecutiveFails = 0;

// Prevent duplicate alerts.
String lastAlertMessage = "";

unsigned long epochMs() {
  time_t now = time(nullptr);
  if (now < 100000) return millis();
  return (unsigned long)now * 1000UL;
}

// ============================ HELPERS ==============================
int readSoilAveraged(uint8_t pin, int samples = SOIL_SAMPLES) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(10);
  }
  return sum / samples;
}

int rawToPercent(int raw) {
  // Higher ADC = drier soil for this capacitive sensor.
  long pct = map(raw, SOIL_DRY_RAW, SOIL_WET_RAW, 0, 100);
  return constrain((int)pct, 0, 100);
}

void publishAlert(const char *type, const String &message) {
  if (!Firebase.ready() || message == lastAlertMessage) return;

  FirebaseJson json;
  json.set("type", type);
  json.set("message", message);
  json.set("timestamp", (double)epochMs());

  if (Firebase.RTDB.setJSON(&fbdo, "/alerts/latest", &json)) {
    lastAlertMessage = message;
  } else {
    Serial.print("Firebase alert error: ");
    Serial.println(fbdo.errorReason());
  }
}

void publishStatus(int raw1, int raw2, int soil1, int soil2, float temp, float humidity) {
  if (!Firebase.ready()) return;

  FirebaseJson json;
  json.set("soil1", soil1);
  json.set("soil2", soil2);
  json.set("soilAvg", (soil1 + soil2) / 2);
  json.set("soilRaw1", raw1);
  json.set("soilRaw2", raw2);

  if (!isnan(temp)) json.set("temperature", temp);
  if (!isnan(humidity)) json.set("humidity", humidity);

  json.set("pumpState", state == ST_WATERING ? "WATERING" : "IDLE");
  json.set("updatedAt", (double)epochMs());

  if (!Firebase.RTDB.setJSON(&fbdo, "/status", &json)) {
    Serial.print("Firebase status error: ");
    Serial.println(fbdo.errorReason());
  }
}

void readControl() {
  if (!Firebase.ready()) return;

  if (Firebase.RTDB.getInt(&fbdo, "/control/threshold")) {
    thresholdPercent = constrain(fbdo.to<int>(), 10, 90);
  }

  if (Firebase.RTDB.getBool(&fbdo, "/control/manualPump")) {
    bool newManual = fbdo.to<bool>();

    if (newManual != manualPump) {
      manualPump = newManual;

      if (manualPump && state == ST_MONITOR) {
        digitalWrite(PIN_RELAY, RELAY_ON);
        state = ST_WATERING;
        stateChangedAt = millis();
        publishAlert("INFO", "Đã bật máy bơm theo lệnh từ website.");
      }

      if (!manualPump && state == ST_WATERING) {
        digitalWrite(PIN_RELAY, RELAY_OFF);
        state = ST_MONITOR;
        stateChangedAt = millis();
        publishAlert("INFO", "Đã dừng máy bơm theo lệnh từ website.");
      }
    }
  }
}

void startWatering(const String &reason) {
  if (state == ST_WATERING) return;

  digitalWrite(PIN_RELAY, RELAY_ON);
  state = ST_WATERING;
  stateChangedAt = millis();

  Serial.print(">> BAT BOM: ");
  Serial.println(reason);
  publishAlert("WARNING", "Đất khô: hệ thống đã bật máy bơm.");
}

void stopWatering(const String &reason) {
  digitalWrite(PIN_RELAY, RELAY_OFF);
  state = ST_MONITOR;
  stateChangedAt = millis();

  Serial.print(">> TAT BOM: ");
  Serial.println(reason);

  if (reason.indexOf("an toan") >= 0) {
    publishAlert("ERROR", "Bơm đã tự ngắt vì vượt thời gian an toàn 60 giây. Kiểm tra cảm biến đất.");
  } else {
    publishAlert("INFO", "Đất đã đủ ẩm: hệ thống đã tắt máy bơm.");
  }
}

void readDHT(float &temp, float &humidity) {
  humidity = dht.readHumidity();
  temp = dht.readTemperature();

  if (isnan(humidity) || isnan(temp)) {
    dhtConsecutiveFails++;
    temp = lastValidTemp;
    humidity = lastValidHumidity;

    Serial.print("CANH BAO: DHT11 doc loi, lan ");
    Serial.println(dhtConsecutiveFails);

    if (dhtConsecutiveFails == DHT_MAX_FAILS_WARN) {
      publishAlert("ERROR", "DHT11 lỗi liên tục 5 lần. Kiểm tra VCC/GND/DATA và điện trở pull-up 4.7k-10k.");
    }
  } else {
    dhtConsecutiveFails = 0;
    lastValidTemp = temp;
    lastValidHumidity = humidity;
    dhtEverValid = true;
  }
}

void handleSerialTestCommand() {
  if (Serial.available() <= 0) return;

  char c = Serial.read();
  while (Serial.available() > 0) Serial.read();

  if (c == 'b' || c == 'B') {
    if (state == ST_MONITOR) {
      manualPump = true;
      digitalWrite(PIN_RELAY, RELAY_ON);
      state = ST_WATERING;
      stateChangedAt = millis();
      publishAlert("INFO", "Đã bật bơm bằng lệnh test Serial.");
    }
  }
}

// ============================== SETUP ==============================
void setup() {
  Serial.begin(115200);
  delay(300);

  dht.begin();

  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, RELAY_OFF);

  Serial.println();
  Serial.println("====================================");
  Serial.println("     SMART GARDEN - ESP32");
  Serial.println("====================================");

  // Keep trying until Wi-Fi is available. The previous code returned from
  // setup() after 20 seconds, which meant Firebase was never configured and
  // the loop's Wi-Fi reconnect alone could not restore Firebase initialization.
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Dang ket noi Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
    // Retry credentials/connectivity without restarting the ESP32.
    static unsigned long lastWiFiRetry = 0;
    if (millis() - lastWiFiRetry > 15000) {
      lastWiFiRetry = millis();
      WiFi.disconnect();
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }
  Serial.println();

  Serial.print("Wi-Fi OK. IP: ");
  Serial.println(WiFi.localIP());

  // Vietnam time (UTC+7) is used for alert timestamps shown on the website.
  configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  config.token_status_callback = tokenStatusCallback;

  Firebase.reconnectNetwork(true);

  fbdo.setBSSLBufferSize(4096, 1024);
  fbdo.setResponseSize(2048);

  // Anonymous authentication. Enable Anonymous in Firebase Console first.
  if (Firebase.signUp(&config, &auth, "", "")) {
    signupOK = true;
    Serial.println("Firebase Anonymous Auth: OK");
  } else {
    Serial.print("Firebase Auth ERROR: ");
    Serial.println(config.signer.signupError.message.c_str());
  }

  Firebase.begin(&config, &auth);

  // Initial control values if they do not exist yet.
  if (Firebase.ready()) {
    if (!Firebase.RTDB.getInt(&fbdo, "/control/threshold")) {
      Firebase.RTDB.setInt(&fbdo, "/control/threshold", thresholdPercent);
    }

    if (!Firebase.RTDB.getBool(&fbdo, "/control/manualPump")) {
      Firebase.RTDB.setBool(&fbdo, "/control/manualPump", false);
    }

    publishAlert("INFO", "ESP32 đã kết nối Firebase và sẵn sàng.");
  }
}

// =============================== LOOP ==============================
void loop() {
  handleSerialTestCommand();

  // Keep Firebase authentication/token alive.
  if (signupOK) {
    Firebase.ready();
  }

  unsigned long now = millis();

  // Read control commands from the website.
  if (now - lastControlRead >= CONTROL_INTERVAL_MS) {
    lastControlRead = now;
    readControl();
  }

  // Read sensors.
  if (now - lastSensorRead >= SENSOR_INTERVAL_MS) {
    lastSensorRead = now;

    float temp = NAN;
    float humidity = NAN;
    readDHT(temp, humidity);

    int raw1 = readSoilAveraged(PIN_SOIL_1);
    int raw2 = readSoilAveraged(PIN_SOIL_2);

    int soil1 = rawToPercent(raw1);
    int soil2 = rawToPercent(raw2);
    int soilAvg = (soil1 + soil2) / 2;

    Serial.print("Dat1=");
    Serial.print(soil1);
    Serial.print("% (ADC ");
    Serial.print(raw1);
    Serial.print(") | Dat2=");
    Serial.print(soil2);
    Serial.print("% (ADC ");
    Serial.print(raw2);
    Serial.print(") | Avg=");
    Serial.print(soilAvg);
    Serial.print("% | Nhiet=");
    Serial.print(temp, 1);
    Serial.print("C | KK=");
    Serial.print(humidity, 1);
    Serial.print("% | Bom=");
    Serial.println(state == ST_WATERING ? "ON" : "OFF");

    publishStatus(raw1, raw2, soil1, soil2, temp, humidity);

    // Manual pump has priority.
    if (!manualPump && state == ST_MONITOR && soilAvg < thresholdPercent) {
      startWatering("độ ẩm trung bình thấp hơn ngưỡng website.");
    }
  }

  // Stop automatic/manual watering based on moisture or safety timeout.
  if (state == ST_WATERING && now - lastWaterCheck >= WATERING_CHECK_MS) {
    lastWaterCheck = now;

    int rawNow = readSoilAveraged(PIN_SOIL_1);
    int soilNow = rawToPercent(rawNow);

    // If manualPump is still ON, keep pump ON until user presses DỪNG.
    if (!manualPump && soilNow >= thresholdPercent + 5) {
      stopWatering("đất đã đủ ẩm.");
    } else if (now - stateChangedAt >= MAX_WATER_SAFETY_MS) {
      manualPump = false;
      Firebase.RTDB.setBool(&fbdo, "/control/manualPump", false);
      stopWatering("het thoi gian an toan.");
    }
  }

  // Auto reconnect Wi-Fi.
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi mat ket noi, dang thu ket noi lai...");
    WiFi.reconnect();
    delay(200);
  }
}
