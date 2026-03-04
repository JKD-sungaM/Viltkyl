#include "web_server.h"

#include <Arduino.h>
#include <WebServer.h>

#include "config.h"
#include "network_manager.h"

namespace {

WebServer server(Config::WEB_SERVER_PORT);
bool serverStarted = false;

String createAliveHtml() {
  String html;
  html.reserve(900);

  html += "<!doctype html><html lang='sv'><head>";
  html += "<meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<title>Viltkyl Alive</title>";
  html += "<style>body{font-family:Arial,sans-serif;background:#f5f7fa;color:#1f2937;margin:0;padding:24px;}";
  html += ".card{max-width:720px;margin:0 auto;background:#fff;padding:20px;border-radius:12px;";
  html += "box-shadow:0 2px 10px rgba(0,0,0,.08);}h1{margin-top:0;}";
  html += "dt{font-weight:700;margin-top:8px;}dd{margin:0 0 8px 0;}";
  html += ".ok{color:#166534;font-weight:700;}</style></head><body>";
  html += "<div class='card'>";
  html += "<h1>Viltkyl \xE2\x80\x93 alive</h1>";
  html += "<p class='ok'>Enheten lever och webbservern svarar.</p>";
  html += "<dl>";
  html += "<dt>Hostnamn</dt><dd>";
  html += Config::WIFI_HOSTNAME;
  html += "</dd>";
  html += "<dt>IP-adress</dt><dd>";
  html += Network::isConnected() ? Network::getLocalIp() : String("Ej ansluten");
  html += "</dd>";
  html += "<dt>Uptime</dt><dd>";
  html += String(millis() / 1000UL);
  html += " sekunder</dd>";
  html += "</dl>";
  html += "</div></body></html>";

  return html;
}

void handleRoot() {
  server.send(200, "text/html; charset=utf-8", createAliveHtml());
}

void handleHealth() {
  String json = "{";
  json += "\"alive\":true,";
  json += "\"wifiConnected\":";
  json += Network::isConnected() ? "true" : "false";
  json += ",\"ip\":\"";
  json += Network::isConnected() ? Network::getLocalIp() : String("");
  json += "\",";
  json += "\"uptimeSeconds\":";
  json += String(millis() / 1000UL);
  json += "}";

  server.send(200, "application/json", json);
}

}  // namespace

namespace WebUi {

void initialize() {
  if (!Network::isConnected()) {
    Serial.println("Web: startar inte, WiFi ej anslutet");
    return;
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/health", HTTP_GET, handleHealth);
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

}  // namespace WebUi
