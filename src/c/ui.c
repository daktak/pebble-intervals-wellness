#include <pebble.h>
#include "ui.h"
#include "health.h"
#include "queue.h"
#include "persist.h"
#include "model.h"
#include "schedule.h"

Window *s_window;
TextLayer *s_time_layer;
TextLayer *s_steps_layer;
TextLayer *s_sleep_layer;
TextLayer *s_rhr_layer;
TextLayer *s_shr_layer;
TextLayer *s_score_layer;
TextLayer *s_status_layer;
AppTimer *s_exit_timer = NULL;
bool s_wakeup_launch = false;
bool s_pending_wakeup = false;
static char s_steps_buf[32];
static char s_sleep_buf[32];
static char s_rhr_buf[32];
static char s_shr_buf[32];
static char s_score_buf[32];
static char s_status_buf[64];

void set_status(const char *msg) {
  snprintf(s_status_buf, sizeof(s_status_buf), "%s", msg);
  if (s_status_layer) text_layer_set_text(s_status_layer, s_status_buf);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "status %s", msg);
}

void exit_timer_callback(void *data) {
  window_stack_pop_all(true);
}

void update_display(void) {
  if (!has_health()) {
    text_layer_set_text(s_steps_layer, "No Health");
    text_layer_set_text(s_sleep_layer, "");
    text_layer_set_text(s_rhr_layer, "");
    text_layer_set_text(s_shr_layer, "");
    text_layer_set_text(s_score_layer, "");
    return;
  }
  int steps = get_steps_today();
  snprintf(s_steps_buf, sizeof(s_steps_buf), "%d steps today", steps);
  text_layer_set_text(s_steps_layer, s_steps_buf);
#if defined(PBL_HEALTH)
  HealthServiceAccessibilityMask sm = health_service_metric_accessible(HealthMetricSleepSeconds, time_start_of_today(), time(NULL));
  int sleep = 0;
  if (sm & HealthServiceAccessibilityMaskAvailable) sleep = (int)health_service_sum_today(HealthMetricSleepSeconds);
  format_sleep(sleep, s_sleep_buf, sizeof(s_sleep_buf));
  text_layer_set_text(s_sleep_layer, s_sleep_buf);
  if (!s_has_cache) {
    if (persist_exists(KEY_QUEUED_Y_DATE)) {
      persist_read_string(KEY_QUEUED_Y_DATE, s_cached_y.date, sizeof(s_cached_y.date));
      s_cached_y.rhr = persist_read_int(KEY_QUEUED_Y_RHR);
      s_cached_y.shr = persist_read_int(KEY_QUEUED_Y_SHR);
      s_cached_y.steps = persist_read_int(KEY_QUEUED_Y_STEPS);
      s_cached_y.sleep = persist_read_int(KEY_QUEUED_Y_SLEEP);
      if (persist_exists(KEY_QUEUED_Y_SCORE)) s_cached_y.sleepScore = persist_read_int(KEY_QUEUED_Y_SCORE);
      if (persist_exists(KEY_QUEUED_Y_QUALITY)) s_cached_y.sleepQuality = persist_read_int(KEY_QUEUED_Y_QUALITY);
      if (persist_exists(KEY_QUEUED_Y_HRV)) s_cached_y.hrv = persist_read_int(KEY_QUEUED_Y_HRV);
      if (persist_exists(KEY_QUEUED_Y_HRVSDNN)) s_cached_y.hrvSDNN = persist_read_int(KEY_QUEUED_Y_HRVSDNN);
      if (persist_exists(KEY_QUEUED_T_DATE)) {
        persist_read_string(KEY_QUEUED_T_DATE, s_cached_t.date, sizeof(s_cached_t.date));
        s_cached_t.rhr = persist_read_int(KEY_QUEUED_T_RHR);
        s_cached_t.shr = persist_read_int(KEY_QUEUED_T_SHR);
        if (persist_exists(KEY_QUEUED_T_SCORE)) s_cached_t.sleepScore = persist_read_int(KEY_QUEUED_T_SCORE);
        if (persist_exists(KEY_QUEUED_T_QUALITY)) s_cached_t.sleepQuality = persist_read_int(KEY_QUEUED_T_QUALITY);
        if (persist_exists(KEY_QUEUED_T_HRV)) s_cached_t.hrv = persist_read_int(KEY_QUEUED_T_HRV);
        if (persist_exists(KEY_QUEUED_T_HRVSDNN)) s_cached_t.hrvSDNN = persist_read_int(KEY_QUEUED_T_HRVSDNN);
      }
      s_has_cache = true;
    } else {
      WellnessDay y = {0};
      time_t today_start = time_start_of_today();
      query_day(today_start - 86400, today_start - 1, &y);
      s_cached_y = y;
      s_cached_t = (WellnessDay){0};
      s_has_cache = true;
    }
  }
  if (s_cached_y.rhr > 0) snprintf(s_rhr_buf, sizeof(s_rhr_buf), "Resting HR %d bpm", s_cached_y.rhr);
  else snprintf(s_rhr_buf, sizeof(s_rhr_buf), "Resting HR --");
  if (s_cached_y.shr > 0) snprintf(s_shr_buf, sizeof(s_shr_buf), "Avg Sleep HR %d bpm", s_cached_y.shr);
  else snprintf(s_shr_buf, sizeof(s_shr_buf), "Avg Sleep HR --");
  text_layer_set_text(s_rhr_layer, s_rhr_buf);
  text_layer_set_text(s_shr_layer, s_shr_buf);
  if (s_cached_y.sleepScore > 0) snprintf(s_score_buf, sizeof(s_score_buf), "Sleep Score %d", s_cached_y.sleepScore);
  else snprintf(s_score_buf, sizeof(s_score_buf), "Sleep Score --");
  text_layer_set_text(s_score_layer, s_score_buf);
#endif
}

static void select_click_handler(ClickRecognizerRef ref, void *ctx) {
  set_status("Sync now");
  try_daily_sync(true);
}

void click_config_provider(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  int w = bounds.size.w;
  s_time_layer = text_layer_create(GRect(0, 8, w, 28));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorWhite);
  text_layer_set_font(s_time_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_time_layer));
  s_steps_layer = text_layer_create(GRect(0, 36, w, 20));
  text_layer_set_background_color(s_steps_layer, GColorClear);
  text_layer_set_text_color(s_steps_layer, GColorWhite);
  text_layer_set_font(s_steps_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_steps_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_steps_layer));
  s_sleep_layer = text_layer_create(GRect(0, 56, w, 18));
  text_layer_set_background_color(s_sleep_layer, GColorClear);
  text_layer_set_text_color(s_sleep_layer, GColorLightGray);
  text_layer_set_font(s_sleep_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_sleep_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_sleep_layer));
  s_rhr_layer = text_layer_create(GRect(0, 74, w, 18));
  text_layer_set_background_color(s_rhr_layer, GColorClear);
  text_layer_set_text_color(s_rhr_layer, GColorLightGray);
  text_layer_set_font(s_rhr_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_rhr_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_rhr_layer));
  s_shr_layer = text_layer_create(GRect(0, 92, w, 18));
  text_layer_set_background_color(s_shr_layer, GColorClear);
  text_layer_set_text_color(s_shr_layer, GColorLightGray);
  text_layer_set_font(s_shr_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_shr_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_shr_layer));
  s_score_layer = text_layer_create(GRect(0, 110, w, 18));
  text_layer_set_background_color(s_score_layer, GColorClear);
  text_layer_set_text_color(s_score_layer, GColorLightGray);
  text_layer_set_font(s_score_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(s_score_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_score_layer));
  s_status_layer = text_layer_create(GRect(5, bounds.size.h - 38, bounds.size.w - 10, 18));
  text_layer_set_background_color(s_status_layer, GColorClear);
  text_layer_set_text_color(s_status_layer, GColorLightGray);
  text_layer_set_font(s_status_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_status_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_status_layer));
  TextLayer *hint = text_layer_create(GRect(0, bounds.size.h - 20, w, 16));
  text_layer_set_background_color(hint, GColorClear);
  text_layer_set_text_color(hint, GColorLightGray);
  text_layer_set_font(hint, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(hint, GTextAlignmentCenter);
  text_layer_set_text(hint, "SELECT to sync");
  layer_add_child(root, text_layer_get_layer(hint));
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  tick_handler(t, MINUTE_UNIT);
  if (s_pending_wakeup && connection_service_peek_pebblekit_connection()) {
    s_pending_wakeup = false;
    try_daily_sync(false);
    if (s_wakeup_launch && s_y_date[0] && is_already_synced(s_y_date)) {
      if (!s_exit_timer) s_exit_timer = app_timer_register(2000, exit_timer_callback, NULL);
    }
  }
  if (!has_health()) set_status("No Health");
  else if (persist_exists(KEY_QUEUED_PENDING) && persist_read_bool(KEY_QUEUED_PENDING)) { set_status("Queued retry"); send_queued(); }
  else if (persist_exists(KEY_LAST_SYNC_DATE)) {
    char last[12];
    persist_read_string(KEY_LAST_SYNC_DATE, last, sizeof(last));
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "Last %s", last);
    set_status(tmp);
  } else set_status("Ready");
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
#if defined(PBL_HEALTH)
  health_service_events_subscribe(health_handler, NULL);
#endif
}

void window_unload(Window *window) {
  tick_timer_service_unsubscribe();
#if defined(PBL_HEALTH)
  health_service_events_unsubscribe();
#endif
  if (s_exit_timer) { app_timer_cancel(s_exit_timer); s_exit_timer = NULL; }
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_steps_layer);
  text_layer_destroy(s_sleep_layer);
  text_layer_destroy(s_rhr_layer);
  text_layer_destroy(s_shr_layer);
  text_layer_destroy(s_score_layer);
  text_layer_destroy(s_status_layer);
}
