#pragma once
#include <pebble.h>
#include "model.h"
bool has_health(void);
int get_steps_today(void);
void format_date(time_t t, char *buf, size_t len);
void format_sleep(int secs, char *buf, size_t len);
int calc_sleep_score(int total, int restful, int rhr, int shr);
int calc_sleep_quality(int score);
int query_day(time_t start, time_t end, WellnessDay *out);
