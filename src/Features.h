#pragma once

// Build-time feature switches.
//
// Every switch defaults to ON, so a plain `pio run` (and CI) builds the full
// firmware. build.bat turns features off with --no-bluetooth, --no-dashboard,
// --no-trmnl and --no-lua: it exports CROSSPOINT_FEATURE_<NAME>=0 and
// scripts/features.py turns that into the -D below, drops the feature's
// sources from the build, and keeps the matching libraries (NimBLE, Lua) out
// of the link. Anything that persists (settings fields, enum values) stays in
// every variant so a settings file survives flashing a different variant.
//
//   CROSSPOINT_FEATURE_BLUETOOTH  BLE HID keyboards / page-turner remotes.
//                                 Mirrors FREEINK_CAP_BLE_HID_HOST (the SDK
//                                 switch, also set by features.py).
//   CROSSPOINT_FEATURE_DASHBOARD  x4-dashboard-server source (screens.json
//                                 screen sets, dashboard.bmp single image).
//   CROSSPOINT_FEATURE_TRMNL      TRMNL / BYOS dashboard source.
//   CROSSPOINT_FEATURE_LUA        Lua script runner and the Scripts menu.
//   CROSSPOINT_FEATURE_TASKS      Task lists, their rendered BMP pages, the
//                                 Tasks menu entry and the Tasks sleep screen.

#ifndef CROSSPOINT_FEATURE_BLUETOOTH
#define CROSSPOINT_FEATURE_BLUETOOTH 1
#endif
#ifndef CROSSPOINT_FEATURE_DASHBOARD
#define CROSSPOINT_FEATURE_DASHBOARD 1
#endif
#ifndef CROSSPOINT_FEATURE_TRMNL
#define CROSSPOINT_FEATURE_TRMNL 1
#endif
#ifndef CROSSPOINT_FEATURE_LUA
#define CROSSPOINT_FEATURE_LUA 1
#endif
#ifndef CROSSPOINT_FEATURE_TASKS
#define CROSSPOINT_FEATURE_TASKS 1
#endif

// The dashboard viewer, its home-menu entry and the two dashboard sleep-screen
// modes exist as long as at least one image source is compiled in.
#define CROSSPOINT_FEATURE_DASHBOARD_ANY (CROSSPOINT_FEATURE_DASHBOARD || CROSSPOINT_FEATURE_TRMNL)
