#include "network_manager.h"

#include <Preferences.h>
#include <WiFi.h>

#include "config.h"

namespace {

Preferences wifiPreferences;
bool nvsReady = false;

bool openPreferences() {
  if (nvsReady) {
    return true;
  }

  nvsReady = wifiPreferences.begin(Config::WIFI_NVS_NAMESPACE, false);
  return nvsReady;
}

bool waitForConnection(unsigned long timeoutMs) {
  unsigned long startedAt = millis();

  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startedAt >= timeoutMs) {
      wl_status_t status = WiFi.status();
      Serial.print("WiFi: timeout vid anslutning. status=");
      Serial.print(static_cast<int>(status));
      Serial.print(" (");
      switch (status) {
        case WL_NO_SSID_AVAIL:
          Serial.print("WL_NO_SSID_AVAIL");
          break;
        case WL_CONNECT_FAILED:
          Serial.print("WL_CONNECT_FAILED");
          break;
        case WL_CONNECTION_LOST:
          Serial.print("WL_CONNECTION_LOST");
          break;
        case WL_DISCONNECTED:
          Serial.print("WL_DISCONNECTED");
          break;
        case WL_IDLE_STATUS:
          Serial.print("WL_IDLE_STATUS");
          break;
        default:
          Serial.print("UNKNOWN");
          break;
      }
      Serial.println(")");
      return false;
    }

    delay(200);
  }

  return true;
}

void logSsidPresence(const String& targetSsid) {
  int networksFound = WiFi.scanNetworks(false, true);

  if (networksFound <= 0) {
    Serial.println("WiFi: scan hittade inga natverk");
    return;
  }

  bool ssidFound = false;
  for (int index = 0; index < networksFound; ++index) {
    String foundSsid = WiFi.SSID(index);
    if (foundSsid == targetSsid) {
      ssidFound = true;
      Serial.print("WiFi: SSID hittad | RSSI=");
      Serial.print(WiFi.RSSI(index));
      Serial.print(" dBm | kanal=");
      Serial.println(WiFi.channel(index));
      break;
    }
  }

  if (!ssidFound) {
    Serial.print("WiFi: SSID '");
    Serial.print(targetSsid);
    Serial.println("' hittades inte i scan (kontrollera 2.4 GHz)");
  }
}

bool parseIpAddress(const char* text, IPAddress& outAddress) {
  if (text == nullptr || strlen(text) == 0) {
    return false;
  }

  return outAddress.fromString(text);
}

bool applyStaticIpIfEnabled() {
  if (!Config::USE_STATIC_IP) {
    return true;
  }

  IPAddress localIp;
  IPAddress gateway;
  IPAddress subnet;
  IPAddress dns1;
  IPAddress dns2;

  bool parsed =
      parseIpAddress(Config::STATIC_IP, localIp) &&
      parseIpAddress(Config::STATIC_GATEWAY, gateway) &&
      parseIpAddress(Config::STATIC_SUBNET, subnet) &&
      parseIpAddress(Config::STATIC_DNS1, dns1) &&
      parseIpAddress(Config::STATIC_DNS2, dns2);

  if (!parsed) {
    Serial.println("WiFi: ogiltig statisk IP-konfiguration i config.h");
    return false;
  }

  if (!WiFi.config(localIp, gateway, subnet, dns1, dns2)) {
    Serial.println("WiFi: kunde inte applicera statisk IP-konfiguration");
    return false;
  }

  Serial.print("WiFi: statisk IP aktiv ");
  Serial.println(localIp);
  return true;
}

}  // namespace

namespace Network {

void initialize() {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);

  if (strlen(Config::WIFI_HOSTNAME) > 0) {
    WiFi.setHostname(Config::WIFI_HOSTNAME);
  }

  if (!openPreferences()) {
    Serial.println("WiFi: kunde inte initiera NVS namespace");
  }
}

bool saveCredentials(const String& ssid, const String& password) {
  if (!openPreferences()) {
    return false;
  }

  if (ssid.isEmpty() || password.isEmpty()) {
    return false;
  }

  bool ssidSaved = wifiPreferences.putString(Config::WIFI_NVS_KEY_SSID, ssid) > 0;
  bool passwordSaved = wifiPreferences.putString(Config::WIFI_NVS_KEY_PASSWORD, password) > 0;
  return ssidSaved && passwordSaved;
}

bool loadCredentials(String& ssidOut, String& passwordOut) {
  if (!openPreferences()) {
    return false;
  }

  ssidOut = wifiPreferences.getString(Config::WIFI_NVS_KEY_SSID, "");
  passwordOut = wifiPreferences.getString(Config::WIFI_NVS_KEY_PASSWORD, "");

  return !ssidOut.isEmpty() && !passwordOut.isEmpty();
}

bool connectWithCredentials(
    const String& ssid,
    const String& password,
    bool persistCredentials,
    unsigned long timeoutMs) {
  if (ssid.isEmpty() || password.isEmpty()) {
    Serial.println("WiFi: SSID/losenord saknas");
    return false;
  }

  if (persistCredentials) {
    if (!saveCredentials(ssid, password)) {
      Serial.println("WiFi: kunde inte spara credentials till NVS");
    }
  }

  Serial.print("WiFi: ansluter till SSID '");
  Serial.print(ssid);
  Serial.println("'...");
  Serial.print("WiFi: losenordslangd=");
  Serial.println(password.length());

  WiFi.disconnect(true, true);
  delay(100);

  logSsidPresence(ssid);

  if (!applyStaticIpIfEnabled()) {
    return false;
  }

  WiFi.begin(ssid.c_str(), password.c_str());

  if (!waitForConnection(timeoutMs)) {
    Serial.println("WiFi: anslutning misslyckades (se status ovan)");
    return false;
  }

  Serial.print("WiFi: ansluten, IP=");
  Serial.println(WiFi.localIP());
  return true;
}

bool connectUsingStoredCredentials(unsigned long timeoutMs) {
  String storedSsid;
  String storedPassword;

  if (!loadCredentials(storedSsid, storedPassword)) {
    Serial.println("WiFi: inga sparade credentials hittades");
    return false;
  }

  return connectWithCredentials(storedSsid, storedPassword, false, timeoutMs);
}

bool isConnected() {
  return WiFi.status() == WL_CONNECTED;
}

String getLocalIp() {
  if (!isConnected()) {
    return "";
  }

  return WiFi.localIP().toString();
}

}  // namespace Network
