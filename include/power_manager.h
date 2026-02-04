#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// Power states - ordered from most active to least active
typedef enum {
    POWER_STATE_ACTIVE = 0,      // Full brightness, no power saving
    POWER_STATE_DIMMED = 1,      // Reduced brightness, light WiFi power saving
    POWER_STATE_LOW_POWER = 2,   // Very low brightness, max WiFi power saving, light sleep
    POWER_STATE_DEEP_SLEEP = 3   // Display off, ESP32 deep sleep
} PowerState;

// Activity sources - can be combined as flags
typedef enum {
    ACTIVITY_NONE = 0,
    ACTIVITY_BUTTON_PRESS = (1 << 0),
    ACTIVITY_TOUCH = (1 << 1),
    ACTIVITY_BREWING = (1 << 2),
    ACTIVITY_HEATING = (1 << 3),
    ACTIVITY_MACHINE_ON = (1 << 4),
    ACTIVITY_WEBSOCKET = (1 << 5)
} ActivitySource;

// Configuration structure (can be stored in NVS)
typedef struct {
    uint32_t active_to_dimmed_ms;      // Time before dimming (default: 180000 = 3 min)
    uint32_t dimmed_to_lowpower_ms;    // Time in dimmed before low power (default: 600000 = 10 min)
    uint32_t lowpower_to_deepsleep_ms; // Time in low power before deep sleep (default: 1800000 = 30 min)
    uint8_t active_brightness;          // Brightness in active state (default: 255)
    uint8_t dimmed_brightness;          // Brightness in dimmed state (default: 100 = ~40%)
    uint8_t lowpower_brightness;        // Brightness in low power state (default: 38 = ~15%)
    uint16_t fade_duration_ms;          // Duration of brightness transitions (default: 500)
    bool enable_sleep_indicator;        // Show visual indicator before deep sleep (default: true)
} PowerConfig;

/**
 * Initialize the power manager
 * Must be called after AMOLED and LVGL are initialized
 *
 * @param amoled_instance Pointer to LilyGo_Class instance
 */
void power_manager_init(void* amoled_instance);

/**
 * Set the GUI mutex for thread-safe LVGL access
 * Must be called before power_manager_init()
 *
 * @param mutex FreeRTOS semaphore handle for GUI protection
 */
void power_manager_set_mutex(void* mutex);

/**
 * Load configuration from NVS
 *
 * @return true if config loaded successfully, false if defaults used
 */
bool power_manager_load_config(void);

/**
 * Save current configuration to NVS
 *
 * @return true if saved successfully
 */
bool power_manager_save_config(void);

/**
 * Set configuration values
 *
 * @param config Pointer to configuration structure
 */
void power_manager_set_config(const PowerConfig* config);

/**
 * Get current configuration values
 *
 * @param config Pointer to structure to fill with current config
 */
void power_manager_get_config(PowerConfig* config);

/**
 * Reset configuration to defaults
 */
void power_manager_set_default_config(void);

/**
 * Register user or machine activity
 * This resets the inactivity timer and may wake from dimmed/low power states
 *
 * @param source The source of the activity
 */
void power_manager_register_activity(ActivitySource source);

/**
 * Set brewing state - locks display to active while brewing
 *
 * @param is_brewing true if machine is currently brewing
 */
void power_manager_set_brewing(bool is_brewing);

/**
 * Set heating state - locks display to active while heating
 *
 * @param is_heating true if any boiler is heating up
 */
void power_manager_set_heating(bool is_heating);

/**
 * Set machine power state
 *
 * @param is_on true if machine is powered on
 */
void power_manager_set_machine_on(bool is_on);

/**
 * Get current power state
 *
 * @return Current PowerState
 */
PowerState power_manager_get_state(void);

/**
 * Check if display is in active state
 *
 * @return true if in POWER_STATE_ACTIVE
 */
bool power_manager_is_active(void);

/**
 * Force return to active state
 * Used for manual wake-up
 */
void power_manager_force_active(void);

/**
 * Manually trigger deep sleep
 * Called when user holds button for 2 seconds
 */
void power_manager_enter_deep_sleep(void);

/**
 * Main loop handler - call from main loop
 * Handles state transitions, brightness fading, and light sleep
 */
void power_manager_loop(void);

/**
 * Handle button press event
 * Registers activity and wakes from low power states
 */
void power_manager_button_pressed(void);

/**
 * Handle long button press (2+ seconds)
 * Triggers deep sleep
 */
void power_manager_button_long_press(void);

#ifdef __cplusplus
}
#endif
