# cross-trmnl

**A [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) fork that turns the Xteink X4 into a TRMNL-style e-ink dashboard — while staying a full e-book reader.**

The device fetches a full-screen image over WiFi and shows it on demand or as
the sleep screen, so every time you put the reader down it becomes an
always-on display for weather, calendar, or anything else you render.

This README covers only what this fork adds. Everything else — the reader
engine, install tooling, custom fonts, internals, and community — comes from
upstream CrossPoint and is documented, unchanged, in
**[docs/CROSSPOINT_README.md](./docs/CROSSPOINT_README.md)** (the original
CrossPoint README, preserved verbatim).

## What this fork adds

- **Dashboard viewer** on the home menu — fetch and show the configured image
  full-screen (Confirm = refresh, Back = exit).
- **Dashboard sleep-screen modes** — *Dashboard* (cached image, instant sleep)
  and *Dashboard + Auto-update* (fresh fetch on every sleep).
- **Two image sources** (Settings → Display → Dashboard source):
  - *Simple* — a plain BMP URL, designed for the companion
    [x4-dashboard-server](https://github.com/wolodarskij/x4-dashboard-server)
    (browser widget editor: text, images, weather, ICS calendar; 1-bit or
    native 4-level grayscale output).
  - *TRMNL* — self-hosted [TRMNL BYOS](https://docs.usetrmnl.com/go) servers
    (tested with [byos_fastapi](https://github.com/usetrmnl/byos_fastapi)):
    auto-provisioning via `/api/setup`, image fetch via `/api/display`,
    on-device PNG→BMP conversion.
- Landscape (800×480) images render full-screen via display rotation;
  4-level grayscale renders everywhere; URL settings editable on-device.
- **Bluetooth keyboards & page-turner remotes** — pair BLE HID devices
  (Settings → Controls → Bluetooth), turn pages from a remote, navigate every
  menu and type into every text field from a real keyboard.
- **Text editor** on the home menu — browse, create, and edit `.txt`/`.md`
  notes on the SD card with the on-screen keyboard or a connected BLE
  keyboard.

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

1. Settings → Display → **Dashboard source** → Simple or TRMNL.
2. Set **Dashboard URL** (Simple) or **TRMNL server URL** (TRMNL).
3. Home → **Dashboard** to fetch and view.
4. Settings → Display → **Sleep Screen** → *Dashboard* or
   *Dashboard + Auto-update* to make it the screensaver.

For the Simple source, run the companion
[x4-dashboard-server](https://github.com/wolodarskij/x4-dashboard-server) and
point the Dashboard URL at `http://<pc-ip>:8080/dashboard.bmp`.

### Bluetooth keyboard / remote

1. Settings → Controls → **Bluetooth** → toggle Bluetooth on → **Scan & Pair**
   and select your device (bonds persist; it auto-reconnects afterwards).
2. A keyboard works immediately: arrows/Enter/Escape navigate menus,
   PageUp/PageDown turn pages, and typing goes into whatever text field is
   open (WiFi passwords, search, settings, the text editor). Remotes map
   their buttons via **Map Remote Buttons**.
3. While reading, toggle Bluetooth from the reader menu; a status-bar icon
   shows when a device is connected.

Notes: BLE only — the ESP32-C3 has no Bluetooth Classic, so Classic-only
devices can't connect. Bluetooth is suspended whenever WiFi is in use (one
radio, and the two stacks don't fit in RAM together) and while it is off it
costs zero heap.

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
