#ifndef CONFIG_H
#define CONFIG_H

#include "Arduino.h"

#ifdef DEBUG
#define debug(x) Serial.print(x)
#define debugln(x) Serial.println(x)
#else
#define debug(x)
#define debugln(x)
#endif

// Captive portal redirection
#define REDIRECT_URL "http://192.168.4.1/"
static constexpr const char *NTP_SERVER = "pool.ntp.org";

#define  BATTERY_VOLTAGE_PIN 4
#define  BREWING_SIM_PIN 15  // GPIO 15 for brewing simulation mode (LOW = brewing, HIGH = normal)

#define uS_TO_S_FACTOR 1000000ULL

// Power Management Configuration
// These are compile-time defaults; actual values can be changed via NVS at runtime

// Timeout durations (milliseconds)
#define POWER_ACTIVE_TO_DIMMED_MS      180000    // 3 minutes until display dims
#define POWER_DIMMED_TO_LOWPOWER_MS    600000    // 10 minutes in dimmed before low power
#define POWER_LOWPOWER_TO_DEEPSLEEP_MS 1800000   // 30 minutes in low power before deep sleep

// Brightness levels (0-255)
#define POWER_BRIGHTNESS_ACTIVE        255       // 100% - full brightness
#define POWER_BRIGHTNESS_DIMMED        100       // ~40% - dimmed state
#define POWER_BRIGHTNESS_LOWPOWER      38        // ~15% - low power state

// Transition settings
#define POWER_FADE_DURATION_MS         500       // Brightness fade animation duration

// Button pin for power management (BOOT button)
#define POWER_BUTTON_PIN               0

#endif