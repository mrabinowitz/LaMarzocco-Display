#include "ui/ui.h"
#include "lamarzocco_machine.h"

extern LaMarzoccoMachine* g_machine;

void wifiSetup(lv_event_t *e)
{
    lv_label_set_text(ui_SSIDLabel, "SSID: " AP_SSID);
    lv_label_set_text(ui_URLLabel, "URL:  http://" AP_SSID ".local");
    lv_scr_load(ui_setupWifiScreen);
}

void turnOnMachine(lv_event_t * e)
{
  // Get the machine control instance
  if (g_machine) {
    Serial.println("===========================================");
    Serial.println("BUTTON PRESSED - Processing...");
    Serial.println("===========================================");
    
    // Check current websocket status
    if (g_machine->is_websocket_connected()) {
      Serial.println("✓ WebSocket is already connected");
    } else {
      Serial.println("⚠ WebSocket not connected, attempting to connect...");
      bool connected = g_machine->connect_websocket();
      if (connected) {
        Serial.println("✓ WebSocket connection initiated");
        // Give it a moment to establish
        delay(1000);
      } else {
        Serial.println("✗ Failed to initiate WebSocket connection");
      }
    }
    
    // Toggle the power
    Serial.println("\nToggling machine power...");
    bool success = g_machine->toggle_power();
    if (success) {
      Serial.println("✓ Power toggle command sent successfully");
      Serial.println("Check WebSocket messages below for confirmation...");
    } else {
      Serial.println("✗ Failed to send power toggle command");
    }
    
    Serial.println("===========================================\n");
  } else {
    Serial.println("ERROR: g_machine is null!");
  }
}