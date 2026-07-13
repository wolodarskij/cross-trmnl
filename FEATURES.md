# cross-trmnl — changes vs. upstream CrossPoint Reader

This fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
adds a **networked dashboard**: the device fetches a full-screen image over
WiFi and shows it on demand and/or as the sleep screen — turning the X4 into a
TRMNL-style e-ink dashboard that is still a full e-book reader.

All changes are additive; the reader, EPUB engine, fonts, themes, file
transfer, and everything else remain untouched upstream code.

## Features

### Dashboard viewer (home menu)
- New **Dashboard** entry on the home screen: connects WiFi (normal selection
  flow), fetches the configured image, and shows it full-screen.
- **Confirm** re-fetches, **Back** exits.
- The last fetched image is cached on the SD card
  (`/.crosspoint/dashboard.bmp`); a failed refresh keeps showing the previous
  image with a small notice instead of an error screen.

### Dashboard as sleep screen
Two new sleep-screen modes (Settings → Display → Sleep Screen):
- **Dashboard** — the cached dashboard image becomes the sleep screen.
  No network on the way to sleep: instant, zero battery cost.
- **Dashboard + Auto-update** — a fresh image is fetched every time the device
  goes to sleep (power button or idle timeout), so the screen the device
  sleeps on is always current.
- Additionally, sleeping while the Dashboard viewer is open always keeps the
  dashboard visible, regardless of the configured sleep-screen mode.

### Two image sources (Settings → Display → Dashboard source)
- **Simple** — fetches a BMP from a plain URL (`Dashboard URL` setting).
  Designed for the companion
  [x4-dashboard-server](https://github.com/wolodarskij/x4-dashboard-server)
  project (widget editor, weather, calendar, 1-bit or native 4-level
  grayscale output).
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
  the web settings API): `dashboardSource`, `dashboardUrl`, `trmnlUrl`,
  `trmnlApiKey`.
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
| `src/activities/dashboard/DashboardActivity.{h,cpp}` | full-screen dashboard viewer |
| `src/network/DashboardImage.{h,cpp}` | shared fetch/cache + source dispatch |
| `src/network/TrmnlClient.{h,cpp}` | TRMNL/BYOS device-API client |
| `src/network/WifiConnector.{h,cpp}` | headless saved-network WiFi connect |
| `FEATURES.md` | this document |

Modified files (all changes small and localized):

| File | Change |
|---|---|
| `src/CrossPointSettings.h` | `DASHBOARD`/`DASHBOARD_AUTOUPDATE` sleep modes, `DASHBOARD_SOURCE` enum, 3 new string settings |
| `src/SettingsList.h` | new settings entries; sleep-label order fix |
| `lib/I18n/translations/english.yaml` | new UI strings |
| `src/activities/boot_sleep/SleepActivity.{h,cpp}` | dashboard sleep-screen render (+ landscape) |
| `src/activities/home/HomeActivity.{h,cpp}` | Dashboard menu item |
| `src/activities/ActivityManager.{h,cpp}` | `goToDashboard()`, `isDashboardActivity()` |
| `src/activities/Activity.h` | `isDashboardActivity()` virtual |
| `src/activities/settings/SettingsActivity.cpp` | on-device string editing; string value display |
| `src/CrossPointState.h` | `sleepingFromDashboard` flag (not persisted) |
| `src/main.cpp` | set the flag in `enterDeepSleep()` |

## Companion projects

- **x4-dashboard-server** — the "Simple" source backend: browser widget editor
  (text/images/weather/ICS calendar), e-ink-accurate preview, 1-bit or native
  grayscale BMP output, portrait/landscape with auto-rotation.
- Any **TRMNL BYOS server** — for the TRMNL source; tested against
  `usetrmnl/byos_fastapi`.

## Device setup

1. Flash: `pio run -e default` and install `.pio/build/default/firmware.bin`
   via Settings → System → SD Card Firmware Update (or `pio run -t upload`).
2. Settings → Display → **Dashboard source** → Simple or TRMNL.
3. Set **Dashboard URL** (Simple) or **TRMNL server URL** (TRMNL).
4. Home → **Dashboard** to fetch and view; Settings → Display →
   **Sleep Screen** → Dashboard / Dashboard + Auto-update to make it the
   screensaver.
