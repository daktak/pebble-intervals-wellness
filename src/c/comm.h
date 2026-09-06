#pragma once
#include <pebble.h>
void inbox_received_handler(DictionaryIterator *iter, void *ctx);
void inbox_dropped_handler(AppMessageResult reason, void *ctx);
void outbox_failed_handler(DictionaryIterator *iter, AppMessageResult reason, void *ctx);
void outbox_sent_handler(DictionaryIterator *iter, void *ctx);
