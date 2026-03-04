# Firmware (Arduino Nano ESP32)

Den här mappen innehåller firmware för styrningen av Viltkyl.

## Innehåll

- `platformio.ini` – bygg/upload-konfiguration för Nano ESP32
- `src/main.cpp` – första USB-testprogram (Serial + blink)

## Snabbkörning (PlatformIO)

1. Installera extension **PlatformIO IDE** i VS Code.
2. Anslut Nano ESP32 med USB-C datakabel.
3. Öppna mappen `firmware` i PlatformIO-projektet.
4. Kör:
   - **Build**
   - **Upload**
   - **Monitor** (115200 baud)

När det fungerar ska du se återkommande `LED: ON/OFF` i serial monitor.
