#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <cmath>
#include <cstring>

#include "amts_config.h"

namespace {

constexpr uint32_t kTaskStackAutomation = 4096;
constexpr uint32_t kTaskStackNetwork = 8192;
constexpr float kUltrasonicTimeoutUs = 30000.0f;

enum class SystemMode : uint8_t {
  Auto,
  Manual,
};

enum class DriveMode : uint8_t {
  Stop,
  Forward,
  PivotLeft,
  PivotRight,
  EmergencyBrake,
  Backward,
};

struct TelemetryFrame {
  float distanceCm = -1.0f;
  int leftIr = 0;
  int rightIr = 0;
  float temperatureC = NAN;
  float humidityPct = NAN;
  bool dhtOk = false;
  bool obstacleDetected = false;
  bool wifiConnected = false;
  DriveMode driveMode = DriveMode::Stop;
  SystemMode systemMode = SystemMode::Auto;
  uint32_t uptimeMs = 0;
};

struct ControlState {
  SystemMode systemMode = SystemMode::Auto;
  DriveMode manualDriveCommand = DriveMode::Stop;
};

TaskHandle_t automationTaskHandle = nullptr;
TaskHandle_t networkTaskHandle = nullptr;
SemaphoreHandle_t telemetryMutex = nullptr;
SemaphoreHandle_t controlMutex = nullptr;
TelemetryFrame telemetryFrame;
ControlState controlState;

DHT dht(amts::DHT_PIN, DHT11);

bool credentialsConfigured(const char *value) {
  return value != nullptr && value[0] != '\0' && std::strncmp(value, "YOUR_", 5) != 0;
}

const char *systemModeToString(SystemMode mode) {
  switch (mode) {
    case SystemMode::Manual:
      return "MANUAL";
    case SystemMode::Auto:
    default:
      return "AUTO";
  }
}

const char *driveModeToString(DriveMode mode) {
  switch (mode) {
    case DriveMode::Forward:
      return "forward";
    case DriveMode::PivotLeft:
      return "left";
    case DriveMode::PivotRight:
      return "right";
    case DriveMode::Backward:
      return "backward";
    case DriveMode::EmergencyBrake:
      return "emergency_brake";
    case DriveMode::Stop:
    default:
      return "stop";
  }
}

void writeMotorOutputs(bool in1, bool in2, bool in3, bool in4) {
  digitalWrite(amts::IN1, in1 ? HIGH : LOW);
  digitalWrite(amts::IN2, in2 ? HIGH : LOW);
  digitalWrite(amts::IN3, in3 ? HIGH : LOW);
  digitalWrite(amts::IN4, in4 ? HIGH : LOW);
}

void setDriveMode(DriveMode mode) {
  switch (mode) {
    case DriveMode::Forward:
      writeMotorOutputs(!amts::INVERT_LEFT_MOTOR, amts::INVERT_LEFT_MOTOR, !amts::INVERT_RIGHT_MOTOR, amts::INVERT_RIGHT_MOTOR);
      break;
    case DriveMode::PivotLeft:
      writeMotorOutputs(amts::INVERT_LEFT_MOTOR, !amts::INVERT_LEFT_MOTOR, !amts::INVERT_RIGHT_MOTOR, amts::INVERT_RIGHT_MOTOR);
      break;
    case DriveMode::PivotRight:
      writeMotorOutputs(!amts::INVERT_LEFT_MOTOR, amts::INVERT_LEFT_MOTOR, amts::INVERT_RIGHT_MOTOR, !amts::INVERT_RIGHT_MOTOR);
      break;
    case DriveMode::Backward:
      writeMotorOutputs(amts::INVERT_LEFT_MOTOR, !amts::INVERT_LEFT_MOTOR, amts::INVERT_RIGHT_MOTOR, !amts::INVERT_RIGHT_MOTOR);
      break;
    case DriveMode::EmergencyBrake:
    case DriveMode::Stop:
    default:
      writeMotorOutputs(false, false, false, false);
      break;
  }
}

void updateControlState(SystemMode mode, DriveMode cmd) {
  if (controlMutex != nullptr && xSemaphoreTake(controlMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    controlState.systemMode = mode;
    controlState.manualDriveCommand = cmd;
    xSemaphoreGive(controlMutex);
  }
}

ControlState getControlState() {
  ControlState state;
  if (controlMutex != nullptr && xSemaphoreTake(controlMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    state = controlState;
    xSemaphoreGive(controlMutex);
  }
  return state;
}

float readDistanceCm() {
  digitalWrite(amts::TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(amts::TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(amts::TRIG, LOW);

  const unsigned long durationUs = pulseIn(amts::ECHO, HIGH, static_cast<unsigned long>(kUltrasonicTimeoutUs));
  if (durationUs == 0) {
    return -1.0f;
  }

  return (static_cast<float>(durationUs) * 0.0343f) / 2.0f;
}

void updateTelemetry(const TelemetryFrame &frame) {
  if (telemetryMutex != nullptr && xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    telemetryFrame = frame;
    xSemaphoreGive(telemetryMutex);
  }
}

TelemetryFrame snapshotTelemetry() {
  TelemetryFrame frame;
  if (telemetryMutex != nullptr && xSemaphoreTake(telemetryMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    frame = telemetryFrame;
    xSemaphoreGive(telemetryMutex);
  }

  frame.uptimeMs = millis();
  frame.wifiConnected = WiFi.status() == WL_CONNECTED;
  return frame;
}

bool readDhtSample(float &temperatureC, float &humidityPct) {
  humidityPct = dht.readHumidity();
  temperatureC = dht.readTemperature();
  return !std::isnan(temperatureC) && !std::isnan(humidityPct);
}

String buildTelemetryJson(const TelemetryFrame &frame) {
  String json;
  json.reserve(380);
  json += '{';
  json += "\"device_id\":\"";
  json += amts::DEVICE_ID;
  json += "\",";
  json += "\"uptime_ms\":";
  json += String(frame.uptimeMs);
  json += ',';
  json += "\"wifi_connected\":";
  json += (frame.wifiConnected ? "true" : "false");
  json += ',';
  json += "\"wifi_rssi_dbm\":";
  json += String(frame.wifiConnected ? WiFi.RSSI() : 0);
  json += ',';
  json += "\"distance_cm\":";
  json += String(frame.distanceCm, 2);
  json += ',';
  json += "\"left_ir\":";
  json += String(frame.leftIr);
  json += ',';
  json += "\"right_ir\":";
  json += String(frame.rightIr);
  json += ',';
  json += "\"obstacle_detected\":";
  json += (frame.obstacleDetected ? "true" : "false");
  json += ',';
  json += "\"system_mode\":\"";
  json += systemModeToString(frame.systemMode);
  json += "\",";
  json += "\"dht_ok\":";
  json += (frame.dhtOk ? "true" : "false");
  json += ',';
  json += "\"drive_mode\":\"";
  json += driveModeToString(frame.driveMode);
  json += "\",";
  json += "\"temperature_c\":";
  json += std::isnan(frame.temperatureC) ? String("null") : String(frame.temperatureC, 2);
  json += ',';
  json += "\"humidity_pct\":";
  json += std::isnan(frame.humidityPct) ? String("null") : String(frame.humidityPct, 2);
  json += '}';

  return json;
}

bool publishTelemetryToFirebase(const TelemetryFrame &frame) {
  if (!credentialsConfigured(amts::FIREBASE_DATABASE_URL) || !credentialsConfigured(amts::FIREBASE_AUTH_TOKEN)) {
    Serial.println("[Network] Firebase settings are placeholders; telemetry publish skipped.");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String(amts::FIREBASE_DATABASE_URL);
  if (!url.endsWith("/")) {
    url += '/';
  }
  url += "telemetry.json?auth=";
  url += amts::FIREBASE_AUTH_TOKEN;

  if (!http.begin(client, url)) {
    Serial.println("[Network] Failed to start HTTPS session.");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  const String payload = buildTelemetryJson(frame);
  const int responseCode = http.PUT(payload);
  const bool success = responseCode > 0 && responseCode < 300;

  Serial.printf("[Network] Publish %s (HTTP %d)\n", success ? "OK" : "FAILED", responseCode);
  if (!success) {
    Serial.println(http.getString());
  }

  http.end();
  return success;
}

void executeEmergencyBrake() {
  setDriveMode(DriveMode::EmergencyBrake);
}

void driveForward() {
  setDriveMode(DriveMode::Forward);
}

void driveBackward() {
  setDriveMode(DriveMode::Backward);
}

void pivotLeft() {
  setDriveMode(DriveMode::PivotLeft);
}

void pivotRight() {
  setDriveMode(DriveMode::PivotRight);
}

void stopMotors() {
  setDriveMode(DriveMode::Stop);
}

void loopAutomation(void *pvParameters) {
  (void)pvParameters;

  for (;;) {
    const float distanceCm = readDistanceCm();
    const int leftIr = digitalRead(amts::LEFT_IR);
    const int rightIr = digitalRead(amts::RIGHT_IR);
    const bool obstacleDetected = distanceCm > 0.0f && distanceCm <= amts::FRONT_OBSTACLE_STOP_CM;

    ControlState ctrl = getControlState();

    TelemetryFrame frame = snapshotTelemetry();
    frame.distanceCm = distanceCm;
    frame.leftIr = leftIr;
    frame.rightIr = rightIr;
    frame.obstacleDetected = obstacleDetected;
    frame.systemMode = ctrl.systemMode;

    if (ctrl.systemMode == SystemMode::Manual) {
      // Manual Driving Mode
      if (obstacleDetected && ctrl.manualDriveCommand == DriveMode::Forward) {
        // Safety override: Halt manual forward movement if obstacle is detected
        executeEmergencyBrake();
        frame.driveMode = DriveMode::EmergencyBrake;
      } else {
        setDriveMode(ctrl.manualDriveCommand);
        frame.driveMode = ctrl.manualDriveCommand;
      }
    } else {
      // Auto Line-Following Mode
      if (obstacleDetected) {
        executeEmergencyBrake();
        frame.driveMode = DriveMode::EmergencyBrake;
      } else if (leftIr == LOW && rightIr == LOW) {
        driveForward();
        frame.driveMode = DriveMode::Forward;
      } else if (leftIr == HIGH && rightIr == LOW) {
        pivotLeft();
        frame.driveMode = DriveMode::PivotLeft;
      } else if (leftIr == LOW && rightIr == HIGH) {
        pivotRight();
        frame.driveMode = DriveMode::PivotRight;
      } else {
        stopMotors();
        frame.driveMode = DriveMode::Stop;
      }
    }

    updateTelemetry(frame);
    vTaskDelay(pdMS_TO_TICKS(amts::AUTOMATION_PERIOD_MS));
  }
}

void pollFirebaseControl() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (!credentialsConfigured(amts::FIREBASE_DATABASE_URL) || !credentialsConfigured(amts::FIREBASE_AUTH_TOKEN)) return;

  static WiFiClientSecure client;
  static HTTPClient http;
  static bool connectionInitialized = false;

  if (!connectionInitialized) {
    client.setInsecure();
    http.setReuse(true);
    connectionInitialized = true;
  }

  String url = String(amts::FIREBASE_DATABASE_URL);
  if (!url.endsWith("/")) {
    url += '/';
  }
  url += "control.json?auth=";
  url += amts::FIREBASE_AUTH_TOKEN;

  if (!http.begin(client, url)) {
    Serial.println("[Network] Failed to begin control GET session.");
    return;
  }

  int responseCode = http.GET();
  if (responseCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (!error) {
      const char* sysModeStr = doc["system_mode"] | "AUTO";
      const char* driveCmdStr = doc["drive_command"] | "stop";

      SystemMode mode = SystemMode::Auto;
      if (strcmp(sysModeStr, "MANUAL") == 0) {
        mode = SystemMode::Manual;
      }

      DriveMode cmd = DriveMode::Stop;
      if (strcmp(driveCmdStr, "forward") == 0) {
        cmd = DriveMode::Forward;
      } else if (strcmp(driveCmdStr, "backward") == 0) {
        cmd = DriveMode::Backward;
      } else if (strcmp(driveCmdStr, "left") == 0) {
        cmd = DriveMode::PivotLeft;
      } else if (strcmp(driveCmdStr, "right") == 0) {
        cmd = DriveMode::PivotRight;
      }

      updateControlState(mode, cmd);
    } else {
      Serial.printf("[Network] JSON parse error: %s\n", error.c_str());
    }
  } else {
    Serial.printf("[Network] Control poll failed, response code: %d\n", responseCode);
  }
}

void loopNetworking(void *pvParameters) {
  (void)pvParameters;

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  dht.begin();

  unsigned long lastWifiAttemptMs = 0;
  unsigned long lastTelemetryAttemptMs = 0;
  unsigned long lastControlPollMs = 0;
  constexpr unsigned long kControlPollPeriodMs = 250;

  for (;;) {
    const unsigned long nowMs = millis();

    if (!credentialsConfigured(amts::WIFI_SSID) || !credentialsConfigured(amts::WIFI_PASSWORD)) {
      Serial.println("[Network] Wi-Fi credentials are placeholders; reconnect loop is idle.");
      vTaskDelay(pdMS_TO_TICKS(amts::TELEMETRY_PERIOD_MS));
      continue;
    }

    if (WiFi.status() != WL_CONNECTED) {
      if (nowMs - lastWifiAttemptMs >= amts::WIFI_RETRY_PERIOD_MS) {
        Serial.printf("[Network] Connecting to %s\n", amts::WIFI_SSID);
        WiFi.disconnect(false);
        WiFi.begin(amts::WIFI_SSID, amts::WIFI_PASSWORD);
        lastWifiAttemptMs = nowMs;
      }

      TelemetryFrame frame = snapshotTelemetry();
      frame.wifiConnected = false;
      updateTelemetry(frame);

      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }

    // Wi-Fi Connected

    // 1. Poll control configurations
    if (nowMs - lastControlPollMs >= kControlPollPeriodMs) {
      pollFirebaseControl();
      lastControlPollMs = nowMs;
    }

    // 2. Publish telemetry state
    if (nowMs - lastTelemetryAttemptMs >= amts::TELEMETRY_PERIOD_MS) {
      float temperatureC = NAN;
      float humidityPct = NAN;
      const bool dhtOk = readDhtSample(temperatureC, humidityPct);

      TelemetryFrame frame = snapshotTelemetry();
      frame.wifiConnected = true;
      frame.temperatureC = dhtOk ? temperatureC : NAN;
      frame.humidityPct = dhtOk ? humidityPct : NAN;
      frame.dhtOk = dhtOk;

      if (dhtOk) {
        Serial.printf("[Core 0] DHT11 temp=%.2f C humidity=%.2f %%\n", temperatureC, humidityPct);
      } else {
        Serial.println("[Core 0] DHT11 read failed; sending null temperature and humidity.");
      }

      const bool published = publishTelemetryToFirebase(frame);
      if (published) {
        Serial.println("[Core 0] Telemetry pushed to Firebase RTDB");
      }

      updateTelemetry(frame);
      lastTelemetryAttemptMs = nowMs;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(250);

  pinMode(amts::IN1, OUTPUT);
  pinMode(amts::IN2, OUTPUT);
  pinMode(amts::IN3, OUTPUT);
  pinMode(amts::IN4, OUTPUT);
  pinMode(amts::TRIG, OUTPUT);
  pinMode(amts::ECHO, INPUT);
  pinMode(amts::LEFT_IR, INPUT);
  pinMode(amts::RIGHT_IR, INPUT);

  stopMotors();

  telemetryMutex = xSemaphoreCreateMutex();
  controlMutex = xSemaphoreCreateMutex();
  if (telemetryMutex == nullptr || controlMutex == nullptr) {
    Serial.println("[Setup] Failed to create telemetry or control mutex.");
    while (true) {
      delay(1000);
    }
  }

  xTaskCreatePinnedToCore(loopAutomation, "AutoTask", kTaskStackAutomation, nullptr, 2, &automationTaskHandle, 1);
  xTaskCreatePinnedToCore(loopNetworking, "NetTask", kTaskStackNetwork, nullptr, 1, &networkTaskHandle, 0);
}

void loop() {
  vTaskDelete(nullptr);
}