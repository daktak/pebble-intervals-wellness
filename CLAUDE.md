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

## SpO2 / Blood Oxygen — hardware yes, SDK no (as of 2026-09-29)

**Do not write SpO2 collection code against the current SDK. There is no API to call.**

Short version: the sensor → algorithm → driver → activity chain is **complete and merged**. Only the
**app-facing export is missing**, and the HRV precedent shows that is a small, precedented change.

The three layers disagree, and conflating them is the trap:

| Layer | SpO2 support | Evidence |
| ----- | ------------ | -------- |
| Hardware | ✅ yes | emery uses a GH3X2X optical module with green (HR/HRV) + red/IR (SpO2) LED slots |
| Firmware | ✅ yes | PebbleOS `main` has `activity_prefs_blood_oxygen_is_enabled()` and friends, default `HRMonitoringInterval_10Min`, plus per-minute logging of `spo2_percent` + `spo2_quality` |
| Public SDK | ❌ **no** | `HealthMetric` has exactly 9 members, no oxygen. `HealthEventType` has 6, no SpO2 event. Zero hits for oxygen/spo2/pulse_ox across all five platforms' public headers |

All of the above re-verified against `main` on 2026-09-29 (firmware **v4.38.4**). Cite these
paths — the old ones 404:

| Symbol | Location on `main` |
| ------ | ------------------- |
| `activity_prefs_blood_oxygen_is_enabled()` / `_set_blood_oxygen_enabled()` | `include/pbl/services/activity/activity.h:429` / `:452` |
| `activity_prefs_get/set_spo2_measurement_interval()` | same file, `:459` / `:463` |
| `activity_prefs_blood_oxygen_activity_tracking_is_enabled()` | same file, `:434` — **second, independent opt-in** |
| `ACTIVITY_SPO2_DEFAULT_PREFERENCES` = `HRMonitoringInterval_10Min` | same file, `:111` |
| `ALG_DLS_MINUTES_RECORD_VERSION 14` = "Added SpO2 percent and quality" | `include/pbl/services/activity/activity_algorithm.h:62` |
| `spo2_percent` / `spo2_quality` (quality reuses the `HeartRateQuality` enum) | same file, `:90-91` |
| `activity_metrics_prv_get_spo2_sample(percent_out, quality_out)` | `src/fw/services/activity/activity_metrics.c:557` |

⚠️ The activity tree was refactored to a CMake/`include/pbl` layout. `src/fw/services/normal/activity/`
no longer exists — don't grep the pre-refactor paths.

A plain SDK upgrade will **not** fix this. The block is the SDK **export list**, not a header:
`tools/generate_native_sdk/exported_symbols.json` on `main` is at **revision 109** with 33
exports, and it contains **zero** SpO2 entries. Everything below the app boundary already exists —
Goodix algorithm hook `GH3X2X_Spo2AlgorithmResultReport()`, driver
`gh3x2x_spo2_result_report()` (`src/fw/drivers/hrm/gh3x2x.c:171`), 49 SpO2 references in
`src/fw/services/hrm/hrm_manager.c` (feature gating + red/IR-vs-green arbitration), and the
activity layer. What is missing is a public accessor plus an export entry.

❌ **A common wrong theory: "the `ActivityMetric` enum must gain a member first."** The enum does
have 23 members and zero oxygen (it ends at `ActivityMetricHeartRateZone3Minutes` →
`ActivityMetricNumMetrics`), and SpO2 is surfaced by a private
`activity_metrics_prv_get_spo2_sample()` rather than as an `ActivityMetric`. It is tempting to
conclude the enum must change. **It must not.** The HRV work proves the app-facing path bypasses the
enum entirely: `HealthMetric` in 4.33.1 has *no* HRV member, yet apps read HRV fine via a dedicated
peek function. Don't go looking for an enum change.

The unmerged PR `coredevices/PebbleOS#1607` was **closed without merging** on 2026-09-08, yet its
code is present in `main` (a maintainer re-landed it). `coredevices/mobileapp#267` is still open,
so nothing surfaces SpO2 to the user yet either.

### The HRV precedent — the exact shape of the missing step

HRV is the worked example. It is worth reading as a template for what SpO2 still needs:

| Step | HRV (done) | SpO2 (not done) |
| ---- | ---------- | --------------- |
| Issue | [#1630](https://github.com/coredevices/PebbleOS/issues/1630) (closed 2026-07-30) | #1607 (closed unmerged) |
| Firmware PR | `coredevices/PebbleOS#1670` | re-landed already |
| Nonfree companion | `pebbleos-nonfree#6` (merged 2026-07-29) — filled the empty `GH3X2X_HrvAlgorithmResultReport()` stub | `pebbleos-nonfree#4` (SpO2 tuning), already merged |
| SDK export | **rev 109**: `health_service_peek_hrv_ppi_ms()`, `health_service_set_hrv_sample_period()` | **nothing** |
| Event type | `HealthEventHRVUpdate = 5` | — no SpO2 event |
| `HealthMetric` member | **none** (proof the enum is irrelevant) | — |

Note that HRV reused #1607's feature-gated `hrm_enable()` pattern rather than reinventing it, and
PR #6's author observed that HRV rides the same green LEDs as HR. So the SpO2 bring-up that HRV
built on is merged and proven — only the export step is outstanding.

### Two opt-ins, not one

A future implementation that only calls `activity_prefs_set_blood_oxygen_enabled()` will get daily
monitoring but **nothing during detected activities**. There are two independent gates:

- `activity_prefs_set_blood_oxygen_enabled()` — daily SpO2 monitoring
- `activity_prefs_set_blood_oxygen_activity_tracking_enabled()` — SpO2 sampling while an activity
  is detected (walk/run)

Also note the split ownership: the on/off bit is **synced from the phone** under
`PREF_KEY_BLOOD_OXYGEN_PREFERENCES`; only the measurement interval is watch-local. So a watch-side
write of the on/off bit can be clobbered by the phone.

⚠️ Open firmware-side question from the PR thread: `prv_activity_spo2_deinit` appears to pause both
the activity algorithm and workout HR, then unpause only the activity one — an asymmetry a reviewer
flagged. Unresolved as far as the discussion shows; re-check before relying on teardown behaviour.

### The trigger to watch for

The **earliest reliable signal** is the export list gaining a SpO2 function, which lands in `main`
well before any SDK release ships:

```bash
# revision is currently 109, zero SpO2 entries as of 2026-09-29
curl -s https://raw.githubusercontent.com/coredevices/PebbleOS/main/tools/generate_native_sdk/exported_symbols.json \
  | grep -in 'spo2\|oxygen\|hrv'      # the hrv hits show exactly what to look for
```

Look for a `health_service_peek_spo2_*` / `health_service_set_spo2_*` pair plus a new
`HealthEventType`. That file's own `_notes` require **two** co-bumped counters, so expect both:
the export-list `revision`, and `PROCESS_INFO_CURRENT_SDK_VERSION_MINOR` in
`pebble_process_info.h` (⚠️ I could not locate that header on `main` — likely renamed in the CMake
refactor, so confirm its current home before relying on it).

The app-facing confirmation comes one step later, in the shipped SDK: an
`_PBL_API_EXISTS_<spo2 symbol>` line in `sdk-core/pebble/<plat>/include/pebble_sdk_version.h`.
Check with `pebble sdk list` (4.33.1 was newest as of 2026-09-29; 4.33.1 is
also what this repo builds against).

⚠️ There are **three separate version tracks**, and reading the wrong one will convince you the SDK
shipped something it didn't:

| Track | Latest | Where |
| ----- | ------ | ----- |
| App SDK (`sdk-core`) | **4.33.1** | `https://sdk.repebble.com` — what `pebble sdk install` fetches |
| Firmware | v4.38.4 | `coredevices/PebbleOS` GitHub Releases — **firmware images only** |
| `main`'s in-dev SDK | `SDK_VERSION` = `0.1.10` | New CMake/`include/pbl` generation, not the shipped 4.x waf SDK |

The GitHub Releases page looks authoritative and its tags look newer than 4.33.1, but it publishes
**zero** `pebble-sdk*.zip` assets — every asset is `firmware_*`/`normal_*`/`prf_*`/`recovery_*`/
`qemu_*`/`sdkshell_*`. Do not use it to judge app-SDK age. Re-check the real ceiling in one call:

```bash
curl -s 'https://sdk.repebble.com/v1/files/sdk-core?channel='   # authoritative SDK list
```

For calibration on how the gate behaves: `PBL_API_EXISTS(x)` expands to
`defined(_PBL_API_EXISTS_##x)`, and of the five platforms targeted here
**only `emery` and `gabbro` define `_PBL_API_EXISTS_health_service_peek_hrv_ppi_ms`**. The other
three compile the HRV path out entirely.

### Constraint that will bite when it lands

The green HR/HRV path and the red/IR SpO2 path **contend for the same optical sensor**. The
firmware arbitrates this already: `src/fw/services/hrm/hrm_manager.c` has explicit red/IR-vs-green
resolution and **SpO2 wins** whenever it is due.

**Prefer asking over taking.** HRV's export pair includes `health_service_set_hrv_sample_period()`,
so an app can *request* sampling rather than fight for the sensor. If SpO2 follows with a symmetric
`health_service_set_spo2_sample_period()`, the contention problem largely disappears — the
firmware's own manager schedules both and the app never has to arbitrate. Check for that function
before hand-rolling anything.

The manual fallback exists if no setter is exported:
`activity_algorithm_activity_hrm_set_paused(bool)` — *"Pause or resume the continuous activity HRM
session so the optical path is free for a periodic SpO2 reading during an activity"* — with
`activity_algorithm_activity_hrm_is_active()` to tell you when a session is running
(`include/pbl/services/activity/activity_algorithm.h:173,177`).

This repo's worker already holds the sensor 3 minutes of every 15 during sleep (`health.c:55`), so a
future SpO2 sampler must **hand the sensor over deliberately** rather than compete for it — and
outside activities it should follow the same pattern during the worker's off-window.

Note the asymmetry to expect: that pause call is a no-op when no activity HR session is active, so
"pause then read" is only valid while an activity is actually being tracked.

### If/when the API appears

Intervals.icu is already ready. `spO2` is a real built-in wellness field — a **float, 0-100 %,
one representative value per day** (official OpenAPI: `"spO2": {"type":"number","format":"float"}`).
Hard constraints:

- **No min, no avg, no series.** Per MedTechCD: *"a wellness field is a single value."* Duplicate
  rows for the same date are rejected, so the day's readings must be reduced on-device first.
- **Agreed reduction for this project: median of the day's accepted readings.** This matches the
  Garmin Health Snapshot and WHOOP convention and is robust to motion-artifact outliers. The
  worker already has `quickselect_median` to reuse. (Google Fit instead sends the day's *first*
  reading; nobody sends a minimum.)
- **Quality-gate before averaging.** The firmware exposes an `HRMQuality` scale
  (OffWrist/Worst/Poor/Acceptable/Good/Excellent), and it already grades SpO2 for us: the driver
  sets `hrm_data.spo2_quality` from the algorithm's confidence and invalid flag
  (`src/fw/drivers/hrm/gh3x2x.c:194,196`). So a future `peek` should return firmware-graded values
  — reject OffWrist and reject below Acceptable, or an off-wrist watch will write garbage into
  Intervals every day. Do not invent a second, private quality scale.
- **`spO2` is display-only** — it does not feed CTL/ATL or any Intervals wellness score.
- **`"locked": true`** stops a connected-device sync from reverting our write (MedTechCD: *"the one
  that updates last will overwrite the former one"*). Deliberately **not** set today: it blocks ALL
  field updates from other devices for that day, so it is only safe if Pebble is the sole source.
  Decide once we know what else syncs to the account.
- For desaturation burden, use a **custom wellness field** (`MinSpO2`, `SpO2LowEvents`) — UpperCamelCase
  codes, created once in the Intervals.icu web UI. Unverified: whether `wellness-bulk` accepts a
  custom field inside its array, and whether `locked: true` survives a later device sync.
- Confirmed already-correct: `sleepScore`, `sleepQuality`, and `readiness` are **not** computed by
  Intervals.icu, so the values this app uploads are accepted and stick. Rate limits are a non-issue
  (2 requests/day against a 5000/day API-key budget).

## Common Gotchas

- **UUID must be unique** — reusing a UUID causes app rejection on installation; generate a new one per project with `uuidgen`
- **Memory is tight** — ~24 KB for code + heap on older platforms; avoid large allocations, prefer stack variables
- **Resources max** — 256 media items per app
- **`version` format** — must be `major.minor.0`; the patch segment is reserved and must be `0`
- **Round displays** — `chalk` and `gabbro` are circular; use `grect_inset()` and `PBL_ROUND` guards to avoid drawing outside the visible circle
- **`layer_mark_dirty()`** — queues a redraw; actual drawing happens in the `layer_update_proc` callback, not immediately
- **The GitHub Releases page cannot tell you the app-SDK version.** `coredevices/PebbleOS` releases
  (v4.38.x) are **firmware** and publish zero `pebble-sdk*.zip` — every asset is
  `firmware_*`/`normal_*`/`prf_*`/`recovery_*`/`qemu_*`/`sdkshell_*`. The app SDK is distributed
  separately from `https://sdk.repebble.com` and its latest is 4.33.1. Checking the wrong one makes
  it look like the SDK shipped something new when it didn't. One call gets the truth:
  `curl -s 'https://sdk.repebble.com/v1/files/sdk-core?channel='`
- **Never hoist `ctx.path.ant_glob()` out of the `wscript` platform loop** — waf caches the node
  list per-env, so one shared list makes each worker's link pull in *every* platform's generated
  `appinfo`/`resource_ids`/`message_keys` objects and fail with dozens of `multiple definition of`
  errors. Use a plain `glob.glob()` for any file-existence probe and keep the real `ant_glob`
  inside the loop.
- **The worker only gets built if `worker_src/c/*.c` is visible to the build.** `Makefile` and
  `.github/workflows/pebble.yml` mount it explicitly; if a future change forgets, the app still
  builds and ships — just with no HRV collection. `wscript` now prints a `WARNING` in that case,
  so check the build log rather than assuming the worker is in the `.pbw`. Verify with
  `unzip -l build/*.pbw | grep worker` or the `worker` block in each `manifest.json`.
