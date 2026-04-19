#pragma once
#include <Wire.h>
#include <stdint.h>

// MPU6050 registers
#define MPU6050_ADDR       0x68
#define MPU6050_PWR_MGMT_1 0x6B
#define MPU6050_ACCEL_XOUT 0x3B

// Tuning constants
#define VIBRATION_THRESHOLD    2500   // raw accel delta — increase if false triggers
#define SHOT_END_QUIET_MS      3000   // ms of quiet to declare shot done
#define SHOT_MIN_MS            5000   // ignore bursts shorter than this

enum class ShotState { IDLE, BREWING, DONE };

struct VibrationSensor {
    ShotState state = ShotState::IDLE;
    unsigned long shot_start_ms = 0;
    unsigned long quiet_since_ms = 0;
    unsigned long shot_duration_ms = 0;
    int16_t prev_ax = 0, prev_ay = 0, prev_az = 0;
};

inline bool _mpu_read(int16_t &ax, int16_t &ay, int16_t &az) {
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU6050_ACCEL_XOUT);
    if (Wire.endTransmission(false) != 0) return false;
    Wire.requestFrom(MPU6050_ADDR, 6);
    if (Wire.available() < 6) return false;
    ax = (Wire.read() << 8) | Wire.read();
    ay = (Wire.read() << 8) | Wire.read();
    az = (Wire.read() << 8) | Wire.read();
    return true;
}

inline void vibration_init(VibrationSensor &v) {
    Wire.beginTransmission(MPU6050_ADDR);
    Wire.write(MPU6050_PWR_MGMT_1);
    Wire.write(0x00); // wake up
    Wire.endTransmission();
    _mpu_read(v.prev_ax, v.prev_ay, v.prev_az);
}

// Call every ~20ms from loop(). Returns true when state changes.
inline bool vibration_tick(VibrationSensor &v) {
    int16_t ax, ay, az;
    if (!_mpu_read(ax, ay, az)) return false;

    int32_t delta = abs((int32_t)ax - v.prev_ax)
                  + abs((int32_t)ay - v.prev_ay)
                  + abs((int32_t)az - v.prev_az);
    v.prev_ax = ax; v.prev_ay = ay; v.prev_az = az;

    unsigned long now = millis();
    bool changed = false;

    switch (v.state) {
        case ShotState::IDLE:
            if (delta > VIBRATION_THRESHOLD) {
                v.state = ShotState::BREWING;
                v.shot_start_ms = now;
                v.quiet_since_ms = 0;
                changed = true;
            }
            break;

        case ShotState::BREWING:
            if (delta < VIBRATION_THRESHOLD) {
                if (v.quiet_since_ms == 0) v.quiet_since_ms = now;
                if (now - v.quiet_since_ms >= SHOT_END_QUIET_MS) {
                    unsigned long dur = now - v.shot_start_ms;
                    if (dur >= SHOT_MIN_MS) {
                        v.shot_duration_ms = dur;
                        v.state = ShotState::DONE;
                    } else {
                        v.state = ShotState::IDLE;
                    }
                    changed = true;
                }
            } else {
                v.quiet_since_ms = 0;
            }
            break;

        case ShotState::DONE:
            break;
    }
    return changed;
}

inline int vibration_elapsed_sec(const VibrationSensor &v) {
    if (v.state == ShotState::BREWING)
        return (int)((millis() - v.shot_start_ms) / 1000);
    if (v.state == ShotState::DONE)
        return (int)(v.shot_duration_ms / 1000);
    return 0;
}

inline void vibration_reset(VibrationSensor &v) {
    v.state = ShotState::IDLE;
    v.shot_start_ms = 0;
    v.quiet_since_ms = 0;
    v.shot_duration_ms = 0;
}
