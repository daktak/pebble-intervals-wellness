#pragma once
#include <pebble.h>
extern Window *s_window;
extern TextLayer *s_time_layer;
extern TextLayer *s_steps_layer;
extern TextLayer *s_sleep_layer;
extern TextLayer *s_rhr_layer;
extern TextLayer *s_shr_layer;
extern TextLayer *s_score_layer;
extern TextLayer *s_status_layer;
extern AppTimer *s_exit_timer;
extern bool s_wakeup_launch;
extern bool s_pending_wakeup;
void update_display(void);
void set_status(const char *msg);
void window_load(Window *window);
void window_unload(Window *window);
void click_config_provider(void *ctx);
void exit_timer_callback(void *data);
