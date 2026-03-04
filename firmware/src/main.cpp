#include <Arduino.h>
#include "config.h"
#include "network_manager.h"
#include "web_server.h"

// Applikationens huvudtillstånd.
// Dessa används för att styra fläkt och knappbeteenden på ett tydligt sätt.
enum class AppState {
  IDLE,
  RUNNING_NORMAL,
  RUNNING_BOOST
};

// Håller all runtime-status för en fysisk knapp.
// previousPressed och previousChangeMs används för debounce.
// pressedSinceMs används för "håll in knappen"-logik.
struct ButtonState {
  uint8_t pin;
  bool previousPressed;
  unsigned long previousChangeMs;
  unsigned long pressedSinceMs;
};

// Global applikationsstatus.
AppState currentState = AppState::IDLE;
ButtonState startButton{Config::BUTTON_START_PIN, false, 0, 0};
ButtonState boostButton{Config::BUTTON_BOOST_PIN, false, 0, 0};
ButtonState stopButton{Config::BUTTON_STOP_PIN, false, 0, 0};

// Tidsstämplar för eventstyrning (boosttid, loggintervall, sensortick).
unsigned long boostStartMs = 0;
unsigned long lastStatusLogMs = 0;
unsigned long lastSensorReadMs = 0;

// Läser rå knappstatus och normaliserar till "pressed=true/false"
// baserat på om knappen är active-low eller active-high.
bool isButtonPressedRaw(uint8_t pin) {
  bool levelHigh = digitalRead(pin) == HIGH;
  return Config::BUTTON_ACTIVE_LOW ? !levelHigh : levelHigh;
}

// Returnerar true endast vid en giltig tryckning (stigande kant till pressed)
// efter debounce-filtret.
bool hasButtonPressedEdge(ButtonState& button, unsigned long nowMs) {
  bool pressed = isButtonPressedRaw(button.pin);

  if (pressed != button.previousPressed) {
    if (nowMs - button.previousChangeMs < Config::BUTTON_DEBOUNCE_MS) {
      return false;
    }

    button.previousChangeMs = nowMs;
    button.previousPressed = pressed;

    if (pressed) {
      button.pressedSinceMs = nowMs;
      return true;
    }
  }

  return false;
}

// Initierar en knapp med intern pull-up och startvärden för debounce.
void initializeButton(ButtonState& button, unsigned long nowMs) {
  pinMode(button.pin, INPUT_PULLUP);
  button.previousPressed = isButtonPressedRaw(button.pin);
  button.previousChangeMs = nowMs;
  button.pressedSinceMs = button.previousPressed ? nowMs : 0;
}

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

// Central tillståndsövergång.
// Hanterar sidoeffekter (t.ex. fläkthastighet) och loggar alltid anledning.
void transitionTo(AppState newState, const char* reason) {
  if (currentState == newState) {
    return;
  }

  currentState = newState;

  Serial.print("State => ");
  Serial.print(stateToText(currentState));
  Serial.print(" | reason: ");
  Serial.println(reason);

  switch (currentState) {
    case AppState::IDLE:
      setFanPercent(0);
      break;
    case AppState::RUNNING_NORMAL:
      setFanPercent(Config::FAN_DEFAULT_PERCENT);
      break;
    case AppState::RUNNING_BOOST:
      boostStartMs = millis();
      setFanPercent(Config::FAN_BOOST_PERCENT);
      break;
  }
}

// Hanterar knapphändelser med prioritet:
// 1) Stop (alltid högst prioritet)
// 2) Start (från IDLE)
// 3) Boost (från RUNNING_NORMAL)
// 4) Boost-cancel via långtryck.
void handleButtons(unsigned long nowMs) {
  bool startPressedEdge = hasButtonPressedEdge(startButton, nowMs);
  bool boostPressedEdge = hasButtonPressedEdge(boostButton, nowMs);
  bool stopPressedEdge = hasButtonPressedEdge(stopButton, nowMs);

  if (stopPressedEdge) {
    transitionTo(AppState::IDLE, "Stop button pressed");
    return;
  }

  if (startPressedEdge && currentState == AppState::IDLE) {
    transitionTo(AppState::RUNNING_NORMAL, "Start button pressed");
    return;
  }

  if (boostPressedEdge && currentState == AppState::RUNNING_NORMAL) {
    transitionTo(AppState::RUNNING_BOOST, "Boost button pressed");
    return;
  }

  if (currentState == AppState::RUNNING_BOOST && boostButton.previousPressed &&
      (nowMs - boostButton.pressedSinceMs >= Config::BOOST_CANCEL_HOLD_MS)) {
    transitionTo(AppState::RUNNING_NORMAL, "Boost cancel hold detected");
  }
}

// Hanterar tidsstyrda events:
// - boost timeout
// - periodisk sensor-tick (placeholder)
// - periodisk statuslogg.
void handleTimedEvents(unsigned long nowMs) {
  if (currentState == AppState::RUNNING_BOOST &&
      (nowMs - boostStartMs >= Config::BOOST_DURATION_MS)) {
    transitionTo(AppState::RUNNING_NORMAL, "Boost timer elapsed");
  }

  if (nowMs - lastSensorReadMs >= Config::SENSOR_READ_INTERVAL_MS) {
    lastSensorReadMs = nowMs;

    if (currentState == AppState::RUNNING_NORMAL || currentState == AppState::RUNNING_BOOST) {
      Serial.println("Sensor tick: DHT22 read placeholder");
    }
  }

  if (nowMs - lastStatusLogMs >= Config::STATUS_LOG_INTERVAL_MS) {
    lastStatusLogMs = nowMs;

    Serial.print("Status | state=");
    Serial.print(stateToText(currentState));
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
// - init knappar
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

  unsigned long nowMs = millis();
  initializeButton(startButton, nowMs);
  initializeButton(boostButton, nowMs);
  initializeButton(stopButton, nowMs);

  Serial.println("Viltkyl: system init klar");
  Serial.println("State machine aktiv. Vantar pa START-knapp.");
  Serial.println("Knappfunktioner: Start=gron, Boost=bla, Stop=rod");

  if (Network::isConnected()) {
    Serial.print("WiFi status: ansluten, IP=");
    Serial.println(Network::getLocalIp());
  }

  WebUi::initialize();
}

// Huvudloop:
// - läs och hantera knappar
// - kör tidsstyrda event.
void loop() {
  unsigned long nowMs = millis();
  WebUi::handleClient();
  handleButtons(nowMs);
  handleTimedEvents(nowMs);
}
