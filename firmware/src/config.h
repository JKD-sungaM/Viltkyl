#pragma once

#include <Arduino.h>

#if __has_include("config_secrets.h")
#include "config_secrets.h"
#define HAS_LOCAL_SECRETS 1
#else
#define HAS_LOCAL_SECRETS 0
#endif

namespace Config {

// Serial
constexpr unsigned long SERIAL_BAUD_RATE = 115200;

// WiFi configuration
// Om dessa lämnas tomma används endast sparade credentials från NVS.
#if HAS_LOCAL_SECRETS
constexpr const char* WIFI_SSID = ConfigSecrets::WIFI_SSID;
constexpr const char* WIFI_PASSWORD = ConfigSecrets::WIFI_PASSWORD;
#else
constexpr const char* WIFI_SSID = "";
constexpr const char* WIFI_PASSWORD = "";
#endif
constexpr const char* WIFI_HOSTNAME = "Viltkyl_Arduino_ESP32";
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;
constexpr uint16_t WEB_SERVER_PORT = 80;

// Statisk IP-konfiguration
// Sätt USE_STATIC_IP till true för att alltid använda en fast adress i ditt lokala nät.
constexpr bool USE_STATIC_IP = true;
constexpr const char* STATIC_IP = "192.168.1.200";
constexpr const char* STATIC_GATEWAY = "192.168.1.1";
constexpr const char* STATIC_SUBNET = "255.255.255.0";
constexpr const char* STATIC_DNS1 = "8.8.8.8";
constexpr const char* STATIC_DNS2 = "1.1.1.1";

// Nätverkslagring i NVS
constexpr const char* WIFI_NVS_NAMESPACE = "wifi";
constexpr const char* WIFI_NVS_KEY_SSID = "ssid";
constexpr const char* WIFI_NVS_KEY_PASSWORD = "password";

// Fan PWM configuration
constexpr uint8_t FAN_PWM_PIN = D6;
constexpr uint8_t FAN_PWM_CHANNEL = 0;
constexpr uint32_t FAN_PWM_FREQUENCY_HZ = 25000;
constexpr uint8_t FAN_PWM_RESOLUTION_BITS = 8;
constexpr uint8_t FAN_DEFAULT_PERCENT = 20;
constexpr uint8_t FAN_BOOST_PERCENT = 100;

// Timing parameters
constexpr unsigned long BOOST_DURATION_MS = 2UL * 60UL * 60UL * 1000UL;
constexpr unsigned long SENSOR_READ_INTERVAL_MS = 2500;
constexpr unsigned long DEGREE_DAY_SAMPLE_INTERVAL_MS = 5UL * 60UL * 1000UL;
constexpr unsigned long STATUS_LOG_INTERVAL_MS = 5000;

// Sensor configuration
// GPIO4 pa Nano ESP32 (motsvarar D20 i anvandarens kopplingsplan)
constexpr uint8_t DHT22_DATA_PIN = D2;
constexpr unsigned long DHT22_WARMUP_MS = 2500;
constexpr bool DHT_DIAGNOSTIC_MODE = false;
constexpr unsigned long DHT_DIAGNOSTIC_INTERVAL_MS = 2000;
constexpr bool USE_FAKE_TEMPERATURE_DATA = false;
constexpr float FAKE_TEMPERATURE_C = 10.0f;
constexpr float FAKE_HUMIDITY_PERCENT = 55.0f;

// Persistence
// Dygnsgradsdata ska checkpointas var 5:e minut för att överleva omstart/strömavbrott.
constexpr unsigned long PERSIST_INTERVAL_MS = 5UL * 60UL * 1000UL;
constexpr const char* PERSIST_NAMESPACE = "viltkyl";
constexpr const char* PERSIST_KEY_DEGREE_DAYS = "deg_days";
constexpr uint16_t PERSIST_VERSION = 1;

}  // namespace Config
