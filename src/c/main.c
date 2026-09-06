#include <pebble.h>
#include "persist.h"
#include "model.h"
#include "health.h"
#include "queue.h"
#include "schedule.h"
#include "ui.h"
#include "comm.h"

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
  hrv_window_update();
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
