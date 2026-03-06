#include <Arduino.h>
#include <DHT.h>
#include <Preferences.h>
#include "config.h"
#include "network_manager.h"
#include "web_server.h"

namespace {

struct SensorReading {
  float temperatureC;
  float humidityPercent;
  bool valid;
};

constexpr uint8_t DHT_READ_RETRIES = 3;
constexpr uint8_t DHT_REINIT_AFTER_FAILURES = 5;

DHT g_dht(Config::DHT22_DATA_PIN, DHT22);
uint8_t g_consecutiveDhtFailures = 0;
Preferences persistPreferences;
bool persistReady = false;

SensorReading tryReadFromDht() {
  // Las en sample och anvand intern cache for andra vardet i samma cykel.
  float humidityPercent = g_dht.readHumidity();
  float temperatureC = g_dht.readTemperature();
  bool valid = !isnan(temperatureC) && !isnan(humidityPercent);
  return SensorReading{temperatureC, humidityPercent, valid};
}

SensorReading readSensorData() {
  for (uint8_t retry = 0; retry < DHT_READ_RETRIES; ++retry) {
    SensorReading reading = tryReadFromDht();
    if (reading.valid) {
      g_consecutiveDhtFailures = 0;
      return reading;
    }
    delay(120);
  }

  g_consecutiveDhtFailures++;
  if (g_consecutiveDhtFailures >= DHT_REINIT_AFTER_FAILURES) {
    Serial.println("DHT22 reinit after repeated NaN");
    g_dht.begin();
    delay(Config::DHT22_WARMUP_MS);
    g_consecutiveDhtFailures = 0;
  }

  return SensorReading{0.0f, 0.0f, false};
}

bool openPersistence() {
  if (persistReady) {
    return true;
  }

  persistReady = persistPreferences.begin(Config::PERSIST_NAMESPACE, false);
  return persistReady;
}

}  // namespace

// Applikationens huvudtillstånd.
// Dessa används för att styra fläkt och knappbeteenden på ett tydligt sätt.
enum class AppState {
  IDLE,
  RUNNING_NORMAL,
  RUNNING_BOOST
};

// Global applikationsstatus.
AppState currentState = AppState::IDLE;
SensorReading latestSensorReading{0.0f, 0.0f, false};
float degreeDays = 0.0f;
uint32_t degreeTrackingSeconds = 0;
unsigned long trackingAccumulatorMs = 0;
unsigned long lastTrackingUpdateMs = 0;
unsigned long lastTrackingPersistMs = 0;

// Tidsstämplar för eventstyrning (boosttid, loggintervall, sensortick).
unsigned long boostStartMs = 0;
unsigned long lastStatusLogMs = 0;
unsigned long lastSensorReadMs = 0;
unsigned long lastDegreeDaySampleMs = 0;
unsigned long lastPersistMs = 0;

// Konverterar procent (0..100) till PWM-duty enligt vald upplösning.
uint8_t percentToDutyCycle(uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }

  uint16_t maxDuty = (1U << Config::FAN_PWM_RESOLUTION_BITS) - 1U;
  return static_cast<uint8_t>((static_cast<uint32_t>(percent) * maxDuty) / 100U);
}

// Sätter fläkthastighet i procent via PWM-kanalen.
void setFanPercent(uint8_t percent) {
  uint8_t duty = percentToDutyCycle(percent);
  ledcWrite(Config::FAN_PWM_CHANNEL, duty);
}

// Hjälpfunktion för läsbar serial-logg av tillstånd.
const char* stateToText(AppState state) {
  switch (state) {
    case AppState::IDLE:
      return "IDLE";
    case AppState::RUNNING_NORMAL:
      return "RUNNING_NORMAL";
    case AppState::RUNNING_BOOST:
      return "RUNNING_BOOST";
    default:
      return "UNKNOWN";
  }
}

void publishTelemetry() {
  WebUi::TelemetrySnapshot snapshot{
      stateToText(currentState),
      latestSensorReading.temperatureC,
      latestSensorReading.humidityPercent,
      degreeDays,
      degreeTrackingSeconds,
      latestSensorReading.valid};
  WebUi::updateTelemetry(snapshot);
}

bool isRunningState(AppState state) {
  return state == AppState::RUNNING_NORMAL || state == AppState::RUNNING_BOOST;
}

void persistTrackingIfNeeded(unsigned long nowMs, bool force) {
  if (!force && (nowMs - lastTrackingPersistMs < Config::TRACKING_PERSIST_INTERVAL_MS)) {
    return;
  }

  if (!openPersistence()) {
    Serial.println("Persist: kunde inte oppna NVS");
    return;
  }

  persistPreferences.putUInt(Config::PERSIST_KEY_DEGREE_TRACKING_SECONDS, degreeTrackingSeconds);
  lastTrackingPersistMs = nowMs;
}

void updateDegreeTracking(unsigned long nowMs) {
  if (lastTrackingUpdateMs == 0) {
    lastTrackingUpdateMs = nowMs;
    return;
  }

  const unsigned long deltaMs = nowMs - lastTrackingUpdateMs;
  lastTrackingUpdateMs = nowMs;

  if (!isRunningState(currentState)) {
    return;
  }

  trackingAccumulatorMs += deltaMs;
  if (trackingAccumulatorMs >= 1000UL) {
    degreeTrackingSeconds += static_cast<uint32_t>(trackingAccumulatorMs / 1000UL);
    trackingAccumulatorMs %= 1000UL;
  }
}

void persistDegreeDaysIfNeeded(unsigned long nowMs, bool force) {
  if (!force && (nowMs - lastPersistMs < Config::PERSIST_INTERVAL_MS)) {
    return;
  }

  if (!openPersistence()) {
    Serial.println("Persist: kunde inte oppna NVS");
    return;
  }

  persistPreferences.putFloat(Config::PERSIST_KEY_DEGREE_DAYS, degreeDays);
  lastPersistMs = nowMs;
}

void clearPersistedDegreeDays() {
  if (!openPersistence()) {
    Serial.println("Persist: kunde inte oppna NVS");
    return;
  }

  persistPreferences.remove(Config::PERSIST_KEY_DEGREE_DAYS);
  persistPreferences.remove(Config::PERSIST_KEY_DEGREE_TRACKING_SECONDS);
}

void resetDegreeDays(const char* reason) {
  const unsigned long nowMs = millis();
  updateDegreeTracking(nowMs);
  degreeDays = 0.0f;
  degreeTrackingSeconds = 0;
  trackingAccumulatorMs = 0;
  clearPersistedDegreeDays();
  lastTrackingPersistMs = nowMs;
  Serial.print("Dygnsgrader reset: ");
  Serial.println(reason);
}

void loadPersistedDegreeDays() {
  if (!openPersistence()) {
    Serial.println("Persist: kunde inte oppna NVS");
    degreeDays = 0.0f;
    return;
  }

  degreeDays = persistPreferences.getFloat(Config::PERSIST_KEY_DEGREE_DAYS, 0.0f);
  degreeTrackingSeconds = persistPreferences.getUInt(Config::PERSIST_KEY_DEGREE_TRACKING_SECONDS, 0);
  trackingAccumulatorMs = 0;

  if (degreeDays > 0.0f) {
    Serial.print("Persist: laddade dygnsgrader=");
    Serial.println(degreeDays, 2);
  }

  if (degreeTrackingSeconds > 0) {
    Serial.print("Persist: laddad dygnsgrader-tid(s)=");
    Serial.println(degreeTrackingSeconds);
  }
}

float calculateDegreeDaysIncrement(float temperatureC) {
  // Dygnsgrader = temperatur * andel av dygn.
  const float dayFraction =
      static_cast<float>(Config::DEGREE_DAY_SAMPLE_INTERVAL_MS) / (24.0f * 60.0f * 60.0f * 1000.0f);
  return temperatureC * dayFraction;
}

// Central tillståndsövergång.
// Hanterar sidoeffekter (t.ex. fläkthastighet) och loggar alltid anledning.
void transitionTo(AppState newState, const char* reason) {
  if (currentState == newState) {
    return;
  }

  const unsigned long nowMs = millis();
  updateDegreeTracking(nowMs);

  currentState = newState;

  Serial.print("State => ");
  Serial.print(stateToText(currentState));
  Serial.print(" | reason: ");
  Serial.println(reason);

  switch (currentState) {
    case AppState::IDLE:
      setFanPercent(0);
      persistTrackingIfNeeded(nowMs, true);
      break;
    case AppState::RUNNING_NORMAL:
      setFanPercent(Config::FAN_DEFAULT_PERCENT);
      break;
    case AppState::RUNNING_BOOST:
      boostStartMs = millis();
      setFanPercent(Config::FAN_BOOST_PERCENT);
      break;
  }

  publishTelemetry();
}

// Temporara serial-kommandon for state-styrning innan fysiska knappar finns.
// 1=IDLE, 2=RUNNING_NORMAL, 3=STOP (kort), 4=STOP long press (rensa dygnsgrader).
void handleSerialCommands() {
  while (Serial.available() > 0) {
    char cmd = static_cast<char>(Serial.read());

    if (cmd == '\r' || cmd == '\n' || cmd == ' ') {
      continue;
    }

    switch (cmd) {
      case '1':
        transitionTo(AppState::IDLE, "Serial command 1");
        break;
      case '2':
        transitionTo(AppState::RUNNING_NORMAL, "Serial command 2");
        break;
      case '3':
        transitionTo(AppState::IDLE, "Serial command 3 (STOP)");
        break;
      case '4':
        resetDegreeDays("Serial command 4 (STOP long press clear)");
        publishTelemetry();
        break;
      default:
        Serial.print("Unknown serial command: ");
        Serial.println(cmd);
        break;
    }
  }
}

// Hanterar tidsstyrda events:
// - boost timeout
// - periodisk sensorläsning (DHT22, Celsius)
// - dygnsgrader var 5:e minut
// - periodisk statuslogg.
void handleTimedEvents(unsigned long nowMs) {
  updateDegreeTracking(nowMs);
  persistTrackingIfNeeded(nowMs, false);

  if (currentState == AppState::RUNNING_BOOST &&
      (nowMs - boostStartMs >= Config::BOOST_DURATION_MS)) {
    transitionTo(AppState::RUNNING_NORMAL, "Boost timer elapsed");
  }

  if (nowMs - lastSensorReadMs >= Config::SENSOR_READ_INTERVAL_MS) {
    lastSensorReadMs = nowMs;

    latestSensorReading = readSensorData();

    if (!latestSensorReading.valid) {
      Serial.println("Sensor read failed: DHT22 returned NaN");
    } else {
      Serial.print("Temp=");
      Serial.print(latestSensorReading.temperatureC, 1);
      Serial.print("C | Hum=");
      Serial.print(latestSensorReading.humidityPercent, 1);
      Serial.println("% | source=dht22");
    }

    publishTelemetry();
  }

  if (nowMs - lastDegreeDaySampleMs >= Config::DEGREE_DAY_SAMPLE_INTERVAL_MS) {
    lastDegreeDaySampleMs = nowMs;

    if (currentState == AppState::RUNNING_NORMAL || currentState == AppState::RUNNING_BOOST) {
      // Anvand senaste giltiga sample for att undvika onodigt tat dubbel-lasning av DHT22.
      SensorReading sample = latestSensorReading;
      if (!sample.valid) {
        sample = readSensorData();
      }

      if (sample.valid) {
        latestSensorReading = sample;
        degreeDays += calculateDegreeDaysIncrement(sample.temperatureC);
        persistDegreeDaysIfNeeded(nowMs, true);

        Serial.print("Dygnsgrader=");
        Serial.println(degreeDays, 2);
      } else {
        Serial.println("Dygnsgrader: hoppar over sample pga ogiltig DHT22-lasning");
      }

      publishTelemetry();
    }
  }

  if (nowMs - lastStatusLogMs >= Config::STATUS_LOG_INTERVAL_MS) {
    lastStatusLogMs = nowMs;

    Serial.print("Status | state=");
    Serial.print(stateToText(currentState));
    Serial.print(" | dygnsgrader=");
    Serial.print(degreeDays, 2);
    Serial.print(" | fan_default=");
    Serial.print(Config::FAN_DEFAULT_PERCENT);
    Serial.print("% | fan_boost=");
    Serial.print(Config::FAN_BOOST_PERCENT);
    Serial.print("% | wifi=");

    if (Network::isConnected()) {
      Serial.print("connected");
      Serial.print(" | ip=");
      Serial.println(Network::getLocalIp());
    } else {
      Serial.println("disconnected");
    }
  }
}

// Setup körs en gång vid uppstart:
// - init serial
// - init PWM för fläkt
// - skriv uppstartsinfo.
void setup() {
  Serial.begin(Config::SERIAL_BAUD_RATE);
  delay(1200);

  // Initierar nätverksmodulen och försöker ansluta till lokalt WiFi.
  // Prioritet:
  // 1) Credentials i config.h (om satta) som även sparas i NVS
  // 2) Tidigare sparade credentials i NVS
  Network::initialize();

  bool connected = false;

  if (strlen(Config::WIFI_SSID) > 0) {
    Serial.println("WiFi: forsoker ansluta med credentials fran config.h");
    connected = Network::connectWithCredentials(
        Config::WIFI_SSID,
        Config::WIFI_PASSWORD,
        true,
        Config::WIFI_CONNECT_TIMEOUT_MS);
  }

  if (!connected) {
    Serial.println("WiFi: forsoker ansluta med sparade credentials (NVS)");
    connected = Network::connectUsingStoredCredentials(Config::WIFI_CONNECT_TIMEOUT_MS);
  }

  if (!connected) {
    Serial.println("WiFi: ej ansluten. Fortsatter i offline-lage.");
  }

  ledcSetup(
      Config::FAN_PWM_CHANNEL,
      Config::FAN_PWM_FREQUENCY_HZ,
      Config::FAN_PWM_RESOLUTION_BITS);
  ledcAttachPin(Config::FAN_PWM_PIN, Config::FAN_PWM_CHANNEL);
  setFanPercent(0);

  pinMode(Config::DHT22_DATA_PIN, INPUT_PULLUP);
  g_dht.begin();
  Serial.println("Sensor mode: DHT22");
  Serial.print("DHT22 GPIO: ");
  Serial.println(Config::DHT22_DATA_PIN);
  Serial.print("DHT22 warmup ms=");
  Serial.println(Config::DHT22_WARMUP_MS);
  delay(Config::DHT22_WARMUP_MS);

  latestSensorReading = readSensorData();
  if (!latestSensorReading.valid) {
    Serial.println("Initial sensor read failed: DHT22 returned NaN (check wiring/pull-up/timing)");
  }

  loadPersistedDegreeDays();
  lastDegreeDaySampleMs = millis();
  lastPersistMs = millis();
  lastTrackingUpdateMs = millis();
  lastTrackingPersistMs = millis();

  Serial.println("Viltkyl: system init klar");
  Serial.println("State machine aktiv. Styrning via serial-kommandon.");
  Serial.println("Serial cmd: 1=IDLE, 2=RUNNING_NORMAL, 3=IDLE, 4=clear dygnsgrader");

  if (degreeDays > 0.0f) {
    Serial.println("Aterupptar korning med tidigare sparade dygnsgrader");
    transitionTo(AppState::RUNNING_NORMAL, "Resume from persisted degree days");
  }

  publishTelemetry();

  if (Network::isConnected()) {
    Serial.print("WiFi status: ansluten, IP=");
    Serial.println(Network::getLocalIp());
  }

  WebUi::initialize();
}

// Huvudloop: kör serialstyrning och tidsstyrda event.
void loop() {
  unsigned long nowMs = millis();
  WebUi::handleClient();
  handleSerialCommands();
  handleTimedEvents(nowMs);
}
