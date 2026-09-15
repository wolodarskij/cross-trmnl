# BleKeyboardHost (project copy)

This is a copy of `freeink-sdk/libs/network/BleKeyboardHost` taken from
freeink-sdk commit `e7d3361` (2026-07-13, "Fix grayscale rendering and add OEM
LUT reference"), with local changes applied on top. It lives here instead of
being edited inside the submodule so the submodule stays pristine and commits
in this repository stay simple. PlatformIO prefers project libraries in `lib/`
over `lib_deps`, so this copy shadows the SDK one; the `lib_deps` entry for it
was removed from `platformio.ini`.

## Local changes

`freeink-sdk.patch` is the exact diff against the origin commit (apply it to the
submodule with `git -C freeink-sdk apply ../lib/BleKeyboardHost/freeink-sdk.patch`
when upstreaming). Only `src/BleKeyboardHost.cpp` differs:

- `parseReportMapHints` accumulates hints across several report maps instead of
  keeping the last one.
- `setupHid` walks every HID service instance (`client->getServices(true)`),
  writes the Report protocol mode, and subscribes to all input Report
  characteristics (an unreadable or short Report Reference descriptor counts as
  input). With report debug on it prints the report ids it found.
- If no Report characteristic could be subscribed it falls back to the Boot
  Keyboard Input Report and switches Protocol Mode to Boot (0).
- `onReportIngest` strips a leading report id when the report is 9 bytes, or
  longer than 9 with a non-zero first byte and a zero third byte.

Motivation: keyboards such as the 8BitDo Retro pair and connect but deliver no
input because they expose several HID service instances / report ids. This is a
hypothesis until confirmed with a `[BleHid]` serial log from such a device.

## Re-syncing with the SDK

1. `git -C freeink-sdk diff e7d3361 HEAD -- libs/network/BleKeyboardHost`
   shows what upstream changed since this copy was taken.
2. Copy the new upstream files here, re-apply `freeink-sdk.patch` (resolve
   conflicts), update the commit hash above and regenerate the patch.
