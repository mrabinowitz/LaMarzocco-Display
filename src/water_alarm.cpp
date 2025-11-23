#include "water_alarm.h"
#include "ui/ui.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Debug output
#define DEBUG_WATER 1
#if DEBUG_WATER
#define water_debug(x) Serial.print(x)
#define water_debugln(x) Serial.println(x)
#else
#define water_debug(x)
#define water_debugln(x)
#endif

// Global variables
static bool g_initialized = false;
static bool g_alarm_active = false;
static SemaphoreHandle_t g_gui_mutex = NULL;

// Helper macro for mutex protection
#define TAKE_MUTEX() if (g_gui_mutex && xSemaphoreTake(g_gui_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
#define GIVE_MUTEX() if (g_gui_mutex) xSemaphoreGive(g_gui_mutex)


/**
 * Set the GUI mutex for thread-safe LVGL access
 */
void water_alarm_set_mutex(void* mutex) {
    g_gui_mutex = (SemaphoreHandle_t)mutex;
    water_debugln("[WaterAlarm] Mutex set for thread-safe operation");
}

/**
 * Initialize the water alarm display system
 */
void water_alarm_init(void) {
    if (g_initialized) {
        water_debugln("[WaterAlarm] Already initialized");
        return;
    }
    
    water_debugln("[WaterAlarm] Initializing water alarm system...");
    
    TAKE_MUTEX() {
        // Hide water alarm elements by default (they are created in SquareLine Studio)
        if (ui_waterImage) {
            lv_obj_add_flag(ui_waterImage, LV_OBJ_FLAG_HIDDEN);
        }
        if (ui_waterAlarmLabel) {
            lv_obj_add_flag(ui_waterAlarmLabel, LV_OBJ_FLAG_HIDDEN);
        }
        
        GIVE_MUTEX();
    }
    
    g_initialized = true;
    g_alarm_active = false;
    water_debugln("[WaterAlarm] Initialization complete");
}

/**
 * Set water alarm state
 */
void water_alarm_set(bool alarm_active) {
    if (!g_initialized) {
        water_debugln("[WaterAlarm] ERROR: Not initialized!");
        return;
    }
    
    // Only update if state changed
    if (g_alarm_active == alarm_active) {
        return;
    }
    
    g_alarm_active = alarm_active;
    
    water_debug("[WaterAlarm] Setting alarm state to: ");
    water_debugln(alarm_active ? "ACTIVE" : "INACTIVE");
    
    TAKE_MUTEX() {
        if (alarm_active) {
            // ALARM ACTIVE: Show water elements, hide boiler elements
            water_debugln("[WaterAlarm] Showing water alarm, hiding boiler elements");
            
            // Show water alarm elements from SquareLine Studio (waterImage and waterAlarmLabel)
            if (ui_waterImage) {
                lv_obj_clear_flag(ui_waterImage, LV_OBJ_FLAG_HIDDEN);
            }
            if (ui_waterAlarmLabel) {
                lv_obj_clear_flag(ui_waterAlarmLabel, LV_OBJ_FLAG_HIDDEN);
            }
            
            // Hide boiler arcs
            if (ui_Arc2) {
                lv_obj_add_flag(ui_Arc2, LV_OBJ_FLAG_HIDDEN);
            }
            if (ui_Arc3) {
                lv_obj_add_flag(ui_Arc3, LV_OBJ_FLAG_HIDDEN);
            }
            
            // Hide coffee and steam images
            if (ui_CoffeeImage) {
                lv_obj_add_flag(ui_CoffeeImage, LV_OBJ_FLAG_HIDDEN);
            }
            if (ui_SteamImage) {
                lv_obj_add_flag(ui_SteamImage, LV_OBJ_FLAG_HIDDEN);
            }
            
            // Hide coffee and steam labels
            if (ui_CoffeeLabel) {
                lv_obj_add_flag(ui_CoffeeLabel, LV_OBJ_FLAG_HIDDEN);
            }
            if (ui_SteamLabel) {
                lv_obj_add_flag(ui_SteamLabel, LV_OBJ_FLAG_HIDDEN);
            }
            
            // Hide temperature labels
            if (ui_CoffeeTempLabel) {
                lv_obj_add_flag(ui_CoffeeTempLabel, LV_OBJ_FLAG_HIDDEN);
            }
            if (ui_BoilerTempLabel) {
                lv_obj_add_flag(ui_BoilerTempLabel, LV_OBJ_FLAG_HIDDEN);
            }
            
        } else {
            // ALARM INACTIVE: Hide water elements, show boiler elements
            water_debugln("[WaterAlarm] Hiding water alarm, showing boiler elements");
            
            // Hide water alarm elements
            if (ui_waterImage) {
                lv_obj_add_flag(ui_waterImage, LV_OBJ_FLAG_HIDDEN);
            }
            if (ui_waterAlarmLabel) {
                lv_obj_add_flag(ui_waterAlarmLabel, LV_OBJ_FLAG_HIDDEN);
            }
            
            // Show boiler arcs
            if (ui_Arc2) {
                lv_obj_clear_flag(ui_Arc2, LV_OBJ_FLAG_HIDDEN);
            }
            if (ui_Arc3) {
                lv_obj_clear_flag(ui_Arc3, LV_OBJ_FLAG_HIDDEN);
            }
            
            // Show coffee and steam images
            if (ui_CoffeeImage) {
                lv_obj_clear_flag(ui_CoffeeImage, LV_OBJ_FLAG_HIDDEN);
            }
            if (ui_SteamImage) {
                lv_obj_clear_flag(ui_SteamImage, LV_OBJ_FLAG_HIDDEN);
            }
            
            // Show coffee and steam labels
            if (ui_CoffeeLabel) {
                lv_obj_clear_flag(ui_CoffeeLabel, LV_OBJ_FLAG_HIDDEN);
            }
            if (ui_SteamLabel) {
                lv_obj_clear_flag(ui_SteamLabel, LV_OBJ_FLAG_HIDDEN);
            }
            
            // Show temperature labels
            if (ui_CoffeeTempLabel) {
                lv_obj_clear_flag(ui_CoffeeTempLabel, LV_OBJ_FLAG_HIDDEN);
            }
            if (ui_BoilerTempLabel) {
                lv_obj_clear_flag(ui_BoilerTempLabel, LV_OBJ_FLAG_HIDDEN);
            }
        }
        
        GIVE_MUTEX();
    }
}

/**
 * Get current water alarm state
 */
bool water_alarm_is_active(void) {
    return g_alarm_active;
}

