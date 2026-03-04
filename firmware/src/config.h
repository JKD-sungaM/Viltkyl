#pragma once

#include <Arduino.h>

namespace Config {

// Serial
constexpr unsigned long SERIAL_BAUD_RATE = 115200;

// Fan PWM configuration
constexpr uint8_t FAN_PWM_PIN = D6;
constexpr uint8_t FAN_PWM_CHANNEL = 0;
constexpr uint32_t FAN_PWM_FREQUENCY_HZ = 25000;
constexpr uint8_t FAN_PWM_RESOLUTION_BITS = 8;
constexpr uint8_t FAN_DEFAULT_PERCENT = 20;
constexpr uint8_t FAN_BOOST_PERCENT = 100;

// Buttons (active low with internal pull-up)
constexpr uint8_t BUTTON_START_PIN = D2;
constexpr uint8_t BUTTON_BOOST_PIN = D3;
constexpr uint8_t BUTTON_STOP_PIN = D4;
constexpr bool BUTTON_ACTIVE_LOW = true;

// Timing parameters
constexpr unsigned long BUTTON_DEBOUNCE_MS = 35;
constexpr unsigned long BOOST_DURATION_MS = 2UL * 60UL * 60UL * 1000UL;
constexpr unsigned long BOOST_CANCEL_HOLD_MS = 2000;
constexpr unsigned long SENSOR_READ_INTERVAL_MS = 2000;
constexpr unsigned long STATUS_LOG_INTERVAL_MS = 5000;

// Persistence
// Dygnsgradsdata ska checkpointas var 5:e minut för att överleva omstart/strömavbrott.
constexpr unsigned long PERSIST_INTERVAL_MS = 5UL * 60UL * 1000UL;
constexpr const char* PERSIST_NAMESPACE = "viltkyl";
constexpr uint16_t PERSIST_VERSION = 1;

}  // namespace Config
