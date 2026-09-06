#include <pebble.h>
#include "health.h"
#include "persist.h"
#include "model.h"

void format_date(time_t t, char *buf, size_t len) {
  struct tm *tm = localtime(&t);
  strftime(buf, len, "%Y-%m-%d", tm);
}

bool has_health(void) {
#if defined(PBL_HEALTH)
  return true;
#else
  return false;
#endif
}

bool has_hr_sensor(void) {
#if !defined(PBL_HEALTH)
  return false;
#else
  time_t n = time(NULL);
  HealthServiceAccessibilityMask m = health_service_metric_aggregate_averaged_accessible(HealthMetricHeartRateBPM, n, n, HealthAggregationAvg, HealthServiceTimeScopeOnce);
  if (m & HealthServiceAccessibilityMaskNotSupported) return false;
  return true;
#endif
}

#if PBL_API_EXISTS(health_service_peek_hrv_ppi_ms)
static bool s_hrv_sampling = false;
#endif

bool hrv_window_active(void) {
  int sh = 22;
  int sm = 0;
  int eh = 8;
  int em = 0;
  if (persist_exists(KEY_HRV_START_HOUR)) sh = persist_read_int(KEY_HRV_START_HOUR);
  if (persist_exists(KEY_HRV_START_MINUTE)) sm = persist_read_int(KEY_HRV_START_MINUTE);
  if (persist_exists(KEY_HRV_END_HOUR)) eh = persist_read_int(KEY_HRV_END_HOUR);
  if (persist_exists(KEY_HRV_END_MINUTE)) em = persist_read_int(KEY_HRV_END_MINUTE);
  if (sh < 0 || sh > 23) sh = 22;
  if (eh < 0 || eh > 23) eh = 8;
  if (sm < 0 || sm > 59) sm = 0;
  if (em < 0 || em > 59) em = 0;
  if (sh == eh && sm == em) return false;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int cur = t->tm_hour * 60 + t->tm_min;
  int start = sh * 60 + sm;
  int end = eh * 60 + em;
  if (start < end) return cur >= start && cur < end;
  return cur >= start || cur < end;
}

void hrv_window_update(void) {
#if PBL_API_EXISTS(health_service_peek_hrv_ppi_ms)
  if (!has_hr_sensor()) return;
  bool active = hrv_window_active();
  if (active && !s_hrv_sampling) {
    if (health_service_set_hrv_sample_period(10)) {
      s_hrv_sampling = true;
      APP_LOG(APP_LOG_LEVEL_DEBUG, "HRV window ON");
    }
  } else if (!active && s_hrv_sampling) {
    health_service_set_hrv_sample_period(0);
    s_hrv_sampling = false;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "HRV window OFF");
  }
#else
  (void)0;
#endif
}

int get_steps_today(void) {
#if defined(PBL_HEALTH)
  HealthServiceAccessibilityMask m = health_service_metric_accessible(HealthMetricStepCount, time_start_of_today(), time(NULL));
  if (m & HealthServiceAccessibilityMaskAvailable) return (int)health_service_sum_today(HealthMetricStepCount);
#endif
  return 0;
}

void format_sleep(int secs, char *buf, size_t len) {
  if (secs <= 0) snprintf(buf, len, "-- sleep");
  else snprintf(buf, len, "%dh%02dm sleep", secs / 3600, (secs % 3600) / 60);
}

int calc_sleep_score(int total, int restful, int rhr, int shr) {
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

int calc_sleep_score_no_hr(int total, int restful) {
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
  int score = (durationScore * 60 + restfulScore * 40) / 100;
  if (score < 0) score = 0;
  if (score > 100) score = 100;
  return score;
}

int calc_sleep_quality(int score) {
  if (score >= 90) return 1;
  if (score >= 80) return 2;
  if (score >= 60) return 3;
  if (score > 0) return 4;
  return 0;
}

int query_day(time_t start, time_t end, WellnessDay *out) {
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
  out->hrv = 0; out->hrvSDNN = 0;
#if PBL_API_EXISTS(health_service_peek_hrv_ppi_ms)
  if (persist_exists(KEY_HRV_NIGHT_RMSSD) && persist_exists(KEY_HRV_NIGHT_DATE)) {
    char hrv_date[12]; persist_read_string(KEY_HRV_NIGHT_DATE, hrv_date, sizeof(hrv_date));
    if (strcmp(hrv_date, out->date) == 0) {
      out->hrv = persist_read_int(KEY_HRV_NIGHT_RMSSD);
      out->hrvSDNN = persist_read_int(KEY_HRV_NIGHT_SDNN);
    }
  }
#endif
  bool hrSensor = has_hr_sensor();
  if (!hrSensor) {
    out->rhr = 0;
    out->shr = 0;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "query %s no HR sensor restful %d", out->date, restful);
    out->sleepScore = calc_sleep_score_no_hr(out->sleep, restful);
    out->sleepQuality = calc_sleep_quality(out->sleepScore);
  } else {
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
    if (hr_count > 0 && out->rhr > 0 && out->shr > 0) {
      out->sleepScore = calc_sleep_score(out->sleep, restful, out->rhr, out->shr);
    } else {
      out->rhr = 0;
      out->shr = 0;
      out->sleepScore = calc_sleep_score_no_hr(out->sleep, restful);
    }
    out->sleepQuality = calc_sleep_quality(out->sleepScore);
    APP_LOG(APP_LOG_LEVEL_DEBUG, "query %s hr_min %d avg %d cnt %d restful %d score %d qual %d", out->date, out->rhr, out->shr, hr_count, restful, out->sleepScore, out->sleepQuality);
  }
#else
  (void)start; (void)end;
#endif
  return 0;
}
