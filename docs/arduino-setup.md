# Setup: Arduino Nano ESP32 (USB-C)

Den här guiden fokuserar på första målet: få stabil kontakt mellan datorn och Nano ESP32.

## 1) Installera verktyg i VS Code

- Installera extension: **PlatformIO IDE**
- Installera extension: **C/C++ (ms-vscode.cpptools)**

## 2) Anslut kortet korrekt

- Använd en **USB-C datakabel** (inte bara laddkabel)
- Koppla Nano ESP32 direkt till datorns USB-port

## 3) Kontrollera COM-port i Windows

1. Öppna Enhetshanteraren
2. Titta under **Portar (COM & LPT)**
3. Notera vilken COM-port Nano får (t.ex. `COM5`)

## 4) Bygg och ladda upp testprogram

I VS Code/PlatformIO:

1. Build projektet i `firmware`
2. Upload till kortet
3. Starta Serial Monitor (115200)

Förväntat resultat:

- Inledande text: `Viltkyl: Nano ESP32 ansluten via USB-C`
- Löpande text: `LED: ON` / `LED: OFF`
- Inbyggd LED blinkar

## 5) Felsökning om upload misslyckas

- Testa annan USB-C kabel
- Testa annan USB-port
- Tryck snabbt två gånger på reset-knappen för bootloader-läge och försök upload igen
- Kontrollera att rätt miljö används: `arduino_nano_esp32`

## Nästa steg

När USB-kontakten är stabil kan vi lägga till:

- Temperaturgivare
- Relästyrning för kylaggregat
- Säkerhetslogik (min/max temperatur, fail-safe)
- Förberedelse för framtida 12V-drift
