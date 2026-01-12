#include <Arduino.h>
#include <LittleFS.h>
#include <Arduino_GFX_Library.h>
#include <SPI.h>

#include <Logger.h>
#include "project_version.h"
#include "config/ConfigManager.h"
#include "wireless/WiFiManager.h"
#include "display/DisplayManager.h"
#include "web/Webserver.h"
#include "web/api.h"

ConfigManager configManager;
const char* AP_SSID = "HelloCubicLite";
const char* AP_PASSWORD = "$str0ngPa$$w0rd";
WiFiManager* wifiManager = nullptr;

static constexpr uint32_t SERIAL_BAUD_RATE = 115200;
static constexpr uint32_t BOOT_DELAY_MS = 200;

Webserver* webserver = nullptr;

/**
 * @brief Initializes the system
 *
 */
void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(BOOT_DELAY_MS);
    Serial.println("");
    Logger::info(("HelloCubic Lite Open Firmware " + String(PROJECT_VER_STR)).c_str());

    if (!LittleFS.begin()) {
        Logger::error("Failed to mount LittleFS");
        return;
    }

    if (configManager.load()) {
        Logger::info("Configuration loaded successfully");
    }

    DisplayManager::begin();
    if (DisplayManager::isReady()) {
        DisplayManager::drawStartup();
    }

    wifiManager = new WiFiManager(configManager.getSSID(), configManager.getPassword(), AP_SSID, AP_PASSWORD);
    wifiManager->begin();

    webserver = new Webserver();
    webserver->begin();

    registerApiEndpoints(webserver);

    webserver->serveStatic("/", "/web/index.html", "text/html");
    webserver->serveStatic("/index.html", "/web/index.html", "text/html");
    webserver->serveStatic("/update.html", "/web/update.html", "text/html");

    webserver->serveStatic("/css/pico.min.css", "/web/css/pico.min.css", "text/css");
    webserver->serveStatic("/css/style.css", "/web/css/style.css", "text/css");
    webserver->serveStatic("/js/alpinejs.min.js", "/web/js/alpinejs.min.js", "application/javascript");
    webserver->serveStatic("/js/main.js", "/web/js/main.js", "application/javascript");
}

void loop() {
    if (webserver != nullptr) {
        webserver->handleClient();
    }
}
