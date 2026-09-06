#include <pebble.h>

#define KEY_SYNC_HOUR 10
#define KEY_SYNC_MINUTE 11
#define KEY_LAST_SYNC_DATE 12
#define KEY_QUEUED_PENDING 15
#define KEY_WAKEUP_ID 16
#define KEY_QUEUED_Y_DATE 20
#define KEY_QUEUED_Y_STEPS 21
#define KEY_QUEUED_Y_SLEEP 22
#define KEY_QUEUED_Y_RHR 23
#define KEY_QUEUED_Y_SHR 24
#define KEY_QUEUED_Y_SCORE 30
#define KEY_QUEUED_T_DATE 25
#define KEY_QUEUED_T_STEPS 26
#define KEY_QUEUED_T_SLEEP 27
#define KEY_QUEUED_T_RHR 28
#define KEY_QUEUED_T_SHR 29
#define KEY_QUEUED_T_SCORE 31

static Window *s_window;
static TextLayer *s_time_layer;
static TextLayer *s_steps_layer;
static TextLayer *s_sleep_layer;
static TextLayer *s_rhr_layer;
static TextLayer *s_shr_layer;
static TextLayer *s_score_layer;
static TextLayer *s_status_layer;
static char s_time_buf[16];
static char s_steps_buf[32];
static char s_sleep_buf[32];
static char s_rhr_buf[32];
static char s_shr_buf[32];
static char s_score_buf[32];
static char s_status_buf[64];
static char s_y_date[12];
static char s_t_date[12];
static bool s_wakeup_launch = false;
static bool s_pending_wakeup = false;
static AppTimer *s_exit_timer = NULL;

static void format_date(time_t t, char *buf, size_t len) {
  struct tm *tm = localtime(&t);
  strftime(buf, len, "%Y-%m-%d", tm);
}

static bool has_health(void) {
#if defined(PBL_HEALTH)
  return true;
#else
  return false;
#endif
}

static int get_steps_today(void) {
#if defined(PBL_HEALTH)
  HealthServiceAccessibilityMask m = health_service_metric_accessible(HealthMetricStepCount, time_start_of_today(), time(NULL));
  if (m & HealthServiceAccessibilityMaskAvailable) return (int)health_service_sum_today(HealthMetricStepCount);
#endif
  return 0;
}

static void format_sleep(int secs, char *buf, size_t len) {
  if (secs <= 0) snprintf(buf, len, "-- sleep");
  else snprintf(buf, len, "%dh%02dm sleep", secs / 3600, (secs % 3600) / 60);
}

static int calc_sleep_score(int total, int restful, int rhr, int shr) {
  if (total <= 0) return 0;
  int durationScore = total * 100 / 28800;
  if (durationScore > 100) durationScore = 100;
  int restfulScore;
  if (restful <= 0) restfulScore = 50;
  else {
    int ratioPct = restful * 100 / total;
    restfulScore = ratioPct * 100 / 25;
    if (restfulScore > 100) restfulScore = 100;
  }
  int hrScore;
  if (rhr <= 0 || shr <= 0) hrScore = 50;
  else {
    int delta = rhr - shr;
    int add = delta * 500 / rhr;
    hrScore = 60 + add;
    if (hrScore < 0) hrScore = 0;
    if (hrScore > 100) hrScore = 100;
  }
  int score = (durationScore * 40 + restfulScore * 35 + hrScore * 25) / 100;
  if (score < 0) score = 0;
  if (score > 100) score = 100;
  return score;
}

typedef struct {
  int steps;
  int sleep;
  int rhr;
  int shr;
  int sleepScore;
  char date[12];
} WellnessDay;

static WellnessDay s_cached_y;
static WellnessDay s_cached_t;
static bool s_has_cache = false;

static int query_day(time_t start, time_t end, WellnessDay *out);

static void update_display(void) {
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
      if (persist_exists(KEY_QUEUED_T_DATE)) {
        persist_read_string(KEY_QUEUED_T_DATE, s_cached_t.date, sizeof(s_cached_t.date));
        s_cached_t.rhr = persist_read_int(KEY_QUEUED_T_RHR);
        s_cached_t.shr = persist_read_int(KEY_QUEUED_T_SHR);
        if (persist_exists(KEY_QUEUED_T_SCORE)) s_cached_t.sleepScore = persist_read_int(KEY_QUEUED_T_SCORE);
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

static void set_status(const char *msg) {
  snprintf(s_status_buf, sizeof(s_status_buf), "%s", msg);
  if (s_status_layer) text_layer_set_text(s_status_layer, s_status_buf);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "status %s", msg);
}

static void exit_timer_callback(void *data) {
  window_stack_pop_all(true);
}

static int query_day(time_t start, time_t end, WellnessDay *out) {
  memset(out, 0, sizeof(*out));
  format_date(start, out->date, sizeof(out->date));
#if defined(PBL_HEALTH)
  HealthServiceAccessibilityMask m;
  m = health_service_metric_accessible(HealthMetricStepCount, start, end);
  if (m & HealthServiceAccessibilityMaskAvailable) out->steps = (int)health_service_sum(HealthMetricStepCount, start, end);
  m = health_service_metric_accessible(HealthMetricSleepSeconds, start, end);
  if (m & HealthServiceAccessibilityMaskAvailable) out->sleep = (int)health_service_sum(HealthMetricSleepSeconds, start, end);
  int restful = 0;
  m = health_service_metric_accessible(HealthMetricSleepRestfulSeconds, start, end);
  if (m & HealthServiceAccessibilityMaskAvailable) restful = (int)health_service_sum(HealthMetricSleepRestfulSeconds, start, end);
  {
    int hr_min = 255;
    int hr_sum = 0;
    int hr_count = 0;
    time_t cur = start;
    while (cur < end) {
      time_t chunk_start = cur;
      time_t chunk_end = cur + 3600;
      if (chunk_end > end) chunk_end = end;
      HealthMinuteData buf[60];
      time_t s = chunk_start;
      time_t e = chunk_end;
      uint32_t n = health_service_get_minute_history(buf, 60, &s, &e);
      for (uint32_t i = 0; i < n; i++) {
        if (buf[i].is_invalid) continue;
        uint8_t hr = buf[i].heart_rate_bpm;
        if (hr == 0 || hr >= 255) continue;
        if ((int)hr < hr_min) hr_min = hr;
        hr_sum += hr;
        hr_count++;
      }
      if (n == 0) cur += 3600;
      else cur = e;
      if (cur <= chunk_start) cur = chunk_start + 60;
    }
    if (hr_count > 0) {
      out->rhr = hr_min;
      out->shr = (hr_sum + hr_count / 2) / hr_count;
    } else {
      time_t now = time(NULL);
      if (end >= now - 3600 && end <= now + 60) {
        int cur = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
        if (cur > 0 && cur < 255) {
          out->shr = cur;
          out->rhr = cur;
        }
      }
    }
    APP_LOG(APP_LOG_LEVEL_DEBUG, "query %s hr_min %d avg %d cnt %d restful %d", out->date, out->rhr, out->shr, hr_count, restful);
    out->sleepScore = calc_sleep_score(out->sleep, restful, out->rhr, out->shr);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "score %s %d dur %d rest %d", out->date, out->sleepScore, out->sleep, restful);
  }
#else
  (void)start; (void)end;
#endif
  return 0;
}

static void send_wellness(WellnessDay *y, WellnessDay *t);

static void send_queued(void) {
  if (!persist_exists(KEY_QUEUED_PENDING) || !persist_read_bool(KEY_QUEUED_PENDING)) return;
  WellnessDay y = {0};
  WellnessDay tt = {0};
  if (persist_exists(KEY_QUEUED_Y_DATE)) persist_read_string(KEY_QUEUED_Y_DATE, y.date, sizeof(y.date));
  if (persist_exists(KEY_QUEUED_Y_STEPS)) y.steps = persist_read_int(KEY_QUEUED_Y_STEPS);
  if (persist_exists(KEY_QUEUED_Y_SLEEP)) y.sleep = persist_read_int(KEY_QUEUED_Y_SLEEP);
  if (persist_exists(KEY_QUEUED_Y_RHR)) y.rhr = persist_read_int(KEY_QUEUED_Y_RHR);
  if (persist_exists(KEY_QUEUED_Y_SHR)) y.shr = persist_read_int(KEY_QUEUED_Y_SHR);
  if (persist_exists(KEY_QUEUED_Y_SCORE)) y.sleepScore = persist_read_int(KEY_QUEUED_Y_SCORE);
  if (persist_exists(KEY_QUEUED_T_DATE)) persist_read_string(KEY_QUEUED_T_DATE, tt.date, sizeof(tt.date));
  if (persist_exists(KEY_QUEUED_T_STEPS)) tt.steps = persist_read_int(KEY_QUEUED_T_STEPS);
  if (persist_exists(KEY_QUEUED_T_SLEEP)) tt.sleep = persist_read_int(KEY_QUEUED_T_SLEEP);
  if (persist_exists(KEY_QUEUED_T_RHR)) tt.rhr = persist_read_int(KEY_QUEUED_T_RHR);
  if (persist_exists(KEY_QUEUED_T_SHR)) tt.shr = persist_read_int(KEY_QUEUED_T_SHR);
  if (persist_exists(KEY_QUEUED_T_SCORE)) tt.sleepScore = persist_read_int(KEY_QUEUED_T_SCORE);
  if (y.date[0] == '\0' && tt.date[0] == '\0') return;
  s_cached_y = y;
  s_cached_t = tt;
  s_has_cache = true;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "retry queued y %s %d t %s %d", y.date, y.steps, tt.date, tt.steps);
  send_wellness(y.date[0] ? &y : NULL, tt.date[0] ? &tt : NULL);
}

static void queue_wellness(WellnessDay *y, WellnessDay *t) {
  if (y) {
    persist_write_string(KEY_QUEUED_Y_DATE, y->date);
    persist_write_int(KEY_QUEUED_Y_STEPS, y->steps);
    persist_write_int(KEY_QUEUED_Y_SLEEP, y->sleep);
    persist_write_int(KEY_QUEUED_Y_RHR, y->rhr);
    persist_write_int(KEY_QUEUED_Y_SHR, y->shr);
    persist_write_int(KEY_QUEUED_Y_SCORE, y->sleepScore);
  } else {
    persist_delete(KEY_QUEUED_Y_DATE);
  }
  if (t) {
    persist_write_string(KEY_QUEUED_T_DATE, t->date);
    persist_write_int(KEY_QUEUED_T_STEPS, t->steps);
    persist_write_int(KEY_QUEUED_T_SLEEP, t->sleep);
    persist_write_int(KEY_QUEUED_T_RHR, t->rhr);
    persist_write_int(KEY_QUEUED_T_SHR, t->shr);
    persist_write_int(KEY_QUEUED_T_SCORE, t->sleepScore);
  } else {
    persist_delete(KEY_QUEUED_T_DATE);
  }
  persist_write_bool(KEY_QUEUED_PENDING, true);
  send_wellness(y, t);
}

static void send_wellness(WellnessDay *y, WellnessDay *t) {
  DictionaryIterator *out;
  AppMessageResult res = app_message_outbox_begin(&out);
  if (res != APP_MSG_OK) {
    set_status("No outbox");
    return;
  }
  if (y) {
    dict_write_int(out, MESSAGE_KEY_Y_STEPS, &y->steps, sizeof(y->steps), true);
    dict_write_int(out, MESSAGE_KEY_Y_SLEEP, &y->sleep, sizeof(y->sleep), true);
    dict_write_int(out, MESSAGE_KEY_Y_RHR, &y->rhr, sizeof(y->rhr), true);
    dict_write_int(out, MESSAGE_KEY_Y_SHR, &y->shr, sizeof(y->shr), true);
    dict_write_int(out, MESSAGE_KEY_Y_SCORE, &y->sleepScore, sizeof(y->sleepScore), true);
    dict_write_cstring(out, MESSAGE_KEY_Y_DATE, y->date);
  }
  if (t) {
    dict_write_int(out, MESSAGE_KEY_T_STEPS, &t->steps, sizeof(t->steps), true);
    dict_write_int(out, MESSAGE_KEY_T_SLEEP, &t->sleep, sizeof(t->sleep), true);
    dict_write_int(out, MESSAGE_KEY_T_RHR, &t->rhr, sizeof(t->rhr), true);
    dict_write_int(out, MESSAGE_KEY_T_SHR, &t->shr, sizeof(t->shr), true);
    dict_write_int(out, MESSAGE_KEY_T_SCORE, &t->sleepScore, sizeof(t->sleepScore), true);
    dict_write_cstring(out, MESSAGE_KEY_T_DATE, t->date);
  }
  int32_t cmd = 0;
  dict_write_int(out, MESSAGE_KEY_CMD, &cmd, sizeof(cmd), true);
  res = app_message_outbox_send();
  if (res != APP_MSG_OK) set_status("Send fail");
  else {
    char tmp[32];
    if (y && t) snprintf(tmp, sizeof(tmp), "Sync %s+%s", y->date + 5, t->date + 5);
    else if (y) snprintf(tmp, sizeof(tmp), "Sync %s", y->date);
    else if (t) snprintf(tmp, sizeof(tmp), "Sync %s", t->date);
    else snprintf(tmp, sizeof(tmp), "Sync");
    set_status(tmp);
  }
}

static bool is_already_synced(const char *date_str) {
  if (!persist_exists(KEY_LAST_SYNC_DATE)) return false;
  char last[12];
  persist_read_string(KEY_LAST_SYNC_DATE, last, sizeof(last));
  return strcmp(last, date_str) == 0;
}

static void try_daily_sync(bool force);

static void schedule_wakeup(void) {
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

static void wakeup_handler(WakeupId id, int32_t cookie) {
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

static void try_daily_sync(bool force) {
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

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
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

#if defined(PBL_HEALTH)
static void health_handler(HealthEventType event, void *ctx) {
  if (event == HealthEventMovementUpdate || event == HealthEventSignificantUpdate || event == HealthEventSleepUpdate || event == HealthEventHeartRateUpdate) update_display();
}
#endif

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *t;
  t = dict_find(iter, MESSAGE_KEY_SYNC_HOUR);
  if (t) {
    int v = 9;
    if (t->type == TUPLE_CSTRING) v = atoi(t->value->cstring);
    else if (t->type == TUPLE_INT) v = (int)t->value->int32;
    else if (t->type == TUPLE_UINT) v = (int)t->value->uint32;
    if (v < 0 || v > 23) v = 9;
    persist_write_int(KEY_SYNC_HOUR, v);
    schedule_wakeup();
  }
  t = dict_find(iter, MESSAGE_KEY_SYNC_MINUTE);
  if (t) {
    int v = 0;
    if (t->type == TUPLE_CSTRING) v = atoi(t->value->cstring);
    else if (t->type == TUPLE_INT) v = (int)t->value->int32;
    else if (t->type == TUPLE_UINT) v = (int)t->value->uint32;
    if (v < 0 || v > 59) v = 0;
    persist_write_int(KEY_SYNC_MINUTE, v);
    schedule_wakeup();
  }
  t = dict_find(iter, MESSAGE_KEY_STATUS);
  if (t) {
    const char *msg = t->value->cstring;
    set_status(msg);
    if (strncmp(msg, "OK", 2) == 0) {
      if (persist_exists(KEY_QUEUED_Y_DATE)) {
        char q[12];
        persist_read_string(KEY_QUEUED_Y_DATE, q, sizeof(q));
        persist_write_string(KEY_LAST_SYNC_DATE, q);
      }
      persist_write_bool(KEY_QUEUED_PENDING, false);
      if (s_wakeup_launch) {
        if (!s_exit_timer) s_exit_timer = app_timer_register(2000, exit_timer_callback, NULL);
      }
      update_display();
    }
  }
}

static void inbox_dropped_handler(AppMessageResult reason, void *context) {
  set_status("Inbox drop");
}

static void outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason, void *context) {
  set_status("No phone");
}

static void outbox_sent_handler(DictionaryIterator *iter, void *context) {}

static void select_click_handler(ClickRecognizerRef ref, void *ctx) {
  set_status("Sync now");
  try_daily_sync(true);
}

static void click_config_provider(void *ctx) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

static void window_load(Window *window) {
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

static void window_unload(Window *window) {
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

static void init(void) {
  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_inbox_dropped(inbox_dropped_handler);
  app_message_register_outbox_failed(outbox_failed_handler);
  app_message_register_outbox_sent(outbox_sent_handler);
  app_message_open(512, 512);
  s_wakeup_launch = (launch_reason() == APP_LAUNCH_WAKEUP);
  wakeup_service_subscribe(wakeup_handler);
  if (s_wakeup_launch) {
    WakeupId id; int32_t cookie;
    if (wakeup_get_launch_event(&id, &cookie)) wakeup_handler(id, cookie);
    else { s_pending_wakeup = true; schedule_wakeup(); }
  } else schedule_wakeup();
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);
}

static void deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
