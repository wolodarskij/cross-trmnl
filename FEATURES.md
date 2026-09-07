# cross-trmnl — changes vs. upstream CrossPoint Reader

This fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
adds two things on top of the reader: a **dashboard** (a set of full-screen
images kept on the SD card, browsed on demand and shown as the sleep screen,
downloaded over WiFi or copied on by hand) and **Lua script execution** (run
programs from the SD card).

All changes are additive; the reader, EPUB engine, fonts, themes, file
transfer, and everything else remain untouched upstream code.

## Features

### Script execution (Lua)
- New **Scripts** entry on the home menu: lists `.lua` files from the SD card
  `/scripts` folder; Confirm runs one, Back returns.
- Embedded **Lua 5.4** interpreter with a device API — scripts can draw on the
  e-ink screen (`screen`), read buttons (`input`), read/write SD files (`fs`,
  writes confined to `/scripts`), fetch over WiFi (`http.get`, connects
  automatically), and query the device (`device.battery`/`mac`/`version`/…).
- Sandboxed and crash-proof: **Back aborts** any script (a VM hook interrupts
  even infinite loops), a **memory cap** stops runaway allocation, errors are
  caught and shown with a traceback, and unsafe stdlib (`os.execute`, `io`,
  `require`, `debug`) is removed.
- Full API + samples: [docs/SCRIPTING.md](./docs/SCRIPTING.md),
  [lua-scripts-src/](./lua-scripts-src).

### Dashboard viewer (home menu)
- New **Dashboard** entry on the home screen. It opens straight into the
  images already on the SD card — **no WiFi, no fetch, no waiting**. A
  dashboard you cannot look at without first waiting for the radio is one you
  stop opening, so connecting is never implicit.
- **Left/Right** (or Up/Down) move between screens, **Confirm** connects and
  downloads, **Back** exits.
- Screens live in `/dashboards` on the card, one BMP per screen. The current
  pick is remembered across reboots by id, so a sync that adds screens ahead
  of yours does not silently switch what you were looking at.
- A footer line names the current screen and its position (`Agenda  (2/4)`)
  when more than one is present, and reports a failed sync without replacing
  the image that is already on screen.

> **Behaviour change:** opening the dashboard no longer fetches automatically
> — for *any* source, including the legacy single-image ones. Press Confirm to
> refresh.

### Screens without a server
`/dashboards` is an ordinary folder. Copy 480×800 BMPs into it over USB, the
web file manager, or by pulling the card, and they appear in the viewer
exactly like downloaded ones — the device never has to reach a server at all.
A sync only ever *adds* to the folder: it never deletes, so hand-copied
screens and screens you removed server-side both survive.

### Dashboard as sleep screen
Two new sleep-screen modes (Settings → Display → Sleep Screen):
- **Dashboard** — the selected dashboard screen becomes the sleep screen.
  No network on the way to sleep: instant, zero battery cost.
- **Dashboard + Auto-update** — a fresh image is fetched every time the device
  goes to sleep (power button or idle timeout), so the screen the device
  sleeps on is always current. With a screen set this refreshes **only the
  selected screen**: pulling the whole set here would multiply radio time by
  the screen count for images nothing is about to draw.
- Additionally, sleeping while the Dashboard viewer is open always keeps the
  dashboard visible, regardless of the configured sleep-screen mode.

### Three image sources (Settings → Display → Dashboard source)
- **Screen set** — many screens from one server (`Dashboard server URL`
  setting, pointing at the server *root*, e.g. `http://192.168.1.20:8080`).
  Implements x4-dashboard-server's
  [screen-list contract](https://github.com/wolodarskij/x4-dashboard-server/blob/main/docs/screens-format.md):
  - `GET {base}/screens.json` for the manifest, rejected unless `format` is
    `x4-dashboard-set`;
  - each listed screen downloaded to `/dashboards/<id>.bmp` via a temp file,
    so a failed transfer leaves the previous copy intact;
  - one screen failing does not abort the run — the rest of the set still
    lands;
  - screen names cached in a `/dashboards/.index` sidecar so the picker can
    label them offline (BMPs carry no name of their own);
  - bounded on purpose: at most 32 screens and a 32 KB manifest, and only
    `id`/`name`/`url` parsed out of it.
- **Simple** — fetches a single BMP from a plain URL (`Dashboard URL`
  setting), into the one-slot cache at `/.crosspoint/dashboard.bmp`. The
  original source, kept unchanged for existing setups; the same
  [x4-dashboard-server](https://github.com/wolodarskij/x4-dashboard-server)
  still serves it at `/dashboard.bmp`.
- **TRMNL** — speaks the TRMNL/BYOS device API against self-hosted servers
  (e.g. [byos_fastapi](https://github.com/usetrmnl/byos_fastapi),
  [terminus](https://github.com/usetrmnl/terminus)):
  - auto-provisions via `GET /api/setup` (`ID` = device MAC) and persists a
    returned `api_key` — no manual key entry needed for typical BYOS setups;
  - fetches via `GET /api/display` with `ID`, `Access-Token`, `FW-Version`,
    `RSSI`, `Width`/`Height` headers;
  - downloads `image_url` (relative URLs resolved against the server base);
  - image format detected by magic bytes: **BMPs cached as-is, PNGs converted
    on-device** to a dithered BMP via the existing `PngToBmpConverter`.

### Rendering
- **Landscape images** (e.g. TRMNL's 800×480) render full-screen by switching
  the display orientation for the draw — rotate the device to read them.
  Portrait images (480×800) render as-is.
- **4-level grayscale BMPs** display with the panel's two-pass grayscale
  render in the Dashboard viewer as well (upstream only did this on the sleep
  screen). Images whose palettes sit on the native gray levels
  (0/85/170/255) are mapped directly with no on-device dithering.

### Settings / UI plumbing
- New settings (all persisted in `settings.json`, editable on-device and via
  the web settings API): `dashboardSource`, `dashboardUrl`, `dashboardSetUrl`,
  `trmnlUrl`, `trmnlApiKey`.
- The selected screen (`dashboardScreenId`) is **state**, not a setting: it
  lives in `state.json` because the user picks it with Left/Right on the
  dashboard, never from the settings menu. It is held in memory as you move
  and written once on exit, so paging through screens does not hammer the
  flash.
- **String settings are now editable on-device** with the on-screen keyboard
  (URL layout with `http://`, `192.168.`, `:8080` snippets) — upstream had
  them web-only.
- New headless WiFi helper (`WifiConnector`) connects to saved networks
  without UI — used by the auto-update sleep path.

### Fixes
- **Sleep-screen option labels**: upstream PR #2480 "sorted" the label list,
  but labels are positional (`enumValues[value]`), which made selecting
  "Cover + Custom" actually enable *Blank* and "None" enable *Cover + Custom*.
  Restored the correct order and documented the invariant.

## Code changes

New files:

| File | Purpose |
|---|---|
| `src/activities/dashboard/DashboardActivity.{h,cpp}` | offline screen browser + full-screen viewer |
| `src/network/DashboardImage.{h,cpp}` | shared fetch/cache, source dispatch, current-image resolution |
| `src/network/DashboardSet.{h,cpp}` | `/dashboards` store + `screens.json` client |
| `src/network/TrmnlClient.{h,cpp}` | TRMNL/BYOS device-API client |
| `src/network/WifiConnector.{h,cpp}` | headless saved-network WiFi connect |
| `lib/Lua/*` | vendored Lua 5.4.7 (safe subset; custom `luaconf.h`/`linit.c`) |
| `src/scripting/ScriptEngine.{h,cpp}` | sandboxed Lua VM: mem cap, abort hook, error capture |
| `src/scripting/ScriptBindings.{h,cpp}` | the `screen`/`input`/`fs`/`http`/`device` API |
| `src/activities/scripts/ScriptBrowserActivity.{h,cpp}` | `/scripts` file list |
| `src/activities/scripts/ScriptRunActivity.{h,cpp}` | run a script + show result/errors |
| `docs/SCRIPTING.md`, `lua-scripts-src/*` | scripting docs + sample scripts |
| `FEATURES.md` | this document |

Modified files (all changes small and localized):

| File | Change |
|---|---|
| `src/CrossPointSettings.h` | `DASHBOARD`/`DASHBOARD_AUTOUPDATE` sleep modes, `DASHBOARD_SOURCE` enum (incl. `SCREENSET`), 4 new string settings |
| `src/SettingsList.h` | new settings entries; sleep-label order fix |
| `lib/I18n/translations/english.yaml` | new UI strings (dashboard + scripts) |
| `src/activities/boot_sleep/SleepActivity.{h,cpp}` | dashboard sleep-screen render (+ landscape), resolved image path |
| `src/JsonSettingsIO.cpp` | persist `dashboardScreenId` in `state.json` |
| `src/activities/home/HomeActivity.{h,cpp}` | Dashboard + Scripts menu items |
| `src/activities/ActivityManager.{h,cpp}` | `goToDashboard()`/`goToScripts()`, `isDashboardActivity()` |
| `src/activities/Activity.h` | `isDashboardActivity()` virtual |
| `src/activities/settings/SettingsActivity.cpp` | on-device string editing; string value display |
| `src/CrossPointState.h` | `sleepingFromDashboard` flag (not persisted); `dashboardScreenId` (persisted) |
| `src/main.cpp` | set the flag in `enterDeepSleep()` |

## Companion projects

- **x4-dashboard-server** — backend for both the "Simple" and "Screen set"
  sources: browser widget editor (text/images/weather/ICS calendar),
  e-ink-accurate preview, 1-bit or native grayscale BMP output,
  portrait/landscape with auto-rotation. It serves any number of screens at
  `/screens.json` while keeping `/dashboard.bmp` for single-image clients, so
  older firmware keeps working unchanged.
- Any **TRMNL BYOS server** — for the TRMNL source; tested against
  `usetrmnl/byos_fastapi`.

## Device setup

1. Flash: `pio run -e default` and install `.pio/build/default/firmware.bin`
   via Settings → System → SD Card Firmware Update (or `pio run -t upload`).
2. Settings → Display → **Dashboard source** → Simple, TRMNL or Screen set.
3. Set the matching address: **Dashboard URL** (Simple), **TRMNL server URL**
   (TRMNL), or **Dashboard server URL** (Screen set — the server root, not a
   file).
4. Home → **Dashboard**, then **Confirm** to download. Afterwards it opens
   offline; **Left/Right** switch screens.
5. Settings → Display → **Sleep Screen** → Dashboard / Dashboard +
   Auto-update to make the selected screen the screensaver.

No server? Skip steps 2–3, copy 480×800 BMPs into `/dashboards` on the card,
and go straight to step 4 — the viewer lists whatever it finds there.
