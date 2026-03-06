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
// Baudrate for serial monitor/debug output.
constexpr unsigned long SERIAL_BAUD_RATE = 115200;

// WiFi configuration
// Om dessa lämnas tomma används endast sparade credentials från NVS.
#if HAS_LOCAL_SECRETS
// Primary WiFi SSID loaded from local secrets file.
constexpr const char* WIFI_SSID = ConfigSecrets::WIFI_SSID;
// Primary WiFi password loaded from local secrets file.
constexpr const char* WIFI_PASSWORD = ConfigSecrets::WIFI_PASSWORD;
#else
// Primary WiFi SSID when no local secrets file exists.
constexpr const char* WIFI_SSID = "";
// Primary WiFi password when no local secrets file exists.
constexpr const char* WIFI_PASSWORD = "";
#endif
// Hostname shown in router/client lists and used by the web UI.
constexpr const char* WIFI_HOSTNAME = "Viltkyl_Arduino_ESP32";
// Max time to wait for WiFi connect attempt before fallback logic continues.
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;
// HTTP port for web server endpoints (/, /health, /api/telemetry).
constexpr uint16_t WEB_SERVER_PORT = 80;

// Statisk IP-konfiguration
// Sätt USE_STATIC_IP till true för att alltid använda en fast adress i ditt lokala nät.
// Enables static network configuration instead of DHCP.
constexpr bool USE_STATIC_IP = true;
// Static local IP address used when USE_STATIC_IP is true.
constexpr const char* STATIC_IP = "192.168.1.200";
// Default gateway used by the static IP config.
constexpr const char* STATIC_GATEWAY = "192.168.1.1";
// Subnet mask used by the static IP config.
constexpr const char* STATIC_SUBNET = "255.255.255.0";
// Primary DNS server used by the static IP config.
constexpr const char* STATIC_DNS1 = "8.8.8.8";
// Secondary DNS server used by the static IP config.
constexpr const char* STATIC_DNS2 = "1.1.1.1";

// Nätverkslagring i NVS
// NVS namespace where WiFi credentials are stored.
constexpr const char* WIFI_NVS_NAMESPACE = "wifi";
// NVS key for stored WiFi SSID.
constexpr const char* WIFI_NVS_KEY_SSID = "ssid";
// NVS key for stored WiFi password.
constexpr const char* WIFI_NVS_KEY_PASSWORD = "password";

// Fan PWM configuration
// Output pin connected to fan PWM control.
constexpr uint8_t FAN_PWM_PIN = D6;
// LEDC/PWM channel index used for fan output.
constexpr uint8_t FAN_PWM_CHANNEL = 0;
// PWM frequency for fan control.
constexpr uint32_t FAN_PWM_FREQUENCY_HZ = 25000;
// PWM resolution in bits.
constexpr uint8_t FAN_PWM_RESOLUTION_BITS = 8;
// Fan speed percentage in normal running mode.
constexpr uint8_t FAN_DEFAULT_PERCENT = 20;
// Fan speed percentage in boost mode.
constexpr uint8_t FAN_BOOST_PERCENT = 100;

// Timing parameters
// Duration of boost mode before auto-return to normal mode.
constexpr unsigned long BOOST_DURATION_MS = 2UL * 60UL * 60UL * 1000UL;
// Interval between sensor reads.
constexpr unsigned long SENSOR_READ_INTERVAL_MS = 2500;
// Interval for degree-day accumulation samples.
constexpr unsigned long DEGREE_DAY_SAMPLE_INTERVAL_MS = 1UL * 60UL * 1000UL;
// Interval for periodic status logging to serial.
constexpr unsigned long STATUS_LOG_INTERVAL_MS = 5000;

// Sensor configuration
// GPIO pin used by DHT22 data line.
constexpr uint8_t DHT22_DATA_PIN = D2;
// Warmup time after DHT init/reinit before first valid read is expected.
constexpr unsigned long DHT22_WARMUP_MS = 2500;

// Persistence
// Dygnsgradsdata ska checkpointas var 5:e minut för att överleva omstart/strömavbrott.
// Minimum interval between persistent writes to NVS.
constexpr unsigned long PERSIST_INTERVAL_MS = 5UL * 60UL * 1000UL;
// NVS namespace for application persistence data.
constexpr const char* PERSIST_NAMESPACE = "viltkyl";
// NVS key for persisted degree-day total.
constexpr const char* PERSIST_KEY_DEGREE_DAYS = "deg_days";
// NVS key for persisted active tracking time (seconds).
constexpr const char* PERSIST_KEY_DEGREE_TRACKING_SECONDS = "deg_time";
// Save interval for active tracking time while running.
constexpr unsigned long TRACKING_PERSIST_INTERVAL_MS = 10000;
// Persistence schema version for future migrations.
constexpr uint16_t PERSIST_VERSION = 1;

// GUI
// Antal decimaler för visning av dygnsgrader i webbgranssnittet.
// Number of decimals used when degree-days value is shown in UI panels.
constexpr uint8_t GUI_DEGREE_DAYS_DECIMALS = 3;

}  // namespace Config
