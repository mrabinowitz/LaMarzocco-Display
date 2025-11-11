#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

// Warm-up duration in seconds (5 minutes)
#define WARMUP_DURATION_SEC 300

// Boiler types
typedef enum {
    BOILER_COFFEE = 0,
    BOILER_STEAM = 1
} BoilerType;

// Boiler state
typedef enum {
    BOILER_STATE_OFF = 0,       // Machine is off or standby
    BOILER_STATE_HEATING = 1,   // Heating up with countdown
    BOILER_STATE_READY = 2      // Ready (countdown reached 0)
} BoilerState;

// Structure to hold boiler information
typedef struct {
    BoilerType type;
    lv_obj_t* arc;              // Arc object (progress indicator)
    lv_obj_t* label;            // Label object (text display)
    int64_t ready_start_time;   // Ready start time in milliseconds (from JSON)
    BoilerState state;          // Current state
    int last_remaining_sec;     // Last calculated remaining seconds (for change detection)
} BoilerInfo;

/**
 * Initialize the boiler display system
 * Sets up references to existing UI components and initializes state
 */
void boiler_display_init(void);

/**
 * Update a specific boiler's status based on machine state and ready start time
 * 
 * Behavior:
 * - If machine is OFF/StandBy: Display "OFF", arc at 0%
 * - If machine is ON and ready_start_time is 0/null: Display "READY", arc at 100% (already ready)
 * - If machine is ON and ready_start_time is valid: Display countdown timer, arc shows progress
 * 
 * @param type Boiler type (BOILER_COFFEE or BOILER_STEAM)
 * @param machine_status Machine status string ("Off", "StandBy", "PoweredOn", etc.)
 * @param boiler_status Boiler status string ("Off", "StandBy", "HeatingUp", "Ready", etc.)
 * @param ready_start_time Ready start time in milliseconds (GMT Unix timestamp), 0 if not available/null
 */
void boiler_display_update(BoilerType type, const char* machine_status, 
                           const char* boiler_status, int64_t ready_start_time);

/**
 * Update all boilers to OFF state
 * Called when machine is powered off or disconnected
 */
void boiler_display_set_all_off(void);

/**
 * Get current Unix timestamp in milliseconds
 * 
 * @return Current time in milliseconds
 */
int64_t boiler_display_get_current_time_ms(void);

/**
 * Timer callback for periodic updates
 * This is called by LVGL timer to update the display
 * 
 * @param timer LVGL timer object
 */
void boiler_display_timer_callback(lv_timer_t* timer);

#ifdef __cplusplus
}
#endif

