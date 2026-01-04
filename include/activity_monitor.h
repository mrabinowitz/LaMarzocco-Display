#pragma once

#include <Arduino.h>

void activity_monitor_init(uint32_t user_timeout_ms, uint32_t machine_timeout_ms);
void activity_monitor_mark_user_activity();
void activity_monitor_mark_machine_activity();
bool activity_monitor_is_user_inactive(uint32_t now_ms);
bool activity_monitor_is_machine_inactive(uint32_t now_ms);
unsigned long activity_monitor_last_user_ms();
unsigned long activity_monitor_last_machine_ms();
