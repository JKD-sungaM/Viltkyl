#include "web_server.h"

#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <math.h>

#include "config.h"
#include "network_manager.h"

namespace {

WebServer server(Config::WEB_SERVER_PORT);
bool serverStarted = false;
WebUi::TelemetrySnapshot latestTelemetry{"IDLE", 0.0f, 0.0f, 0.0f, false, false};
constexpr size_t DEGREE_HISTORY_CAPACITY = 240;
constexpr const char* HISTORY_KEY_COUNT = "hist_cnt";
constexpr const char* HISTORY_KEY_LAST = "hist_last";
constexpr const char* HISTORY_KEY_DATA = "hist_blob";
constexpr size_t CLIMATE_HISTORY_CAPACITY = 150;
constexpr uint32_t CLIMATE_HISTORY_MIN_INTERVAL_SECONDS = 60;
constexpr const char* CLIMATE_KEY_COUNT = "env_cnt";
constexpr const char* CLIMATE_KEY_DATA = "env_blob";
constexpr float DEGREE_RESET_EPSILON = 0.0005f;
constexpr float DEGREE_RESET_THRESHOLD = 0.05f;
Preferences webPreferences;
bool webPreferencesReady = false;

struct DegreePoint {
  uint32_t uptimeSeconds;
  float degreeDays;
};

struct ClimatePoint {
  uint32_t uptimeSeconds;
  float temperatureC;
  float humidityPercent;
};

DegreePoint degreeHistory[DEGREE_HISTORY_CAPACITY];
size_t degreeHistoryStart = 0;
size_t degreeHistoryCount = 0;
float lastRecordedDegreeDays = -1.0f;
ClimatePoint climateHistory[CLIMATE_HISTORY_CAPACITY];
size_t climateHistoryStart = 0;
size_t climateHistoryCount = 0;

bool openWebPreferences() {
  if (webPreferencesReady) {
    return true;
  }

  webPreferencesReady = webPreferences.begin(Config::PERSIST_NAMESPACE, false);
  return webPreferencesReady;
}

void persistDegreeHistory() {
  if (!openWebPreferences()) {
    return;
  }

  DegreePoint linearized[DEGREE_HISTORY_CAPACITY];
  for (size_t index = 0; index < degreeHistoryCount; ++index) {
    size_t ringIndex = (degreeHistoryStart + index) % DEGREE_HISTORY_CAPACITY;
    linearized[index] = degreeHistory[ringIndex];
  }

  webPreferences.putUInt(HISTORY_KEY_COUNT, static_cast<uint32_t>(degreeHistoryCount));
  webPreferences.putFloat(HISTORY_KEY_LAST, lastRecordedDegreeDays);
  const size_t expectedBytes = degreeHistoryCount * sizeof(DegreePoint);
  const size_t writtenBytes = webPreferences.putBytes(HISTORY_KEY_DATA, linearized, expectedBytes);
  if (writtenBytes != expectedBytes) {
    Serial.print("Web: degree-historik write mismatch, wrote=");
    Serial.print(static_cast<unsigned long>(writtenBytes));
    Serial.print(" expected=");
    Serial.println(static_cast<unsigned long>(expectedBytes));
  }
}

void persistClimateHistory() {
  if (!openWebPreferences()) {
    return;
  }

  ClimatePoint linearized[CLIMATE_HISTORY_CAPACITY];
  for (size_t index = 0; index < climateHistoryCount; ++index) {
    size_t ringIndex = (climateHistoryStart + index) % CLIMATE_HISTORY_CAPACITY;
    linearized[index] = climateHistory[ringIndex];
  }

  webPreferences.putUInt(CLIMATE_KEY_COUNT, static_cast<uint32_t>(climateHistoryCount));
  const size_t expectedBytes = climateHistoryCount * sizeof(ClimatePoint);
  const size_t writtenBytes = webPreferences.putBytes(CLIMATE_KEY_DATA, linearized, expectedBytes);
  if (writtenBytes != expectedBytes) {
    Serial.print("Web: climate-historik write mismatch, wrote=");
    Serial.print(static_cast<unsigned long>(writtenBytes));
    Serial.print(" expected=");
    Serial.println(static_cast<unsigned long>(expectedBytes));
  }
}

void loadPersistedDegreeHistory() {
  if (!openWebPreferences()) {
    return;
  }

  uint32_t persistedCount = webPreferences.getUInt(HISTORY_KEY_COUNT, 0);
  if (persistedCount == 0) {
    return;
  }

  if (persistedCount > DEGREE_HISTORY_CAPACITY) {
    persistedCount = DEGREE_HISTORY_CAPACITY;
  }

  DegreePoint linearized[DEGREE_HISTORY_CAPACITY];
  size_t expectedSize = static_cast<size_t>(persistedCount) * sizeof(DegreePoint);
  size_t loadedSize = webPreferences.getBytes(HISTORY_KEY_DATA, linearized, expectedSize);
  size_t loadedCount = loadedSize / sizeof(DegreePoint);

  if (loadedCount == 0) {
    return;
  }

  degreeHistoryStart = 0;
  degreeHistoryCount = loadedCount;
  for (size_t index = 0; index < loadedCount; ++index) {
    degreeHistory[index] = linearized[index];
  }

  lastRecordedDegreeDays = webPreferences.getFloat(HISTORY_KEY_LAST, degreeHistory[loadedCount - 1].degreeDays);
}

void loadPersistedClimateHistory() {
  if (!openWebPreferences()) {
    return;
  }

  uint32_t persistedCount = webPreferences.getUInt(CLIMATE_KEY_COUNT, 0);
  if (persistedCount == 0) {
    return;
  }

  if (persistedCount > CLIMATE_HISTORY_CAPACITY) {
    persistedCount = CLIMATE_HISTORY_CAPACITY;
  }

  ClimatePoint linearized[CLIMATE_HISTORY_CAPACITY];
  size_t expectedSize = static_cast<size_t>(persistedCount) * sizeof(ClimatePoint);
  size_t loadedSize = webPreferences.getBytes(CLIMATE_KEY_DATA, linearized, expectedSize);
  size_t loadedCount = loadedSize / sizeof(ClimatePoint);
  if (loadedCount == 0) {
    return;
  }

  climateHistoryStart = 0;
  climateHistoryCount = loadedCount;
  for (size_t index = 0; index < loadedCount; ++index) {
    climateHistory[index] = linearized[index];
  }
}

void clearPersistedDegreeHistory() {
  degreeHistoryStart = 0;
  degreeHistoryCount = 0;
  lastRecordedDegreeDays = -1.0f;

  if (!openWebPreferences()) {
    return;
  }

  webPreferences.remove(HISTORY_KEY_COUNT);
  webPreferences.remove(HISTORY_KEY_LAST);
  webPreferences.remove(HISTORY_KEY_DATA);
}

void clearPersistedClimateHistory() {
  climateHistoryStart = 0;
  climateHistoryCount = 0;

  if (!openWebPreferences()) {
    return;
  }

  webPreferences.remove(CLIMATE_KEY_COUNT);
  webPreferences.remove(CLIMATE_KEY_DATA);
}

void appendDegreeDaysHistory(float degreeDays) {
  if (lastRecordedDegreeDays >= 0.0f && fabsf(degreeDays - lastRecordedDegreeDays) < DEGREE_RESET_EPSILON) {
    return;
  }

  uint32_t timestamp = millis() / 1000UL;
  if (degreeHistoryCount > 0) {
    size_t lastIndex = (degreeHistoryStart + degreeHistoryCount - 1) % DEGREE_HISTORY_CAPACITY;
    uint32_t lastTimestamp = degreeHistory[lastIndex].uptimeSeconds;
    uint32_t sampleSeconds = static_cast<uint32_t>(Config::DEGREE_DAY_SAMPLE_INTERVAL_MS / 1000UL);
    uint32_t expectedNext = lastTimestamp + sampleSeconds;
    if (timestamp < expectedNext) {
      timestamp = expectedNext;
    }
  }

  DegreePoint point{timestamp, degreeDays};
  size_t writeIndex = 0;

  if (degreeHistoryCount < DEGREE_HISTORY_CAPACITY) {
    writeIndex = (degreeHistoryStart + degreeHistoryCount) % DEGREE_HISTORY_CAPACITY;
    degreeHistoryCount++;
  } else {
    degreeHistoryStart = (degreeHistoryStart + 1) % DEGREE_HISTORY_CAPACITY;
    writeIndex = (degreeHistoryStart + degreeHistoryCount - 1) % DEGREE_HISTORY_CAPACITY;
  }

  degreeHistory[writeIndex] = point;
  lastRecordedDegreeDays = degreeDays;
  persistDegreeHistory();
}

void appendClimateHistory(float temperatureC, float humidityPercent) {
  const uint32_t timestamp = millis() / 1000UL;
  if (climateHistoryCount > 0) {
    const size_t lastIndex = (climateHistoryStart + climateHistoryCount - 1) % CLIMATE_HISTORY_CAPACITY;
    const ClimatePoint& lastPoint = climateHistory[lastIndex];
    const bool withinMinInterval = timestamp < (lastPoint.uptimeSeconds + CLIMATE_HISTORY_MIN_INTERVAL_SECONDS);
    const bool nearlySameTemp = fabsf(temperatureC - lastPoint.temperatureC) < 0.05f;
    const bool nearlySameHumidity = fabsf(humidityPercent - lastPoint.humidityPercent) < 0.2f;

    if (withinMinInterval && nearlySameTemp && nearlySameHumidity) {
      return;
    }
  }

  const ClimatePoint point{timestamp, temperatureC, humidityPercent};
  size_t writeIndex = 0;

  if (climateHistoryCount < CLIMATE_HISTORY_CAPACITY) {
    writeIndex = (climateHistoryStart + climateHistoryCount) % CLIMATE_HISTORY_CAPACITY;
    climateHistoryCount++;
  } else {
    climateHistoryStart = (climateHistoryStart + 1) % CLIMATE_HISTORY_CAPACITY;
    writeIndex = (climateHistoryStart + climateHistoryCount - 1) % CLIMATE_HISTORY_CAPACITY;
  }

  climateHistory[writeIndex] = point;
  persistClimateHistory();
}

String createTelemetryJson(bool includeHistory) {
  String json;
  json.reserve(includeHistory ? 24000 : 700);

  uint32_t timelineNowSeconds = millis() / 1000UL;
  if (degreeHistoryCount > 0) {
    size_t lastIndex = (degreeHistoryStart + degreeHistoryCount - 1) % DEGREE_HISTORY_CAPACITY;
    uint32_t historyNow = degreeHistory[lastIndex].uptimeSeconds;
    if (timelineNowSeconds < historyNow) {
      timelineNowSeconds = historyNow;
    }
  }

  uint32_t degreeTrackingSeconds = 0;
  if (degreeHistoryCount > 0) {
    const uint32_t trackingStart = degreeHistory[degreeHistoryStart].uptimeSeconds;
    if (timelineNowSeconds >= trackingStart) {
      degreeTrackingSeconds = timelineNowSeconds - trackingStart;
    }
  }

  json += "{";
  json += "\"alive\":true,";
  json += "\"wifiConnected\":";
  json += Network::isConnected() ? "true" : "false";
  json += ",\"host\":\"";
  json += Config::WIFI_HOSTNAME;
  json += "\",";
  json += "\"ip\":\"";
  json += Network::isConnected() ? Network::getLocalIp() : String("");
  json += "\",";
  json += "\"uptimeSeconds\":";
  json += String(millis() / 1000UL);
  json += ",\"degreeTrackingSeconds\":";
  json += String(degreeTrackingSeconds);
  json += ",\"appState\":\"";
  json += latestTelemetry.appState;
  json += "\",";
  json += "\"temperatureC\":";
  json += latestTelemetry.sensorValid ? String(latestTelemetry.temperatureC, 2) : String("null");
  json += ",\"humidityPercent\":";
  json += latestTelemetry.sensorValid ? String(latestTelemetry.humidityPercent, 1) : String("null");
  json += ",\"sensorValid\":";
  json += latestTelemetry.sensorValid ? "true" : "false";
  json += ",\"fakeData\":";
  json += latestTelemetry.fakeData ? "true" : "false";
  json += ",\"degreeDays\":";
  json += String(latestTelemetry.degreeDays, 2);

  if (includeHistory) {
    json += ",\"degreeDaysHistory\":[";
    for (size_t index = 0; index < degreeHistoryCount; ++index) {
      size_t ringIndex = (degreeHistoryStart + index) % DEGREE_HISTORY_CAPACITY;
      const DegreePoint& point = degreeHistory[ringIndex];

      if (index > 0) {
        json += ",";
      }

      json += "{";
      json += "\"t\":";
      json += String(point.uptimeSeconds);
      json += ",\"degreeDays\":";
      json += String(point.degreeDays, 2);
      json += "}";
    }
    json += "]";

    json += ",\"climateHistory\":[";
    for (size_t index = 0; index < climateHistoryCount; ++index) {
      size_t ringIndex = (climateHistoryStart + index) % CLIMATE_HISTORY_CAPACITY;
      const ClimatePoint& point = climateHistory[ringIndex];

      if (index > 0) {
        json += ",";
      }

      json += "{";
      json += "\"t\":";
      json += String(point.uptimeSeconds);
      json += ",\"temperatureC\":";
      json += String(point.temperatureC, 2);
      json += ",\"humidityPercent\":";
      json += String(point.humidityPercent, 1);
      json += "}";
    }
    json += "]";
  }

  json += "}";
  return json;
}

String createAliveHtml() {
  String html = R"HTML(<!doctype html>
<html lang="sv">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Viltkyl Dashboard</title>
  <link rel="stylesheet" href="https://unpkg.com/@mantine/core@7/styles.css" />
  <link rel="stylesheet" href="https://unpkg.com/@mantine/charts@7/styles.css" />
  <style>
    html, body {
      margin: 0;
      min-height: 100%;
    }

    body {
      background:
        radial-gradient(circle at 12% 18%, rgba(14, 165, 233, 0.18), transparent 34%),
        radial-gradient(circle at 82% 10%, rgba(20, 184, 166, 0.16), transparent 32%),
        linear-gradient(145deg, #0b1220, #111827 48%, #0b1220);
      color: #e2e8f0;
      font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', sans-serif;
      padding: 16px;
    }

    .app-shell {
      max-width: 1100px;
      margin: 0 auto;
    }

    #fallback-root .head {
      display: flex;
      justify-content: space-between;
      align-items: baseline;
      gap: 12px;
      margin-bottom: 12px;
      flex-wrap: wrap;
    }

    #fallback-root .grid {
      display: grid;
      gap: 12px;
      grid-template-columns: repeat(12, 1fr);
    }

    #fallback-root .card {
      grid-column: span 12;
      border: 1px solid rgba(148, 163, 184, 0.28);
      border-radius: 14px;
      padding: 14px;
      background: rgba(17, 24, 39, 0.82);
    }

    @media (min-width: 900px) {
      #fallback-root .card.system,
      #fallback-root .card.metrics {
        grid-column: span 6;
      }
    }

    #fallback-root .row {
      display: flex;
      justify-content: space-between;
      gap: 10px;
      margin: 6px 0;
    }

    #fallback-root .muted {
      color: #94a3b8;
    }

    #fallback-root .value {
      font-weight: 700;
      text-align: right;
    }

    .chart-wrap {
      width: 100%;
      height: 320px;
      border-radius: 12px;
      border: 1px solid rgba(148, 163, 184, 0.32);
      background: linear-gradient(180deg, rgba(30, 41, 59, 0.8), rgba(15, 23, 42, 0.86));
      padding: 8px;
    }

    #degreeChart,
    #climateChart {
      width: 100%;
      height: 100%;
      display: block;
    }

    #mantine-root {
      min-height: 1px;
    }
  </style>
</head>
<body>
  <main id="fallback-root" class="app-shell">
    <div class="head">
      <h1 style="margin:0; font-size:1.8rem;">Viltkyl Dashboard</h1>
      <div class="muted">System/Miljo: 2s | Grafer: 1 min</div>
    </div>
    <div class="grid">
      <section class="card system">
        <h2 style="margin-top:0;">System</h2>
        <div class="row"><span class="muted">Hostnamn</span><span class="value" id="host">__HOSTNAME__</span></div>
        <div class="row"><span class="muted">IP-adress</span><span class="value" id="ip">-</span></div>
        <div class="row"><span class="muted">Uptime</span><span class="value" id="uptime">-</span></div>
        <div class="row"><span class="muted">Dygnsgrader tid</span><span class="value" id="degreeTracking">-</span></div>
        <div class="row"><span class="muted">State</span><span class="value" id="state">-</span></div>
      </section>
      <section class="card metrics">
        <h2 style="margin-top:0;">Miljo</h2>
        <div class="row"><span class="muted">Temp</span><span class="value" id="temp">-</span></div>
        <div class="row"><span class="muted">Luftfuktighet</span><span class="value" id="humidity">-</span></div>
        <div class="row"><span class="muted">Dygnsgrader</span><span class="value" id="degreeDays">-</span></div>
      </section>
      <section class="card chart">
        <h2 style="margin-top:0;">Dygnsgrader - tillvaxt</h2>
        <div class="muted" style="margin-bottom:8px;">Y-axel max 40 dygnsgrader</div>
        <div class="chart-wrap">
          <svg id="degreeChart" viewBox="0 0 560 260" preserveAspectRatio="none"></svg>
        </div>
        <div class="muted" id="source" style="margin-top:8px;">Kalla: -</div>
      </section>
      <section class="card chart">
        <h2 style="margin-top:0;">Temperatur och luftfuktighet over tid</h2>
        <div class="muted" style="margin-bottom:8px;">Cyan = temperatur (C), violet = luftfuktighet (%)</div>
        <div class="chart-wrap">
          <svg id="climateChart" viewBox="0 0 560 260" preserveAspectRatio="none"></svg>
        </div>
      </section>
    </div>
  </main>
  <div id="mantine-root"></div>

  <script type="module">
    const hostName = '__HOSTNAME__';
    const degreeDaysDecimals = __DEGREE_DAYS_DECIMALS__;
    const fastRefreshIntervalMs = 2000;
    const chartRefreshIntervalMs = 60000;
    const fallbackRoot = document.getElementById('fallback-root');
    let mantineUiHealthy = false;

    function showFallback() {
      if (fallbackRoot) {
        fallbackRoot.style.display = 'block';
      }
    }

    function hideFallback() {
      if (fallbackRoot) {
        fallbackRoot.style.display = 'none';
      }
    }

    window.addEventListener('error', function () {
      if (mantineUiHealthy) {
        showFallback();
      }
    });

    window.addEventListener('unhandledrejection', function () {
      if (mantineUiHealthy) {
        showFallback();
      }
    });

    function formatUptime(totalSeconds) {
      const sec = Number(totalSeconds || 0);
      const hours = Math.floor(sec / 3600);
      const minutes = Math.floor((sec % 3600) / 60);
      const seconds = sec % 60;
      return hours + 'h ' + minutes + 'm ' + seconds + 's';
    }

    function drawChart(history) {
      const svg = document.getElementById('degreeChart');
      if (!svg) {
        return;
      }

      const width = 560;
      const height = 260;
      const padding = { top: 16, right: 18, bottom: 40, left: 42 };
      const innerW = width - padding.left - padding.right;
      const innerH = height - padding.top - padding.bottom;
      const maxY = 40;
      const firstT = history.length > 0 ? Number(history[0].t || 0) : 0;
      const lastT = history.length > 0 ? Number(history[history.length - 1].t || firstT) : firstT;
      const rangeT = Math.max(1, lastT - firstT);

      let accumulatedMax = 0;
      const points = (history || []).map(function (p, index, arr) {
        const x = arr.length <= 1 ? padding.left : padding.left + (index / (arr.length - 1)) * innerW;
        const raw = Number(p.degreeDays || 0);
        accumulatedMax = Math.max(accumulatedMax, raw);
        const v = Math.max(0, Math.min(maxY, accumulatedMax));
        const y = padding.top + (1 - v / maxY) * innerH;
        return { x: x, y: y };
      });

      let markup = '';

      for (let i = 0; i <= 4; i++) {
        const y = padding.top + (i / 4) * innerH;
        const yValue = Math.round(maxY - (i / 4) * maxY);
        markup += '<line x1="' + padding.left + '" y1="' + y + '" x2="' + (width - padding.right) + '" y2="' + y + '" stroke="#334155" stroke-dasharray="4 4" />';
        markup += '<text x="' + (padding.left - 8) + '" y="' + (y + 4) + '" text-anchor="end" font-size="11" fill="#94a3b8">' + yValue + '</text>';
      }

      markup += '<line x1="' + padding.left + '" y1="' + (height - padding.bottom) + '" x2="' + (width - padding.right) + '" y2="' + (height - padding.bottom) + '" stroke="#64748b" />';
      markup += '<line x1="' + padding.left + '" y1="' + padding.top + '" x2="' + padding.left + '" y2="' + (height - padding.bottom) + '" stroke="#64748b" />';

      for (let i = 0; i <= 4; i++) {
        const frac = i / 4;
        const x = padding.left + frac * innerW;
        const t = firstT + frac * rangeT;
        const h = t / 3600;
        const label = h >= 10 ? h.toFixed(0) : h.toFixed(1);
        markup += '<line x1="' + x + '" y1="' + (height - padding.bottom) + '" x2="' + x + '" y2="' + (height - padding.bottom + 5) + '" stroke="#64748b" />';
        markup += '<text x="' + x + '" y="' + (height - 10) + '" text-anchor="middle" font-size="11" fill="#94a3b8">' + label + '</text>';
      }

      markup += '<text x="' + (padding.left + innerW / 2) + '" y="' + (height - 2) + '" text-anchor="middle" font-size="11" fill="#94a3b8">Tid (timmar)</text>';
      markup += '<text x="14" y="' + (padding.top + innerH / 2) + '" text-anchor="middle" font-size="11" fill="#94a3b8" transform="rotate(-90 14 ' + (padding.top + innerH / 2) + ')">Dygnsgrader (0-40)</text>';

      if (points.length > 0) {
        const polyline = points.map(function (p) {
          return p.x + ',' + p.y;
        }).join(' ');
        markup += '<polyline fill="none" stroke="#22d3ee" stroke-width="3" points="' + polyline + '" />';
        if (points.length === 1) {
          markup += '<circle cx="' + points[0].x + '" cy="' + points[0].y + '" r="4" fill="#22d3ee" />';
        }
      }

      svg.innerHTML = markup;
    }

    function drawClimateChart(history) {
      const svg = document.getElementById('climateChart');
      if (!svg) {
        return;
      }

      const width = 560;
      const height = 260;
      const padding = { top: 16, right: 46, bottom: 40, left: 46 };
      const innerW = width - padding.left - padding.right;
      const innerH = height - padding.top - padding.bottom;
      const data = history || [];
      const tempValues = data.map(function (p) { return Number(p.temperatureC || 0); });
      const humValues = data.map(function (p) { return Number(p.humidityPercent || 0); });

      if (data.length === 0) {
        svg.innerHTML = '';
        return;
      }

      const tempMinRaw = Math.min.apply(null, tempValues);
      const tempMaxRaw = Math.max.apply(null, tempValues);
      const tempMin = Math.floor(tempMinRaw - 1);
      const tempMax = Math.ceil(tempMaxRaw + 1);
      const tempRange = Math.max(1, tempMax - tempMin);

      const tempPoints = data.map(function (point, index, arr) {
        const x = arr.length <= 1 ? padding.left : padding.left + (index / (arr.length - 1)) * innerW;
        const temp = Number(point.temperatureC || 0);
        const y = padding.top + (1 - ((temp - tempMin) / tempRange)) * innerH;
        return { x: x, y: y };
      });

      const humPoints = data.map(function (point, index, arr) {
        const x = arr.length <= 1 ? padding.left : padding.left + (index / (arr.length - 1)) * innerW;
        const hum = Math.max(0, Math.min(100, Number(point.humidityPercent || 0)));
        const y = padding.top + (1 - hum / 100) * innerH;
        return { x: x, y: y };
      });

      const firstT = Number(data[0].t || 0);
      const lastT = Number(data[data.length - 1].t || firstT);
      const rangeT = Math.max(1, lastT - firstT);
      let markup = '';

      for (let i = 0; i <= 4; i++) {
        const y = padding.top + (i / 4) * innerH;
        const tempLabel = (tempMax - (i / 4) * tempRange).toFixed(1);
        const humLabel = Math.round(100 - (i / 4) * 100);

        markup += '<line x1="' + padding.left + '" y1="' + y + '" x2="' + (width - padding.right) + '" y2="' + y + '" stroke="#334155" stroke-dasharray="4 4" />';
        markup += '<text x="' + (padding.left - 8) + '" y="' + (y + 4) + '" text-anchor="end" font-size="11" fill="#67e8f9">' + tempLabel + '</text>';
        markup += '<text x="' + (width - padding.right + 8) + '" y="' + (y + 4) + '" text-anchor="start" font-size="11" fill="#c4b5fd">' + humLabel + '</text>';
      }

      markup += '<line x1="' + padding.left + '" y1="' + (height - padding.bottom) + '" x2="' + (width - padding.right) + '" y2="' + (height - padding.bottom) + '" stroke="#64748b" />';
      markup += '<line x1="' + padding.left + '" y1="' + padding.top + '" x2="' + padding.left + '" y2="' + (height - padding.bottom) + '" stroke="#64748b" />';
      markup += '<line x1="' + (width - padding.right) + '" y1="' + padding.top + '" x2="' + (width - padding.right) + '" y2="' + (height - padding.bottom) + '" stroke="#64748b" />';

      for (let i = 0; i <= 4; i++) {
        const frac = i / 4;
        const x = padding.left + frac * innerW;
        const t = firstT + frac * rangeT;
        const h = t / 3600;
        const label = h >= 10 ? h.toFixed(0) : h.toFixed(1);
        markup += '<line x1="' + x + '" y1="' + (height - padding.bottom) + '" x2="' + x + '" y2="' + (height - padding.bottom + 5) + '" stroke="#64748b" />';
        markup += '<text x="' + x + '" y="' + (height - 10) + '" text-anchor="middle" font-size="11" fill="#94a3b8">' + label + '</text>';
      }

      markup += '<text x="' + (padding.left + innerW / 2) + '" y="' + (height - 2) + '" text-anchor="middle" font-size="11" fill="#94a3b8">Tid (timmar)</text>';
      markup += '<text x="15" y="' + (padding.top + innerH / 2) + '" text-anchor="middle" font-size="11" fill="#67e8f9" transform="rotate(-90 15 ' + (padding.top + innerH / 2) + ')">Temperatur (C)</text>';
      markup += '<text x="545" y="' + (padding.top + innerH / 2) + '" text-anchor="middle" font-size="11" fill="#c4b5fd" transform="rotate(90 545 ' + (padding.top + innerH / 2) + ')">Luftfuktighet (%)</text>';

      if (tempPoints.length > 0) {
        const tempLine = tempPoints.map(function (p) { return p.x + ',' + p.y; }).join(' ');
        const humLine = humPoints.map(function (p) { return p.x + ',' + p.y; }).join(' ');
        markup += '<polyline fill="none" stroke="#22d3ee" stroke-width="2.5" points="' + tempLine + '" />';
        markup += '<polyline fill="none" stroke="#a78bfa" stroke-width="2.5" points="' + humLine + '" />';
      }

      svg.innerHTML = markup;
    }

    function updateFallbackLive(data, sourceText) {
      if (!fallbackRoot || !data) {
        return;
      }

      const sensorValid = !!data.sensorValid;
      document.getElementById('host').textContent = hostName;
      document.getElementById('ip').textContent = data.ip || 'Ej ansluten';
      document.getElementById('uptime').textContent = formatUptime(data.uptimeSeconds);
      document.getElementById('degreeTracking').textContent = formatUptime(data.degreeTrackingSeconds || 0);
      document.getElementById('state').textContent = data.appState || '-';
      document.getElementById('degreeDays').textContent = Number(data.degreeDays || 0).toFixed(degreeDaysDecimals);
      document.getElementById('temp').textContent = sensorValid ? Number(data.temperatureC).toFixed(2) + ' C' : 'Ingen giltig sensorlasning';
      document.getElementById('humidity').textContent = sensorValid ? Number(data.humidityPercent).toFixed(1) + ' %' : '-';
      document.getElementById('source').textContent = sourceText;
    }

    function updateFallbackCharts(data) {
      if (!fallbackRoot || !data) {
        return;
      }

      drawChart(data.degreeDaysHistory || []);
      drawClimateChart(data.climateHistory || []);
    }

    async function fetchLiveTelemetry() {
      const response = await fetch('/health', { cache: 'no-store' });
      return response.json();
    }

    async function fetchHistoryTelemetry() {
      const response = await fetch('/api/telemetry', { cache: 'no-store' });
      return response.json();
    }

    let mantineSetData = null;
    let mantineSetSourceText = null;
    let mantineDataCache = null;

    async function refreshLive() {
      try {
        const data = await fetchLiveTelemetry();
        const sourceText = 'Kalla: ' + (data.fakeData ? 'fake' : 'DHT22');
        updateFallbackLive(data, sourceText);

        if (mantineSetData && mantineSetSourceText) {
          mantineDataCache = Object.assign({}, mantineDataCache || {}, data);
          mantineSetData(mantineDataCache);
          mantineSetSourceText(sourceText);
        }
      } catch (error) {
        if (fallbackRoot) {
          document.getElementById('source').textContent = 'Kalla: fel vid hamtning';
        }

        if (mantineSetSourceText) {
          mantineSetSourceText('Kalla: fel vid hamtning');
        }
      }
    }

    async function refreshCharts() {
      try {
        const data = await fetchHistoryTelemetry();
        updateFallbackCharts(data);

        if (mantineSetData) {
          mantineDataCache = Object.assign({}, mantineDataCache || {}, data);
          mantineSetData(mantineDataCache);
        }
      } catch (error) {
        // Lat live-panelerna fortsatta uppdatera aven om historik-anrop fallerar.
      }
    }

    async function mountMantineUi() {
      try {
        const ReactModule = await import('https://esm.sh/react@18.3.1?bundle');
        const ReactDomClientModule = await import('https://esm.sh/react-dom@18.3.1/client?bundle');
        const MantineModule = await import('https://esm.sh/@mantine/core@7.17.1?bundle');
        const MantineChartsModule = await import('https://esm.sh/@mantine/charts@7.17.1?bundle');

        const React = ReactModule.default;
        const createRoot = ReactDomClientModule.createRoot;
        const {
          MantineProvider,
          Paper,
          Title,
          Text,
          Badge,
          Group,
          Grid,
          Stack,
          Loader
        } = MantineModule;
        const AreaChart = MantineChartsModule.AreaChart;

        function App() {
          const [data, setData] = React.useState(null);
          const [sourceText, setSourceText] = React.useState('Kalla: laddar...');

          React.useEffect(function () {
            mantineUiHealthy = true;
            hideFallback();

            mantineSetData = function (nextData) {
              setData(nextData);
            };

            mantineSetSourceText = function (nextText) {
              setSourceText(nextText);
            };

            return function () {
              mantineUiHealthy = false;
              showFallback();
              mantineSetData = null;
              mantineSetSourceText = null;
            };
          }, []);

          const sensorValid = !!(data && data.sensorValid);
          const tempText = sensorValid ? Number(data.temperatureC).toFixed(2) + ' C' : 'Ingen giltig sensorlasning';
          const humidityText = sensorValid ? Number(data.humidityPercent).toFixed(1) + ' %' : '-';
          const degreeDaysText = data ? Number(data.degreeDays || 0).toFixed(degreeDaysDecimals) : '-';
          const ipText = data && data.ip ? data.ip : 'Ej ansluten';
          const uptimeText = data ? formatUptime(data.uptimeSeconds) : '-';
          const degreeTrackingText = data ? formatUptime(data.degreeTrackingSeconds || 0) : '-';
          const stateText = data && data.appState ? data.appState : '-';
          const degreeChartData = (data && data.degreeDaysHistory ? data.degreeDaysHistory : []).map(function (point) {
            return {
              time: (Number(point.t || 0) / 3600).toFixed(1),
              degreeDays: Number(point.degreeDays || 0)
            };
          });
          const climateChartData = (data && data.climateHistory ? data.climateHistory : []).map(function (point) {
            return {
              time: (Number(point.t || 0) / 3600).toFixed(1),
              temperatureC: Number(point.temperatureC || 0),
              humidityPercent: Number(point.humidityPercent || 0)
            };
          });

          return React.createElement(
            MantineProvider,
            { defaultColorScheme: 'dark', forceColorScheme: 'dark' },
            React.createElement(
              'main',
              { className: 'app-shell' },
              React.createElement(
                Group,
                { justify: 'space-between', align: 'end', mb: 'md' },
                React.createElement(Title, { order: 1, c: 'gray.0' }, 'Viltkyl Dashboard'),
                React.createElement(Text, { size: 'sm', c: 'dimmed' }, 'System/Miljo: 2s | Grafer: 1 min')
              ),
              React.createElement(
                Grid,
                { gutter: 'md' },
                React.createElement(
                  Grid.Col,
                  { span: { base: 12, md: 6 } },
                  React.createElement(
                    Paper,
                    { withBorder: true, radius: 'lg', p: 'md', bg: 'dark.7' },
                    React.createElement(Title, { order: 3, mb: 'sm' }, 'System'),
                    React.createElement(
                      Stack,
                      { gap: 'xs' },
                      React.createElement(Group, { justify: 'space-between' }, React.createElement(Text, { c: 'dimmed' }, 'Hostnamn'), React.createElement(Text, { fw: 700 }, hostName)),
                      React.createElement(Group, { justify: 'space-between' }, React.createElement(Text, { c: 'dimmed' }, 'IP-adress'), React.createElement(Text, { fw: 700 }, ipText)),
                      React.createElement(Group, { justify: 'space-between' }, React.createElement(Text, { c: 'dimmed' }, 'Uptime'), React.createElement(Text, { fw: 700 }, uptimeText)),
                      React.createElement(Group, { justify: 'space-between' }, React.createElement(Text, { c: 'dimmed' }, 'Dygnsgrader tid'), React.createElement(Text, { fw: 700 }, degreeTrackingText)),
                      React.createElement(Group, { justify: 'space-between' }, React.createElement(Text, { c: 'dimmed' }, 'State'), React.createElement(Badge, { color: 'cyan', variant: 'light' }, stateText))
                    )
                  )
                ),
                React.createElement(
                  Grid.Col,
                  { span: { base: 12, md: 6 } },
                  React.createElement(
                    Paper,
                    { withBorder: true, radius: 'lg', p: 'md', bg: 'dark.7' },
                    React.createElement(Title, { order: 3, mb: 'sm' }, 'Miljo'),
                    React.createElement(
                      Stack,
                      { gap: 'xs' },
                      React.createElement(Group, { justify: 'space-between' }, React.createElement(Text, { c: 'dimmed' }, 'Temp'), React.createElement(Text, { fw: 700 }, tempText)),
                      React.createElement(Group, { justify: 'space-between' }, React.createElement(Text, { c: 'dimmed' }, 'Luftfuktighet'), React.createElement(Text, { fw: 700 }, humidityText)),
                      React.createElement(Group, { justify: 'space-between' }, React.createElement(Text, { c: 'dimmed' }, 'Dygnsgrader'), React.createElement(Text, { fw: 700 }, degreeDaysText))
                    )
                  )
                ),
                React.createElement(
                  Grid.Col,
                  { span: 12 },
                  React.createElement(
                    Paper,
                    { withBorder: true, radius: 'lg', p: 'md', bg: 'dark.7' },
                    React.createElement(Title, { order: 3, mb: 6 }, 'Dygnsgrader - tillvaxt'),
                    React.createElement(Text, { size: 'sm', c: 'dimmed', mb: 'sm' }, 'Y-axel max 40 dygnsgrader'),
                    degreeChartData.length > 0 ? React.createElement(AreaChart, {
                      h: 300,
                      data: degreeChartData,
                      dataKey: 'time',
                      withLegend: false,
                      withDots: false,
                      withGradient: true,
                      curveType: 'monotone',
                      strokeWidth: 2,
                      gridAxis: 'xy',
                      yAxisProps: { domain: [0, 40] },
                      series: [{ name: 'degreeDays', color: 'cyan.5' }]
                    }) : React.createElement(Text, { size: 'sm', c: 'dimmed' }, 'Ingen historik an laddad'),
                    React.createElement(
                      Group,
                      { justify: 'space-between', mt: 'sm' },
                      React.createElement(Text, { size: 'sm', c: 'dimmed' }, sourceText),
                      !data ? React.createElement(Group, { gap: 'xs' }, React.createElement(Loader, { size: 'xs' }), React.createElement(Text, { size: 'sm', c: 'dimmed' }, 'Laddar...')) : null
                    )
                  )
                )
                ,
                React.createElement(
                  Grid.Col,
                  { span: 12 },
                  React.createElement(
                    Paper,
                    { withBorder: true, radius: 'lg', p: 'md', bg: 'dark.7' },
                    React.createElement(Title, { order: 3, mb: 6 }, 'Temperatur och luftfuktighet over tid'),
                    React.createElement(Text, { size: 'sm', c: 'dimmed', mb: 'sm' }, 'Cyan = temperatur (C), violet = luftfuktighet (%)'),
                    climateChartData.length > 0 ? React.createElement(AreaChart, {
                      h: 300,
                      data: climateChartData,
                      dataKey: 'time',
                      withLegend: true,
                      withDots: false,
                      withGradient: true,
                      curveType: 'monotone',
                      strokeWidth: 2,
                      gridAxis: 'xy',
                      series: [
                        { name: 'temperatureC', color: 'cyan.5', label: 'Temperatur (C)' },
                        { name: 'humidityPercent', color: 'violet.5', label: 'Luftfuktighet (%)' }
                      ]
                    }) : React.createElement(Text, { size: 'sm', c: 'dimmed' }, 'Ingen historik an laddad')
                  )
                )
              )
            )
          );
        }

        createRoot(document.getElementById('mantine-root')).render(React.createElement(App));
      } catch (error) {
        showFallback();
        console.warn('Mantine UI kunde inte laddas, fallback visas.', error);
      }
    }

    await mountMantineUi();
    await refreshCharts();
    await refreshLive();
    setInterval(refreshLive, fastRefreshIntervalMs);
    setInterval(refreshCharts, chartRefreshIntervalMs);
  </script>
</body>
</html>)HTML";

  html.replace("__HOSTNAME__", Config::WIFI_HOSTNAME);
  html.replace("__DEGREE_DAYS_DECIMALS__", String(Config::GUI_DEGREE_DAYS_DECIMALS));
  return html;
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", createAliveHtml());
}

void handleHealth() {
  server.send(200, "application/json", createTelemetryJson(false));
}

void handleTelemetry() {
  server.send(200, "application/json", createTelemetryJson(true));
}

}  // namespace

namespace WebUi {

void initialize() {
  if (!Network::isConnected()) {
    Serial.println("Web: startar inte, WiFi ej anslutet");
    return;
  }

  loadPersistedDegreeHistory();
  loadPersistedClimateHistory();
  Serial.print("Web: laddad degree-historik punkter=");
  Serial.println(static_cast<unsigned long>(degreeHistoryCount));
  Serial.print("Web: laddad climate-historik punkter=");
  Serial.println(static_cast<unsigned long>(climateHistoryCount));
  appendDegreeDaysHistory(latestTelemetry.degreeDays);
  if (latestTelemetry.sensorValid) {
    appendClimateHistory(latestTelemetry.temperatureC, latestTelemetry.humidityPercent);
  }
  server.on("/", HTTP_GET, handleRoot);
  server.on("/health", HTTP_GET, handleHealth);
  server.on("/api/telemetry", HTTP_GET, handleTelemetry);
  server.begin();
  serverStarted = true;

  Serial.print("Web: aktiv pa http://");
  Serial.print(Network::getLocalIp());
  Serial.print(":");
  Serial.println(Config::WEB_SERVER_PORT);
}

void handleClient() {
  if (!serverStarted) {
    return;
  }

  server.handleClient();
}

void updateTelemetry(const TelemetrySnapshot& snapshot) {
  // Rensa dygnsgrader-historik endast vid explicit reset till nara noll.
  const uint32_t uptimeSeconds = millis() / 1000UL;
  const bool resetWindowOpen = uptimeSeconds > 120;
  if (lastRecordedDegreeDays >= 0.0f &&
      resetWindowOpen &&
      snapshot.degreeDays + DEGREE_RESET_EPSILON < lastRecordedDegreeDays &&
      snapshot.degreeDays <= DEGREE_RESET_THRESHOLD) {
    clearPersistedDegreeHistory();
  }

  latestTelemetry = snapshot;
  appendDegreeDaysHistory(snapshot.degreeDays);
  if (snapshot.sensorValid) {
    appendClimateHistory(snapshot.temperatureC, snapshot.humidityPercent);
  }
}

}  // namespace WebUi
