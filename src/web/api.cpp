#include <Arduino.h>
#include <Logger.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPUpdateServer.h>

#include "web/Webserver.h"
#include <Updater.h>

ESP8266HTTPUpdateServer httpUpdater;
static bool otaError = false;
static size_t otaSize = 0;
static String otaStatus;

void otaUpload(Webserver* webserver, int mode);
void otaFinished(Webserver* webserver);

/**
 * @brief Register API endpoints for the webserver
 * @param webserver Pointer to the Webserver instance
 *
 * @return void
 */
void registerApiEndpoints(Webserver* webserver) {
    Logger::info("Registering API endpoints", "API");

    webserver->on("/api/v1/reboot", HTTP_POST, [webserver]() {
        JsonDocument doc;
        int constexpr rebootDelayMs = 1000;

        doc["status"] = "rebooting";
        String json;
        serializeJson(doc, json);

        webserver->raw().send(HTTP_CODE_OK, "application/json", json);

        delay(rebootDelayMs);
        ESP.restart();  // NOLINT(readability-static-accessed-through-instance)
    });

    // Just in case for now the old updater endpoint is still here
    httpUpdater.setup(&webserver->raw(), "/legacyupdate");

    webserver->raw().on(
        "/api/v1/ota/fw", HTTP_POST, [webserver]() { otaFinished(webserver); },
        [webserver]() { otaUpload(webserver, U_FLASH); });
    webserver->raw().on(
        "/api/v1/ota/fs", HTTP_POST, [webserver]() { otaFinished(webserver); },
        [webserver]() { otaUpload(webserver, U_FS); });
}

/**
 * @brief Handle OTA upload
 * @param webserver Pointer to the Webserver instance
 * @param mode Update mode U_FLASH U_FS
 *
 * @return void
 */
void otaUpload(Webserver* webserver, int mode) {
    HTTPUpload& upload = webserver->raw().upload();

    switch (upload.status) {
        case UPLOAD_FILE_START: {
            Logger::info(("OTA start: " + upload.filename).c_str(), "API");

            otaError = false;
            otaSize = 0;
            otaStatus = "";
            int constexpr security_space = 0x1000;
            u_int constexpr bin_mask = 0xFFFFF000;

            FSInfo fs_info;
            LittleFS.info(fs_info);
            size_t fsSize = fs_info.totalBytes;
            size_t maxSketchSpace =
                (ESP.getFreeSketchSpace() - security_space) &  // NOLINT(readability-static-accessed-through-instance)
                bin_mask;
            size_t place = (mode == U_FS) ? fsSize : maxSketchSpace;

            if (!Update.begin(place, mode)) {
                otaError = true;
                otaStatus = Update.getErrorString();
                Logger::error(("Update.begin failed: " + otaStatus).c_str(), "API");
            }

            break;
        }

        case UPLOAD_FILE_WRITE: {
            if (!otaError) {
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    otaError = true;
                    otaStatus = Update.getErrorString();
                    Logger::error(("Write failed: " + otaStatus).c_str(), "API");
                }
                otaSize += upload.currentSize;
            }

            break;
        }

        case UPLOAD_FILE_END: {
            if (!otaError) {
                if (Update.end(true)) {
                    if (mode == U_FS) {
                        Logger::info("OTA FS update complete, mounting file system...", "API");
                        LittleFS.begin();
                    }

                    otaStatus = "Update OK (" + String(otaSize) + " bytes)";
                    Logger::info(otaStatus.c_str(), "API");
                } else {
                    otaError = true;
                    otaStatus = Update.getErrorString();
                }
            }

            break;
        }

        case UPLOAD_FILE_ABORTED: {
            Update.end();
            otaError = true;
            otaStatus = "Update aborted";

            break;
        }

        default:
            break;
    }
}

/**
 * @brief Handle OTA finished
 * @param webserver Pointer to the Webserver instance
 *
 * @return void
 */
void otaFinished(Webserver* webserver) {
    JsonDocument doc;
    int constexpr rebootDelayMs = 5000;

    doc["status"] = "Upload successful";
    doc["message"] = otaStatus;

    if (otaError) {
        doc["status"] = "Error";
    }

    String json;
    serializeJson(doc, json);
    webserver->raw().send(HTTP_CODE_OK, "application/json", json);

    if (!otaError) {
        delay(rebootDelayMs);
        ESP.restart();  // NOLINT(readability-static-accessed-through-instance)
    }
}