"""
PlatformIO pre-script: optional firmware features.

Reads CROSSPOINT_FEATURE_<NAME> from the environment (build.bat sets them from
its --no-<feature> flags); anything unset is ON. For every feature that is off
it adds -DCROSSPOINT_FEATURE_<NAME>=0 (see src/Features.h for the guards),
removes the feature's source directories from the build so the LDF never sees
their includes, and keeps the feature's library out of the link.

Features: BLUETOOTH, DASHBOARD (x4-dashboard-server source), TRMNL, LUA.
The `slim` env always builds without Bluetooth.
"""

import os

Import("env")  # noqa: F821 - provided by PlatformIO

FEATURES = ("BLUETOOTH", "DASHBOARD", "TRMNL", "LUA")


def _enabled(name):
    value = os.environ.get("CROSSPOINT_FEATURE_" + name, "1").strip().lower()
    return value not in ("0", "off", "no", "false")


flags = {name: _enabled(name) for name in FEATURES}
if env["PIOENV"] == "slim":
    flags["BLUETOOTH"] = False

defines = [("CROSSPOINT_FEATURE_%s" % name, 1 if on else 0) for name, on in flags.items()]
# The SDK's own switch for BleKeyboardHost: 1 compiles the NimBLE central, 0 links
# its no-op stubs. Owned here rather than per env in platformio.ini so the two
# switches can never disagree.
defines.append(("FREEINK_CAP_BLE_HID_HOST", 1 if flags["BLUETOOTH"] else 0))
env.Append(CPPDEFINES=defines)

# Source exclusions. Excluding a directory is what keeps its library out: the
# LDF scans the remaining sources for #includes, so with src/scripting gone
# nothing references lua.h and lib/Lua is never compiled.
excludes = []
if not flags["LUA"]:
    excludes += ["-<scripting/>", "-<activities/scripts/>"]
if not flags["TRMNL"]:
    excludes += ["-<network/TrmnlClient.cpp>"]
if not flags["DASHBOARD"]:
    excludes += ["-<network/DashboardSet.cpp>"]
if not (flags["DASHBOARD"] or flags["TRMNL"]):
    excludes += ["-<activities/dashboard/>", "-<network/DashboardImage.cpp>"]
if excludes:
    # platformio.ini sets no build_src_filter, so SRC_FILTER is unset here and
    # the builder would use its default; restate that default plus our removals.
    base = env.get("SRC_FILTER") or ["+<*>", "-<.git/>", "-<.svn/>"]
    if isinstance(base, str):
        base = [base]
    env.Replace(SRC_FILTER=list(base) + excludes)

if not flags["BLUETOOTH"]:
    # BleKeyboardHost.cpp #includes NimBLE inside its capability guard, which the
    # LDF cannot evaluate, so NimBLE-Arduino would still be compiled and ~7 KB of
    # it linked. lib_ignore is read again at LDF time, so amending it here works.
    config = env.GetProjectConfig()
    section = "env:" + env["PIOENV"]
    ignore = list(env.GetProjectOption("lib_ignore", []) or [])
    if "NimBLE-Arduino" not in ignore:
        ignore.append("NimBLE-Arduino")
        config.set(section, "lib_ignore", ignore)

print("Features: " + ", ".join("%s=%s" % (n.lower(), "on" if on else "OFF") for n, on in flags.items()))
