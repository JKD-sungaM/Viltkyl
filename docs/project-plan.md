# Viltkyl – Projektplan (MVP)

## 1. Målbild
Mikrokontrollern ska:
- ansluta till lokalt Wi‑Fi,
- läsa temperatur + fukt från DHT22,
- styra fläkt med PWM via MOSFET,
- hantera tre fysiska knappar (start/boost/stopp),
- beräkna och visa dygnsgrader i en lokal webbvy på enhetens IP,
- spara data för dygnsgrader var 5:e minut så att värden kan återställas efter strömavbrott/omstart.

## 2. Föreslagen pin-mappning
> Rekommendation: använd Arduino pin-namn i koden (D2, D3, osv.) för att undvika förväxling mellan Arduino-pin och ESP32 intern GPIO-mappning.

- Fläkt PWM (MOSFET gate): `D6`
- DHT22 data: `D7`
- Grön knapp (Start): `D2`
- Blå knapp (Boost): `D3`
- Röd knapp (Stop): `D4`

Elektriskt:
- Knappar: `INPUT_PULLUP` och aktiv låg (tryckt = `LOW`).
- DHT22: 3.3V, GND, DATA till `D7`, med 10k pull-up på DATA till 3.3V.
- Fläkt: IRLZ44N lågsidekoppling, gemensam GND mellan 12V och ESP32, frihjulsdiod över fläktlast.

## 3. Tillståndsmodell (state machine)
1. `IDLE_WAIT_START`
   - Startläge efter boot.
   - Väntar på grön knapp.
2. `RUNNING_NORMAL`
   - DHT22 mäts periodiskt.
   - Dygnsgrader ackumuleras.
   - Fläkt körs på standardhastighet (t.ex. 20%).
3. `RUNNING_BOOST`
   - Fläkt 100% i max 2 timmar.
   - Om blå knapp hålls in i 2 sekunder: avbryt boost och återgå till normal.
4. `STOPPED`
   - Röd knapp stoppar loggning + fläkt av.
   - Väntar på ny grön start.

## 4. Konfigurationsfil (förslag)
Fil: `firmware/src/config.h`

Parametrar att lägga i konfig:
- Wi‑Fi: `WIFI_SSID`, `WIFI_PASSWORD`, `HOSTNAME`
- Nätverk: `USE_STATIC_IP`, `STATIC_IP`, `GATEWAY`, `SUBNET`, `DNS1`, `DNS2`
- Fläkt: `FAN_PWM_PIN`, `FAN_PWM_FREQ_HZ`, `FAN_PWM_RES_BITS`, `FAN_DEFAULT_PERCENT`, `FAN_BOOST_PERCENT`
- Knappar: `BTN_START_PIN`, `BTN_BOOST_PIN`, `BTN_STOP_PIN`, `BUTTON_ACTIVE_LOW`, `BOOST_CANCEL_HOLD_SECONDS`
- Sensor: `DHT_PIN`, `DHT_TYPE`, `SENSOR_READ_INTERVAL_MS`
- Drift/logik: `BOOST_DURATION_SECONDS`, `WEB_PORT`, `LOG_INTERVAL_MS`
- Dygnsgrader: `BASE_TEMP_C`, `ACCUMULATION_INTERVAL_SEC`, `PERSIST_INTERVAL_MS` (300000)
- Persistens: `PERSIST_NAMESPACE`, `PERSIST_VERSION`

## 5. Datamodell (MVP)
- Senaste mätning:
  - temperatur (°C)
  - fukt (%)
  - timestamp
- Driftstatus:
  - state (`IDLE`, `NORMAL`, `BOOST`, `STOPPED`)
  - fan_percent
  - boost_remaining_sec
- Dygnsgrader:
  - ackumulerat värde senaste 24h
  - ev. historik per timme (ringbuffer 24 poster)
  - senast sparad checkpoint (skrivs var 5:e minut till icke-flyktigt minne)

## 5.1 Persistenskrav (strömavbrott)
- Dygnsgradsdata och nödvändig metadata ska sparas till NVS var 5:e minut.
- Vid uppstart ska firmware försöka återläsa senaste sparade värden innan normal drift startar.
- Om återläsning misslyckas ska systemet starta säkert med tomma/initiala värden och logga fel på serial.

## 6. Webbgränssnitt (lokalt på enheten)
- Endpoint `GET /`:
  - enkel HTML-sida med status, temp, fukt, dygnsgrader, fläktläge.
- Endpoint `GET /api/status`:
  - JSON med aktuella värden.
- Endpoint `POST /api/start`, `POST /api/boost`, `POST /api/stop` (valfritt i MVP).

## 7. Implementationsordning
1. Konfig + pin-definitioner.
2. Knapplogik + state machine.
3. PWM-styrning av fläkt.
4. DHT22-inläsning.
5. Dygnsgradsberäkning.
6. Persistens till NVS var 5:e minut + återläsning vid boot.
7. Wi‑Fi och enkel webbserver.
8. Stabilitetstest (strömcykel, knappspam, nätverksbortfall).

## 8. Nästa kodsteg
- Skapa `config.h` + `types.h`.
- Bygga state machine i `main.cpp`.
- Lägg in serial debug-utskrifter per tillstånd och knapp-event.
- Implementera `saveState()`/`restoreState()` för dygnsgrader (NVS, 5 min intervall).
