#include <pebble_worker.h>

#define KEY_HRV_START_HOUR 40
#define KEY_HRV_START_MINUTE 41
#define KEY_HRV_END_HOUR 42
#define KEY_HRV_END_MINUTE 43
#define KEY_HRV_NIGHT_RMSSD 44
#define KEY_HRV_NIGHT_SDNN 45
#define KEY_HRV_NIGHT_DATE 46
#define KEY_HRV_RING_CNT 51
#define KEY_HRV_RING_RMSSD 52
#define KEY_HRV_RING_SDNN 53

#define PPI_BUF_SIZE 64
#define BURST_BUF_SIZE 50

static bool s_hrv_sampling = false;
static uint16_t s_ppi_buf[PPI_BUF_SIZE];
static int s_ppi_cnt = 0;
static int s_burst_rmssd[BURST_BUF_SIZE];
static int s_burst_sdnn[BURST_BUF_SIZE];
static int s_burst_cnt = 0;
static bool s_was_active = false;

// Static date buffers to avoid stack allocation
static char s_today_buf[12];
static char s_nightly_buf[12];
static int s_tmp_rmssd[BURST_BUF_SIZE];
static int s_tmp_sdnn[BURST_BUF_SIZE];
static char s_date_buf[12];

static int quickselect_median(int *arr, int n);

static void worker_log(const char *msg) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "worker: %s", msg);
}

static void worker_log_int(const char *msg, int val) {
  APP_LOG(APP_LOG_LEVEL_DEBUG, "worker: %s %d", msg, val);
}

static bool hrv_window_active(void) {
  worker_log("hrv_window_active start");
  int sh = 22; int sm = 0; int eh = 8; int em = 0;
  worker_log("hrv_window_active persist read start");
  if (persist_exists(KEY_HRV_START_HOUR)) sh = persist_read_int(KEY_HRV_START_HOUR);
  if (persist_exists(KEY_HRV_START_MINUTE)) sm = persist_read_int(KEY_HRV_START_MINUTE);
  if (persist_exists(KEY_HRV_END_HOUR)) eh = persist_read_int(KEY_HRV_END_HOUR);
  if (persist_exists(KEY_HRV_END_MINUTE)) em = persist_read_int(KEY_HRV_END_MINUTE);
  worker_log("hrv_window_active persist read done");
  if (sh < 0 || sh > 23) sh = 22;
  if (eh < 0 || eh > 23) eh = 8;
  if (sm < 0 || sm > 59) sm = 0;
  if (em < 0 || em > 59) em = 0;
  if (sh == eh && sm == em) return false;
  worker_log("hrv_window_active time start");
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  worker_log("hrv_window_active time done");
  int cur = t->tm_hour * 60 + t->tm_min;
  int start = sh * 60 + sm;
  int end = eh * 60 + em;
  if (start < end) return cur >= start && cur < end;
  return cur >= start || cur < end;
}

static bool duty_active(void) {
  worker_log("duty_active start");
  if (!hrv_window_active()) {
    worker_log("duty_active hrv_window_active false");
    worker_log("duty_active returning false");
    return false;
  }
  worker_log("duty_active hrv_window_active true");
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int mins = t->tm_hour * 60 + t->tm_min;
  int sh = 22; int sm = 0;
  if (persist_exists(KEY_HRV_START_HOUR)) sh = persist_read_int(KEY_HRV_START_HOUR);
  if (persist_exists(KEY_HRV_START_MINUTE)) sm = persist_read_int(KEY_HRV_START_MINUTE);
  int start = sh * 60 + sm;
  int elapsed = mins - start;
  if (elapsed < 0) elapsed += 1440;
  return (elapsed % 15) < 3;
}

static void format_date(time_t t, char *buf, size_t len) {
  struct tm *tm = localtime(&t);
  strftime(buf, len, "%Y-%m-%d", tm);
}

static void calc_rmssd_sdnn(uint16_t *buf, int n, int *out_rmssd, int *out_sdnn) {
  worker_log_int("calc_rmssd_sdnn n", n);
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
  worker_log_int("burst_end ppi_cnt", s_ppi_cnt);
  worker_log_int("burst_end burst_cnt", s_burst_cnt);
  if (s_ppi_cnt < 2) { s_ppi_cnt = 0; return; }
  int rmssd, sdnn;
  calc_rmssd_sdnn(s_ppi_buf, s_ppi_cnt, &rmssd, &sdnn);
  if (rmssd > 0 && s_burst_cnt < BURST_BUF_SIZE) {
    s_burst_rmssd[s_burst_cnt] = rmssd;
    s_burst_sdnn[s_burst_cnt] = sdnn;
    s_burst_cnt++;
    persist_write_int(KEY_HRV_RING_CNT, s_burst_cnt);
    persist_write_data(KEY_HRV_RING_RMSSD, s_burst_rmssd, s_burst_cnt * sizeof(int));
    persist_write_data(KEY_HRV_RING_SDNN, s_burst_sdnn, s_burst_cnt * sizeof(int));
  }
  s_ppi_cnt = 0;
}

static void night_end(void) {
  worker_log_int("night_end burst_cnt", s_burst_cnt);
  if (s_burst_cnt == 0) return;
  worker_log("night_end copy");
  for (int i = 0; i < s_burst_cnt; i++) {
    s_tmp_rmssd[i] = s_burst_rmssd[i];
    s_tmp_sdnn[i] = s_burst_sdnn[i];
  }
  worker_log("night_end quickselect");
  int median_rmssd = quickselect_median(s_tmp_rmssd, s_burst_cnt);
  int median_sdnn = quickselect_median(s_tmp_sdnn, s_burst_cnt);
  worker_log("night_end persist_write");
  persist_write_int(KEY_HRV_NIGHT_RMSSD, median_rmssd);
  persist_write_int(KEY_HRV_NIGHT_SDNN, median_sdnn);
  s_burst_cnt = 0;
  worker_log("night_end done");
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
  if (event == HealthEventHRVUpdate) {
    uint16_t ppi = health_service_peek_hrv_ppi_ms();
    if (ppi > 250 && ppi < 2200 && s_ppi_cnt < PPI_BUF_SIZE) {
      s_ppi_buf[s_ppi_cnt++] = ppi;
      if (s_ppi_cnt % 50 == 0) {
        worker_log_int("hrv_event ppi", ppi);
        worker_log_int("hrv_event cnt", s_ppi_cnt);
      }
    }
  }
}

static void restore_bursts(void) {
  worker_log("restore_bursts");
  if (!persist_exists(KEY_HRV_RING_CNT)) return;
  int cnt = persist_read_int(KEY_HRV_RING_CNT);
  if (cnt <= 0 || cnt > BURST_BUF_SIZE) return;
  int sz_r = persist_get_size(KEY_HRV_RING_RMSSD);
  int sz_s = persist_get_size(KEY_HRV_RING_SDNN);
  if (sz_r != cnt * (int)sizeof(int) || sz_s != cnt * (int)sizeof(int)) return;
  persist_read_data(KEY_HRV_RING_RMSSD, s_burst_rmssd, sz_r);
  persist_read_data(KEY_HRV_RING_SDNN, s_burst_sdnn, sz_s);
  s_burst_cnt = cnt;
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  worker_log_int("tick_handler hour", tick_time->tm_hour);
  worker_log_int("tick_handler min", tick_time->tm_min);
  worker_log("tick_handler hrv_window_active");
  bool active = hrv_window_active();
  worker_log_int("tick_handler active", active);
  worker_log("tick_handler duty_active");
  bool duty = duty_active();
  worker_log_int("tick_handler duty", duty);
  bool should_sample = active && duty;
  worker_log_int("tick_handler should_sample", should_sample);
  worker_log_int("tick_handler s_hrv_sampling", s_hrv_sampling);
  if (should_sample && !s_hrv_sampling) {
    worker_log("tick_handler starting sampling");
    health_service_set_hrv_sample_period(30);
    s_hrv_sampling = true;
    s_ppi_cnt = 0;
  } else if (!should_sample && s_hrv_sampling) {
    worker_log("tick_handler stopping sampling");
    health_service_set_hrv_sample_period(0);
    s_hrv_sampling = false;
    burst_end();
  }
  worker_log_int("tick_handler s_was_active", s_was_active);
  worker_log_int("tick_handler s_burst_cnt", s_burst_cnt);
  if (!active && s_was_active) {
    worker_log("tick_handler branch 1: !active && s_was_active");
    if (s_ppi_cnt > 0) burst_end();
    night_end();
  } else if (!active && !s_was_active && s_burst_cnt == 0) {
    worker_log("tick_handler branch 2: !active && !s_was_active && s_burst_cnt == 0");
    restore_bursts();
    if (s_burst_cnt > 0) {
      if (s_ppi_cnt > 0) burst_end();
      night_end();
    }
  } else if (!active && s_burst_cnt > 0) {
    worker_log("tick_handler branch 3 start");
    worker_log("tick_handler branch 3 calling night_end");
    if (s_ppi_cnt > 0) burst_end();
    night_end();
    worker_log("tick_handler branch 3 done");
  }
  worker_log("tick_handler end");
  s_was_active = active;
}

int main(void) {
  worker_log("main start");
#if PBL_API_EXISTS(health_service_peek_hrv_ppi_ms)
  restore_bursts();
  worker_log("after restore_bursts");
  health_service_events_subscribe(hrv_event_handler, NULL);
  worker_log("after health_service_events_subscribe");
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  worker_log("after tick_timer_service_subscribe");
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  tick_handler(t, MINUTE_UNIT);
  worker_log("after initial tick_handler");
  worker_event_loop();
#else
  worker_event_loop();
#endif
}
