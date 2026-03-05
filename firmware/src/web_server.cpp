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
Preferences webPreferences;
bool webPreferencesReady = false;

struct DegreePoint {
  uint32_t uptimeSeconds;
  float degreeDays;
};

DegreePoint degreeHistory[DEGREE_HISTORY_CAPACITY];
size_t degreeHistoryStart = 0;
size_t degreeHistoryCount = 0;
float lastRecordedDegreeDays = -1.0f;

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
  webPreferences.putBytes(HISTORY_KEY_DATA, linearized, degreeHistoryCount * sizeof(DegreePoint));
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

void appendDegreeDaysHistory(float degreeDays) {
  if (lastRecordedDegreeDays >= 0.0f && fabsf(degreeDays - lastRecordedDegreeDays) < 0.0005f) {
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

String createTelemetryJson(bool includeHistory) {
  String json;
  json.reserve(includeHistory ? 12000 : 700);

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
  <style>
    :root {
      --bg-1: #edf6ff;
      --bg-2: #f8fff0;
      --card: rgba(255, 255, 255, 0.82);
      --border: rgba(19, 49, 72, 0.16);
      --text: #102a43;
      --muted: #486581;
      --accent: #0d9488;
      --accent-2: #fb7185;
      --chip: #d1fae5;
    }

    * { box-sizing: border-box; }

    body {
      margin: 0;
      min-height: 100vh;
      font-family: 'Trebuchet MS', 'Segoe UI', sans-serif;
      color: var(--text);
      background:
        radial-gradient(circle at 8% 10%, rgba(13, 148, 136, 0.2), transparent 40%),
        radial-gradient(circle at 92% 16%, rgba(251, 113, 133, 0.18), transparent 35%),
        linear-gradient(135deg, var(--bg-1), var(--bg-2));
      padding: 18px;
    }

    .dashboard {
      max-width: 1100px;
      margin: 0 auto;
      animation: fade-in 380ms ease-out;
    }

    .header {
      margin: 6px 0 16px;
      display: flex;
      flex-wrap: wrap;
      justify-content: space-between;
      gap: 10px;
      align-items: baseline;
    }

    .header h1 {
      margin: 0;
      font-size: clamp(1.4rem, 2.4vw, 2.1rem);
      letter-spacing: 0.02em;
    }

    .grid {
      display: grid;
      gap: 14px;
      grid-template-columns: repeat(12, minmax(0, 1fr));
    }

    .card {
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 18px;
      padding: 18px;
      backdrop-filter: blur(8px);
      box-shadow: 0 14px 36px rgba(15, 23, 42, 0.08);
    }

    .card h2 {
      margin: 0 0 12px;
      font-size: 1.06rem;
      color: #0f172a;
    }

    .card.system { grid-column: span 12; }
    .card.metrics { grid-column: span 12; }
    .card.chart { grid-column: span 12; }

    @media (min-width: 960px) {
      .card.system { grid-column: span 6; }
      .card.metrics { grid-column: span 6; }
      .card.chart { grid-column: span 12; }
    }

    .kv {
      display: grid;
      grid-template-columns: 1fr auto;
      row-gap: 10px;
      column-gap: 12px;
      font-size: 0.98rem;
    }

    .label { color: var(--muted); }

    .value {
      font-weight: 700;
      color: #0f172a;
      text-align: right;
    }

    .chip {
      display: inline-block;
      padding: 3px 9px;
      border-radius: 999px;
      background: var(--chip);
      color: #065f46;
      font-weight: 700;
      font-size: 0.82rem;
      margin-left: 8px;
    }

    .muted {
      color: var(--muted);
      font-size: 0.9rem;
    }

    .chart-wrap {
      width: 100%;
      height: 320px;
      border-radius: 12px;
      background: rgba(236, 253, 245, 0.65);
      border: 1px solid rgba(13, 148, 136, 0.22);
      padding: 8px;
    }

    #degreeChart {
      width: 100%;
      height: 100%;
      display: block;
    }

    .footer {
      margin-top: 8px;
      color: var(--muted);
      font-size: 0.84rem;
    }

    @keyframes fade-in {
      from { opacity: 0; transform: translateY(8px); }
      to { opacity: 1; transform: translateY(0); }
    }
  </style>
</head>
<body>
  <main class="dashboard">
    <div class="header">
      <h1>Viltkyl Dashboard</h1>
      <div class="muted">Automatisk uppdatering var 2s</div>
    </div>

    <div class="grid">
      <section class="card system">
        <h2>System</h2>
        <div class="kv">
          <div class="label">Hostnamn</div><div class="value" id="host">__HOSTNAME__</div>
          <div class="label">IP-adress</div><div class="value" id="ip">-</div>
          <div class="label">Uptime</div><div class="value" id="uptime">-</div>
          <div class="label">State</div><div class="value" id="state">-</div>
        </div>
      </section>

      <section class="card metrics">
        <h2>Miljo</h2>
        <div class="kv">
          <div class="label">Temp</div><div class="value" id="temp">-</div>
          <div class="label">Luftfuktighet</div><div class="value" id="humidity">-</div>
          <div class="label">Dygnsgrader</div><div class="value" id="degreeDays">-</div>
        </div>
      </section>

      <section class="card chart">
        <h2>Dygnsgrader - tillvaxt</h2>
        <div class="muted">Y-axel max 40 dygnsgrader</div>
        <div class="chart-wrap">
          <svg id="degreeChart" viewBox="0 0 560 260" preserveAspectRatio="none"></svg>
        </div>
        <div class="footer" id="source">Kalla: -</div>
      </section>
    </div>
  </main>

  <script>
    function formatUptime(totalSeconds) {
      const sec = Number(totalSeconds || 0);
      const hours = Math.floor(sec / 3600);
      const minutes = Math.floor((sec % 3600) / 60);
      const seconds = sec % 60;
      return hours + 'h ' + minutes + 'm ' + seconds + 's';
    }

    function drawChart(history) {
      const svg = document.getElementById('degreeChart');
      if (!svg) return;

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
        markup += '<line x1="' + padding.left + '" y1="' + y + '" x2="' + (width - padding.right) + '" y2="' + y + '" stroke="#cbd5e1" stroke-dasharray="4 4" />';
        markup += '<text x="' + (padding.left - 8) + '" y="' + (y + 4) + '" text-anchor="end" font-size="11" fill="#334155">' + yValue + '</text>';
      }

      markup += '<line x1="' + padding.left + '" y1="' + (height - padding.bottom) + '" x2="' + (width - padding.right) + '" y2="' + (height - padding.bottom) + '" stroke="#94a3b8" />';
      markup += '<line x1="' + padding.left + '" y1="' + padding.top + '" x2="' + padding.left + '" y2="' + (height - padding.bottom) + '" stroke="#94a3b8" />';

      for (let i = 0; i <= 4; i++) {
        const frac = i / 4;
        const x = padding.left + frac * innerW;
        const t = firstT + frac * rangeT;
        const h = t / 3600;
        const label = h >= 10 ? h.toFixed(0) : h.toFixed(1);
        markup += '<line x1="' + x + '" y1="' + (height - padding.bottom) + '" x2="' + x + '" y2="' + (height - padding.bottom + 5) + '" stroke="#64748b" />';
        markup += '<text x="' + x + '" y="' + (height - 10) + '" text-anchor="middle" font-size="11" fill="#334155">' + label + '</text>';
      }

      markup += '<text x="' + (padding.left + innerW / 2) + '" y="' + (height - 2) + '" text-anchor="middle" font-size="11" fill="#334155">Tid (timmar)</text>';
      markup += '<text x="14" y="' + (padding.top + innerH / 2) + '" text-anchor="middle" font-size="11" fill="#334155" transform="rotate(-90 14 ' + (padding.top + innerH / 2) + ')">Dygnsgrader (0-40)</text>';

      if (points.length > 0) {
        const polyline = points.map(function (p) { return p.x + ',' + p.y; }).join(' ');
        markup += '<polyline fill="none" stroke="#0d9488" stroke-width="3" points="' + polyline + '" />';
        if (points.length === 1) {
          markup += '<circle cx="' + points[0].x + '" cy="' + points[0].y + '" r="4" fill="#0d9488" />';
        }
      }

      svg.innerHTML = markup;
    }

    async function refresh() {
      try {
        const response = await fetch('/api/telemetry', { cache: 'no-store' });
        const data = await response.json();

        document.getElementById('ip').textContent = data.ip || 'Ej ansluten';
        document.getElementById('uptime').textContent = formatUptime(data.uptimeSeconds);
        document.getElementById('state').textContent = data.appState || '-';
        document.getElementById('degreeDays').textContent = Number(data.degreeDays || 0).toFixed(2);

        if (data.sensorValid) {
          document.getElementById('temp').textContent = Number(data.temperatureC).toFixed(2) + ' C';
          document.getElementById('humidity').textContent = Number(data.humidityPercent).toFixed(1) + ' %';
        } else {
          document.getElementById('temp').textContent = 'Ingen giltig sensorlasning';
          document.getElementById('humidity').textContent = '-';
        }

        document.getElementById('source').textContent = 'Kalla: ' + (data.fakeData ? 'fake' : 'DHT22');
        drawChart(data.degreeDaysHistory || []);
      } catch (error) {
        document.getElementById('source').textContent = 'Kalla: fel vid hamtning';
      }
    }

    refresh();
    setInterval(refresh, 2000);
  </script>
</body>
</html>)HTML";

  html.replace("__HOSTNAME__", Config::WIFI_HOSTNAME);
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
  appendDegreeDaysHistory(latestTelemetry.degreeDays);
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
  // Om dygnsgrader nollas (long-press stop) rensar vi historiken i samma steg.
  if (lastRecordedDegreeDays >= 0.0f && snapshot.degreeDays + 0.0005f < lastRecordedDegreeDays) {
    clearPersistedDegreeHistory();
  }

  latestTelemetry = snapshot;
  appendDegreeDaysHistory(snapshot.degreeDays);
}

}  // namespace WebUi
