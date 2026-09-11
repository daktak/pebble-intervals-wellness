#include <pebble.h>
#include "comm.h"
#include "persist.h"
#include "schedule.h"
#include "ui.h"
#include "queue.h"
#include "health.h"

void inbox_received_handler(DictionaryIterator *iter, void *ctx) {
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
  t = dict_find(iter, MESSAGE_KEY_HRV_START_HOUR);
  if (t) {
    int v = 22;
    if (t->type == TUPLE_CSTRING) v = atoi(t->value->cstring);
    else if (t->type == TUPLE_INT) v = (int)t->value->int32;
    else if (t->type == TUPLE_UINT) v = (int)t->value->uint32;
    if (v < 0 || v > 23) v = 22;
    persist_write_int(KEY_HRV_START_HOUR, v);
    hrv_window_update();
  }
  t = dict_find(iter, MESSAGE_KEY_HRV_START_MINUTE);
  if (t) {
    int v = 0;
    if (t->type == TUPLE_CSTRING) v = atoi(t->value->cstring);
    else if (t->type == TUPLE_INT) v = (int)t->value->int32;
    else if (t->type == TUPLE_UINT) v = (int)t->value->uint32;
    if (v < 0 || v > 59) v = 0;
    persist_write_int(KEY_HRV_START_MINUTE, v);
    hrv_window_update();
  }
  t = dict_find(iter, MESSAGE_KEY_HRV_END_HOUR);
  if (t) {
    int v = 8;
    if (t->type == TUPLE_CSTRING) v = atoi(t->value->cstring);
    else if (t->type == TUPLE_INT) v = (int)t->value->int32;
    else if (t->type == TUPLE_UINT) v = (int)t->value->uint32;
    if (v < 0 || v > 23) v = 8;
    persist_write_int(KEY_HRV_END_HOUR, v);
    hrv_window_update();
  }
  t = dict_find(iter, MESSAGE_KEY_HRV_END_MINUTE);
  if (t) {
    int v = 0;
    if (t->type == TUPLE_CSTRING) v = atoi(t->value->cstring);
    else if (t->type == TUPLE_INT) v = (int)t->value->int32;
    else if (t->type == TUPLE_UINT) v = (int)t->value->uint32;
    if (v < 0 || v > 59) v = 0;
    persist_write_int(KEY_HRV_END_MINUTE, v);
    hrv_window_update();
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

void inbox_dropped_handler(AppMessageResult reason, void *ctx) {
  set_status("Inbox drop");
}

void outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason, void *ctx) {
  set_status("No phone");
}

void outbox_sent_handler(DictionaryIterator *iter, void *ctx) {}
