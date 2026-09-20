# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## Project Overview

This is a Pebble smartwatch app/watchface template. The default starter is a minimal digital clock watchface (`src/c/main.c`). Builds run inside a Docker container using `pebble-tool`.

## Build and Development Commands

```bash
# Build with Docker (no local SDK needed)
make docker-run

# Build locally (requires pebble-tool + SDK installed)
make build

# Build and run in emulator
make pt1   # Pebble Time (basalt, 144×168)
make pt2   # Pebble Time Round (emery, 200×228)
```

### Installing pebble-tool locally

```bash
# Requires Python 3.10–3.13
uv tool install pebble-tool --python 3.13
pebble sdk install latest
```

## Hardware Platforms

| Platform  | Device                   | Shape | Resolution | Colors  | App RAM |
| --------- | ------------------------ | ----- | ---------- | ------- | ------- |
| `aplite`  | Pebble / Pebble Steel    | Rect  | 144×168    | 2 (B/W) | ~24 KB  |
| `basalt`  | Pebble Time / Time Steel | Rect  | 144×168    | 64      | ~24 KB  |
| `chalk`   | Pebble Time Round        | Round | 180×180    | 64      | ~24 KB  |
| `diorite` | Pebble 2 SE              | Rect  | 144×168    | 2 (B/W) | ~24 KB  |
| `emery`   | Pebble Time 2            | Rect  | 200×228    | 64      | ~128 KB |
| `gabbro`  | Pebble 2 Round           | Round | 260×260    | 64      | ~128 KB |

> Use `layer_get_bounds()` instead of hardcoded pixel values — layout adapts to each platform automatically.

### Platform macros

```c
#if defined(PBL_COLOR)
  // Pebble Time platforms (basalt, chalk, emery, gabbro)
#endif

#if defined(PBL_ROUND)
  // Round display (chalk, gabbro)
#endif

#if defined(PBL_PLATFORM_BASALT)
  // Basalt-specific code
#endif
```

## package.json Reference

Key fields under the `"pebble"` object:

| Field                | Notes                                                          |
| -------------------- | -------------------------------------------------------------- |
| `uuid`               | UUID v4, unique per app. Generate with `uuidgen`. Never reuse. |
| `version`            | Must follow `major.minor.0` format (e.g. `"1.2.0"`)            |
| `sdkVersion`         | Always `"3"`                                                   |
| `targetPlatforms`    | Array of platform names — only listed platforms are built      |
| `watchapp.watchface` | `true` for watchface, `false` for regular watchapp             |
| `watchapp.hiddenApp` | Hides app from system menu when `true`                         |
| `enableMultiJS`      | `true` to allow multiple JS files with `require()`             |
| `capabilities`       | `["configurable"]` for settings page, `["location"]` for GPS   |
| `messageKeys`        | Named keys for AppMessage communication                        |
| `resources.media`    | Bundled assets — max 256 per app                               |

## Architecture

### Pure C watchface (default)

- `src/c/main.c` — all watch-side code: UI, services, event loop
- `package.json` — app metadata, UUID, target platforms
- `wscript` — Pebble SDK waf build configuration

### With phone-side JavaScript

Add `src/pkjs/index.js` and update `wscript`:

```python
ctx.pbl_bundle(binaries=binaries,
               js=ctx.path.ant_glob(['src/pkjs/**/*.js']),
               js_entry_file='src/pkjs/index.js')
```

Enable in `package.json`:

```json
"enableMultiJS": true,
"capabilities": ["configurable"]
```

## C API Quick Reference

### Foundation

| Module              | Purpose                                                             |
| ------------------- | ------------------------------------------------------------------- |
| `App`               | `app_event_loop()`, launch/exit                                     |
| `Timer`             | `app_timer_register()`, one-shot and repeating timers               |
| `Wall Time`         | `time()`, `localtime()`, `tick_timer_service_subscribe()`           |
| `Storage`           | `persist_read_int()`, `persist_write_int()` — survives app restarts |
| `AppMessage`        | Two-way communication with phone JS                                 |
| `Logging`           | `APP_LOG(APP_LOG_LEVEL_DEBUG, "msg")`                               |
| `Memory Management` | `malloc()`, `free()` — limited heap                                 |
| `WatchInfo`         | `watch_info_get_model()`, `watch_info_get_color()`                  |

### User Interface

| Module      | Purpose                                                                 |
| ----------- | ----------------------------------------------------------------------- |
| `Window`    | Root container: `window_create()`, `window_set_window_handlers()`       |
| `Layers`    | `layer_create()`, `layer_set_update_proc()`, `layer_mark_dirty()`       |
| `TextLayer` | `text_layer_create()`, `text_layer_set_text()`, `text_layer_set_font()` |
| `Clicks`    | Button handling: `window_set_click_config_provider()`                   |
| `Animation` | `animation_create()`, `animation_schedule()`                            |
| `Vibes`     | `vibes_short_pulse()`, `vibes_long_pulse()`                             |
| `Light`     | `light_enable_interaction()`                                            |

### Graphics (inside `layer_update_proc`)

```c
// Shapes
graphics_context_set_fill_color(ctx, GColorRed);
graphics_fill_rect(ctx, GRect(x, y, w, h), corner_radius, GCornerNone);
graphics_fill_circle(ctx, GPoint(cx, cy), radius);

// Strokes
graphics_context_set_stroke_color(ctx, GColorWhite);
graphics_context_set_stroke_width(ctx, 2);
graphics_draw_line(ctx, GPoint(x1, y1), GPoint(x2, y2));

// Text
graphics_draw_text(ctx, "hello", fonts_get_system_font(FONT_KEY_GOTHIC_18),
                   GRect(0, 0, 144, 20), GTextOverflowModeWordWrap,
                   GTextAlignmentCenter, NULL);
```

### Common patterns

**Window lifecycle**

```c
static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  // create layers, subscribe to services
}

static void window_unload(Window *window) {
  // destroy layers, unsubscribe from services
}

window_set_window_handlers(s_window, (WindowHandlers){
  .load = window_load,
  .unload = window_unload,
});
```

**Tick timer**

```c
static void tick_handler(struct tm *tick_time, TimeUnits units_changed) { }
tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
// Unsubscribe in window_unload: tick_timer_service_unsubscribe();
```

**Repeating app timer (animation)**

```c
static AppTimer *s_timer;

static void timer_cb(void *data) {
  layer_mark_dirty(s_layer);
  s_timer = app_timer_register(100, timer_cb, NULL); // 10 fps
}

// Start: s_timer = app_timer_register(100, timer_cb, NULL);
// Stop in window_unload: app_timer_cancel(s_timer);
```

**Persistent storage**

```c
#define STORAGE_KEY_SETTING 0

persist_write_int(STORAGE_KEY_SETTING, value);
int value = persist_read_int(STORAGE_KEY_SETTING); // 0 if not set
```

## JavaScript (if used)

The Pebble SDK uses an **ES5 JavaScript parser**. These will break the build silently or with cryptic errors:

| ❌ Avoid                          | ✅ Use instead          |
| --------------------------------- | ----------------------- |
| Trailing commas in objects/arrays | Remove the last comma   |
| `() =>` arrow functions           | `function() {}`         |
| `` `template ${literals}` ``      | `"concat" + var`        |
| `const` / `let`                   | `var`                   |
| Spread / destructuring / ES6+     | Equivalent ES5 patterns |

Note: The Alloy framework (modern JS) only targets `emery` and `gabbro`.

## Worker Fault Debugging (Pebble background workers)

This repo has a background worker (`worker_src/c/worker.c`) that collects HRV/sleep data even
when the UI app is not foregrounded.

### Critical: `persist_write_string()` crashes the worker (empirically proven)

**Never call `persist_write_string()` from inside the worker** (`worker_src/c/worker.c`).
On emery (and observed on the real watch) the PBL syscall behind it (function id `0x314`)
hard-faults the worker process immediately, while `persist_write_int()` and
`persist_write_data()` work fine.

**Symptoms / how this bug presented (real incident):**
- Worker started, `restore_bursts cnt 39` logged, then immediately `Worker fault!`
  with `PC: 0x1214c8a9 LR: ???` — identical PC across multiple different worker builds.
- A constant fault PC across *different binaries* means the crash is in **shared
  firmware/worker-support code**, NOT your own worker code. The worker ELF symbols
  (`.text` ~0xa8–0xbd0) never cover an address like `0x1214c8a9`.
- The bug was originally misdiagnosed as the HRV activity-mask peek
  (`health_service_peek_current_activities()`). That theory was WRONG — see
  `git log`/revert history for the dead end. Do not "fix" the peek again; it is safe.

**How to isolate a worker crash reliably:**
1. Add `APP_LOG` markers immediately before and after each suspect API call
   (e.g. `"before write date"` / `"after write date"`), rebuild, reinstall, re-test.
2. The last marker printed right before the fault identifies the exact syscall.
3. Keep the faltering watch logs (`logs.txt` in repo root, pulled from the phone).

**Safe replacement (same bytes, same app-side readers):**
Instead of:
```c
persist_write_string(KEY_HRV_NIGHT_DATE, date);   // CRASHES the worker
```
use:
```c
persist_write_data(KEY_HRV_NIGHT_DATE, date, (uint16_t)(strlen(date) + 1)); // SAFE
```
`persist_write_string()` is just a wrapper around `persist_write_data(key, str, strlen+1)`,
so an app-side `persist_read_string()` reads the stored bytes back identically. The worker's
HRV ring (`KEY_HRV_RING_*`, keys 51–53) already uses `persist_write_data()` with success,
which is the proven-safe pattern.

### Critical: `strftime()` also crashes the worker (empirically proven)

**Never call `strftime()` from inside the worker.**
While refactoring the `night_end()` date formatting, `strftime()` replaced `snprintf()` and
the worker immediately faulted again:
- `worker start with HRV API` → `restore_bursts cnt 39` → `Worker fault! PC: 0x12086931`
  (new PC, right before the `night_end` med log).
- `strftime` is dispatched through the **same worker-support trampoline table** as
  `persist_write_string` (worker ELF exports it as a `T` symbol just like the persist calls),
  and the firmware-side implementation faults in worker context.
- Reverting to `snprintf(date, sizeof(date), "%04d-%02d-%02d", ...)` fixed it:
  `snprintf` is a proven-safe worker trampoline (used by the same `night_end` for multiple
  builds). `strftime` no longer appears in the worker ELF after revert.
- A **different constant crash PC** (`0x12086931` vs `0x1214c8a9`) while the code change was
  a one-line format swap is a strong signature that you introduced a broken worker syscall.

**Rule of thumb for workers:**
- ✅ `persist_write_int` — safe
- ✅ `persist_write_data` — safe (proven via HRV ring)
- ✅ `snprintf` — safe (proven in `night_end` date formatting)
- ❌ `persist_write_string` — faults the worker; avoid or route through the app
- ❌ `strftime` — faults the worker the same way; build date strings with `snprintf` instead
  (format `%04d-%02d-%02d` across `tm_year+1900`, `tm_mon+1`, `tm_mday`)
- ⚠️ Any new libc-ish call added to the worker should be treated as suspect until proven —
  workers only get the safe exports in the PBL trampoline table, and failures show up as
  `Worker fault!` with a constant PC in shared firmware code, not in your `.text`.

The worker cannot run in the emulator, so every worker change requires a physical watch
install + `logs.txt` capture for validation.

## Common Gotchas

- **UUID must be unique** — reusing a UUID causes app rejection on installation; generate a new one per project with `uuidgen`
- **Memory is tight** — ~24 KB for code + heap on older platforms; avoid large allocations, prefer stack variables
- **Resources max** — 256 media items per app
- **`version` format** — must be `major.minor.0`; the patch segment is reserved and must be `0`
- **Round displays** — `chalk` and `gabbro` are circular; use `grect_inset()` and `PBL_ROUND` guards to avoid drawing outside the visible circle
- **`layer_mark_dirty()`** — queues a redraw; actual drawing happens in the `layer_update_proc` callback, not immediately
