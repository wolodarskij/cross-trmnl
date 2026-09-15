# cross-trmnl

**A [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) fork that turns the Xteink X4 into a TRMNL-style e-ink dashboard — while staying a full e-book reader.**

The device keeps a set of full-screen images on its SD card and shows them on
demand or as the sleep screen, so every time you put the reader down it
becomes an always-on display for weather, calendar, or anything else you
render. Download them over WiFi, or just copy BMPs onto the card — the
dashboard works with no server at all.

This README covers only what this fork adds. Everything else — the reader
engine, install tooling, custom fonts, internals, and community — comes from
upstream CrossPoint and is documented, unchanged, in
**[docs/CROSSPOINT_README.md](./docs/CROSSPOINT_README.md)** (the original
CrossPoint README, preserved verbatim).

## What this fork adds

- **Multi-screen dashboard** on the home menu — opens instantly on the images
  already on the card, with **no network access at all**. Left/Right switch
  screens, Confirm connects and downloads them, Back exits.
- **Works without a server** — drop 480×800 BMPs into `/dashboards` on the SD
  card and they show up alongside anything downloaded. Syncing only ever adds
  files; it never deletes what you put there.
- **Dashboard sleep-screen modes** — *Dashboard* (selected screen, instant
  sleep) and *Dashboard + Auto-update* (refreshes just that screen on every
  sleep).
- **Three image sources** (Settings → Display → Dashboard source):
  - *Screen set* — many screens from one server, via
    [x4-dashboard-server](https://github.com/wolodarskij/x4-dashboard-server)'s
    `screens.json` manifest.
  - *Simple* — a single plain BMP URL from the same server (also the
    compatibility path for older setups).
  - *TRMNL* — self-hosted [TRMNL BYOS](https://docs.usetrmnl.com/go) servers
    (tested with [byos_fastapi](https://github.com/usetrmnl/byos_fastapi)):
    auto-provisioning via `/api/setup`, image fetch via `/api/display`,
    on-device PNG→BMP conversion.
- Landscape (800×480) images render full-screen via display rotation;
  4-level grayscale renders everywhere; URL settings editable on-device.
- **Script execution (Lua)** — a **Scripts** home-menu entry runs `.lua` files
  from the SD card `/scripts` folder. Scripts get a sandboxed device API to
  draw, read buttons, read/write files, and fetch over WiFi; Back aborts any
  script, memory is capped, and errors are caught. See
  [docs/SCRIPTING.md](./docs/SCRIPTING.md).
- **Bluetooth keyboards & page-turner remotes** — pair BLE HID devices
  (Settings → System → Bluetooth), turn pages from a remote, navigate every
  menu and type into every text field from a real keyboard, in the keyboard's
  own layout (15 built in: US, UK, German, French, Spanish, Italian,
  Portuguese, Brazilian, Swedish/Finnish, Danish, Russian, Ukrainian, Polish,
  Czech, Turkish).
- **Text editor** on the home menu — browse, create, and edit `.txt`/`.md`
  notes on the SD card with the on-screen keyboard or a connected BLE
  keyboard. A new file opens straight into the line editor; with a keyboard
  connected the page is typed into directly and Confirm saves.

**The complete change list vs. upstream is in [FEATURES.md](./FEATURES.md).**

## Device support

**Tested on the Xteink X4 only, so far.** It should also run on the
**Xteink X3**: CrossPoint ships one firmware for both devices, and the
dashboard code goes exclusively through CrossPoint's portable
rendering/network layers — screen dimensions are read at runtime, oversized
images are scaled to fit, and TRMNL servers are told the actual panel size.
Untested on X3 hardware though — if you try it, please open an issue with your
results (worst case the image is letterboxed or the landscape rotation
direction needs a flip).

## Build & install

There are no prebuilt releases for this fork — build `firmware.bin` yourself:

```bash
git clone --recursive https://github.com/wolodarskij/cross-trmnl
cd cross-trmnl
# if cloned without --recursive:
git submodule update --init --recursive

pio run -e default        # output: .pio/build/default/firmware.bin
```

On Windows use `build.bat` instead — same thing, but it forces Python to UTF-8
first, without which PlatformIO's console writer dies on a cp1252 console and
the failure looks like a compile error:

```
build.bat                 build the default env
build.bat default upload  build, then flash over USB
build.bat slim clean      any env, any pio target
```

Every fork feature is a build option. All are on by default; drop the ones
you do not want with any of these flags, in any position:

```
build.bat default --no-lua                 no Lua scripts / Scripts menu
build.bat default --no-bluetooth           no BLE keyboards or remotes
build.bat default --no-dashboard --no-trmnl   no dashboard at all
```

`--no-dashboard` removes the x4-dashboard-server source, `--no-trmnl` the
TRMNL one; with both gone the Dashboard menu entry and sleep modes go too.
Without `build.bat`, export `CROSSPOINT_FEATURE_<BLUETOOTH|DASHBOARD|TRMNL|LUA>=0`
before `pio run`. Settings for a feature that is compiled out are kept in the
settings file, so switching variants does not lose them.

Then install it either way:

- **SD Card Firmware Update** (if the device already runs CrossPoint or
  cross-trmnl): copy `firmware.bin` to the SD card, then on the device go to
  Settings → System → SD Card Firmware Update.
- **Web flasher** (fresh device): connect over USB-C and use the "Custom .bin"
  option at https://crosspointreader.com/#flash-tools.

See [docs/CROSSPOINT_README.md](./docs/CROSSPOINT_README.md) for the full
install/build/debug details (prerequisites, USB-locked devices, `esptool`,
serial logging), which apply here unchanged.

## Using it

1. Settings → Display → **Dashboard source** → Screen set, Simple or TRMNL.
2. Set the matching address: **Dashboard server URL** (Screen set),
   **Dashboard URL** (Simple), or **TRMNL server URL** (TRMNL).
3. Home → **Dashboard**, then **Confirm** to download. From then on it opens
   offline; **Left/Right** switch between screens.
4. Settings → Display → **Sleep Screen** → *Dashboard* or
   *Dashboard + Auto-update* to make the selected screen the screensaver.

Run the companion
[x4-dashboard-server](https://github.com/wolodarskij/x4-dashboard-server) and
point **Dashboard server URL** at `http://<pc-ip>:8080` (the root — the device
appends `/screens.json` itself). For the older Simple source, point
**Dashboard URL** at `http://<pc-ip>:8080/dashboard.bmp` instead.

**No server at all:** copy 480×800 BMPs into a `/dashboards` folder on the SD
card and go straight to step 3. One file per screen; the filename is the name
you will see.

### Bluetooth keyboard / remote

1. Settings → System → **Bluetooth** → toggle Bluetooth on → **Scan & Pair**
   and select your device (bonds persist; it auto-reconnects afterwards, on
   every screen — home, browser, settings and reader alike). **Paired
   Devices** marks the live link as *Connected*.
2. A keyboard works immediately: arrows/Enter/Escape navigate menus,
   PageUp/PageDown turn pages, and typing goes into whatever text field is
   open (WiFi passwords, search, settings, the text editor). Remotes map
   their buttons via **Map Remote Buttons**.
3. Pick the keyboard's language under **Keyboard layout** so keys type what
   is printed on them (AltGr levels and Caps Lock included). **Hide on-screen
   keyboard** drops the on-screen keys from text fields while a keyboard is
   connected: type on the keyboard, Enter/OK confirms, Esc/Back cancels, and
   the field grows the on-screen keys back the moment the keyboard drops off.
4. While reading, toggle Bluetooth from the reader menu; a status-bar icon
   shows when a device is connected.

Notes: BLE only — the ESP32-C3 has no Bluetooth Classic, so Classic-only
devices can't connect. Bluetooth is suspended whenever WiFi is in use (one
radio, and the two stacks don't fit in RAM together) and while it is off it
costs zero heap. Adding a keyboard layout is one table in
`src/BleKeyboardLayouts.cpp` (only the keys that differ from US or from a
parent layout) plus a name in `english.yaml`; unused layouts live in flash
and cost no RAM.

The BLE host itself is a project copy of the FreeInk SDK's `BleKeyboardHost`
in `lib/BleKeyboardHost`, carrying HID-report fixes the SDK does not have yet
(keyboards with several HID service instances, e.g. the 8BitDo Retro). The
`freeink-sdk` submodule is never modified; `lib/BleKeyboardHost/UPSTREAM.md`
records the origin commit and the patch, so the copy can be re-synced with
upstream or the fix sent there.

### Text editor

Home → **Text editor** → pick a `.txt`/`.md` file or **+ New text file**.
Up/Down select a line, **Confirm** edits it with the on-screen keyboard,
**long-Confirm** opens options (insert/delete line, save, save & exit), and
**Back** prompts before discarding unsaved changes. With a BLE keyboard you
just type: Enter splits lines, Backspace joins them, **Ctrl+S** saves,
**Escape** exits.

Saves are crash-safe (temp file + rename), and unsaved work is written back
automatically if the device sleeps while the editor is open — only an explicit
*Discard* throws edits away. Limits: files up to **32 KB** and **1200 lines**
open in the editor (the whole document is held in RAM); anything larger stays
readable in the normal reader. Editing a line that contains non-ASCII text via
the *on-screen* keyboard can mangle it — that keyboard is byte-oriented and
predates this feature; the BLE keyboard path is UTF-8-safe.

## Credits

- **[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)** —
  this fork exists only because of the outstanding work of the CrossPoint
  community: the reader engine, display drivers, WiFi stack, settings and web
  UI this project builds on are all theirs. This is **not** an official
  CrossPoint project — for the original firmware, releases, and community, go
  to the [upstream repository](https://github.com/crosspoint-reader/crosspoint-reader)
  and consider [funding their contributors](https://app.royalty.dev/crosspoint-reader/crosspoint-reader).

- **[TRMNL](https://usetrmnl.com)** — the inspiration for the whole dashboard
  concept, and the reason a fleet of self-hosted servers exists for this fork
  to talk to. TRMNL didn't just build a lovely e-ink dashboard product — they
  published an open [device API](https://docs.usetrmnl.com/go), which is exactly 
  the openness that  makes projects like this one possible. 
  If you want a polished dashboard ecosystem, buy their hardware or use their service.

---

cross-trmnl is **not affiliated with Xteink, TRMNL, or CrossPoint.** It is an
independent community fork.
