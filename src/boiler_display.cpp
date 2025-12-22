#include "boiler_display.h"
#include "brewing_display.h"
#include "water_alarm.h"  // Need to check water alarm state
#include "ui/ui.h"
#include <Arduino.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Debug output
#define DEBUG_BOILER 1
#if DEBUG_BOILER
#define boiler_debug(x) Serial.print(x)
#define boiler_debugln(x) Serial.println(x)
#else
#define boiler_debug(x)
#define boiler_debugln(x)
#endif

// Global boiler information
static BoilerInfo g_boilers[2];
static lv_timer_t* g_update_timer = NULL;
static bool g_initialized = false;
static bool g_timer_paused = true;  // Track timer pause state manually
static SemaphoreHandle_t g_gui_mutex = NULL;  // Mutex for thread-safe LVGL access

// Forward declarations for helper functions
static void update_arc_and_label(BoilerInfo* boiler, int remaining_seconds);
static int calculate_remaining_seconds(int64_t ready_start_time, int64_t now_ms);
static void set_boiler_off(BoilerInfo* boiler);
static void set_boiler_heating(BoilerInfo* boiler, int64_t ready_start_time);
static void set_boiler_ready(BoilerInfo* boiler);
static void set_boiler_ready_no_mutex(BoilerInfo* boiler);  // Internal version without mutex
static void restart_update_timer(void);
static const char* boiler_type_name(BoilerType type);

// Helper macro for mutex protection
#define TAKE_MUTEX() if (g_gui_mutex && xSemaphoreTake(g_gui_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
#define GIVE_MUTEX() if (g_gui_mutex) xSemaphoreGive(g_gui_mutex)

/**
 * Set the GUI mutex for thread-safe LVGL access
 */
void boiler_display_set_mutex(void* mutex) {
    g_gui_mutex = (SemaphoreHandle_t)mutex;
    boiler_debugln("[Boiler] Mutex set for thread-safe operation");
}

/**
 * Initialize the boiler display system
 */
void boiler_display_init(void) {
    if (g_initialized) {
        boiler_debugln("[Boiler] Already initialized");
        return;
    }
    
    boiler_debugln("[Boiler] Initializing boiler display system...");
    
    // Initialize Coffee Boiler (left arc)
    g_boilers[BOILER_COFFEE].type = BOILER_COFFEE;
    g_boilers[BOILER_COFFEE].arc = ui_Arc2;
    g_boilers[BOILER_COFFEE].label = ui_CoffeeLabel;
    g_boilers[BOILER_COFFEE].ready_start_time = 0;
    g_boilers[BOILER_COFFEE].state = BOILER_STATE_OFF;
    g_boilers[BOILER_COFFEE].last_remaining_sec = -1;
    
    // Initialize Steam Boiler (right arc)
    g_boilers[BOILER_STEAM].type = BOILER_STEAM;
    g_boilers[BOILER_STEAM].arc = ui_Arc3;
    g_boilers[BOILER_STEAM].label = ui_SteamLabel;
    g_boilers[BOILER_STEAM].ready_start_time = 0;
    g_boilers[BOILER_STEAM].state = BOILER_STATE_OFF;
    g_boilers[BOILER_STEAM].last_remaining_sec = -1;
    
    // Set both boilers to OFF state initially
    set_boiler_off(&g_boilers[BOILER_COFFEE]);
    set_boiler_off(&g_boilers[BOILER_STEAM]);
    
    // Create a timer for periodic updates (starts at 30 seconds, will be adjusted dynamically)
    g_update_timer = lv_timer_create(boiler_display_timer_callback, 30000, NULL);
    lv_timer_pause(g_update_timer);  // Start paused, will be enabled when needed
    g_timer_paused = true;
    
    g_initialized = true;
    boiler_debugln("[Boiler] Initialization complete");
}

/**
 * Update a specific boiler's status based on machine state and ready start time
 * 
 * Logic:
 * - Machine OFF/StandBy → Display "OFF", arc at 0%
 * - Machine ON + readyStartTime is null/0 → Display "READY", arc at 100% (already ready)
 * - Machine ON + readyStartTime is valid → Display countdown, arc shows progress
 */
void boiler_display_update(BoilerType type, const char* machine_status, 
                           const char* boiler_status, int64_t ready_start_time,
                           const char* target_value) {
    if (!g_initialized) {
        boiler_debugln("[Boiler] ERROR: Not initialized!");
        return;
    }
    
    // Update temperature/level label (with mutex protection)
    if (target_value != NULL && strlen(target_value) > 0) {
        TAKE_MUTEX() {
            if (type == BOILER_COFFEE) {
                // Coffee boiler: display temperature (e.g., "94°C")
                lv_label_set_text(ui_CoffeeTempLabel, target_value);
                boiler_debug("[Coffee] Target temp: ");
                boiler_debugln(target_value);
            } else if (type == BOILER_STEAM) {
                // Steam boiler: display level (e.g., "L2" for Level2)
                lv_label_set_text(ui_BoilerTempLabel, target_value);
                boiler_debug("[Steam] Target level: ");
                boiler_debugln(target_value);
            }
            GIVE_MUTEX();
        }
    }
    
    if (type >= 2) {
        boiler_debugln("[Boiler] ERROR: Invalid boiler type!");
        return;
    }
    
    BoilerInfo* boiler = &g_boilers[type];
    
    boiler_debug("[");
    boiler_debug(boiler_type_name(type));
    boiler_debug("] Update - Machine: ");
    boiler_debug(machine_status);
    boiler_debug(", Boiler: ");
    boiler_debug(boiler_status);
    boiler_debug(", TargetReadyTime: ");
    boiler_debugln((long long)ready_start_time);
    
    // If we have a valid ready time, show when it will be ready in local timezone
    // Note: ready_start_time is the timestamp when boiler WILL BE ready (target time)
    if (ready_start_time > 0) {
        time_t ready_time_sec = (time_t)(ready_start_time / 1000);  // Convert ms to seconds
        struct tm timeinfo;
        localtime_r(&ready_time_sec, &timeinfo);  // Convert to local timezone
        
        char time_str[32];
        strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);
        boiler_debug("  ✓ Ready at: ");
        boiler_debug(time_str);
        boiler_debug(" local time");
        boiler_debugln();
    }
    
    // Check machine status first
    if (strcmp(machine_status, "Off") == 0 || strcmp(machine_status, "StandBy") == 0) {
        // Machine is OFF or in StandBy - set boiler to OFF
        if (boiler->state != BOILER_STATE_OFF) {
            boiler_debug("[");
            boiler_debug(boiler_type_name(type));
            boiler_debugln("] -> OFF (machine off/standby)");
            set_boiler_off(boiler);
            restart_update_timer();  // Recalculate timer period
        }
        return;
    }
    
    // Check if boiler itself is OFF or StandBy (e.g., steam boiler disabled while machine is on)
    if (strcmp(boiler_status, "Off") == 0 || strcmp(boiler_status, "StandBy") == 0) {
        // Boiler is disabled - set to OFF
        if (boiler->state != BOILER_STATE_OFF) {
            boiler_debug("[");
            boiler_debug(boiler_type_name(type));
            boiler_debugln("] -> OFF (boiler disabled)");
            set_boiler_off(boiler);
            restart_update_timer();  // Recalculate timer period
        }
        return;
    }
    
    // Check if boiler status is explicitly "Ready"
    if (strcmp(boiler_status, "Ready") == 0) {
        // Boiler is READY - set to READY state immediately
        if (boiler->state != BOILER_STATE_READY) {
            boiler_debug("[");
            boiler_debug(boiler_type_name(type));
            boiler_debugln("] -> READY (status is Ready)");
            set_boiler_ready(boiler);
            restart_update_timer();
        } else {
            // Already in READY state, but force update display to ensure it's current
            boiler_debug("[");
            boiler_debug(boiler_type_name(type));
            boiler_debugln("] Already READY - forcing display update (status is Ready)");
            set_boiler_ready(boiler);  // Force update display
        }
        return;
    }
    
    // Machine is ON (PoweredOn, BrewingMode, etc.) and boiler is enabled
    if (ready_start_time <= 0) {
        // No valid ready start time - machine is ON but boiler is already READY (not heating)
        if (boiler->state != BOILER_STATE_READY) {
            boiler_debug("[");
            boiler_debug(boiler_type_name(type));
            boiler_debugln("] -> READY (no heating needed)");
            set_boiler_ready(boiler);
            restart_update_timer();
        } else {
            // Already in READY state, but force update display to ensure it's current
            // This ensures the display updates even if it was showing old countdown values
            boiler_debug("[");
            boiler_debug(boiler_type_name(type));
            boiler_debugln("] Already READY - forcing display update (callback received)");
            set_boiler_ready(boiler);  // Force update display
        }
        return;
    }
    
    // Calculate remaining time
    int64_t now_ms = boiler_display_get_current_time_ms();
    int remaining_sec = calculate_remaining_seconds(ready_start_time, now_ms);
    
    boiler_debug("[");
    boiler_debug(boiler_type_name(type));
    boiler_debug("] Remaining: ");
    boiler_debug(remaining_sec);
    boiler_debugln(" sec");
    
    if (remaining_sec <= 0) {
        // Boiler is READY
        if (boiler->state != BOILER_STATE_READY) {
            boiler_debug("[");
            boiler_debug(boiler_type_name(type));
            boiler_debugln("] -> READY");
            set_boiler_ready(boiler);
            restart_update_timer();
        } else {
            // Already in READY state, but force update display to ensure it's current
            // Always update even if already READY to catch any stale display values
            boiler_debug("[");
            boiler_debug(boiler_type_name(type));
            boiler_debugln("] Already READY - forcing display update (callback received)");
            set_boiler_ready(boiler);  // Force update display
        }
    } else {
        // Boiler is HEATING
        if (boiler->state != BOILER_STATE_HEATING || boiler->ready_start_time != ready_start_time) {
            boiler_debug("[");
            boiler_debug(boiler_type_name(type));
            boiler_debugln("] -> HEATING");
            set_boiler_heating(boiler, ready_start_time);
            restart_update_timer();
        } else {
            // Update the display with current countdown
            // Always update to ensure display stays current even if value hasn't changed
            update_arc_and_label(boiler, remaining_sec);
        }
    }
}

/**
 * Set all boilers to OFF
 */
void boiler_display_set_all_off(void) {
    if (!g_initialized) return;
    
    boiler_debugln("[Boiler] Setting all boilers to OFF");
    set_boiler_off(&g_boilers[BOILER_COFFEE]);
    set_boiler_off(&g_boilers[BOILER_STEAM]);
    
    // Pause the timer when everything is off
    if (g_update_timer && !g_timer_paused) {
        lv_timer_pause(g_update_timer);
        g_timer_paused = true;
    }
}

/**
 * Get current Unix timestamp in milliseconds (GMT/UTC)
 * Note: This returns GMT time regardless of timezone configuration
 * The timezone offset is only used for display purposes via getLocalTime()
 */
int64_t boiler_display_get_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    // gettimeofday returns seconds since Unix epoch (1970-01-01 00:00:00 UTC)
    // This is always in GMT/UTC, timezone doesn't affect this value
    return (int64_t)tv.tv_sec * 1000LL + (int64_t)tv.tv_usec / 1000LL;
}

/**
 * Timer callback for periodic updates
 */
void boiler_display_timer_callback(lv_timer_t* timer) {
    if (!g_initialized) return;
    
    boiler_debugln("[Boiler] Timer callback - updating all boilers");
    
    int64_t now_ms = boiler_display_get_current_time_ms();
    bool any_heating = false;
    
    // Update each boiler that is in HEATING or READY state
    for (int i = 0; i < 2; i++) {
        BoilerInfo* boiler = &g_boilers[i];
        
        if (boiler->state == BOILER_STATE_HEATING) {
            int remaining_sec = calculate_remaining_seconds(boiler->ready_start_time, now_ms);
            
            if (remaining_sec <= 0) {
                // Transition to READY
                boiler_debug("[");
                boiler_debug(boiler_type_name(boiler->type));
                boiler_debugln("] Timer: -> READY");
                // Use no_mutex version since we're in timer callback (LVGL task context)
                set_boiler_ready_no_mutex(boiler);
            } else {
                // Update countdown display
                update_arc_and_label(boiler, remaining_sec);
                any_heating = true;
            }
        } else if (boiler->state == BOILER_STATE_READY) {
            // Force periodic refresh of READY state to ensure display is current
            // This helps catch any missed updates from WebSocket callbacks
            boiler->last_remaining_sec = -1;  // Force update by resetting last value
            // Use no_mutex version since we're in timer callback (LVGL task context)
            set_boiler_ready_no_mutex(boiler);  // Refresh display
        }
    }
    
    // Restart timer with appropriate period
    // Keep timer running even if only READY boilers exist, but at slower rate
    bool any_ready = false;
    for (int i = 0; i < 2; i++) {
        if (g_boilers[i].state == BOILER_STATE_READY) {
            any_ready = true;
            break;
        }
    }
    
    if (any_heating) {
        restart_update_timer();
    } else if (any_ready) {
        // Keep timer running at slow rate (5 seconds) to refresh READY state
        lv_timer_set_period(g_update_timer, 5000);
        if (g_timer_paused) {
            lv_timer_resume(g_update_timer);
            g_timer_paused = false;
            boiler_debugln("[Boiler] Timer resumed for READY state refresh (5s)");
        }
    } else {
        // No boilers active, pause timer
        boiler_debugln("[Boiler] No boilers active, pausing timer");
        if (!g_timer_paused) {
            lv_timer_pause(g_update_timer);
            g_timer_paused = true;
        }
    }
}

// ============================================================================
// HELPER FUNCTIONS (PRIVATE)
// ============================================================================

/**
 * Calculate remaining seconds until boiler is ready
 * Both ready_start_time and now_ms are GMT Unix timestamps in milliseconds
 * 
 * IMPORTANT: ready_start_time is when the boiler WILL BE ready (not when it started heating)
 * 
 * @param ready_start_time Time when boiler will be ready in milliseconds GMT
 * @param now_ms Current time in milliseconds GMT
 * @return Remaining seconds (0 or negative means ready)
 */
static int calculate_remaining_seconds(int64_t ready_start_time, int64_t now_ms) {
    // Both timestamps are in GMT, so direct comparison is valid
    // Calculate remaining time until ready
    int64_t remaining_ms = ready_start_time - now_ms;
    int remaining_sec = (int)(remaining_ms / 1000);
    
    // Calculate for display
    int remaining_min = remaining_sec / 60;
    int remaining_sec_part = remaining_sec % 60;
    
    #if DEBUG_BOILER
    if (remaining_sec > 0) {
        boiler_debug("  [Time calc: Remaining ");
        boiler_debug(remaining_min);
        boiler_debug(" min ");
        boiler_debug(remaining_sec_part);
        boiler_debugln(" sec]");
    }
    #endif
    
    return remaining_sec;
}

/**
 * Update arc and label based on remaining seconds
 * 
 * @param boiler Boiler info structure
 * @param remaining_seconds Remaining seconds until ready
 */
static void update_arc_and_label(BoilerInfo* boiler, int remaining_seconds) {
    if (!boiler || !boiler->arc || !boiler->label) return;
    
    // Only update if value changed significantly (avoid flicker)
    // But allow updates when transitioning to/from ready state
    // Also allow updates when last_remaining_sec is 0 (READY state) to force refresh
    bool force_update = (boiler->last_remaining_sec == -1) || 
                        (remaining_seconds <= 0 && boiler->last_remaining_sec > 0) ||
                        (remaining_seconds > 0 && boiler->last_remaining_sec <= 0) ||
                        (boiler->last_remaining_sec == 0 && remaining_seconds <= 0);  // Force refresh when READY
    
    // Always update if value changed, or if we're forcing an update
    // For callbacks, we want to ensure display is always current, so we update even if value hasn't changed
    // This helps catch any display sync issues where the screen shows stale values
    if (!force_update && boiler->last_remaining_sec == remaining_seconds) {
        // Still update to ensure display is current (helps with stale "6 sec" or "9 sec" displays)
        // This is especially important when callbacks are received for READY boilers
        force_update = true;
    }
    
    boiler->last_remaining_sec = remaining_seconds;
    
    // Calculate arc value (100% at start, 0% at end)
    // Arc represents time REMAINING, so it decreases as time passes
    // Note: We use WARMUP_DURATION_SEC (300s) as the assumed max duration
    // The arc will be accurate if actual warmup is ~5 minutes
    int arc_value = (remaining_seconds * 100) / WARMUP_DURATION_SEC;
    if (arc_value < 0) arc_value = 0;
    if (arc_value > 100) arc_value = 100;
    
    // Update label text
    char label_text[16];
    if (remaining_seconds > 60) {
        // Display as minutes (rounded up)
        int minutes = (remaining_seconds + 59) / 60;  // Round up
        snprintf(label_text, sizeof(label_text), "%d min", minutes);
    } else if (remaining_seconds > 0) {
        // Display as seconds
        snprintf(label_text, sizeof(label_text), "%d sec", remaining_seconds);
    } else {
        // Ready
        snprintf(label_text, sizeof(label_text), "READY");
    }
    
    // Update arc and label with mutex protection
    TAKE_MUTEX() {
        // Check if brewing OR water alarm is active - if so, keep arcs and labels hidden
        bool brewing_active = brewing_display_is_active();
        bool water_alarm_active = water_alarm_is_active();
        
        if (!brewing_active && !water_alarm_active) {
            // Only show arcs and labels if BOTH brewing AND water alarm are NOT active
            lv_obj_clear_flag(boiler->arc, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(boiler->label, LV_OBJ_FLAG_HIDDEN);
        } else {
            // Brewing or water alarm is active - keep them hidden
            lv_obj_add_flag(boiler->arc, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(boiler->label, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Update values even if hidden (so they're correct when shown later)
        lv_arc_set_value(boiler->arc, arc_value);
        lv_label_set_text(boiler->label, label_text);
        
        // Force a refresh by invalidating the objects to ensure display updates
        lv_obj_invalidate(boiler->arc);
        lv_obj_invalidate(boiler->label);
        GIVE_MUTEX();
    }
    
    boiler_debug("[");
    boiler_debug(boiler_type_name(boiler->type));
    boiler_debug("] Display: ");
    boiler_debug(label_text);
    boiler_debug(" (arc: ");
    boiler_debug(arc_value);
    boiler_debugln("%)");
}

/**
 * Set boiler to OFF state
 */
static void set_boiler_off(BoilerInfo* boiler) {
    if (!boiler || !boiler->arc || !boiler->label) return;
    
    boiler->state = BOILER_STATE_OFF;
    boiler->ready_start_time = 0;
    boiler->last_remaining_sec = -1;
    
    // Set arc to 0% and label to "OFF" with mutex protection
    TAKE_MUTEX() {
        // Check if brewing OR water alarm is active - if so, keep arcs and labels hidden
        bool brewing_active = brewing_display_is_active();
        bool water_alarm_active = water_alarm_is_active();
        
        if (!brewing_active && !water_alarm_active) {
            // Only show arcs and labels if BOTH brewing AND water alarm are NOT active
            lv_obj_clear_flag(boiler->arc, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(boiler->label, LV_OBJ_FLAG_HIDDEN);
        } else {
            // Brewing or water alarm is active - keep them hidden
            lv_obj_add_flag(boiler->arc, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(boiler->label, LV_OBJ_FLAG_HIDDEN);
        }
        
        // Update values even if hidden (so they're correct when shown later)
        lv_arc_set_value(boiler->arc, 0);
        lv_label_set_text(boiler->label, "OFF");
        
        // Invalidate to ensure display updates
        lv_obj_invalidate(boiler->arc);
        lv_obj_invalidate(boiler->label);
        GIVE_MUTEX();
    }
}

/**
 * Set boiler to HEATING state
 */
static void set_boiler_heating(BoilerInfo* boiler, int64_t ready_start_time) {
    if (!boiler || !boiler->arc || !boiler->label) return;
    
    boiler->state = BOILER_STATE_HEATING;
    boiler->ready_start_time = ready_start_time;
    boiler->last_remaining_sec = -1;  // Force update on next call
    
    // Calculate initial remaining time and update display
    int64_t now_ms = boiler_display_get_current_time_ms();
    int remaining_sec = calculate_remaining_seconds(ready_start_time, now_ms);
    update_arc_and_label(boiler, remaining_sec);
    
    // Resume timer if it was paused
    if (g_update_timer && g_timer_paused) {
        lv_timer_resume(g_update_timer);
        g_timer_paused = false;
        boiler_debugln("[Boiler] Timer resumed");
    }
}

/**
 * Set boiler to READY state (internal version without mutex)
 * MUST be called from within LVGL task or with mutex already held
 */
static void set_boiler_ready_no_mutex(BoilerInfo* boiler) {
    if (!boiler || !boiler->arc || !boiler->label) {
        boiler_debug("[");
        boiler_debug(boiler_type_name(boiler->type));
        boiler_debugln("] ERROR: NULL objects in set_boiler_ready!");
        return;
    }
    
    boiler->state = BOILER_STATE_READY;
    boiler->last_remaining_sec = -1;
    
    // Check if brewing OR water alarm is active - if so, keep arcs and labels hidden
    bool brewing_active = brewing_display_is_active();
    bool water_alarm_active = water_alarm_is_active();
    
    if (!brewing_active && !water_alarm_active) {
        // Only show arcs and labels if BOTH brewing AND water alarm are NOT active
        lv_obj_clear_flag(boiler->arc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(boiler->label, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Brewing or water alarm is active - keep them hidden
        lv_obj_add_flag(boiler->arc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(boiler->label, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Update values even if hidden (so they're correct when shown later)
    lv_arc_set_value(boiler->arc, 100);
    lv_label_set_text(boiler->label, "READY");
    
    // Force a refresh by invalidating the objects
    lv_obj_invalidate(boiler->arc);
    lv_obj_invalidate(boiler->label);
    
    // Update last_remaining_sec to 0 to track READY state (0 seconds remaining)
    boiler->last_remaining_sec = 0;
    
    boiler_debug("[");
    boiler_debug(boiler_type_name(boiler->type));
    boiler_debugln("] Display updated to READY");
}

/**
 * Set boiler to READY state (public version with mutex)
 */
static void set_boiler_ready(BoilerInfo* boiler) {
    if (!boiler || !boiler->arc || !boiler->label) {
        boiler_debug("[");
        boiler_debug(boiler_type_name(boiler->type));
        boiler_debugln("] ERROR: NULL objects in set_boiler_ready!");
        return;
    }
    
    TAKE_MUTEX() {
        set_boiler_ready_no_mutex(boiler);
        GIVE_MUTEX();
    }
}

/**
 * Restart update timer with appropriate period based on current boiler states
 */
static void restart_update_timer(void) {
    if (!g_update_timer) return;
    
    // Check if any boiler needs frequent updates (< 60 seconds remaining)
    bool needs_fast_update = false;
    int64_t now_ms = boiler_display_get_current_time_ms();
    
    for (int i = 0; i < 2; i++) {
        BoilerInfo* boiler = &g_boilers[i];
        if (boiler->state == BOILER_STATE_HEATING) {
            int remaining_sec = calculate_remaining_seconds(boiler->ready_start_time, now_ms);
            if (remaining_sec > 0 && remaining_sec < 60) {
                needs_fast_update = true;
                break;
            }
        }
    }
    
    // Set timer period based on state
    uint32_t period_ms;
    if (needs_fast_update) {
        period_ms = 1000;  // 1 second when < 60 seconds remaining
        boiler_debugln("[Boiler] Timer set to 1 second (fast updates)");
    } else {
        period_ms = 30000;  // 30 seconds otherwise
        boiler_debugln("[Boiler] Timer set to 30 seconds (slow updates)");
    }
    
    lv_timer_set_period(g_update_timer, period_ms);
    
    // Resume timer if it's paused and there's something to update
    bool any_active = false;
    for (int i = 0; i < 2; i++) {
        if (g_boilers[i].state != BOILER_STATE_OFF) {
            any_active = true;
            break;
        }
    }
    
    if (any_active && g_timer_paused) {
        lv_timer_resume(g_update_timer);
        g_timer_paused = false;
        boiler_debugln("[Boiler] Timer resumed");
    } else if (!any_active && !g_timer_paused) {
        lv_timer_pause(g_update_timer);
        g_timer_paused = true;
        boiler_debugln("[Boiler] Timer paused (no active boilers)");
    }
}

/**
 * Get boiler type name for debugging
 */
static const char* boiler_type_name(BoilerType type) {
    return (type == BOILER_COFFEE) ? "Coffee" : "Steam";
}

