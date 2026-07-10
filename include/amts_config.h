#pragma once

#include <Arduino.h>

namespace amts {

constexpr uint8_t IN1 = 16;
constexpr uint8_t IN2 = 17;
constexpr uint8_t IN3 = 18;
constexpr uint8_t IN4 = 19;
constexpr uint8_t TRIG = 21;
constexpr uint8_t ECHO = 22;
constexpr uint8_t LEFT_IR = 34;
constexpr uint8_t RIGHT_IR = 35;
constexpr uint8_t DHT_PIN = 23;

constexpr uint32_t AUTOMATION_PERIOD_MS = 15;
constexpr uint32_t TELEMETRY_PERIOD_MS = 5000;
constexpr uint32_t WIFI_RETRY_PERIOD_MS = 10000;
constexpr float OBSTACLE_STOP_CM = 15.0f;
constexpr float FRONT_OBSTACLE_STOP_CM = 20.0f;

constexpr char DEVICE_ID[] = "amts-esp32u";
constexpr char WIFI_SSID[] = "YOUR_SSID";
constexpr char WIFI_PASSWORD[] = "YOUR_PASSWORD";
constexpr char FIREBASE_DATABASE_URL[] = "YOUR_FIREBASE_DATABASE_URL";
constexpr char FIREBASE_AUTH_TOKEN[] = "YOUR_FIREBASE_AUTH_TOKEN";

constexpr bool INVERT_LEFT_MOTOR = true;
constexpr bool INVERT_RIGHT_MOTOR = true;

}  // namespace amts