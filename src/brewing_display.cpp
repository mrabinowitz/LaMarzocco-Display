#include "brewing_display.h"
#include "water_alarm.h"
#include "ui/ui.h"
#include <Arduino.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Debug output
#define DEBUG_BREWING 1
#if DEBUG_BREWING
#define brewing_debug(x) Serial.print(x)
#define brewing_debugln(x) Serial.println(x)
#else
#define brewing_debug(x)
#define brewing_debugln(x)
#endif

// Global variables
static bool g_initialized = false;
static bool g_is_brewing = false;
static int64_t g_brewing_start_time = 0;
static lv_timer_t* g_update_timer = NULL;
static bool g_timer_paused = true;
static SemaphoreHandle_t g_gui_mutex = NULL;
static bool g_flashing = false;
static unsigned long g_flash_start_time = 0;
static const unsigned long FLASH_DURATION_MS = 3000;  // 3 seconds

// Forward declarations
static void update_elapsed_time(void);
static void show_brewing_ui(void);
static void hide_brewing_ui(void);
static void restore_normal_ui(void);

// Helper macro for mutex protection
#define TAKE_MUTEX() if (g_gui_mutex && xSemaphoreTake(g_gui_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
#define GIVE_MUTEX() if (g_gui_mutex) xSemaphoreGive(g_gui_mutex)

/**
 * Set the GUI mutex for thread-safe LVGL access
 */
void brewing_display_set_mutex(void* mutex) {
    g_gui_mutex = (SemaphoreHandle_t)mutex;
    brewing_debugln("[Brewing] Mutex set for thread-safe operation");
}

/**
 * Initialize the brewing display system
 */
void brewing_display_init(void) {
    if (g_initialized) {
        brewing_debugln("[Brewing] Already initialized");
        return;
    }
    
    brewing_debugln("[Brewing] Initializing brewing display system...");
    
    TAKE_MUTEX() {
        // Hide brewing elements by default
        if (ui_SecPanel) {
            lv_obj_add_flag(ui_SecPanel, LV_OBJ_FLAG_HIDDEN);
        }
        if (ui_SecValueLabel) {
            lv_obj_add_flag(ui_SecValueLabel, LV_OBJ_FLAG_HIDDEN);
        }
        if (ui_SecondsLabel) {
            lv_obj_add_flag(ui_SecondsLabel, LV_OBJ_FLAG_HIDDEN);
        }
        GIVE_MUTEX();
    }
    
    // Create a timer for periodic updates (100ms for smooth time updates)
    g_update_timer = lv_timer_create(brewing_display_timer_callback, 100, NULL);
    lv_timer_pause(g_update_timer);  // Start paused
    g_timer_paused = true;
    
    g_initialized = true;
    g_is_brewing = false;
    g_brewing_start_time = 0;
    g_flashing = false;
    brewing_debugln("[Brewing] Initialization complete");
}

/**
 * Update brewing state based on machine status
 */
void brewing_display_update(bool is_brewing, int64_t brewing_start_time) {
    if (!g_initialized) {
        brewing_debugln("[Brewing] ERROR: Not initialized!");
        return;
    }
    
    // Check if state changed
    bool state_changed = (g_is_brewing != is_brewing);
    
    if (state_changed) {
        brewing_debug("[Brewing] State changed: ");
        brewing_debugln(is_brewing ? "BREWING" : "NOT BREWING");
        
        if (is_brewing) {
            // Starting to brew
            g_is_brewing = true;
            g_brewing_start_time = brewing_start_time;
            g_flashing = false;
            show_brewing_ui();
            
            // Resume timer for updates
            if (g_update_timer && g_timer_paused) {
                lv_timer_resume(g_update_timer);
                g_timer_paused = false;
                brewing_debugln("[Brewing] Timer resumed");
            }
        } else {
            // Stopped brewing - start flashing
            g_is_brewing = false;
            g_flashing = true;
            g_flash_start_time = millis();
            brewing_debugln("[Brewing] Brewing stopped, starting flash effect");
            
            // Keep timer running for flash effect
            if (g_update_timer && g_timer_paused) {
                lv_timer_resume(g_update_timer);
                g_timer_paused = false;
            }
        }
    } else if (is_brewing && brewing_start_time != g_brewing_start_time) {
        // Brewing start time changed (shouldn't happen, but handle it)
        g_brewing_start_time = brewing_start_time;
        brewing_debug("[Brewing] Brewing start time updated: ");
        brewing_debugln((long long)brewing_start_time);
    }
}

/**
 * Timer callback for periodic updates
 */
void brewing_display_timer_callback(lv_timer_t* timer) {
    if (!g_initialized) return;
    
    if (g_is_brewing) {
        // Update elapsed time during brewing
        update_elapsed_time();
    } else if (g_flashing) {
        // Handle flashing effect after brewing stops
        unsigned long elapsed = millis() - g_flash_start_time;
        
        if (elapsed >= FLASH_DURATION_MS) {
            // Flash duration complete - restore normal UI
            g_flashing = false;
            restore_normal_ui();
            
            // Pause timer
            if (g_update_timer && !g_timer_paused) {
                lv_timer_pause(g_update_timer);
                g_timer_paused = true;
                brewing_debugln("[Brewing] Timer paused (brewing complete)");
            }
        } else {
            // Flash effect: toggle visibility every 200ms
            bool visible = (elapsed / 200) % 2 == 0;
            
            TAKE_MUTEX() {
                if (ui_SecPanel) {
                    if (visible) {
                        lv_obj_clear_flag(ui_SecPanel, LV_OBJ_FLAG_HIDDEN);
                    } else {
                        lv_obj_add_flag(ui_SecPanel, LV_OBJ_FLAG_HIDDEN);
                    }
                }
                if (ui_SecValueLabel) {
                    if (visible) {
                        lv_obj_clear_flag(ui_SecValueLabel, LV_OBJ_FLAG_HIDDEN);
                    } else {
                        lv_obj_add_flag(ui_SecValueLabel, LV_OBJ_FLAG_HIDDEN);
                    }
                }
                if (ui_SecondsLabel) {
                    if (visible) {
                        lv_obj_clear_flag(ui_SecondsLabel, LV_OBJ_FLAG_HIDDEN);
                    } else {
                        lv_obj_add_flag(ui_SecondsLabel, LV_OBJ_FLAG_HIDDEN);
                    }
                }
                GIVE_MUTEX();
            }
        }
    }
}

/**
 * Update elapsed time display
 */
static void update_elapsed_time(void) {
    if (g_brewing_start_time <= 0) {
        return;
    }
    
    int64_t now_ms = brewing_display_get_current_time_ms();
    int64_t elapsed_ms = now_ms - g_brewing_start_time;
    
    if (elapsed_ms < 0) {
        // Time hasn't started yet (clock sync issue)
        elapsed_ms = 0;
    }
    
    // Calculate seconds and milliseconds
    int total_seconds = (int)(elapsed_ms / 1000);
    int milliseconds = (int)((elapsed_ms % 1000) / 100);  // Tenths of seconds
    
    // Format as "SS.M" (e.g., "24.3")
    char time_str[16];
    snprintf(time_str, sizeof(time_str), "%d.%d", total_seconds, milliseconds);
    
    TAKE_MUTEX() {
        if (ui_SecValueLabel) {
            lv_label_set_text(ui_SecValueLabel, time_str);
        }
        GIVE_MUTEX();
    }
}

/**
 * Show brewing UI elements and hide normal UI elements
 */
static void show_brewing_ui(void) {
    TAKE_MUTEX() {
        brewing_debugln("[Brewing] Showing brewing UI, hiding normal UI");
        
        // Show brewing elements
        if (ui_SecPanel) {
            lv_obj_clear_flag(ui_SecPanel, LV_OBJ_FLAG_HIDDEN);
        }
        if (ui_SecValueLabel) {
            lv_obj_clear_flag(ui_SecValueLabel, LV_OBJ_FLAG_HIDDEN);
        }
        if (ui_SecondsLabel) {
            lv_obj_clear_flag(ui_SecondsLabel, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Hide power and steam buttons
        if (ui_powerButton) {
            lv_obj_add_flag(ui_powerButton, LV_OBJ_FLAG_HIDDEN);
        }
        if (ui_steamButton) {
            lv_obj_add_flag(ui_steamButton, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Hide both arcs
        if (ui_Arc2) {
            lv_obj_add_flag(ui_Arc2, LV_OBJ_FLAG_HIDDEN);
        }
        if (ui_Arc3) {
            lv_obj_add_flag(ui_Arc3, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Hide coffee and steam labels
        if (ui_CoffeeLabel) {
            lv_obj_add_flag(ui_CoffeeLabel, LV_OBJ_FLAG_HIDDEN);
        }
        if (ui_SteamLabel) {
            lv_obj_add_flag(ui_SteamLabel, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Keep time, wifi, and battery visible (they should already be visible)
        // No need to explicitly show them
        
        GIVE_MUTEX();
    }
}

/**
 * Hide brewing UI elements (called during flash effect)
 */
static void hide_brewing_ui(void) {
    TAKE_MUTEX() {
        if (ui_SecPanel) {
            lv_obj_add_flag(ui_SecPanel, LV_OBJ_FLAG_HIDDEN);
        }
        if (ui_SecValueLabel) {
            lv_obj_add_flag(ui_SecValueLabel, LV_OBJ_FLAG_HIDDEN);
        }
        if (ui_SecondsLabel) {
            lv_obj_add_flag(ui_SecondsLabel, LV_OBJ_FLAG_HIDDEN);
        }
        GIVE_MUTEX();
    }
}

/**
 * Restore normal UI elements after brewing completes
 */
static void restore_normal_ui(void) {
    TAKE_MUTEX() {
        brewing_debugln("[Brewing] Restoring normal UI");
        
        // Hide brewing elements
        hide_brewing_ui();
        
        // Check if water alarm is active - if so, don't show elements that water alarm hides
        bool water_alarm_active = water_alarm_is_active();
        
        // Show power and steam buttons (water alarm doesn't hide these)
        if (ui_powerButton) {
            lv_obj_clear_flag(ui_powerButton, LV_OBJ_FLAG_HIDDEN);
        }
        if (ui_steamButton) {
            lv_obj_clear_flag(ui_steamButton, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Show arcs and labels only if water alarm is not active
        // If water alarm is active, it will manage these elements
        if (!water_alarm_active) {
            if (ui_Arc2) {
                lv_obj_clear_flag(ui_Arc2, LV_OBJ_FLAG_HIDDEN);
            }
            if (ui_Arc3) {
                lv_obj_clear_flag(ui_Arc3, LV_OBJ_FLAG_HIDDEN);
            }
            if (ui_CoffeeLabel) {
                lv_obj_clear_flag(ui_CoffeeLabel, LV_OBJ_FLAG_HIDDEN);
            }
            if (ui_SteamLabel) {
                lv_obj_clear_flag(ui_SteamLabel, LV_OBJ_FLAG_HIDDEN);
            }
        }
        
        GIVE_MUTEX();
    }
}

/**
 * Get current Unix timestamp in milliseconds (GMT/UTC)
 */
int64_t brewing_display_get_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000LL + (int64_t)tv.tv_usec / 1000LL;
}

