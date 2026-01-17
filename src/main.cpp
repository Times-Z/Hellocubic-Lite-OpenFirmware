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
#include "web/Api.h"

ConfigManager configManager;
const char* AP_SSID = "GeekMagic";
const char* AP_PASSWORD = "$str0ngPa$$w0rd";
WiFiManager* wifiManager = nullptr;

static constexpr uint32_t SERIAL_BAUD_RATE = 115200;
static constexpr uint32_t BOOT_DELAY_MS = 200;
static constexpr int LOADING_BAR_TEXT_X = 50;
static constexpr int LOADING_BAR_TEXT_Y = 80;
static constexpr int LOADING_BAR_Y = 110;
static constexpr int LOADING_DELAY_MS = 1000;

Webserver* webserver = nullptr;

/**
 * @brief Initializes the system
 *
 */
void setup() {
    Serial.begin(SERIAL_BAUD_RATE);
    delay(BOOT_DELAY_MS);
    Serial.println("");
    Logger::info(("GeekMagic Open Firmware " + String(PROJECT_VER_STR)).c_str());

    constexpr int TOTAL_STEPS = 5;
    int step = 0;

    if (!LittleFS.begin()) {
        if (DisplayManager::isReady()) {
            DisplayManager::drawLoadingBar((float)step / TOTAL_STEPS, LOADING_BAR_Y);
        }
        Logger::error("Failed to mount LittleFS");
        return;
    }

    step++;

    if (configManager.load()) {
        Logger::info("Configuration loaded successfully");
    }
    step++;

    DisplayManager::begin();
    if (DisplayManager::isReady()) {
        DisplayManager::drawTextWrapped(LOADING_BAR_TEXT_X, LOADING_BAR_TEXT_Y, "Starting...", 2, LCD_WHITE, LCD_BLACK,
                                        true);
        DisplayManager::drawLoadingBar((float)step / TOTAL_STEPS, LOADING_BAR_Y);
    }
    step++;

    wifiManager = new WiFiManager(configManager.getSSID(), configManager.getPassword(), AP_SSID, AP_PASSWORD);
    wifiManager->begin();
    if (DisplayManager::isReady()) {
        DisplayManager::drawLoadingBar((float)step / TOTAL_STEPS, LOADING_BAR_Y);
    }
    step++;

    webserver = new Webserver();
    webserver->begin();
    if (DisplayManager::isReady()) {
        DisplayManager::drawLoadingBar((float)step / TOTAL_STEPS, LOADING_BAR_Y);
    }
    step++;

    registerApiEndpoints(webserver);

    webserver->serveStatic("/", "/web/index.html", "text/html");
    webserver->serveStatic("/header.html", "/web/header.html", "text/html");
    webserver->serveStatic("/footer.html", "/web/footer.html", "text/html");
    webserver->serveStatic("/index.html", "/web/index.html", "text/html");
    webserver->serveStatic("/update.html", "/web/update.html", "text/html");
    webserver->serveStatic("/gif_upload.html", "/web/gif_upload.html", "text/html");

    webserver->serveStatic("/css/pico.min.css", "/web/css/pico.min.css", "text/css");
    webserver->serveStatic("/css/style.css", "/web/css/style.css", "text/css");
    webserver->serveStatic("/js/alpinejs.min.js", "/web/js/alpinejs.min.js", "application/javascript");
    webserver->serveStatic("/js/main.js", "/web/js/main.js", "application/javascript");

    if (DisplayManager::isReady()) {
        DisplayManager::drawLoadingBar(1.0F, LOADING_BAR_Y);
    }

    delay(LOADING_DELAY_MS);

    DisplayManager::drawStartup(wifiManager->getIP().toString());
}

void loop() {
    if (webserver != nullptr) {
        webserver->handleClient();
    }
    DisplayManager::update();
}
