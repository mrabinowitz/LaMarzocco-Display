#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include <stdbool.h>

/**
 * Initialize the water alarm display system
 * Creates water drop image and alarm label on the main screen
 * Elements are hidden by default
 */
void water_alarm_init(void);

/**
 * Set the GUI mutex for thread-safe LVGL access
 * Must be called before water_alarm_init()
 * 
 * @param mutex FreeRTOS semaphore handle for GUI protection
 */
void water_alarm_set_mutex(void* mutex);

/**
 * Set water alarm state
 * Shows/hides alarm elements and coffee/steam elements
 * 
 * @param alarm_active true = show water alarm, hide boiler elements
 *                     false = hide water alarm, show boiler elements
 */
void water_alarm_set(bool alarm_active);

/**
 * Get current water alarm state
 * 
 * @return true if alarm is active, false otherwise
 */
bool water_alarm_is_active(void);

#ifdef __cplusplus
}
#endif

