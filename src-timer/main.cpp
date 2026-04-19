#include <Arduino.h>
#include <Wire.h>
#include "vibration.h"
#include "display.h"

// I2C pins for MPU6050
#define SDA_PIN 6
#define SCL_PIN 7

// How long to show the flashed result before going idle (ms)
#define RESULT_SHOW_MS 30000

VibrationSensor vib;
Display disp;

enum class AppState { IDLE, BREWING, RESULT };
AppState app = AppState::IDLE;

unsigned long result_started_ms = 0;
uint8_t flash_count = 0;
unsigned long flash_last_ms = 0;
int last_seconds = 0;

unsigned long last_display_update_ms = 0;
unsigned long last_tick_ms = 0;

void setup() {
    Wire.begin(SDA_PIN, SCL_PIN);
    vibration_init(vib);
    display_init(disp);
    display_show_idle(disp);
}

void loop() {
    unsigned long now = millis();

    // Sample MPU6050 every 20ms
    if (now - last_tick_ms >= 20) {
        last_tick_ms = now;
        vibration_tick(vib);
    }

    switch (app) {
        case AppState::IDLE:
            if (vib.state == ShotState::BREWING) {
                app = AppState::BREWING;
                display_show_seconds(disp, 0);
            }
            break;

        case AppState::BREWING:
            if (vib.state == ShotState::DONE) {
                last_seconds = vibration_elapsed_sec(vib);
                app = AppState::RESULT;
                result_started_ms = now;
                flash_count = 0;
                flash_last_ms = now;
                display_set_brightness(disp, 15);
            } else {
                // Update display every second
                if (now - last_display_update_ms >= 1000) {
                    last_display_update_ms = now;
                    display_show_seconds(disp, vibration_elapsed_sec(vib));
                }
            }
            break;

        case AppState::RESULT:
            if (flash_count < 10) {
                display_flash_result(disp, last_seconds, flash_count, flash_last_ms);
            } else if (now - result_started_ms < RESULT_SHOW_MS) {
                // Show result dim after flashing
                if (flash_count == 10) {
                    display_set_brightness(disp, 3);
                    display_show_seconds(disp, last_seconds);
                    flash_count = 11; // mark as shown
                }
            } else {
                // Return to idle
                display_set_brightness(disp, 8);
                display_show_idle(disp);
                vibration_reset(vib);
                app = AppState::IDLE;
            }
            break;
    }
}
