#include <pebble.h>
#include "schedule.h"
#include "persist.h"
#include "model.h"
#include "health.h"
#include "queue.h"
#include "ui.h"

char s_y_date[12];
char s_t_date[12];

void schedule_wakeup(void) {
  int h = 9;
  int m = 0;
  if (persist_exists(KEY_SYNC_HOUR)) h = persist_read_int(KEY_SYNC_HOUR);
  if (persist_exists(KEY_SYNC_MINUTE)) m = persist_read_int(KEY_SYNC_MINUTE);
  if (h < 0 || h > 23) h = 9;
  if (m < 0 || m > 59) m = 0;
  time_t now = time(NULL);
  time_t today_start = time_start_of_today();
  time_t target = today_start + h * 3600 + m * 60;
  if (target <= now) target += 86400;
  if (persist_exists(KEY_WAKEUP_ID)) {
    WakeupId old = (WakeupId)persist_read_int(KEY_WAKEUP_ID);
    wakeup_cancel(old);
  }
  WakeupId id = wakeup_schedule(target, 0, true);
  if ((int)id >= 0) {
    persist_write_int(KEY_WAKEUP_ID, (int)id);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "wakeup %d:%02d id %d target %ld", h, m, (int)id, (long)target);
  } else {
    persist_delete(KEY_WAKEUP_ID);
  }
}

void wakeup_handler(WakeupId id, int32_t cookie) {
  (void)id; (void)cookie;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "wakeup fired");
  s_pending_wakeup = true;
  if (s_window && connection_service_peek_pebblekit_connection()) {
    s_pending_wakeup = false;
    try_daily_sync(false);
    if (s_wakeup_launch && persist_exists(KEY_QUEUED_Y_DATE)) {
      char y[12];
      persist_read_string(KEY_QUEUED_Y_DATE, y, sizeof(y));
      if (is_already_synced(y)) {
        if (!s_exit_timer) s_exit_timer = app_timer_register(2000, exit_timer_callback, NULL);
      }
    }
  }
  schedule_wakeup();
}

void try_daily_sync(bool force) {
  if (!has_health()) { set_status("No Health"); return; }
  if (persist_exists(KEY_QUEUED_PENDING) && persist_read_bool(KEY_QUEUED_PENDING)) { send_queued(); return; }
  time_t now = time(NULL);
  time_t today_start = time_start_of_today();
  time_t y0 = today_start - 86400;
  time_t y1 = today_start - 1;
  WellnessDay y = {0};
  WellnessDay tt = {0};
  query_day(y0, y1, &y);
  query_day(today_start, now, &tt);
  s_cached_y = y;
  s_cached_t = tt;
  s_has_cache = true;
  strncpy(s_y_date, y.date, sizeof(s_y_date));
  strncpy(s_t_date, tt.date, sizeof(s_t_date));
  if (s_rhr_layer) update_display();
  if (!force && y.date[0] && is_already_synced(y.date)) {
    if (tt.date[0]) {
      WellnessDay *py = NULL;
      if (force) py = &y;
      queue_wellness(py, &tt);
      return;
    }
    return;
  }
  queue_wellness(&y, &tt);
}

void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  static char s_time_buf[16];
  strftime(s_time_buf, sizeof(s_time_buf), "%H:%M", tick_time);
  text_layer_set_text(s_time_layer, s_time_buf);
  update_display();
  if (s_pending_wakeup && connection_service_peek_pebblekit_connection()) {
    s_pending_wakeup = false;
    try_daily_sync(false);
    if (s_wakeup_launch && s_y_date[0] && is_already_synced(s_y_date)) {
      if (!s_exit_timer) s_exit_timer = app_timer_register(2000, exit_timer_callback, NULL);
    }
  }
  int sync_h = 9; int sync_m = 0;
  if (persist_exists(KEY_SYNC_HOUR)) sync_h = persist_read_int(KEY_SYNC_HOUR);
  if (persist_exists(KEY_SYNC_MINUTE)) sync_m = persist_read_int(KEY_SYNC_MINUTE);
  if (tick_time->tm_hour == sync_h && tick_time->tm_min == sync_m) {
    if (persist_exists(KEY_QUEUED_PENDING) && persist_read_bool(KEY_QUEUED_PENDING)) { send_queued(); return; }
    time_t today_start = time_start_of_today();
    WellnessDay y = {0};
    query_day(today_start - 86400, today_start - 1, &y);
    if (!is_already_synced(y.date)) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "scheduled sync %d:%02d", sync_h, sync_m);
      try_daily_sync(false);
    }
  } else {
    if (persist_exists(KEY_QUEUED_PENDING) && persist_read_bool(KEY_QUEUED_PENDING)) {
      if (tick_time->tm_min % 5 == 0) send_queued();
    }
  }
}

void health_handler(HealthEventType event, void *ctx) {
  if (event == HealthEventMovementUpdate || event == HealthEventSignificantUpdate || event == HealthEventSleepUpdate || event == HealthEventHeartRateUpdate) update_display();
}
