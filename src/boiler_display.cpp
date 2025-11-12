#include "boiler_display.h"
#include "ui/ui.h"
#include <Arduino.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

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

// Forward declarations for helper functions
static void update_arc_and_label(BoilerInfo* boiler, int remaining_seconds);
static int calculate_remaining_seconds(int64_t ready_start_time, int64_t now_ms);
static void set_boiler_off(BoilerInfo* boiler);
static void set_boiler_heating(BoilerInfo* boiler, int64_t ready_start_time);
static void set_boiler_ready(BoilerInfo* boiler);
static void restart_update_timer(void);
static const char* boiler_type_name(BoilerType type);

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
                           const char* boiler_status, int64_t ready_start_time) {
    if (!g_initialized) {
        boiler_debugln("[Boiler] ERROR: Not initialized!");
        return;
    }
    
    if (type >= 2) {
        boiler_debugln("[Boiler] ERROR: Invalid boiler type!");
        return;
    }
    
    BoilerInfo* boiler = &g_boilers[type];
    
    boiler_debug("[Boiler] Update ");
    boiler_debug(boiler_type_name(type));
    boiler_debug(" - Machine: ");
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
            boiler_debug("[Boiler] ");
            boiler_debug(boiler_type_name(type));
            boiler_debugln(" -> OFF (machine off/standby)");
            set_boiler_off(boiler);
            restart_update_timer();  // Recalculate timer period
        }
        return;
    }
    
    // Machine is ON (PoweredOn, BrewingMode, etc.)
    if (ready_start_time <= 0) {
        // No valid ready start time - machine is ON but boiler is already READY (not heating)
        if (boiler->state != BOILER_STATE_READY) {
            boiler_debug("[Boiler] ");
            boiler_debug(boiler_type_name(type));
            boiler_debugln(" -> READY (no heating needed)");
            set_boiler_ready(boiler);
            restart_update_timer();
        }
        return;
    }
    
    // Calculate remaining time
    int64_t now_ms = boiler_display_get_current_time_ms();
    int remaining_sec = calculate_remaining_seconds(ready_start_time, now_ms);
    
    boiler_debug("[Boiler] ");
    boiler_debug(boiler_type_name(type));
    boiler_debug(" remaining: ");
    boiler_debug(remaining_sec);
    boiler_debugln(" sec");
    
    if (remaining_sec <= 0) {
        // Boiler is READY
        if (boiler->state != BOILER_STATE_READY) {
            boiler_debug("[Boiler] ");
            boiler_debug(boiler_type_name(type));
            boiler_debugln(" -> READY");
            set_boiler_ready(boiler);
            restart_update_timer();
        }
    } else {
        // Boiler is HEATING
        if (boiler->state != BOILER_STATE_HEATING || boiler->ready_start_time != ready_start_time) {
            boiler_debug("[Boiler] ");
            boiler_debug(boiler_type_name(type));
            boiler_debugln(" -> HEATING");
            set_boiler_heating(boiler, ready_start_time);
            restart_update_timer();
        } else {
            // Update the display with current countdown
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
    
    // Update each boiler that is in HEATING state
    for (int i = 0; i < 2; i++) {
        BoilerInfo* boiler = &g_boilers[i];
        
        if (boiler->state == BOILER_STATE_HEATING) {
            int remaining_sec = calculate_remaining_seconds(boiler->ready_start_time, now_ms);
            
            if (remaining_sec <= 0) {
                // Transition to READY
                boiler_debug("[Boiler] Timer: ");
                boiler_debug(boiler_type_name(boiler->type));
                boiler_debugln(" -> READY");
                set_boiler_ready(boiler);
            } else {
                // Update countdown display
                update_arc_and_label(boiler, remaining_sec);
                any_heating = true;
            }
        }
    }
    
    // Restart timer with appropriate period
    if (any_heating) {
        restart_update_timer();
    } else {
        // No boilers heating, pause timer
        boiler_debugln("[Boiler] No boilers heating, pausing timer");
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
    if (boiler->last_remaining_sec == remaining_seconds) {
        return;  // No change
    }
    
    boiler->last_remaining_sec = remaining_seconds;
    
    // Calculate arc value (0% at start, 100% when ready)
    // Arc represents progress towards ready state, so it increases as time passes
    // Note: We use WARMUP_DURATION_SEC (300s) as the assumed max duration
    // The arc will be accurate if actual warmup is ~5 minutes
    // INVERTED: 100 - (remaining * 100 / max) so it fills up as it heats
    int arc_value = 100 - ((remaining_seconds * 100) / WARMUP_DURATION_SEC);
    if (arc_value < 0) arc_value = 0;
    if (arc_value > 100) arc_value = 100;
    
    // Update arc with animation for smooth transition
    lv_arc_set_value(boiler->arc, arc_value);
    
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
    
    lv_label_set_text(boiler->label, label_text);
    
    boiler_debug("[Boiler] ");
    boiler_debug(boiler_type_name(boiler->type));
    boiler_debug(" display: ");
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
    
    // Set arc to 0%
    lv_arc_set_value(boiler->arc, 0);
    
    // Set label to "OFF"
    lv_label_set_text(boiler->label, "OFF");
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
 * Set boiler to READY state
 */
static void set_boiler_ready(BoilerInfo* boiler) {
    if (!boiler || !boiler->arc || !boiler->label) return;
    
    boiler->state = BOILER_STATE_READY;
    boiler->last_remaining_sec = 0;
    
    // Set arc to 100% (full circle when ready)
    lv_arc_set_value(boiler->arc, 100);
    
    // Set label to "READY"
    lv_label_set_text(boiler->label, "READY");
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

