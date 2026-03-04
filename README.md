# Viltkyl

Styrning för viltkyl med Arduino Nano ESP32.

## Projektstruktur

- `firmware/` – firmware för Nano ESP32
- `docs/` – dokumentation och setup-guider
- `backend/` – framtida backend/API
- `frontend/` – framtida UI
- `scripts/` – hjälpskript

## Kom igång (USB-C kontakt)

1. Installera rekommenderade VS Code-extensions.
2. Anslut Arduino Nano ESP32 med USB-C datakabel.
3. Följ setupguiden i `docs/arduino-setup.md`.
4. Bygg/ladda upp firmware i `firmware/`.

## Första mål

Verifiera stabil kontakt med kortet via USB genom att:

- kunna bygga och ladda upp firmware
- se serial-output på 115200 baud
- bekräfta blinktest med inbyggd LED
