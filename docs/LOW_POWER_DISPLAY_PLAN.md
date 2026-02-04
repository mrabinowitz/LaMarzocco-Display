# Low-Power Display Feature - Implementation Plan

## Overview

A tiered power management system for the La Marzocco Display with intelligent activity detection from both user input and machine state. The system manages four power states with smooth transitions, configurable timeouts, and machine-aware behavior.

## Power State Diagram

```
┌─────────────┐  3 min   ┌─────────────┐  10 min  ┌─────────────┐  30 min  ┌─────────────┐
│   ACTIVE    │─────────►│   DIMMED    │─────────►│  LOW_POWER  │─────────►│ DEEP_SLEEP  │
│             │          │             │          │             │          │             │
│ Bright:100% │◄─────────│ Bright:40%  │◄─────────│ Bright:15%  │          │ Display OFF │
│ WiFi: Full  │ activity │ WiFi: Min   │ activity │ WiFi: Max   │          │ Full restart│
└─────────────┘          └─────────────┘          └─────────────┘          └─────────────┘
      ▲                                                                           │
      │              BREWING/HEATING: Locks to ACTIVE state                       │
      │                                                                           │
      └───────────────────────────── Button press (full restart) ─────────────────┘
```

## Power State Configuration

| State | Brightness | Loop Behavior | WiFi Mode | Timeout |
|-------|-----------|---------------|-----------|---------|
| **Active** | 100% (255) | Normal 10ms | `WIFI_PS_NONE` | 3 min → Dimmed |
| **Dimmed** | 40% (100) | Normal 10ms | `WIFI_PS_MIN_MODEM` | 10 min → Low Power |
| **Low Power** | 15% (38) | Light sleep + GPIO wake | `WIFI_PS_MAX_MODEM` | 30 min → Deep Sleep |
| **Deep Sleep** | Off | ESP32 deep sleep | Off | Manual (2s hold) or auto |

All timeouts are configurable and stored in NVS (flash).

## Activity Detection

Activity from any of these sources resets the system to **Active** state:

| Source | Trigger | Notes |
|--------|---------|-------|
| Button press | GPIO 0 LOW | Instant wake, even from light sleep |
| Brewing | `is_brewing == true` | **Locks** to Active while brewing |
| Heating | `boiler_status == "HeatingUp"` | **Locks** to Active while heating |
| UI interaction | Power/Steam button tap | Touch events |
| WebSocket | Significant state changes | Machine on/off, etc. |

**Important:** During brewing or heating, the system is locked to Active state and will not dim or sleep regardless of timeouts.

## Visual Feedback

### Brightness Transitions
- Smooth ease-out fade over 500ms
- No jarring instant brightness changes

### Deep Sleep Indicator
- Before entering deep sleep, display performs 3 gentle "breathing" pulses
- Brightness pulses down to 50% then back up, 3 times
- Gives user ~1 second warning to interact and prevent sleep

## New Files

### `include/power_manager.h`

```c
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

// Power states
typedef enum {
    POWER_STATE_ACTIVE = 0,
    POWER_STATE_DIMMED = 1,
    POWER_STATE_LOW_POWER = 2,
    POWER_STATE_DEEP_SLEEP = 3
} PowerState;

// Activity sources
typedef enum {
    ACTIVITY_NONE = 0,
    ACTIVITY_BUTTON_PRESS = (1 << 0),
    ACTIVITY_TOUCH = (1 << 1),
    ACTIVITY_BREWING = (1 << 2),
    ACTIVITY_HEATING = (1 << 3),
    ACTIVITY_MACHINE_ON = (1 << 4),
    ACTIVITY_WEBSOCKET = (1 << 5)
} ActivitySource;

// Configuration structure (stored in NVS)
typedef struct {
    uint32_t active_to_dimmed_ms;      // Default: 180000 (3 min)
    uint32_t dimmed_to_lowpower_ms;    // Default: 600000 (10 min)
    uint32_t lowpower_to_deepsleep_ms; // Default: 1800000 (30 min)
    uint8_t active_brightness;          // Default: 255
    uint8_t dimmed_brightness;          // Default: 100 (~40%)
    uint8_t lowpower_brightness;        // Default: 38 (~15%)
    uint16_t fade_duration_ms;          // Default: 500
    bool enable_sleep_indicator;        // Default: true
} PowerConfig;

// Initialization
void power_manager_init(void* amoled_instance);
void power_manager_set_mutex(void* mutex);
bool power_manager_load_config(void);
bool power_manager_save_config(void);

// Configuration
void power_manager_set_config(const PowerConfig* config);
void power_manager_get_config(PowerConfig* config);
void power_manager_set_default_config(void);

// Activity registration
void power_manager_register_activity(ActivitySource source);

// Machine state hooks
void power_manager_set_brewing(bool is_brewing);
void power_manager_set_heating(bool is_heating);
void power_manager_set_machine_on(bool is_on);

// State queries
PowerState power_manager_get_state(void);
bool power_manager_is_active(void);

// Manual controls
void power_manager_force_active(void);
void power_manager_enter_deep_sleep(void);

// Main loop handler
void power_manager_loop(void);

// Button handlers
void power_manager_button_pressed(void);
void power_manager_button_long_press(void);

#ifdef __cplusplus
}
#endif
```

### `src/power_manager.cpp`

Key components:
- State machine with timeout-based transitions
- Smooth brightness fading with ease-out interpolation
- WiFi power mode management (`esp_wifi_set_ps()`)
- Light sleep with GPIO interrupt wake
- NVS configuration persistence
- Sleep indicator animation

## Modifications to Existing Files

### `main.cpp`

```cpp
// Add include
#include "power_manager.h"

// In setup() or Task_LVGL, after ui_init():
power_manager_set_mutex((void*)gui_mutex);
power_manager_init((void*)&amoled);

// In loop(), add:
power_manager_loop();

// Replace button handling with:
if (digitalRead(0) == LOW) {
    delay(100); // Debounce
    power_manager_button_pressed();

    unsigned long startTime = millis();
    while (digitalRead(0) == LOW) {
        if (millis() - startTime > 2000) {
            power_manager_button_long_press();
            break;
        }
    }
}
```

### `lamarzocco_machine.cpp`

Add hooks in `_websocket_message_handler()`:

```cpp
#include "power_manager.h"

// After parsing brewing state:
power_manager_set_brewing(is_brewing);

// After parsing boiler status:
bool is_heating = (strcmp(coffee_boiler_status, "HeatingUp") == 0) ||
                  (strcmp(steam_boiler_status, "HeatingUp") == 0);
power_manager_set_heating(is_heating);

// After parsing machine status:
power_manager_set_machine_on(strcmp(machine_status, "PoweredOn") == 0);
power_manager_register_activity(ACTIVITY_WEBSOCKET);
```

### `config.h`

```cpp
// Power Management Defaults
#define POWER_ACTIVE_TO_DIMMED_MS      180000   // 3 minutes
#define POWER_DIMMED_TO_LOWPOWER_MS    600000   // 10 minutes
#define POWER_LOWPOWER_TO_DEEPSLEEP_MS 1800000  // 30 minutes

#define POWER_BRIGHTNESS_ACTIVE        255      // 100%
#define POWER_BRIGHTNESS_DIMMED        100      // ~40%
#define POWER_BRIGHTNESS_LOWPOWER      38       // ~15%

#define POWER_FADE_DURATION_MS         500
```

### `custom_ui_event.cpp`

```cpp
#include "power_manager.h"

// In button event handlers:
void ui_event_powerButton(lv_event_t * e) {
    power_manager_register_activity(ACTIVITY_TOUCH);
    // ... existing code
}
```

## Implementation Phases

| Phase | Description | Priority |
|-------|-------------|----------|
| **1** | Core infrastructure - `power_manager.h/.cpp`, state machine, brightness fading | High |
| **2** | Activity detection - button press, brewing/heating hooks in WebSocket handler | High |
| **3** | WiFi power management - mode switching per state | Medium |
| **4** | Light sleep integration - GPIO wake for responsive buttons in low power | Medium |
| **5** | Deep sleep - auto timeout, breathing pulse indicator, manual 2s hold | Medium |
| **6** | Configuration persistence - NVS load/save | Low |
| **7** | Testing & refinement | High |

## Key Technical Decisions

1. **Light sleep with GPIO interrupt** - Buttons stay responsive (~instant) even while CPU sleeps in Low Power state

2. **WiFi modem sleep modes** - WebSocket connection stays alive with slightly higher latency, no reconnection delay on wake

3. **Brewing/heating locks Active** - Critical machine operations always have full display brightness and responsiveness

4. **NVS configuration storage** - Timeouts persist across reboots, easy to tune without recompiling

5. **Smooth fading** - 500ms ease-out transitions for professional feel

## Configuration Defaults

| Setting | Default | Description |
|---------|---------|-------------|
| `active_to_dimmed_ms` | 180000 | 3 minutes to dim |
| `dimmed_to_lowpower_ms` | 600000 | 10 minutes to low power |
| `lowpower_to_deepsleep_ms` | 1800000 | 30 minutes to deep sleep |
| `active_brightness` | 255 | 100% brightness |
| `dimmed_brightness` | 100 | ~40% brightness |
| `lowpower_brightness` | 38 | ~15% brightness |
| `fade_duration_ms` | 500 | Transition animation time |
| `enable_sleep_indicator` | true | Show breathing pulse before sleep |

## Testing Checklist

- [ ] Idle 3 min → transitions to Dimmed (40% brightness)
- [ ] Idle 13 min → transitions to Low Power (15% brightness)
- [ ] Button press in Dimmed → returns to Active instantly
- [ ] Button press in Low Power → wakes from light sleep, returns to Active
- [ ] Start brewing while Dimmed → returns to Active, stays locked
- [ ] Brewing ends → normal timeout progression resumes
- [ ] Boiler heating → prevents any dimming
- [ ] Boiler ready (idle) → normal timeout progression
- [ ] 2s button hold → enters Deep Sleep
- [ ] Deep sleep + button → full restart
- [ ] Auto deep sleep after 43 min total idle
- [ ] Breathing pulse appears before deep sleep
- [ ] WebSocket remains connected in Dimmed state
- [ ] WebSocket remains connected in Low Power state
- [ ] WiFi reconnects properly after wake from any state

## Future Enhancements (Out of Scope)

- Settings UI for runtime configuration
- Different profiles (e.g., "Battery Saver" vs "Always On")
- Wake on machine power-on (if detectable via external signal)
- Scheduled sleep times
