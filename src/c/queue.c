#include <pebble.h>
#include "queue.h"
#include "persist.h"
#include "model.h"
#include "ui.h"

WellnessDay s_cached_y;
WellnessDay s_cached_t;
bool s_has_cache = false;

bool is_already_synced(const char *date_str) {
  if (!persist_exists(KEY_LAST_SYNC_DATE)) return false;
  char last[12];
  persist_read_string(KEY_LAST_SYNC_DATE, last, sizeof(last));
  return strcmp(last, date_str) == 0;
}

void send_wellness(WellnessDay *y, WellnessDay *t) {
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
    dict_write_int(out, MESSAGE_KEY_Y_QUALITY, &y->sleepQuality, sizeof(y->sleepQuality), true);
    dict_write_int(out, MESSAGE_KEY_Y_HRV, &y->hrv, sizeof(y->hrv), true);
    dict_write_int(out, MESSAGE_KEY_Y_HRVSDNN, &y->hrvSDNN, sizeof(y->hrvSDNN), true);
    dict_write_cstring(out, MESSAGE_KEY_Y_DATE, y->date);
  }
  if (t) {
    dict_write_int(out, MESSAGE_KEY_T_STEPS, &t->steps, sizeof(t->steps), true);
    dict_write_int(out, MESSAGE_KEY_T_SLEEP, &t->sleep, sizeof(t->sleep), true);
    dict_write_int(out, MESSAGE_KEY_T_RHR, &t->rhr, sizeof(t->rhr), true);
    dict_write_int(out, MESSAGE_KEY_T_SHR, &t->shr, sizeof(t->shr), true);
    dict_write_int(out, MESSAGE_KEY_T_SCORE, &t->sleepScore, sizeof(t->sleepScore), true);
    dict_write_int(out, MESSAGE_KEY_T_QUALITY, &t->sleepQuality, sizeof(t->sleepQuality), true);
    dict_write_int(out, MESSAGE_KEY_T_HRV, &t->hrv, sizeof(t->hrv), true);
    dict_write_int(out, MESSAGE_KEY_T_HRVSDNN, &t->hrvSDNN, sizeof(t->hrvSDNN), true);
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

void send_queued(void) {
  if (!persist_exists(KEY_QUEUED_PENDING) || !persist_read_bool(KEY_QUEUED_PENDING)) return;
  WellnessDay y = {0};
  WellnessDay tt = {0};
  if (persist_exists(KEY_QUEUED_Y_DATE)) persist_read_string(KEY_QUEUED_Y_DATE, y.date, sizeof(y.date));
  if (persist_exists(KEY_QUEUED_Y_STEPS)) y.steps = persist_read_int(KEY_QUEUED_Y_STEPS);
  if (persist_exists(KEY_QUEUED_Y_SLEEP)) y.sleep = persist_read_int(KEY_QUEUED_Y_SLEEP);
  if (persist_exists(KEY_QUEUED_Y_RHR)) y.rhr = persist_read_int(KEY_QUEUED_Y_RHR);
  if (persist_exists(KEY_QUEUED_Y_SHR)) y.shr = persist_read_int(KEY_QUEUED_Y_SHR);
  if (persist_exists(KEY_QUEUED_Y_SCORE)) y.sleepScore = persist_read_int(KEY_QUEUED_Y_SCORE);
  if (persist_exists(KEY_QUEUED_Y_QUALITY)) y.sleepQuality = persist_read_int(KEY_QUEUED_Y_QUALITY);
  if (persist_exists(KEY_QUEUED_Y_HRV)) y.hrv = persist_read_int(KEY_QUEUED_Y_HRV);
  if (persist_exists(KEY_QUEUED_Y_HRVSDNN)) y.hrvSDNN = persist_read_int(KEY_QUEUED_Y_HRVSDNN);
  if (persist_exists(KEY_QUEUED_T_DATE)) persist_read_string(KEY_QUEUED_T_DATE, tt.date, sizeof(tt.date));
  if (persist_exists(KEY_QUEUED_T_STEPS)) tt.steps = persist_read_int(KEY_QUEUED_T_STEPS);
  if (persist_exists(KEY_QUEUED_T_SLEEP)) tt.sleep = persist_read_int(KEY_QUEUED_T_SLEEP);
  if (persist_exists(KEY_QUEUED_T_RHR)) tt.rhr = persist_read_int(KEY_QUEUED_T_RHR);
  if (persist_exists(KEY_QUEUED_T_SHR)) tt.shr = persist_read_int(KEY_QUEUED_T_SHR);
  if (persist_exists(KEY_QUEUED_T_SCORE)) tt.sleepScore = persist_read_int(KEY_QUEUED_T_SCORE);
  if (persist_exists(KEY_QUEUED_T_QUALITY)) tt.sleepQuality = persist_read_int(KEY_QUEUED_T_QUALITY);
  if (persist_exists(KEY_QUEUED_T_HRV)) tt.hrv = persist_read_int(KEY_QUEUED_T_HRV);
  if (persist_exists(KEY_QUEUED_T_HRVSDNN)) tt.hrvSDNN = persist_read_int(KEY_QUEUED_T_HRVSDNN);
  if (y.date[0] == '\0' && tt.date[0] == '\0') return;
  s_cached_y = y;
  s_cached_t = tt;
  s_has_cache = true;
  APP_LOG(APP_LOG_LEVEL_DEBUG, "retry queued y %s %d t %s %d", y.date, y.steps, tt.date, tt.steps);
  send_wellness(y.date[0] ? &y : NULL, tt.date[0] ? &tt : NULL);
}

void queue_wellness(WellnessDay *y, WellnessDay *t) {
  if (y) {
    persist_write_string(KEY_QUEUED_Y_DATE, y->date);
    persist_write_int(KEY_QUEUED_Y_STEPS, y->steps);
    persist_write_int(KEY_QUEUED_Y_SLEEP, y->sleep);
    persist_write_int(KEY_QUEUED_Y_RHR, y->rhr);
    persist_write_int(KEY_QUEUED_Y_SHR, y->shr);
    persist_write_int(KEY_QUEUED_Y_SCORE, y->sleepScore);
    persist_write_int(KEY_QUEUED_Y_QUALITY, y->sleepQuality);
    persist_write_int(KEY_QUEUED_Y_HRV, y->hrv);
    persist_write_int(KEY_QUEUED_Y_HRVSDNN, y->hrvSDNN);
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
    persist_write_int(KEY_QUEUED_T_QUALITY, t->sleepQuality);
    persist_write_int(KEY_QUEUED_T_HRV, t->hrv);
    persist_write_int(KEY_QUEUED_T_HRVSDNN, t->hrvSDNN);
  } else {
    persist_delete(KEY_QUEUED_T_DATE);
  }
  persist_write_bool(KEY_QUEUED_PENDING, true);
  send_wellness(y, t);
}
