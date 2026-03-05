#pragma once

namespace WebUi {

struct TelemetrySnapshot {
	const char* appState;
	float temperatureC;
	float humidityPercent;
	float degreeDays;
	bool sensorValid;
	bool fakeData;
};

// Startar enkel webbserver om WiFi är anslutet.
void initialize();

// Hanterar inkommande HTTP-klienter. Ska kallas i loop().
void handleClient();

// Uppdaterar data som visas i webbgranssnitt och /health.
void updateTelemetry(const TelemetrySnapshot& snapshot);

}  // namespace WebUi
