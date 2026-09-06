#pragma once
#include <pebble.h>
#include "model.h"
extern WellnessDay s_cached_y;
extern WellnessDay s_cached_t;
extern bool s_has_cache;
void send_queued(void);
void queue_wellness(WellnessDay *y, WellnessDay *t);
void send_wellness(WellnessDay *y, WellnessDay *t);
bool is_already_synced(const char *date_str);
