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
  sleeps on is always current. This refreshes **only the selected screen**:
  pulling every screen here would multiply radio time by the screen count for
  images nothing is about to draw.
- Additionally, sleeping while the Dashboard viewer is open always keeps the
  dashboard visible, regardless of the configured sleep-screen mode.

### Two image sources (Settings → Display → Dashboard source)
Each source is also a build option (`--no-dashboard`, `--no-trmnl`); with
only one compiled in the selector disappears and that source is used.
- **X4 dashboard server** — one `Dashboard server address` setting (the
  server root, e.g. `192.168.1.20:8080`; scheme optional). The firmware asks
  the server for its screens and implements x4-dashboard-server's
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

### Optional features (build switches)
- Every fork feature is a build option: `build.bat default --no-bluetooth
  --no-dashboard --no-trmnl --no-lua --no-tasks` in any combination (all on by
  default;
  `slim` is always without Bluetooth). `scripts/features.py` turns the flags
  into `CROSSPOINT_FEATURE_*` defines (`src/Features.h`), drops the feature's
  sources from the build so the LDF never pulls its library (Lua, NimBLE),
  and hides its settings rows. Settings fields and enum values stay in every
  variant so `settings.json` survives flashing a different one.

### Bluetooth HID input (BLE keyboards & page-turner remotes)
- Built on a project copy of the FreeInk SDK's `BleKeyboardHost`
  (`lib/BleKeyboardHost`, NimBLE central, enabled via
  `FREEINK_CAP_BLE_HID_HOST`). The copy carries HID-report fixes the submodule
  lacks; `lib/BleKeyboardHost/UPSTREAM.md` records the origin commit and the
  diff, and the submodule itself is never modified. The lifecycle is ported from
  upstream's `feat-bluetooth` branch: scan/pair/bond UI
  (Settings → System → Bluetooth), NVS-persisted bonds with auto-reconnect,
  a per-button remap UI for remotes, an in-reader Bluetooth toggle, and a
  status-bar icon while connected. The paired-device list marks the live
  link and no longer re-issues a connect to it.
- **RAM lifecycle**: while Bluetooth is enabled the stack is resident on every
  screen (so a keyboard drives home, browser and settings too) except while
  WiFi is up or the device is going to sleep; `begin()`/`end()` return the
  full ~52 KB to the heap, heap floors defer starts, and heap-starved reader
  builds shed the stack. `slim` builds compile the capability out entirely
  (zero flash/RAM).
- **Keyboard layouts**: text entry resolves HID usages against a selectable
  layout table (`src/BleKeyboardLayouts.cpp`, 15 layouts, AltGr levels, Caps
  Lock tracking) instead of the SDK's fixed US map. Tables are `constexpr`
  overrides on a parent layout, live in flash, and cost no RAM whether or not
  they are selected; a new layout is one table plus one name string.
- **Hide on-screen keyboard**: an option in the Bluetooth menu that hides the
  on-screen keys in every text field while a keyboard is connected (Enter/Esc
  and the front buttons confirm/cancel; the keys return when it disconnects).
- **Keyboard-first additions on top of upstream**: a default navigation map
  (arrows/Enter/Escape/PageUp/PageDown work with no mapping session) and a
  text-sink mode that routes full key events into whatever text UI is open —
  every existing field (WiFi passwords, OPDS search, settings strings) and the
  text editor accept BLE typing with batched e-ink repaints.
- BLE only: the ESP32-C3 has no Bluetooth Classic radio.

### Text editor (fork-local)
- Home-menu editor for `.txt`/`.md` files: line-based editing with the on-screen
  keyboard, full typing with a BLE keyboard (Enter/Backspace/arrows/Ctrl+S/
  Escape), new-file creation from the picker, crash-safe atomic saves (temp +
  rename, the `ProgressFile` pattern, with the temp dropped on any failure), and
  TxtReader page-cache invalidation so edits show up correctly in the reader.
- The document lives in RAM as per-line strings (no single large block), gated on
  **file size (32 KB)**, **line count (1200)**, free heap *and* largest-free-block.
  The line-count gate is load-bearing: the line vector needs one contiguous
  `lineCount * sizeof(std::string)` block, and under `-fno-exceptions` a failed
  allocation aborts the firmware rather than returning null. Larger files stay
  readable through the streaming reader.
- Unsaved work survives sleep: `onExit()` writes the buffer back unless the user
  chose *Discard*. `preventAutoSleep()` alone is not enough — it is only consulted
  on the *current* activity, so it does not fire while the line editor is on top.
- Known limitation: editing a line containing non-ASCII through the on-screen
  keyboard can corrupt it (`KeyboardEntryActivity` is byte-oriented and predates
  this work). The BLE typing path is UTF-8-aware throughout.
- Note: upstream `SCOPE.md` explicitly excludes notepads/typed notes — this
  feature is deliberately fork-local and not intended for an upstream PR.

### Task lists (fork-local)
- Home-menu viewer for Markdown checklists in `/tasks`: pick a list, and Confirm
  cycles the selected task through open -> in progress -> done. A long-Confirm
  (or touch long-press) hides/shows completed tasks and exports the pages.
- `/tasks` is created at boot, so it can be picked in the Text editor before the
  Tasks menu is ever opened. When it is missing or holds no `.md` lists it is
  seeded with a starter `todo.md` that shows the format.
- Editing hands off to the text editor rather than duplicating it: **Edit list**
  in the long-Confirm options (and Confirm on an empty or unreadable list) saves
  pending marks, frees the list, opens the file in the editor, and reloads it on
  return. Both documents are whole-file in RAM, so they are never held at once.
  **+ New list** in the list picker takes a name on the keyboard, writes a
  heading plus one empty task, and opens it in the editor.
- The Text editor has its own home-menu icon (Lucide `square-pen`, generated by
  `scripts/gen_editor_icons.py`, which also emits the `file-plus` row icon).
- Deliberately not an editor. The files are ordinary `.md`, so the text editor
  above writes the task text and this only marks it (see Edit list above).
  `- [ ]` / `- [/]` / `- [x]` with an optional trailing `due:YYYY-MM-DD`; `#` headings become section rows.
- **Lossless by construction.** Every line keeps its original bytes, and the only
  mutation the model can make is overwriting the single character between the
  brackets. Lines the parser does not recognise as tasks — prose, nested bullets,
  a `[!]` marker some other tool wrote — are carried through untouched, so a save
  cannot damage a file this code did not fully understand. Saving uses the same
  crash-safe temp + rename shape as the editor.
- Bounded like the editor and for the same reason (the document is one contiguous
  line vector): **16 KB**, **500 lines**, plus free-heap and largest-free-block
  gates. `TaskLine` stores spans into the raw line rather than copies, so a line
  costs one string rather than three.
- **Rendered pages.** Leaving a list (which includes the sleep transition, since
  `onExit()` runs on stacked activities) saves it and re-renders it to
  `/tasks/<name>-<page>.bmp`, capped at 8 pages. Rendering draws into the
  framebuffer and serialises with the existing `ScreenshotUtil::
  saveFramebufferAsBmp`, which rotates to the panel's own portrait size — the
  same shape the dashboard sleep path already consumes, so the new `TASKS`
  sleep-screen mode is a near-copy of `renderDashboardSleepScreen`.
- Re-render is gated on an FNV-1a hash of the content, not the file's size or
  mtime: ticking a task changes neither, so a metadata check would leave the
  screensaver stale forever. The stamp also covers the font and hide-done
  choices, so changing either re-renders.
- Drawing the pages by hand (rather than through the FreeInkUI list) is what
  allows real typographic state: in-progress rows are bold, done rows struck
  through. `fui::ListProps` styles a whole list, not individual rows, so the
  on-screen view carries state in a per-row checkbox glyph instead.
- **Task font** (`taskFontFamily` / `taskFontSize`) is independent of the
  reader's, so a list can be set large enough to read across a room. That needed
  `SdCardFontSystem::ensureExtraSize()`: `loadFamily()` keeps exactly one
  reader-size font resident, so an SD family at a different task size is simply
  not loaded, and `resolveFontId()` can only ever return the reader's. It falls
  back through `snapToNearestPointSize` to a built-in Noto face.

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
| `src/tasks/TaskFile.{h,cpp}` | Markdown checklist model: lossless parse, marker-only mutation, crash-safe save |
| `src/tasks/TaskRenderer.{h,cpp}` | page layout + BMP output, content-hash staleness stamp, task font resolution |
| `src/activities/tasks/TaskListBrowserActivity.{h,cpp}` | `/tasks` list picker |
| `src/activities/tasks/TasksActivity.{h,cpp}` | the list view: mark state, hide done, export |
| `src/components/icons/tasks.h`, `taskStateIcons.h` | menu icon + the three state glyphs (`scripts/gen_task_icons.py`) |
| `FEATURES.md` | this document |

Modified files (all changes small and localized):

| File | Change |
|---|---|
| `src/CrossPointSettings.h` | `DASHBOARD`/`DASHBOARD_AUTOUPDATE`/`TASKS` sleep modes, `DASHBOARD_SOURCE` enum (X4, TRMNL), new string settings, task font fields |
| `src/SettingsList.h` | new settings entries; task font builders; sleep-label order fix |
| `lib/I18n/translations/english.yaml` | new UI strings (dashboard, scripts, tasks) |
| `src/activities/boot_sleep/SleepActivity.{h,cpp}` | dashboard + tasks sleep-screen render (+ landscape), resolved image path |
| `src/CrossPointState.{h,cpp}` | persist `dashboardScreenId` and `taskListId` in `state.json` |
| `src/activities/home/HomeActivity.{h,cpp}` | Dashboard, Scripts + Tasks menu items |
| `src/activities/ActivityManager.{h,cpp}` | `goToDashboard()`/`goToScripts()`/`goToTasks()`, `isDashboardActivity()` |
| `src/activities/Activity.h` | `isDashboardActivity()` virtual |
| `src/activities/settings/SettingsActivity.cpp` | on-device string editing; string value display |
| `src/CrossPointState.h` | `sleepingFromDashboard` flag (not persisted); `dashboardScreenId` (persisted) |
| `src/main.cpp` | set the flag in `enterDeepSleep()` |
| `src/SdCardFontSystem.{h,cpp}` | `ensureExtraSize()` for fonts rendered at a non-reader size |

## Companion projects

- **x4-dashboard-server** — backend for the X4 dashboard source: browser
  widget editor (text/images/weather/ICS calendar), e-ink-accurate preview,
  1-bit or native grayscale BMP output, portrait/landscape with auto-rotation.
  It serves any number of screens through `screens.json`.
- Any **TRMNL BYOS server** — for the TRMNL source; tested against
  `usetrmnl/byos_fastapi`.

## Device setup

1. Flash: `pio run -e default` and install `.pio/build/default/firmware.bin`
   via Settings → System → SD Card Firmware Update (or `pio run -t upload`).
2. Set the address: **Dashboard server address** (x4-dashboard-server root)
   or **TRMNL server URL**. With both sources compiled in, Settings → Display
   → **Dashboard source** chooses between them.
3. Home → **Dashboard**, then **Confirm** to download. Afterwards it opens
   offline; **Left/Right** switch screens.
4. Settings → Display → **Sleep Screen** → Dashboard / Dashboard +
   Auto-update to make the selected screen the screensaver.

No server? Skip step 2, copy 480×800 BMPs into `/dashboards` on the card,
and go straight to step 3 — the viewer lists whatever it finds there.
