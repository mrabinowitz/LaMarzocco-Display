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
static lv_obj_t* g_water_image = NULL;
static lv_obj_t* g_water_label = NULL;
static bool g_initialized = false;
static bool g_alarm_active = false;
static SemaphoreHandle_t g_gui_mutex = NULL;

// Helper macro for mutex protection
#define TAKE_MUTEX() if (g_gui_mutex && xSemaphoreTake(g_gui_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
#define GIVE_MUTEX() if (g_gui_mutex) xSemaphoreGive(g_gui_mutex)

// Forward declarations
LV_IMG_DECLARE(ui_img_drop_2_png);

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
        // Create water drop image on main screen
        g_water_image = lv_img_create(ui_mainScreen);
        lv_img_set_src(g_water_image, &ui_img_drop_2_png);
        lv_obj_set_width(g_water_image, LV_SIZE_CONTENT);
        lv_obj_set_height(g_water_image, LV_SIZE_CONTENT);
        lv_obj_set_align(g_water_image, LV_ALIGN_CENTER);
        lv_obj_set_x(g_water_image, 0);
        lv_obj_set_y(g_water_image, -20);  // Slightly above center
        lv_obj_add_flag(g_water_image, LV_OBJ_FLAG_HIDDEN);  // Hidden by default
        
        // Create water alarm label below the image
        g_water_label = lv_label_create(ui_mainScreen);
        lv_label_set_text(g_water_label, "NO WATER");
        lv_obj_set_width(g_water_label, LV_SIZE_CONTENT);
        lv_obj_set_height(g_water_label, LV_SIZE_CONTENT);
        lv_obj_set_align(g_water_label, LV_ALIGN_CENTER);
        lv_obj_set_x(g_water_label, 0);
        lv_obj_set_y(g_water_label, 40);  // Below the water drop
        lv_obj_set_style_text_color(g_water_label, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);  // Red text
        lv_obj_set_style_text_font(g_water_label, &lv_font_montserrat_20, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(g_water_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_flag(g_water_label, LV_OBJ_FLAG_HIDDEN);  // Hidden by default
        
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
            
            // Show water alarm elements
            if (g_water_image) {
                lv_obj_clear_flag(g_water_image, LV_OBJ_FLAG_HIDDEN);
            }
            if (g_water_label) {
                lv_obj_clear_flag(g_water_label, LV_OBJ_FLAG_HIDDEN);
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
            
            // Note: Keep labels visible so user can still see target temps/levels
            // Only hide the arcs and logos
            
        } else {
            // ALARM INACTIVE: Hide water elements, show boiler elements
            water_debugln("[WaterAlarm] Hiding water alarm, showing boiler elements");
            
            // Hide water alarm elements
            if (g_water_image) {
                lv_obj_add_flag(g_water_image, LV_OBJ_FLAG_HIDDEN);
            }
            if (g_water_label) {
                lv_obj_add_flag(g_water_label, LV_OBJ_FLAG_HIDDEN);
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

