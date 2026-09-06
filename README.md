# Intervals Wellness for Pebble

Daily wellness sync from Pebble Health to [Intervals.icu](https://intervals.icu) — pushes **steps**, **sleep**, **resting HR** and **avg sleeping HR** at your chosen time (default **09:00**) via `health` + `wakeup`.

Derived from [pebble-steptember](https://github.com/daktak/pebble-steptember) (scheduling/wakeup) and [pebble_intervals_icu](https://github.com/daktak/pebble_intervals_icu) (Intervals API + Clay).

## Features

- **Watchapp** (not watchface) for `basalt`, `chalk`, `diorite`, `emery` — all Pebble platforms **except `aplite`** (no Health/wakeup on SDK 2.x)
- **Capabilities:** `configurable` + `health` + `wakeup`; `enableMultiJS`
- **Clay** (`@rebble/clay`) for `API_KEY` and `SYNC_HOUR`/`SYNC_MINUTE` (default `09:00`); athlete is always `0` (authenticated user)
- **Auth:** `base-64` Basic `API_KEY:<key>` to `https://intervals.icu/api/v1/athlete/0/wellness-bulk`
- **Core 4 fields** (Pebble → Intervals wellness):
  - `steps` ← `HealthMetricStepCount`
  - `sleepSecs` ← `HealthMetricSleepSeconds`
  - `restingHR` ← `HealthMetricHeartRateBPM` `Min` aggregated
  - `avgSleepingHR` ← `HealthMetricHeartRateBPM` `Avg` aggregated
- **Both-days push:** at sync time the watch queries `yesterday 00:00–23:59` (final) **and** `today 00:00→now` (live) and `PUT`s one `wellness-bulk` with 2 records `[{id: YYYY-MM-DD, …}]`
- **Scheduling:** `wakeup_schedule` at `time_start_of_today() + h*3600 + m*60` (local), `+86400` if past; rescheduled on config save and after each fire. Also checks every minute tick so a foreground app syncs without wakeup. Once-per-day dedup via `LAST_SYNC_DATE`, `SELECT` forces immediate sync, `QUEUED_PENDING` retry every 5 min, auto-exit 2s after `OK` if wakeup-launched

## Watch UI

Single status screen: time, steps today, sleep today, current HR, status line (`Ready` / `Last YYYY-MM-DD` / `Queued retry` / `Sync…` / `OK …` / `ERR …`), `SELECT to sync` hint. `SELECT` queues current yesterday+today wellness and sends to phone.

## Quick Start

1. **Create API key** in Intervals.icu: `Settings → Developer Settings → API Key` (`https://intervals.icu/settings`).
2. **Install:** `pebble build && pebble install --emulator basalt` (or sideload `build/pebble-intervals-wellness.pbw` via Rebble).
3. **Configure:** open Pebble phone app → `Intervals Wellness` gear → set `API Key` and `Sync Hour/Minute` → Save (pushes `SYNC_HOUR/MINUTE` to watch, reschedules wakeup).
4. **Sync:** at `09:00` (or press `SELECT`) the watch sends `Y_STEPS/Y_SLEEP/Y_RHR/Y_SHR/Y_DATE` + `T_STEPS/.../T_DATE` via `AppMessage`; phone `PUT`s `wellness-bulk`; watch shows `OK YYYY-MM-DD` on success.

## Intervals Details

- **Endpoint:** `PUT /api/v1/athlete/0/wellness-bulk` — body is array of `{id, steps, sleepSecs, restingHR, avgSleepingHR}`. Only provided fields are updated.
- **Auth:** `Authorization: Basic base64("API_KEY:" + key)` — same as `pebble_intervals_icu`.
- **Dates:** `id` is local `YYYY-MM-DD`. Yesterday is queried as `[today_start-86400, today_start-1]`; today as `[today_start, now]`. Verify in Intervals.icu calendar → wellness popup or `GET /wellness?oldest=YYYY-MM-DD&newest=YYYY-MM-DD`.

## Project Layout

- `src/c/main.c` — Health queries, wakeup/tick scheduling, persistence (`SYNC_HOUR`, `SYNC_MINUTE`, `LAST_SYNC_DATE`, `QUEUED_*`, `WAKEUP_ID`), AppMessage protocol, status UI
- `src/pkjs/index.js` — Clay bootstrap, `base-64` auth, `wellness-bulk` PUT, sync-time pushback on `ready`
- `src/pkjs/config.json` — Clay schema (API key, sync time)
- `package.json` — app metadata, `messageKeys` (`Y_STEPS`/`Y_SLEEP`/`Y_RHR`/`Y_SHR`/`Y_DATE`, `T_…`, `CMD`, `STATUS`, `API_KEY`, `SYNC_HOUR`, `SYNC_MINUTE`)
- `wscript` — bundles `src/pkjs/index.js`
- `resources/images/menu_icon.png` — launcher icon

## Building

Requires [Pebble SDK 4.33](https://developer.repebble.com/) (`pebble-tool`).

```sh
pebble build                  # → build/pebble-intervals-wellness.pbw (+ per-platform .bins)
pebble clean && pebble build  # force clean (regenerates message keys)
make build                    # via Makefile wrapper
make docker-run               # Docker (no local SDK) — mounts src/resources/package.json/wscript
```

Install: `pebble install --emulator basalt --logs` or `pebble install --phone <ip>`.

## AppMessage Keys

`Y_STEPS`, `Y_SLEEP`, `Y_RHR`, `Y_SHR`, `Y_DATE`, `T_STEPS`, `T_SLEEP`, `T_RHR`, `T_SHR`, `T_DATE`, `CMD`, `STATUS`, `API_KEY`, `SYNC_HOUR`, `SYNC_MINUTE`

`STATUS` values: `OK <date>` / `OK <y>+<t>` on success, `ERR …` (`ERR no API key`, `ERR bad key` (401), `ERR net`, `ERR <http>`), `No Health`, `No outbox`/`No phone`.

## Notes

- Pebble Health is midnight-split; `sleepSecs` for a `23:00–07:00` night is split across two calendar days by the platform. Core sync uses daily sums; reassembling sleep sessions spanning midnight (via `health_service_activities_iterate`) is a future enhancement.
- `HR` aggregation is `Min`/`Avg` over the queried window; if unavailable the current `peek` value is used.
- SDK PKJS is ES5 only — no trailing commas, arrow functions, template literals, `const`/`let`, or destructuring.
- `targetPlatforms` excludes `aplite`; `basalt`/`chalk`/`diorite`/`emery` share the same `pebble-app.elf`.

## License

Same as templates. Pebble SDK and Intervals.icu API are property of their owners.
