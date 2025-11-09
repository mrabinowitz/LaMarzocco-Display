#include "ui/ui.h"

void wifiSetup(lv_event_t *e)
{
    lv_label_set_text(ui_SSIDLabel, "SSID: " AP_SSID);
    lv_label_set_text(ui_URLLabel, "URL:  http://" AP_SSID ".local");
    lv_scr_load(ui_setupWifiScreen);
}

void turnOnMachine(lv_event_t * e)
{
    
}