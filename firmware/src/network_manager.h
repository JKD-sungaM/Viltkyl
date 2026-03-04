#pragma once

#include <Arduino.h>

namespace Network {

// Initierar nätverksmodulen. Ska kallas en gång i setup().
void initialize();

// Sparar WiFi-credentials i NVS (överlever omstart/strömavbrott).
// Returnerar true om data sparades korrekt.
bool saveCredentials(const String& ssid, const String& password);

// Läser WiFi-credentials från NVS.
// Returnerar true om både SSID och lösenord hittades.
bool loadCredentials(String& ssidOut, String& passwordOut);

// Försöker ansluta med specificerade credentials.
// Om persistCredentials=true sparas uppgifterna i NVS före anslutning.
bool connectWithCredentials(
    const String& ssid,
    const String& password,
    bool persistCredentials,
    unsigned long timeoutMs);

// Försöker ansluta med credentials från NVS.
bool connectUsingStoredCredentials(unsigned long timeoutMs);

// Returnerar true om enheten är ansluten till WiFi.
bool isConnected();

// Returnerar IP-adress som text, eller tom sträng om ej ansluten.
String getLocalIp();

}  // namespace Network
