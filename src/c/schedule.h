#pragma once
#include <pebble.h>
extern char s_y_date[12];
extern char s_t_date[12];
void schedule_wakeup(void);
void wakeup_handler(WakeupId id, int32_t cookie);
void try_daily_sync(bool force);
void tick_handler(struct tm *tick_time, TimeUnits units_changed);
void health_handler(HealthEventType event, void *ctx);
