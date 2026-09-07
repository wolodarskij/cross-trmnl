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
