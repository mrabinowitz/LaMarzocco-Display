#include <Arduino.h>
#include <LilyGo_AMOLED.h>
#include <LV_Helper.h>
#include <ui/ui.h>
#include "Preferences.h"
#include "web.h"
#include <WiFi.h>
#include "config.h"

Preferences preferences;

LilyGo_Class amoled;

SemaphoreHandle_t gui_mutex;
void Task_LVGL(void *pvParameters);

// WiFi connection variables
const int MAX_WIFI_RETRIES = 10;
const int WIFI_TIMEOUT_MS = 15000;

bool connectToWiFi(const String &ssid, const String &password)
{
  debugln("Attempting to connect to WiFi...");
  WiFi.begin(ssid.c_str(), password.c_str());
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < MAX_WIFI_RETRIES)
  {
    delay(WIFI_TIMEOUT_MS / MAX_WIFI_RETRIES);
    debug(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    debugln("");
    debugln("WiFi connected!");
    debug("IP address: ");
    debugln(WiFi.localIP());
    return true;
  }
  else
  {
    debugln("");
    debugln("Failed to connect to WiFi");
    WiFi.disconnect();
    return false;
  }
}

void setup()
{
  Serial.begin(115200);
  preferences.begin("config", false);

  bool rslt = false;

  // Automatically determine the access device
  rslt = amoled.begin();

  if (!rslt)
  {
    while (1)
    {
      debug("The board model cannot be detected, please raise the Core Debug Level to an error");
      delay(1000);
    }
  }

  gui_mutex = xSemaphoreCreateMutex();
  if (gui_mutex == NULL)
  {
    // Handle semaphore creation failure
    log_i("gui_mutex semaphore creation failure");
    return;
  }

  xTaskCreatePinnedToCore(Task_LVGL,
                          "Task_LVGL",
                          1024 * 10,
                          NULL,
                          3,
                          NULL,
                          0);

  delay(1000);

  String ssid = preferences.getString("SSID", "");
  String pass = preferences.getString("PASS", "");
  if (ssid == "" || pass == "")
  {
    debugln("No WiFi credentials found, starting WiFi setup");
    lv_disp_load_scr(ui_NoConnectionScreen);
    setupWEB();
  }
  else
  {
    debugln("Found WiFi credentials");
    if (connectToWiFi(ssid, pass))
    {
      lv_disp_load_scr(ui_mainScreen);
    }
    else
    {
      debugln("WiFi connection failed after retries, starting WiFi setup");
      lv_disp_load_scr(ui_NoConnectionScreen);
      setupWEB();
    }
  }
}

void loop()
{
}

void Task_LVGL(void *pvParameters)
{
  beginLvglHelper(amoled);
  ui_init();
  // Main LVGL loop
  while (1)
  {
    if (xSemaphoreTake(gui_mutex, portMAX_DELAY) == pdTRUE)
    {
      lv_timer_handler();
      xSemaphoreGive(gui_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
