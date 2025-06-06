#include <WiFi.h>
#include <WiFiClient.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <esp_heap_caps.h>
#include "config.h"
#include "web_handle.h"

uint64_t timer = 0;

// DNS server
const byte DNS_PORT = 53;
DNSServer dnsServer;
WebServer server(80);

void setupAP()
{
    log_i("Configuring access point...");
    WiFi.softAP(AP_SSID);
    delay(100);
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
    if (!MDNS.begin(AP_SSID)) // using same name as SSID, shottimer.local
        debugln("Error setting up MDNS responder!");
    else
        debugln("mDNS responder started");
    log_i("The hotspot has been established");
}

void webTask(void *args)
{

    while (1)
    {
        dnsServer.processNextRequest();
        server.handleClient();
        vTaskDelay(10); // allow the cpu to switch to other tasks
    }
    vTaskDelete(NULL);
}

void setupWEB(void)
{
    setupAP();
    initFS();

    // // load css
    server.on("/styles.css", HTTP_GET, cssHandler);

    // load HTML pages
    server.on("/", HTTP_GET, mainHandler);
    server.on("/ssids", HTTP_GET, sendSSID);
    server.on("/statusData", HTTP_GET, sendStatus);
    server.on("/wifiConfig", HTTP_POST, saveWifiHandler);
    server.on("/cloudConfig", HTTP_POST, saveCloudHandler);
    server.on("/machineConfig", HTTP_POST, saveMachineHandler);

    server.on("/restart", HTTP_GET, restartHander);

    server.onNotFound(handleNotFound); // for unhandled cases

    server.begin();
    log_i("HTTP server started");
    timer = millis();

    TaskHandle_t t1;
    xTaskCreatePinnedToCore((void (*)(void *))webTask, "webTask", 8192, NULL, 10, &t1, 0);
}
