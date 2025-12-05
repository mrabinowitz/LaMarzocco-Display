#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * Set the GUI mutex for thread-safe LVGL access
 * Must be called before brewing_display_init()
 * 
 * @param mutex FreeRTOS semaphore handle for GUI protection
 */
void brewing_display_set_mutex(void* mutex);

/**
 * Initialize the brewing display system
 * Sets up references to existing UI components and initializes state
 */
void brewing_display_init(void);

/**
 * Update brewing state based on machine status
 * 
 * @param is_brewing True if machine is currently brewing
 * @param brewing_start_time Timestamp when brewing started (milliseconds GMT), 0 if not brewing
 */
void brewing_display_update(bool is_brewing, int64_t brewing_start_time);

/**
 * Timer callback for periodic updates
 * This is called by LVGL timer to update the elapsed time display
 * 
 * @param timer LVGL timer object
 */
void brewing_display_timer_callback(lv_timer_t* timer);

/**
 * Get current Unix timestamp in milliseconds
 * 
 * @return Current time in milliseconds
 */
int64_t brewing_display_get_current_time_ms(void);

#ifdef __cplusplus
}
#endif

