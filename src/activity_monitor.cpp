#include "activity_monitor.h"

namespace {
  uint32_t user_timeout_ms = 0;
  uint32_t machine_timeout_ms = 0;
  unsigned long last_user_ms = 0;
  unsigned long last_machine_ms = 0;
  bool initialized = false;
}

void activity_monitor_init(uint32_t user_timeout, uint32_t machine_timeout)
{
  user_timeout_ms = user_timeout;
  machine_timeout_ms = machine_timeout;
  unsigned long now = millis();
  last_user_ms = now;
  last_machine_ms = now;
  initialized = true;
}

void activity_monitor_mark_user_activity()
{
  last_user_ms = millis();
  if (!initialized) {
    last_machine_ms = last_user_ms;
    initialized = true;
  }
}

void activity_monitor_mark_machine_activity()
{
  last_machine_ms = millis();
  if (!initialized) {
    last_user_ms = last_machine_ms;
    initialized = true;
  }
}

static bool is_inactive(unsigned long last_ms, uint32_t timeout_ms, uint32_t now_ms)
{
  if (!initialized || timeout_ms == 0) {
    return false;
  }
  if (now_ms < last_ms) {
    return false;
  }
  return static_cast<uint32_t>(now_ms - last_ms) >= timeout_ms;
}

bool activity_monitor_is_user_inactive(uint32_t now_ms)
{
  return is_inactive(last_user_ms, user_timeout_ms, now_ms);
}

bool activity_monitor_is_machine_inactive(uint32_t now_ms)
{
  return is_inactive(last_machine_ms, machine_timeout_ms, now_ms);
}

unsigned long activity_monitor_last_user_ms()
{
  return last_user_ms;
}

unsigned long activity_monitor_last_machine_ms()
{
  return last_machine_ms;
}
