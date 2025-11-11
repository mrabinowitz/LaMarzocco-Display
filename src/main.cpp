#include <Arduino.h>
#include <LilyGo_AMOLED.h>
#include <LV_Helper.h>
#include <ui/ui.h>
#include "Preferences.h"
#include "web.h"
#include <WiFi.h>
#include "config.h"
#include "update_screen.h"
#include "lamarzocco_client.h"
#include "lamarzocco_websocket.h"
#include "lamarzocco_machine.h"
#include "lamarzocco_auth.h"
#include "boiler_display.h"

Preferences preferences;
LaMarzoccoClient* g_client = nullptr;
LaMarzoccoWebSocket* g_websocket = nullptr;
LaMarzoccoMachine* g_machine = nullptr;

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

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  delay(500);

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
      
      // Initialize La Marzocco client
      String email = preferences.getString("USER_EMAIL", "");
      String password = preferences.getString("USER_PASS", "");
      String machine_serial = preferences.getString("MACHINE", "");
      
      if (email.length() > 0 && password.length() > 0 && machine_serial.length() > 0) {
        debugln("Initializing La Marzocco client...");
        
        // Check if installation key exists, if not generate it first
        InstallationKey key;
        if (!LaMarzoccoAuth::load_installation_key(preferences, key)) {
          debugln("Generating installation key...");
          
          // Clear any partial keys that might exist (check before removing to avoid errors)
          if (preferences.isKey("INSTALLATION_ID")) preferences.remove("INSTALLATION_ID");
          if (preferences.isKey("INSTALLATION_SECRET")) preferences.remove("INSTALLATION_SECRET");
          if (preferences.isKey("INSTALLATION_PRIVKEY")) preferences.remove("INSTALLATION_PRIVKEY");
          if (preferences.isKey("INSTALLATION_PUBKEY")) preferences.remove("INSTALLATION_PUBKEY");
          if (preferences.isKey("INSTALLATION_PRIVKEY_LEN")) preferences.remove("INSTALLATION_PRIVKEY_LEN");
          if (preferences.isKey("INSTALLATION_PUBKEY_LEN")) preferences.remove("INSTALLATION_PUBKEY_LEN");
          if (preferences.isKey("INST_ID")) preferences.remove("INST_ID");
          if (preferences.isKey("INST_SECRET")) preferences.remove("INST_SECRET");
          if (preferences.isKey("INST_PRIVKEY")) preferences.remove("INST_PRIVKEY");
          if (preferences.isKey("INST_PUBKEY")) preferences.remove("INST_PUBKEY");
          if (preferences.isKey("INST_PRIVLEN")) preferences.remove("INST_PRIVLEN");
          if (preferences.isKey("INST_PUBLEN")) preferences.remove("INST_PUBLEN");
          
          String installation_id = LaMarzoccoAuth::generate_uuid();
          if (LaMarzoccoAuth::generate_installation_key(installation_id, key)) {
            if (LaMarzoccoAuth::save_installation_key(preferences, key)) {
              debugln("Installation key generated and saved");
            } else {
              debugln("Failed to save installation key");
            }
          } else {
            debugln("Failed to generate installation key");
          }
        } else {
          debugln("Installation key found");
        }
        
        g_client = new LaMarzoccoClient(preferences);
        if (g_client->init(email, password, machine_serial)) {
          // Register client if needed
          debugln("Registering client...");
          if (!g_client->register_client()) {
            debugln("Registration failed - will retry on first API call");
            // Note: Registration failures are not critical, will retry during API calls
          }
          
          // Try to get access token (authenticate)
          if (!g_client->get_access_token()) {
            debugln("Authorization failed - invalid credentials");
            

            showNoConnectionScreen(
              "Authorization Failed!\n"
              "Invalid credentials\n"
              "Please restart WiFi Setup"
            );
            
            delete g_client;
            g_client = nullptr;
            setupWEB();
          } else {
            // Initialize websocket and machine
            g_websocket = new LaMarzoccoWebSocket(*g_client);
            g_machine = new LaMarzoccoMachine(*g_client, *g_websocket);
            
            debugln("La Marzocco client initialized");
            
            // Auto-connect WebSocket on startup
            debugln("Auto-connecting to WebSocket...");
            if (g_machine->connect_websocket()) {
              debugln("✓ WebSocket connection initiated on startup");
            } else {
              debugln("✗ Failed to initiate WebSocket connection on startup");
              // Note: WebSocket failures are not critical, will retry automatically
            }
          }
        } else {
          debugln("Failed to initialize La Marzocco client");
          
          showNoConnectionScreen(
            "Client Init Failed!\n"
            "Missing installation key\n"
            "Please restart WiFi Setup"
          );
          
          delete g_client;
          g_client = nullptr;
          setupWEB();
        }
      } else {
        debugln("Missing La Marzocco credentials");
        // Note: Missing credentials is expected on first run, no error message needed
      }
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
  updateDateTime();
  updateStatusImages();  // Update battery and WiFi images (initial + every 30 seconds)
  checkWiFiConnection(); // Monitor WiFi connection and redirect if disconnected
  
  // Handle websocket and machine loop - MUST be called frequently
  // WebSocket requires regular loop() calls to process messages
  if (g_machine) {
    g_machine->loop();  // This calls websocket.loop()
  }
  
  // Small delay to prevent watchdog issues, but keep loop responsive
  delay(10);
  
  // Periodic connection check (every 30 seconds)
  static unsigned long last_check = 0;
  if (millis() - last_check > 30000) {
    last_check = millis();
    if (g_machine) {
      if (g_machine->is_websocket_connected()) {
        Serial.println("[STATUS] WebSocket is connected and running");
      } else {
        Serial.println("[STATUS] WebSocket is NOT connected");
      }
    }
  }
}

void Task_LVGL(void *pvParameters)
{
  beginLvglHelper(amoled);
  ui_init();
  
  // Initialize boiler display system after UI is ready
  boiler_display_init();
  
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
