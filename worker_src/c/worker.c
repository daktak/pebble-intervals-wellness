#include <pebble_worker.h>

#define KEY_HRV_NIGHT_RMSSD 44
#define KEY_HRV_NIGHT_SDNN 45
#define KEY_HRV_NIGHT_DATE 46
#define KEY_HRV_RING_CNT 51
#define KEY_HRV_RING_RMSSD 52
#define KEY_HRV_RING_SDNN 53

#if PBL_API_EXISTS(health_service_peek_hrv_ppi_ms)
#define PPI_BUF_SIZE 64
#define BURST_BUF_SIZE 50
#define SLEEP_EVENT_IDLE_SECS (10 * 60)
#define MIN_BURSTS 4
#define MIN_SESSION_SECS (2 * 3600)

static bool s_hrv_sampling = false;
static uint16_t s_ppi_buf[PPI_BUF_SIZE];
static int s_ppi_cnt = 0;
static int s_burst_rmssd[BURST_BUF_SIZE];
static int s_burst_sdnn[BURST_BUF_SIZE];
static int s_burst_cnt = 0;
static bool s_finalized = false;
static bool s_restore_idle_done = false;
static bool s_sleeping = false;
static time_t s_last_sleep_event_utc = 0;
static time_t s_session_start_utc = 0;

// Static temp arrays to avoid stack allocation
static int s_tmp_rmssd[BURST_BUF_SIZE];
static int s_tmp_sdnn[BURST_BUF_SIZE];

static int quickselect_median(int *arr, int n);

static bool sleep_active(void) {
  if (!s_sleeping) return false;
  time_t now = time(NULL);
  return (now - s_last_sleep_event_utc) < SLEEP_EVENT_IDLE_SECS;
}

static bool duty_active(void) {
  if (!sleep_active()) return false;
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int mins = t->tm_hour * 60 + t->tm_min;
  return (mins % 15) < 3;
}

static void calc_rmssd_sdnn(uint16_t *buf, int n, int *out_rmssd, int *out_sdnn) {
  *out_rmssd = 0; *out_sdnn = 0;
  if (n < 2) return;
  int sum_diff_sq = 0;
  int valid_diffs = 0;
  for (int i = 1; i < n; i++) {
    int d = (int)buf[i] - (int)buf[i-1];
    if (d < -500 || d > 500) continue;
    sum_diff_sq += d * d;
    valid_diffs++;
  }
  if (valid_diffs > 0) {
    int mean_sq = sum_diff_sq / valid_diffs;
    int rmssd = 0;
    int lo = 0, hi = 500;
    while (lo <= hi) { int mid = (lo+hi)/2; if (mid*mid <= mean_sq) { rmssd = mid; lo = mid+1; } else hi = mid-1; }
    *out_rmssd = rmssd;
  }
  int sum = 0;
  for (int i = 0; i < n; i++) sum += buf[i];
  int mean = sum / n;
  int var_sum = 0;
  for (int i = 0; i < n; i++) { int d = (int)buf[i] - mean; var_sum += d*d; }
  int var = var_sum / n;
  int sdnn = 0; int lo = 0, hi = 500;
  while (lo <= hi) { int mid = (lo+hi)/2; if (mid*mid <= var) { sdnn = mid; lo = mid+1; } else hi = mid-1; }
  *out_sdnn = sdnn;
}

static void burst_end(void) {
  if (s_ppi_cnt < 2) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "burst_end ppi_cnt %d too few", s_ppi_cnt);
    s_ppi_cnt = 0;
    return;
  }
  int rmssd, sdnn;
  calc_rmssd_sdnn(s_ppi_buf, s_ppi_cnt, &rmssd, &sdnn);
  if (rmssd > 0 && s_burst_cnt < BURST_BUF_SIZE) {
    if (s_burst_cnt == 0) s_session_start_utc = time(NULL);
    s_burst_rmssd[s_burst_cnt] = rmssd;
    s_burst_sdnn[s_burst_cnt] = sdnn;
    s_burst_cnt++;
    persist_write_int(KEY_HRV_RING_CNT, s_burst_cnt);
    persist_write_data(KEY_HRV_RING_RMSSD, s_burst_rmssd, s_burst_cnt * sizeof(int));
    persist_write_data(KEY_HRV_RING_SDNN, s_burst_sdnn, s_burst_cnt * sizeof(int));
    APP_LOG(APP_LOG_LEVEL_DEBUG, "burst_end rmssd %d sdnn %d ppi %d total %d", rmssd, sdnn, s_ppi_cnt, s_burst_cnt);
  } else {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "burst_end rmssd %d ppi %d discarded (buf full %d)", rmssd, s_ppi_cnt, s_burst_cnt);
  }
  s_ppi_cnt = 0;
}

static void night_end(struct tm *tick_time) {
  bool reject = false;
  if (s_burst_cnt < MIN_BURSTS) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "night_end %d bursts too few (min %d) - keeping ring", s_burst_cnt, MIN_BURSTS);
    reject = true;
  }
  time_t now = time(NULL);
  if (!reject && s_session_start_utc > 0 && (now - s_session_start_utc) < MIN_SESSION_SECS) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "night_end session too short (%lds) - keeping ring", (long)(now - s_session_start_utc));
    reject = true;
  }
  if (reject) return;
  for (int i = 0; i < s_burst_cnt; i++) {
    s_tmp_rmssd[i] = s_burst_rmssd[i];
    s_tmp_sdnn[i] = s_burst_sdnn[i];
  }
  int median_rmssd = quickselect_median(s_tmp_rmssd, s_burst_cnt);
  int median_sdnn = quickselect_median(s_tmp_sdnn, s_burst_cnt);
  persist_write_int(KEY_HRV_NIGHT_RMSSD, median_rmssd);
  persist_write_int(KEY_HRV_NIGHT_SDNN, median_sdnn);
  if (tick_time) {
    char date[16];
    snprintf(date, sizeof(date), "%04d-%02d-%02d", tick_time->tm_year + 1900, tick_time->tm_mon + 1, tick_time->tm_mday);
    persist_write_data(KEY_HRV_NIGHT_DATE, date, (uint16_t)(strlen(date) + 1));
    APP_LOG(APP_LOG_LEVEL_DEBUG, "night_end med rmssd %d sdnn %d cnt %d date %s", median_rmssd, median_sdnn, s_burst_cnt, date);
  } else {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "night_end med rmssd %d sdnn %d cnt %d (timestamp unavailable)", median_rmssd, median_sdnn, s_burst_cnt);
  }
  persist_write_int(KEY_HRV_RING_CNT, 0);
  s_burst_cnt = 0;
  s_session_start_utc = 0;
}

// Quickselect median - O(n) average, minimal stack
static int quickselect_median(int *arr, int n) {
  int k = n / 2;
  int left = 0, right = n - 1;
  while (left < right) {
    int pivot = arr[(left + right) / 2];
    int i = left, j = right;
    while (i <= j) {
      while (arr[i] < pivot) i++;
      while (arr[j] > pivot) j--;
      if (i <= j) {
        int tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
        i++; j--;
      }
    }
    if (k <= j) right = j;
    else if (k >= i) left = i;
    else break;
  }
  return arr[k];
}

static void hrv_event_handler(HealthEventType event, void *ctx) {
  if (event == HealthEventSleepUpdate) {
    s_sleeping = true;
    s_last_sleep_event_utc = time(NULL);
    return;
  }
  if (event == HealthEventHRVUpdate) {
    uint16_t ppi = health_service_peek_hrv_ppi_ms();
    if (ppi > 250 && ppi < 2200 && s_ppi_cnt < PPI_BUF_SIZE) {
      s_ppi_buf[s_ppi_cnt++] = ppi;
      if (s_ppi_cnt == 1 || s_ppi_cnt % 10 == 0) {
        APP_LOG(APP_LOG_LEVEL_DEBUG, "ppi %u cnt %d", ppi, s_ppi_cnt);
      }
    }
  }
}

static void restore_bursts(void) {
  if (!persist_exists(KEY_HRV_RING_CNT)) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "restore_bursts no ring");
    return;
  }
  int cnt = persist_read_int(KEY_HRV_RING_CNT);
  if (cnt <= 0 || cnt > BURST_BUF_SIZE) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "restore_bursts invalid cnt %d", cnt);
    return;
  }
  int sz_r = persist_get_size(KEY_HRV_RING_RMSSD);
  int sz_s = persist_get_size(KEY_HRV_RING_SDNN);
  if (sz_r != cnt * (int)sizeof(int) || sz_s != cnt * (int)sizeof(int)) {
    APP_LOG(APP_LOG_LEVEL_DEBUG, "restore_bursts size mismatch cnt %d sz_r %d sz_s %d", cnt, sz_r, sz_s);
    return;
  }
  persist_read_data(KEY_HRV_RING_RMSSD, s_burst_rmssd, sz_r);
  persist_read_data(KEY_HRV_RING_SDNN, s_burst_sdnn, sz_s);
  s_burst_cnt = cnt;
  s_session_start_utc = time(NULL);
  APP_LOG(APP_LOG_LEVEL_DEBUG, "restore_bursts cnt %d", cnt);
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  // Authoritative sleep state each tick: the 10-minute idle window can no
  // longer expire mid-night just because the health service stops re-emitting
  // SleepUpdate events during settled sleep.
  if (health_service_peek_current_activities() & HealthActivitySleep) {
    s_sleeping = true;
    s_last_sleep_event_utc = time(NULL);
  }
  bool active = sleep_active();
  bool duty = duty_active();
  bool should_sample = active && duty;
  if (should_sample && !s_hrv_sampling) {
    health_service_set_hrv_sample_period(30);
    s_hrv_sampling = true;
    s_ppi_cnt = 0;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "sampling ON active=%d duty=%d", active, duty);
  } else if (!should_sample && s_hrv_sampling) {
    health_service_set_hrv_sample_period(0);
    s_hrv_sampling = false;
    burst_end();
    APP_LOG(APP_LOG_LEVEL_DEBUG, "sampling OFF active=%d duty=%d", active, duty);
  }

  // Finalize a finished night once per inactive stretch (worst case: the ring
  // survives a too-thin/too-short attempt and the same session continues).
  if (!active && s_burst_cnt > 0 && !s_finalized) {
    s_finalized = true;
    if (s_ppi_cnt > 0) burst_end();
    night_end(tick_time);
  } else if (!active && s_burst_cnt == 0 && !s_restore_idle_done) {
    s_restore_idle_done = true;
    restore_bursts();
    if (s_burst_cnt > 0) {
      s_finalized = true;
      if (s_ppi_cnt > 0) burst_end();
      night_end(tick_time);
    }
  }
  // A new active stretch after a finalize starts a fresh session.
  if (active && s_finalized) {
    s_finalized = false;
  }
}
#endif

int main(void) {
#if PBL_API_EXISTS(health_service_peek_hrv_ppi_ms)
  APP_LOG(APP_LOG_LEVEL_DEBUG, "worker start with HRV API");
  restore_bursts();
  health_service_events_subscribe(hrv_event_handler, NULL);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  tick_handler(t, MINUTE_UNIT);
  worker_event_loop();
#else
  APP_LOG(APP_LOG_LEVEL_DEBUG, "worker start WITHOUT HRV API");
  worker_event_loop();
#endif
}
