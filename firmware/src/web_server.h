#pragma once

namespace WebUi {

// Startar enkel webbserver om WiFi är anslutet.
void initialize();

// Hanterar inkommande HTTP-klienter. Ska kallas i loop().
void handleClient();

}  // namespace WebUi
