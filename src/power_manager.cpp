#include "power_manager.h"
#include "config.h"
#include <Arduino.h>
#include <LilyGo_AMOLED.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <driver/gpio.h>

// Default configuration values
#define DEFAULT_ACTIVE_TO_DIMMED_MS      180000    // 3 minutes
#define DEFAULT_DIMMED_TO_LOWPOWER_MS    600000    // 10 minutes
#define DEFAULT_LOWPOWER_TO_DEEPSLEEP_MS 1800000   // 30 minutes
#define DEFAULT_ACTIVE_BRIGHTNESS        255       // 100%
#define DEFAULT_DIMMED_BRIGHTNESS        100       // ~40%
#define DEFAULT_LOWPOWER_BRIGHTNESS      38        // ~15%
#define DEFAULT_FADE_DURATION_MS         500       // 500ms fade
#define DEFAULT_ENABLE_SLEEP_INDICATOR   true

// NVS namespace and keys
#define NVS_NAMESPACE "power_mgr"
#define NVS_KEY_ACTIVE_DIM    "act_dim_ms"
#define NVS_KEY_DIM_LOW       "dim_low_ms"
#define NVS_KEY_LOW_SLEEP     "low_slp_ms"
#define NVS_KEY_BRT_ACTIVE    "brt_act"
#define NVS_KEY_BRT_DIMMED    "brt_dim"
#define NVS_KEY_BRT_LOWPWR    "brt_low"
#define NVS_KEY_FADE_DUR      "fade_ms"
#define NVS_KEY_SLEEP_IND     "slp_ind"

// Light sleep interval in low power mode
#define LIGHT_SLEEP_INTERVAL_MS 200

// Static state variables
static PowerState g_current_state = POWER_STATE_ACTIVE;
static PowerConfig g_config;
static unsigned long g_last_activity_time = 0;
static uint8_t g_current_brightness = DEFAULT_ACTIVE_BRIGHTNESS;
static uint8_t g_target_brightness = DEFAULT_ACTIVE_BRIGHTNESS;
static bool g_is_brewing = false;
static bool g_is_heating = false;
static bool g_machine_on = false;
static bool g_fade_in_progress = false;
static unsigned long g_fade_start_time = 0;
static uint8_t g_fade_start_brightness = 0;
static SemaphoreHandle_t g_gui_mutex = NULL;
static LilyGo_Class* g_amoled = NULL;
static bool g_initialized = false;
static Preferences g_prefs;

// Forward declarations
static void transition_to_state(PowerState new_state);
static void update_wifi_power_mode(PowerState state);
static void update_fade_brightness(unsigned long now);
static void start_brightness_fade(uint8_t target);
static void show_sleep_indicator(void);
static void enter_light_sleep_with_gpio_wake(void);
static bool is_activity_locked(void);

// Mutex helper macros
#define TAKE_MUTEX() if (g_gui_mutex && xSemaphoreTake((SemaphoreHandle_t)g_gui_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
#define GIVE_MUTEX() xSemaphoreGive((SemaphoreHandle_t)g_gui_mutex)

void power_manager_set_default_config(void) {
    g_config.active_to_dimmed_ms = DEFAULT_ACTIVE_TO_DIMMED_MS;
    g_config.dimmed_to_lowpower_ms = DEFAULT_DIMMED_TO_LOWPOWER_MS;
    g_config.lowpower_to_deepsleep_ms = DEFAULT_LOWPOWER_TO_DEEPSLEEP_MS;
    g_config.active_brightness = DEFAULT_ACTIVE_BRIGHTNESS;
    g_config.dimmed_brightness = DEFAULT_DIMMED_BRIGHTNESS;
    g_config.lowpower_brightness = DEFAULT_LOWPOWER_BRIGHTNESS;
    g_config.fade_duration_ms = DEFAULT_FADE_DURATION_MS;
    g_config.enable_sleep_indicator = DEFAULT_ENABLE_SLEEP_INDICATOR;
}

void power_manager_set_mutex(void* mutex) {
    g_gui_mutex = (SemaphoreHandle_t)mutex;
}

void power_manager_init(void* amoled_instance) {
    g_amoled = (LilyGo_Class*)amoled_instance;

    // Set defaults first
    power_manager_set_default_config();

    // Try to load from NVS
    power_manager_load_config();

    // Initialize state
    g_current_state = POWER_STATE_ACTIVE;
    g_last_activity_time = millis();
    g_current_brightness = g_config.active_brightness;
    g_target_brightness = g_config.active_brightness;
    g_fade_in_progress = false;
    g_is_brewing = false;
    g_is_heating = false;
    g_machine_on = false;

    // Set initial brightness
    if (g_amoled) {
        g_amoled->setBrightness(g_current_brightness);
    }

    // Set initial WiFi power mode
    update_wifi_power_mode(g_current_state);

    g_initialized = true;

    Serial.println("[POWER] Power manager initialized");
    Serial.printf("[POWER] Timeouts: Active->Dim=%lums, Dim->Low=%lums, Low->Sleep=%lums\n",
                  g_config.active_to_dimmed_ms,
                  g_config.dimmed_to_lowpower_ms,
                  g_config.lowpower_to_deepsleep_ms);
}

bool power_manager_load_config(void) {
    if (!g_prefs.begin(NVS_NAMESPACE, true)) {  // Read-only
        Serial.println("[POWER] Failed to open NVS for reading, using defaults");
        return false;
    }

    // Load each setting, use current value as default if not found
    g_config.active_to_dimmed_ms = g_prefs.getULong(NVS_KEY_ACTIVE_DIM, g_config.active_to_dimmed_ms);
    g_config.dimmed_to_lowpower_ms = g_prefs.getULong(NVS_KEY_DIM_LOW, g_config.dimmed_to_lowpower_ms);
    g_config.lowpower_to_deepsleep_ms = g_prefs.getULong(NVS_KEY_LOW_SLEEP, g_config.lowpower_to_deepsleep_ms);
    g_config.active_brightness = g_prefs.getUChar(NVS_KEY_BRT_ACTIVE, g_config.active_brightness);
    g_config.dimmed_brightness = g_prefs.getUChar(NVS_KEY_BRT_DIMMED, g_config.dimmed_brightness);
    g_config.lowpower_brightness = g_prefs.getUChar(NVS_KEY_BRT_LOWPWR, g_config.lowpower_brightness);
    g_config.fade_duration_ms = g_prefs.getUShort(NVS_KEY_FADE_DUR, g_config.fade_duration_ms);
    g_config.enable_sleep_indicator = g_prefs.getBool(NVS_KEY_SLEEP_IND, g_config.enable_sleep_indicator);

    g_prefs.end();

    Serial.println("[POWER] Configuration loaded from NVS");
    return true;
}

bool power_manager_save_config(void) {
    if (!g_prefs.begin(NVS_NAMESPACE, false)) {  // Read-write
        Serial.println("[POWER] Failed to open NVS for writing");
        return false;
    }

    g_prefs.putULong(NVS_KEY_ACTIVE_DIM, g_config.active_to_dimmed_ms);
    g_prefs.putULong(NVS_KEY_DIM_LOW, g_config.dimmed_to_lowpower_ms);
    g_prefs.putULong(NVS_KEY_LOW_SLEEP, g_config.lowpower_to_deepsleep_ms);
    g_prefs.putUChar(NVS_KEY_BRT_ACTIVE, g_config.active_brightness);
    g_prefs.putUChar(NVS_KEY_BRT_DIMMED, g_config.dimmed_brightness);
    g_prefs.putUChar(NVS_KEY_BRT_LOWPWR, g_config.lowpower_brightness);
    g_prefs.putUShort(NVS_KEY_FADE_DUR, g_config.fade_duration_ms);
    g_prefs.putBool(NVS_KEY_SLEEP_IND, g_config.enable_sleep_indicator);

    g_prefs.end();

    Serial.println("[POWER] Configuration saved to NVS");
    return true;
}

void power_manager_set_config(const PowerConfig* config) {
    if (config) {
        memcpy(&g_config, config, sizeof(PowerConfig));
    }
}

void power_manager_get_config(PowerConfig* config) {
    if (config) {
        memcpy(config, &g_config, sizeof(PowerConfig));
    }
}

void power_manager_register_activity(ActivitySource source) {
    if (!g_initialized) return;

    g_last_activity_time = millis();

    // If not in active state, wake up
    if (g_current_state != POWER_STATE_ACTIVE) {
        Serial.printf("[POWER] Activity detected (source: 0x%02X), waking to active\n", source);
        transition_to_state(POWER_STATE_ACTIVE);
    }
}

void power_manager_set_brewing(bool is_brewing) {
    if (g_is_brewing != is_brewing) {
        g_is_brewing = is_brewing;
        Serial.printf("[POWER] Brewing state: %s\n", is_brewing ? "ACTIVE" : "inactive");

        if (is_brewing) {
            // Brewing started - ensure we're in active state
            power_manager_register_activity(ACTIVITY_BREWING);
        }
    }
}

void power_manager_set_heating(bool is_heating) {
    if (g_is_heating != is_heating) {
        g_is_heating = is_heating;
        Serial.printf("[POWER] Heating state: %s\n", is_heating ? "ACTIVE" : "inactive");

        if (is_heating) {
            // Heating started - ensure we're in active state
            power_manager_register_activity(ACTIVITY_HEATING);
        }
    }
}

void power_manager_set_machine_on(bool is_on) {
    if (g_machine_on != is_on) {
        g_machine_on = is_on;
        Serial.printf("[POWER] Machine power: %s\n", is_on ? "ON" : "OFF");

        if (is_on) {
            power_manager_register_activity(ACTIVITY_MACHINE_ON);
        }
    }
}

PowerState power_manager_get_state(void) {
    return g_current_state;
}

bool power_manager_is_active(void) {
    return g_current_state == POWER_STATE_ACTIVE;
}

void power_manager_force_active(void) {
    power_manager_register_activity(ACTIVITY_BUTTON_PRESS);
}

void power_manager_button_pressed(void) {
    power_manager_register_activity(ACTIVITY_BUTTON_PRESS);
}

void power_manager_button_long_press(void) {
    Serial.println("[POWER] Long press detected, entering deep sleep");
    power_manager_enter_deep_sleep();
}

void power_manager_enter_deep_sleep(void) {
    Serial.println("[POWER] Preparing for deep sleep...");

    // Show sleep indicator if enabled
    if (g_config.enable_sleep_indicator) {
        show_sleep_indicator();
    }

    // Turn off display
    if (g_amoled) {
        g_amoled->setBrightness(0);
    }

    // Wait for button release to prevent instant wake
    while (digitalRead(0) == LOW) {
        delay(10);
    }
    delay(100);  // Debounce

    Serial.println("[POWER] Entering deep sleep...");

    // Configure wake source - GPIO 0 (BOOT button)
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);  // Wake on LOW

    // Enter deep sleep (this doesn't return)
    esp_deep_sleep_start();
}

void power_manager_loop(void) {
    if (!g_initialized) return;

    unsigned long now = millis();

    // Handle brightness fading
    if (g_fade_in_progress) {
        update_fade_brightness(now);
    }

    // Check if activity is locked (brewing or heating)
    if (is_activity_locked()) {
        // Reset activity timer to keep display active
        g_last_activity_time = now;

        // Ensure we're in active state
        if (g_current_state != POWER_STATE_ACTIVE) {
            transition_to_state(POWER_STATE_ACTIVE);
        }
        return;
    }

    // Calculate idle time
    unsigned long idle_time = now - g_last_activity_time;

    // State machine for power transitions
    switch (g_current_state) {
        case POWER_STATE_ACTIVE:
            if (idle_time >= g_config.active_to_dimmed_ms) {
                transition_to_state(POWER_STATE_DIMMED);
            }
            break;

        case POWER_STATE_DIMMED: {
            unsigned long dimmed_threshold = g_config.active_to_dimmed_ms +
                                             g_config.dimmed_to_lowpower_ms;
            if (idle_time >= dimmed_threshold) {
                transition_to_state(POWER_STATE_LOW_POWER);
            }
            break;
        }

        case POWER_STATE_LOW_POWER: {
            unsigned long sleep_threshold = g_config.active_to_dimmed_ms +
                                            g_config.dimmed_to_lowpower_ms +
                                            g_config.lowpower_to_deepsleep_ms;
            if (idle_time >= sleep_threshold) {
                transition_to_state(POWER_STATE_DEEP_SLEEP);
            } else {
                // In low power mode, use light sleep between loop iterations
                // This saves power while maintaining responsiveness
                enter_light_sleep_with_gpio_wake();
            }
            break;
        }

        case POWER_STATE_DEEP_SLEEP:
            // Should not reach here - deep sleep doesn't return
            // But if we somehow get here, trigger deep sleep
            power_manager_enter_deep_sleep();
            break;
    }
}

// --- Private helper functions ---

static bool is_activity_locked(void) {
    // Brewing or heating locks the display to active state
    return g_is_brewing || g_is_heating;
}

static void transition_to_state(PowerState new_state) {
    if (new_state == g_current_state) return;

    PowerState old_state = g_current_state;
    g_current_state = new_state;

    const char* state_names[] = {"ACTIVE", "DIMMED", "LOW_POWER", "DEEP_SLEEP"};
    Serial.printf("[POWER] State transition: %s -> %s\n",
                  state_names[old_state], state_names[new_state]);

    // Update WiFi power mode
    update_wifi_power_mode(new_state);

    // Handle brightness for each state
    switch (new_state) {
        case POWER_STATE_ACTIVE:
            start_brightness_fade(g_config.active_brightness);
            break;

        case POWER_STATE_DIMMED:
            start_brightness_fade(g_config.dimmed_brightness);
            break;

        case POWER_STATE_LOW_POWER:
            start_brightness_fade(g_config.lowpower_brightness);
            break;

        case POWER_STATE_DEEP_SLEEP:
            power_manager_enter_deep_sleep();
            break;
    }
}

static void update_wifi_power_mode(PowerState state) {
    wifi_ps_type_t ps_mode;
    const char* mode_name;

    switch (state) {
        case POWER_STATE_ACTIVE:
            ps_mode = WIFI_PS_NONE;
            mode_name = "NONE (full power)";
            break;
        case POWER_STATE_DIMMED:
            ps_mode = WIFI_PS_MIN_MODEM;
            mode_name = "MIN_MODEM";
            break;
        case POWER_STATE_LOW_POWER:
        case POWER_STATE_DEEP_SLEEP:
            ps_mode = WIFI_PS_MAX_MODEM;
            mode_name = "MAX_MODEM";
            break;
        default:
            ps_mode = WIFI_PS_NONE;
            mode_name = "NONE (default)";
    }

    esp_err_t result = esp_wifi_set_ps(ps_mode);
    if (result == ESP_OK) {
        Serial.printf("[POWER] WiFi power save: %s\n", mode_name);
    } else {
        Serial.printf("[POWER] Failed to set WiFi power save: %d\n", result);
    }
}

static void start_brightness_fade(uint8_t target) {
    if (target == g_current_brightness) {
        g_fade_in_progress = false;
        return;
    }

    g_target_brightness = target;
    g_fade_start_brightness = g_current_brightness;
    g_fade_start_time = millis();
    g_fade_in_progress = true;

    Serial.printf("[POWER] Starting brightness fade: %d -> %d\n",
                  g_current_brightness, target);
}

static void update_fade_brightness(unsigned long now) {
    if (!g_fade_in_progress) return;

    unsigned long elapsed = now - g_fade_start_time;

    if (elapsed >= g_config.fade_duration_ms) {
        // Fade complete
        g_current_brightness = g_target_brightness;
        g_fade_in_progress = false;
        Serial.printf("[POWER] Fade complete, brightness: %d\n", g_current_brightness);
    } else {
        // Calculate progress with ease-out curve
        float progress = (float)elapsed / (float)g_config.fade_duration_ms;
        // Ease-out: 1 - (1 - t)^2
        progress = 1.0f - (1.0f - progress) * (1.0f - progress);

        // Interpolate brightness
        int16_t diff = (int16_t)g_target_brightness - (int16_t)g_fade_start_brightness;
        g_current_brightness = g_fade_start_brightness + (uint8_t)(diff * progress);
    }

    // Apply brightness
    if (g_amoled) {
        g_amoled->setBrightness(g_current_brightness);
    }
}

static void show_sleep_indicator(void) {
    if (!g_amoled) return;

    Serial.println("[POWER] Showing sleep indicator (breathing pulse)");

    // Breathing pulse effect - 3 cycles
    uint8_t base_brightness = g_config.lowpower_brightness;
    uint8_t min_brightness = base_brightness / 2;

    for (int cycle = 0; cycle < 3; cycle++) {
        // Fade down
        for (int b = base_brightness; b >= min_brightness; b -= 5) {
            g_amoled->setBrightness(b);
            delay(15);
        }
        // Fade up
        for (int b = min_brightness; b <= base_brightness; b += 5) {
            g_amoled->setBrightness(b);
            delay(15);
        }
    }
}

static void enter_light_sleep_with_gpio_wake(void) {
    // Configure GPIO 0 as wake source
    gpio_wakeup_enable(GPIO_NUM_0, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    // Also set a timer wake to periodically check state
    esp_sleep_enable_timer_wakeup(LIGHT_SLEEP_INTERVAL_MS * 1000);  // Convert to microseconds

    // Enter light sleep
    esp_light_sleep_start();

    // On wake, check if it was a button press
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    if (wakeup_cause == ESP_SLEEP_WAKEUP_GPIO) {
        // Button was pressed - register activity
        Serial.println("[POWER] Woke from light sleep (GPIO)");
        power_manager_register_activity(ACTIVITY_BUTTON_PRESS);
    }
    // If timer wakeup, just continue with normal loop processing
}
