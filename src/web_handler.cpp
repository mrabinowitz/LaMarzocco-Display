#include "web_handle.h"
#include "FS.h"
#include "SPIFFS.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include "config.h"
#include "Preferences.h"
#include <set>

extern Preferences preferences;
extern WebServer server;

struct CaseInsensitiveCompare
{
    bool operator()(const String &a, const String &b) const
    {
        String lowerA = a;
        lowerA.toLowerCase();
        String lowerB = b;
        lowerB.toLowerCase();
        return lowerA < lowerB;
    }
};

void initFS(void)
{
    SPIFFS.begin();
}

void streamFile(String path)
{
    File file = SPIFFS.open(path, "r");
    if (!file)
    {
        log_i("%s file not found!", path.c_str());
        return;
    }
    server.streamFile(file, "text/html");
    file.close();
}

void handleNotFound(void)
{
    server.sendHeader("Location", REDIRECT_URL, true);
    server.send(302, "text/plain", "");
}

void cssHandler(void)
{
    File CSSfile = SPIFFS.open("/styles.css", "r");
    server.streamFile(CSSfile, "text/css");
    CSSfile.close();
}

void mainHandler(void)
{
    streamFile("/main.html");
}

void sendSSID(void)
{
    int n = WiFi.scanNetworks(); // Scan for available networks
    if (n == 0)
        Serial.println("No networks found");
    else
        Serial.println("Networks found:");
    std::set<String, CaseInsensitiveCompare> ssidSet;
    for (int i = 0; i < n; ++i)
        ssidSet.insert(WiFi.SSID(i));

    JsonDocument jsonDoc;
    JsonArray ssidArray = jsonDoc.to<JsonArray>();
    for (const auto &ssid : ssidSet)
    {
        ssidArray.add(ssid);
    }

    String jsonString;
    serializeJson(jsonDoc, jsonString);
    server.send(200, "application/json", jsonString);
}

void sendStatus(void)
{
    JsonDocument jsonDoc;
    jsonDoc["wifi"] = preferences.getString("SSID", "N/A");
    jsonDoc["email"] = preferences.getString("USER_EMAIL", "N/A");
    jsonDoc["machine"] = preferences.getString("MACHINE", "N/A");
    String jsonString;
    serializeJson(jsonDoc, jsonString);
    server.send(200, "application/json", jsonString);
}

void saveWifiHandler(void)
{
    String ssid = server.arg("ssid");
    if (ssid == "OTHERS")
        ssid = server.arg("manual_ssid");
    preferences.putString("SSID", ssid);
    preferences.putString("PASS", server.arg("password"));
    streamFile("/credential.html");
}

void saveCloudHandler(void)
{
    preferences.putString("USER_EMAIL", server.arg("user_email"));
    preferences.putString("USER_PASS", server.arg("user_pass"));
    streamFile("/machine.html");
}

void saveMachineHandler(void)
{
    preferences.putString("MACHINE", server.arg("machine"));
    streamFile("/status.html");
}

void restartHander(void)
{
    server.send(200);
    delay(1000);
    ESP.restart();
}
