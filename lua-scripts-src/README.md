# Example scripts

Copy these `.lua` files into a `/scripts` folder on the device's SD card, then
run them from **Home → Scripts**.

| Script | Shows |
|---|---|
| `hello.lua` | Minimal script — draw text, wait for a button. |
| `battery.lua` | Device info (`device.*`) + a drawn battery gauge. |
| `meminfo.lua` | What this device allows a script and how the budget was derived — all four `device.mem()` returns. Run it before tuning the reserve in Settings. |
| `paint.lua` | A drawing pad — square/circle brushes from 1 to 32 px, Confirm paints, Back erases (hold Back to exit), and a Power tools panel with New/Open/Save. Pictures round-trip as 1-bit BMPs in `/scripts/paint/` via `screen.save`/`screen.image`, so any BMP dropped there can be edited too. |
| `http_dashboard.lua` | Fetch data over WiFi with `http.get` and display it. |
| `life.lua` | Conway's Game of Life — grid editor + animation loop with `screen.update("fast")`. |
| `adventure.lua` | "The Sword of Ember Peak" — a branching adventure with dialog choices and portraits, an Items menu (Power: Left/Right item, Up/Down deed), and four endings (some of them mushrooms). Needs `adventure/` and `engine/` on the card. |
| `catgo.lua` | "NINE LIVES" — turn-based stealth puzzle (think Hitman GO): dodge patrols, watch their sight lines, reach the old man. Needs `catgo/` and `engine/` on the card. |
| `engine/` | The shared game engine (`game`, `menu`, `dialog`, `tilemap`, `patrol`, `vector`). Copy the whole folder to `/scripts/engine/` once — every game uses the same copy. Modules load on demand, so a script only pays for what it actually calls: `vectorsvg` (the SVG reader), `buttonsv` (stacked Power captions) and `textpack` (packed story text — see `tools/gamedev/pack_dialogs.py`) stay unloaded unless used. |

Device memory, measured on the examples — the Lua budget is 96–160 KB depending
on free heap:

| Script | Lua heap needed |
|---|---|
| `hello.lua` | ~11 KB |
| `meminfo.lua` | ~12 KB |
| `paint.lua` | ~20 KB empty, ~40 KB with its stamp list full |
| the shared engine + a one-room world, no game content | ~29 KB |
| `life.lua` | ~33 KB |
| `catgo.lua` | ~57 KB |
| `adventure.lua` | ~114 KB |

Check your own with `--min-heap`, and gate it in CI with `--fail-over-budget`;
see [Memory](../docs/SCRIPTING.md#memory). On the device itself, run
`meminfo.lua` — it prints what this device allows and how it got there.

These `.lua` files are the source of truth: edit them here, run them from here
in the simulator. To put a smaller, faster-loading copy on the card, compile the
tree with [`tools/luac/`](../tools/luac/README.md) — output goes to
`lua-scripts-build/`, never back into this folder.

Full API reference: [`docs/SCRIPTING.md`](../docs/SCRIPTING.md).

To try a script on your PC before copying it to the card, use the simulator in
[`tools/lua-sim/`](../tools/lua-sim/):

```
python tools/lua-sim/sim.py lua-scripts-src/life.lua
```
