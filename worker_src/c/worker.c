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

#define PPI_BUF_SIZE 256
#define BURST_BUF_SIZE 200

static bool s_hrv_sampling = false;
static uint16_t s_ppi_buf[PPI_BUF_SIZE];
static int s_ppi_cnt = 0;
static int s_burst_rmssd[BURST_BUF_SIZE];
static int s_burst_sdnn[BURST_BUF_SIZE];
static int s_burst_cnt = 0;
static bool s_was_active = false;

static bool hrv_window_active(void) {
  int sh = 22; int sm = 0; int eh = 8; int em = 0;
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

static bool duty_active(void) {
  if (!hrv_window_active()) return false;
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
  if (s_burst_cnt == 0) return;
  int tmp_rmssd[BURST_BUF_SIZE];
  int tmp_sdnn[BURST_BUF_SIZE];
  for (int i = 0; i < s_burst_cnt; i++) { tmp_rmssd[i] = s_burst_rmssd[i]; tmp_sdnn[i] = s_burst_sdnn[i]; }
  for (int i = 0; i < s_burst_cnt; i++) for (int j = i+1; j < s_burst_cnt; j++) if (tmp_rmssd[j] < tmp_rmssd[i]) { int t = tmp_rmssd[i]; tmp_rmssd[i] = tmp_rmssd[j]; tmp_rmssd[j] = t; }
  for (int i = 0; i < s_burst_cnt; i++) for (int j = i+1; j < s_burst_cnt; j++) if (tmp_sdnn[j] < tmp_sdnn[i]) { int t = tmp_sdnn[i]; tmp_sdnn[i] = tmp_sdnn[j]; tmp_sdnn[j] = t; }
  int median_rmssd = tmp_rmssd[s_burst_cnt/2];
  int median_sdnn = tmp_sdnn[s_burst_cnt/2];
  char date[12];
  format_date(time(NULL), date, sizeof(date));
  persist_write_int(KEY_HRV_NIGHT_RMSSD, median_rmssd);
  persist_write_int(KEY_HRV_NIGHT_SDNN, median_sdnn);
  persist_write_string(KEY_HRV_NIGHT_DATE, date);
  s_burst_cnt = 0;
}

static void hrv_event_handler(HealthEventType event, void *ctx) {
  if (event == HealthEventHRVUpdate) {
    uint16_t ppi = health_service_peek_hrv_ppi_ms();
    if (ppi > 250 && ppi < 2200 && s_ppi_cnt < PPI_BUF_SIZE) {
      s_ppi_buf[s_ppi_cnt++] = ppi;
    }
  }
}

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  bool active = hrv_window_active();
  bool duty = duty_active();
  bool should_sample = active && duty;
  if (should_sample && !s_hrv_sampling) {
    health_service_set_hrv_sample_period(10);
    s_hrv_sampling = true;
    s_ppi_cnt = 0;
  } else if (!should_sample && s_hrv_sampling) {
    health_service_set_hrv_sample_period(0);
    s_hrv_sampling = false;
    burst_end();
  }
  if (!active && s_was_active) {
    if (s_ppi_cnt > 0) burst_end();
    night_end();
  }
  s_was_active = active;
}

int main(void) {
#if PBL_API_EXISTS(health_service_peek_hrv_ppi_ms)
  health_service_events_subscribe(hrv_event_handler, NULL);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  tick_handler(t, MINUTE_UNIT);
  worker_event_loop();
#else
  worker_event_loop();
#endif
}
