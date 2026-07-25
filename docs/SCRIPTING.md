# Script Execution (Lua)

cross-trmnl can run **Lua 5.4** scripts from the SD card. Put `.lua` files in a
`/scripts` folder on the card, then open **Scripts** from the home menu, pick
one, and press Confirm to run it. A short Back press is delivered as `"back"`
(e.g. to close a dialog); call `device.exit()` when the script should quit
itself. **Long-hold Back (~0.8s) always force-quits**, including stuck loops —
games cannot override that.

Copy the samples in [`lua-scripts-src/`](../lua-scripts-src) onto the card to
get started.

## Safety

Scripts are sandboxed and cannot brick the device:

- **Long-hold Back** (~0.8s) always force-aborts via the VM instruction hook
  and blocking APIs (`input.wait` / `input.poll` / `device.sleep`). A short
  Back press is soft: delivered to `input.wait` / `input.poll` as `"back"`.
- **Memory is capped**; a script that allocates too much fails cleanly with
  "not enough memory" (and how much it used, of how much it was allowed)
  instead of crashing the firmware. The budget is derived at start-up from the
  free heap, less a reserve for the rest of the system, clamped to 96–160 KB.
  The reserve is a setting — see [Memory](#memory) for how to read the numbers
  and what moving it costs.
- **No file is read whole above 50,000 bytes** — the script, each `require`d
  module, and `fs.read`. A larger file stops the script with an error naming it
  and the limit, instead of arriving cut mid-token and failing as a syntax
  error at an arbitrary line. See [File size](#file-size).
- **Errors are caught** and shown on screen with a traceback.
- **File writes are confined to `/scripts`** (reads can be anywhere on the SD
  card), so a script can't overwrite system files.
- No `os.execute`, `io`, `loadlib`, `load`, or `debug` — only the safe
  standard libraries (`string`, `table`, `math`, `utf8`, `coroutine`) plus the
  device API below.
- `require("name")` exists in a sandboxed form, searching `/scripts/engine/`
  (the shared game engine) then `/scripts/` (dots map to subdirectories, so
  `require("mygame.world")` loads `/scripts/mygame/world.lua`), preferring a
  precompiled `.luac` over `.lua` in each, with the result cached. There is no
  package system and no C loader. See [Precompiling](#precompiling-scripts).

## Language

Standard Lua 5.4 with 32-bit integers and 32-bit floats (the ESP32-C3 has no
FPU). `string`, `table`, `math`, `utf8`, and `coroutine` are available.
`print(...)` and `device.log(...)` write to the serial monitor.

## Device API

### `screen` — draw on the e-ink display
Drawing does not appear until you call `screen.update()`.

| Function | Description |
|---|---|
| `screen.clear([ink])` | Fill the screen. Omitted = white; an ink name (below) fills at that level; a number is a raw framebuffer byte (`0x00` = black). |
| `screen.text(x, y, str [, opts])` | Draw text. `opts` = `{size=, bold=, align=}`. `size` is `"small"`/`"medium"`/`"large"`. `align` is `"left"` (default), `"center"`, or `"right"`. |
| `screen.line(x1, y1, x2, y2 [, width])` | Draw a line. |
| `screen.rect(x, y, w, h [, fill [, ink]])` | Rectangle; `fill=true` for a solid block. Optional `ink` (default `true` = black; `false` = white, or an ink name) clears/fills under overlays. |
| `screen.pixel(x, y [, on])` | Set a pixel (`on=false` clears it, or pass an ink name). |
| `screen.image(path, x, y [, w, h])` | Draw a BMP file, scaled to fit `w`×`h` (default: full screen). Returns `true` on success. |
| `screen.save(path [, x, y, w, h])` | Write a region of the framebuffer (default: the whole screen) as a 1-bit BMP that `screen.image` can read back — see `paint.lua`, which round-trips its canvas through it. The region is clipped to the screen; writes are confined to `/scripts` like `fs.write`. Returns `true` on success. |
| `screen.width()` / `screen.height()` | Screen size in pixels. |
| `screen.invert()` | Invert the whole framebuffer. |
| `screen.update([mode])` | Push the framebuffer to the panel. `mode` = `"fast"` (default), `"full"` (cleanest, slower), or `"half"`. |

#### Ink levels

Wherever an `ink` argument is accepted you may pass `true`/`false` as before, or
one of four level names — the same palette the device's own UI draws with:

| Name | Ink coverage |
|---|---|
| `"white"` | 0% |
| `"lightgray"` / `"lightgrey"` | 25% |
| `"darkgray"` / `"darkgrey"` | 50% |
| `"black"` | 100% |

The panel is **1 bit per pixel**, so the two grays are ordered dither, not real
gray: they are patterns keyed on *absolute screen position*, which has two
consequences worth designing around. Adjacent gray fills tile seamlessly, so
large areas look uniform. But a *small* shape only inks where the pattern
happens to fall — a single `"lightgray"` pixel appears one time in four, and a
`"darkgray"` one every other time. Below roughly 4×4, prefer black or white, or
accept that a shape's weight shifts as it moves. An unrecognised name falls back
to the default rather than raising.

### `input` — buttons
Button names: `"confirm"`, `"left"`, `"right"`, `"up"`, `"down"`, `"power"`,
`"back"`.

| Function | Description |
|---|---|
| `input.wait()` | Block until a button is pressed; returns its name (including soft `"back"`). Long-hold Back aborts instead. |
| `input.poll()` | Non-blocking; returns a button name or `nil`. Soft `"back"` same as wait. |
| `input.down(name)` | `true` while that button is held. |

### `fs` — SD card files
Writes/removes are limited to `/scripts`; relative paths resolve there.

| Function | Description |
|---|---|
| `fs.read(path)` | Return file contents as a string, or `nil` if missing. Raises if the file is over the 50,000-byte read limit ([File size](#file-size)) — a short string would be indistinguishable from a whole one. |
| `fs.readRange(path, offset, len)` | Return `len` bytes starting at byte `offset`, or `nil` if the file is missing. The piecewise counterpart of `fs.read` for files that are deliberately larger than a script's memory — packed story text is read one record at a time, the way `screen.image` streams rows. `len` is capped at 4096, and a short read (offset past the end) raises rather than returning half a record. |
| `fs.write(path, str)` | Overwrite a file. Returns `true` on success. |
| `fs.append(path, str)` | Append to a file. Appending reads the file whole first, so it raises rather than rewrite a file that is already over the read limit. |
| `fs.remove(path)` | Delete a file. |
| `fs.mkdir(path)` | Create a directory. |
| `fs.exists(path)` | `true` if the path exists. |
| `fs.list([dir])` | Table of entry names (directories end with `/`). Default `dir` is `/scripts`. |

### `http` — network
| Function | Description |
|---|---|
| `http.get(url)` | GET a URL; returns the body as a string, or `nil, error`. WiFi is connected automatically from saved networks on first use, and torn down when the script exits. Use `http://` for LAN servers. |

### `device` — device info & timing
| Function | Description |
|---|---|
| `device.battery()` | Battery percentage (integer). |
| `device.sleep(ms)` | Sleep; long-hold Back force-aborts. |
| `device.millis()` | Milliseconds since boot. |
| `device.mac()` | WiFi MAC address string. |
| `device.version()` | Firmware version string. |
| `device.log(str)` | Write a line to the serial monitor. |
| `device.exit()` | End the script (Stopped screen), e.g. after an exit confirm. |
| `device.mem()` | Lua heap use: returns `used, peak, limit, freeHeapAtStart` in bytes. `limit` is derived at launch from the free heap minus a reserve, then clamped to a band — so the fourth value is what tells you whether a small `limit` means a tight heap or a policy clamp. On the simulator the fourth value is **0**: the host has no such heap, and inventing a plausible number would put a fabrication where a measurement belongs. `meminfo.lua` prints all four. |

## Example

```lua
-- clock-ish greeting with a battery bar
local pct = device.battery()
screen.clear()
screen.text(screen.width() // 2, 60, "Good day!", {size = "large", bold = true, align = "center"})
screen.text(20, 140, "Battery " .. pct .. "%", {size = "medium"})
screen.rect(20, 175, screen.width() - 40, 24)
screen.rect(20, 175, (screen.width() - 40) * pct // 100, 24, true)
screen.update("full")
input.wait()
```

See [`lua-scripts-src/`](../lua-scripts-src) for `hello.lua`, `battery.lua`,
`paint.lua`, `http_dashboard.lua`, `life.lua`, plus two small games:
`adventure.lua` (tile maps, dialogs, BMP sprites loaded from files) and
`catgo.lua` (a turn-based stealth puzzle).

## The game engine

The games are built on a reusable engine in
[`lua-scripts-src/engine/`](../lua-scripts-src/engine/) — one
self-contained directory, copied to `/scripts/engine/` on the card **once**
and shared by every game. The engine is an **optional add-on**: plain
scripts need nothing besides their `.lua` files in `/scripts`, and the
scripting system runs fine without the engine directory present.

| Module | Provides |
|---|---|
| `require("buttons")` | Device-aligned action chrome: `buttons.draw{back=, confirm=, power=}` — Back bottom-left (5% inset), Confirm after it, Power vertical on the right from 15% down. Actions are Title Case. `buttons.showButtonNames` (default false) toggles button-name prefixes. |
| `require("game")` | The world shell: `game.new{sprites, tiles, rooms, start, footer, hud, playerSprite}` → rooms with edge exits (`:enterRoom`), rendering (`:draw`/`:refresh`/`:toast`), portrait dialogs (`:say`/`:ask`), `:endScreen`, `:adjacentTo`, and `:run{onUse, onPower, onPickup}` — the standard explore loop. Movement never rewrites map tiles (walkable scenery survives walking over it); only `onPickup` chars (cleared to `.`) and `:setTile`/dialog `mapset` change the map — so never use the same char for scenery and a pickup. |
| `require("menu")` | `menu.show{title, sprites, items}` — a generic item-carousel menu: Left/Right item, Up/Down action, Confirm do, Power close. `allowBack = false` makes it refuse to close (Back and Power do nothing, and their captions are hidden) for menus the player must act on; an empty menu always closes. |
| `require("dialogtree")` | Data-driven story runner: `dialogtree.run(G, tree, start, vars)` walks nodes (pages / choices / branches / map edits / endings). `tree.speakers` maps ids to `{name, portrait?}` so nodes set `speaker = "elder"` once and pages inherit (override with `page.speaker` or explicit `who`/`right`). Talk lists (`tree.talk.elder = {speaker=..., {when=..., pages=...}, ...}`) colocate greeting conditions with text; `start` may be a node id, an inline node, or such a list. Conditions via `dialogtree.check` (`{sword = true}`, `{["mushrooms>="] = 3}`, `near_S`, `room`). `locked = true` on a node makes its windows refuse Back and stays on for the rest of that walk (`locked = false` releases it), so marking where a fight starts covers everything it leads to; the flag is local to `run`, so it always lifts when the walk ends. |
| `require("dialog")` | `dialog.show(pages, opts)` — full-screen book-style dialog pages with optional `left`/`right` portrait sprites; `dialog.choose(prompt, options, opts)` — a full-screen choice, returns the picked index. Back closes a window by default; `opts.allowBack = false` makes it refuse — Back does nothing and its caption is hidden, so the player must answer (`show` never returns false, `choose` never returns nil). |
| `require("tilemap")` | ASCII tile maps: `tilemap.new(lines, {solid = "#E"})` → grid with `:at`/`:set`/`:solid`/`:find`; `tilemap.draw(grid, opts)` renders chars via sprites or functions; `tilemap.sprite` (BMP with fallback box); `tilemap.step`/`tilemap.DIRS` for 4-direction movement. |
| `require("patrol")` | GO-style enemies: `patrol.new(specs)` ping-pong patrols with facing, `patrol.step`, `patrol.vision`, `patrol.seen`. |
| `require("screenfit")` | Resolution scaling: `screenfit.apply{device = "x4"}` declares the panel a script was designed for (`"x4"` 480×800, `"x3"` 528×792, or explicit `width`/`height`). On a different panel the global `screen` is wrapped so every coordinate is scaled — `mode = "fit"` (uniform, centered; default), `"stretch"`, or `"off"`. `screen.width()`/`height()` keep reporting the design resolution, so layout math needs no changes. |
| `require("vector")` | Simple vector drawing + constrained SVG: `vector.line`/`rect`/`polyline`/`polygon`/`circle`; `vector.shape{...}:draw(x,y,size)` for tile scenery (normalized 0–1 cmds); `vector.path(cmds,x,y,w,h)`; `vector.svg(str,x,y,w,h)` / `vector.svgFile(path,x,y,w,h)` for a subset (`viewBox`, `rect`/`circle`/`line`/`polyline`/`polygon`/`path` M/L/H/V/Z, `g` translate/scale). The SVG reader lives in `vectorsvg.lua` and loads only when `svg`/`svgFile` is first touched — it costs ~11 KB on the device, and most scripts never draw one. |
| `require("buttonsv")` | Loaded by `buttons` on demand, never directly: the stacked Power caption and its font-metrics table (~5 KB on the device). A script with no `power` hint never loads it. |

A game keeps its **data** in require-able modules — tile definitions + maps
(`adventure/world.lua`, `catgo/levels.lua`), the story as a dialog tree
(`adventure/dialogs.lua`), and inventory definitions (`adventure/items.lua`)
— while the top-level script is thin wiring. Editors for sprites, maps,
dialog and items live in [`tools/gamedev/`](../tools/gamedev/); each reads the
game's own data module directly, so there is no separate editor format. The
dialog editor opens a story and its sibling `items.lua` together — item
actions point at dialog scenes and share the story variables, so renaming a
scene, variable, speaker or conversation propagates across both files.
`engine_editor.py` opens a whole game and hosts the tile, dialog/item and
sprite editors as tabs of one window (Save-all, Reload-all, Play ▶ in the
simulator). A story-variable add/rename/remove there also reaches the game's
**wiring** script — the `vars = {…}` initial-value table and the `vars.name`
reads move with the dialog/item edits so the three files never drift; dynamic
`vars[expr]` accesses can't be rewritten safely and are reported instead.

## Testing scripts on a PC

[`tools/lua-sim/`](../tools/lua-sim/) is a host-side simulator of this API
(Python + Lua 5.4). It opens a window showing the framebuffer and maps the
keyboard to the device buttons, or runs headless with a scripted key sequence
for automated checks:

```
pip install lupa pillow
python tools/lua-sim/sim.py lua-scripts-src/life.lua              # interactive
python tools/lua-sim/sim.py lua-scripts-src/paint.lua \
    --keys confirm,right,confirm --frames out/                      # headless
```

It approximates fonts and refresh timing but matches the API surface and
`/scripts` write confinement. Frames are thresholded to 1 bit per pixel like the
real panel, so dithered grays read correctly instead of being lost among
antialiased text; `--no-quantize` turns that off for a smoother preview.

Point it at the **source** tree. It cannot run `.luac` — see
[Precompiling](#precompiling-scripts) for why, and for how to verify a compiled
tree instead of running it.

## Memory

The device is the constraint that bites first: a dialog-heavy game can exhaust
the Lua budget while it is still loading. The simulator measures it for you.

```
python tools/lua-sim/sim.py mygame.lua --keys confirm,confirm      # report
python tools/lua-sim/sim.py mygame.lua --keys ... --fail-over-budget   # gate
python tools/lua-sim/sim.py mygame.lua --keys ... --min-heap       # headroom
```

Every run prints a per-`require` breakdown plus the peak and final heap, so an
expensive module is obvious. `--fail-over-budget` answers *does this fit?* the
way the device does — it imposes the equivalent ceiling and exits **2** if the
script cannot survive it (0 = fits, 1 = script error), which makes it usable as
a regression gate. `--min-heap` bisects to the smallest heap the script
survives on, which is the number to watch as a game grows.

**Host bytes are not device bytes.** The device builds Lua with `LUA_32BITS`,
so a `TValue` is 8 bytes against the 64-bit host's 16, a hash `Node` 16 vs 24,
a `Table` 32 vs 56. But bytecode (4 bytes/instruction), line info and string
payloads are byte-identical on both, and for real scripts that incompressible
part is about 28% of the heap. The measured whole-heap ratio is therefore
**0.68**, not the 0.5 a naive word-size argument gives — the simulator applies
it for you. (An earlier version of this page recommended `--max-mem 196608` on
that naive 2× assumption. It was ~35% too generous, and passed a game that then
ran out of memory on hardware.)

Rules of thumb, measured on the shipped examples: a bare script costs ~11 KB,
the shared engine plus a one-room world about **29 KB** before any game
content, `life.lua` ~33 KB, `catgo.lua` ~57 KB and `adventure.lua` ~114 KB.

What is actually in that heap, measured on `adventure.lua`: about **46%
compiled code** (`Proto` objects and bytecode), **36% tables and hash nodes**,
and only **17% strings** — in `dialogs.lua` the tables holding the strings cost
28,976 bytes against the strings' 13,831, two thirds overhead to one third
payload. The levers that work: don't load engine modules you never call; don't
write into a `require`d data module at runtime (that growth is permanent — the
module is cached for the whole run); [precompile](#precompiling-scripts), which
attacks the 46%; and for a dialog-heavy game, **pack the story**
(`tools/gamedev/pack_dialogs.py`): prose moves to a sidecar `.bin` read back
one page at a time through `fs.readRange`, text-only pages become bare
integers, and chosen node groups split into arc modules `require`d only when a
playthrough first visits them. The authored `dialogs.lua` stays untouched — the
pack is a build step, proven equivalent by replaying every route against both
trees frame-for-frame. On adventure it cut the dialog module's boot-time
residency by more than half and turned one 17.3 KB contiguous file read into a
4.7 KB core plus 1–3 KB arcs.

### Reading the numbers on hardware

Run [`meminfo.lua`](../lua-scripts-src/meminfo.lua) from the script browser.
It prints all four `device.mem()` returns and the subtraction between them:

```
Free heap at launch      213.2 KB
Held back                 72.0 KB     <- the reserve setting, as applied
Allowed a script         141.2 KB
Used right now            35.5 KB
Peak this run             35.5 KB
```

If **held back** does not equal the reserve you set, the budget was clamped
rather than derived — the floor raised it on a tight heap, or the ceiling
capped it on a generous one. That distinction matters because the two want
opposite fixes.

### The three settings (Settings → System)

| Setting | Default | What it does |
|---|---|---|
| **Script Heap Reserve (KB)** | 72 | Heap withheld from the script, 32–128 KB. Lowering it hands a game more memory. |
| **Strip Script Debug Info** | off | Frees line numbers, local and upvalue names after each chunk loads. ~17 KB on `adventure.lua`. |
| **Show Script Memory Report** | off | Prints the figures above after *every* script ends, including one that failed. |

The reserve is not free memory sitting idle. The framebuffer is already
allocated when a script starts, but rendering, the SD layer and the
out-of-memory screen itself all still need room — and that screen is drawn
while the failed script's arena is *still resident*, which is why the setting
will not go below 32 KB. Lower it while watching `meminfo.lua`, not blindly.

Stripping defaults to off because it takes the file and line out of every
runtime error, which works directly against every other diagnostic here. It is
what you turn on when a game does not fit. If you are shipping rather than
debugging, prefer [precompiling with `-s`](#precompiling-scripts) — same
saving, and the device does no work for it.

### File size

A second, separate ceiling, and the one a growing story file meets first: **no
file is read as a whole above 50,000 bytes** (`HalStorage::kMaxReadFileBytes`)
— not the script, not a `require`d module, not `fs.read` or the read half of
`fs.append`.

It guards the *general* heap, not the Lua budget above. A whole-file read lands
in one contiguous Arduino String outside the script's budget — on an ESP32-C3
with no PSRAM, 320 KB of DRAM and ~52 KB of that already static, so roughly
275 KB of heap before the framebuffer takes its 48 KB — and it is still live
while the parser builds the compiled chunk. So it competes for exactly the
reserve that the memory budget set aside for the rest of the system: at 50,000
bytes a single read is already about two thirds of the 72 KB default — which is
worth remembering before lowering that setting.

Raising it would buy little. 50 KB of Lua source compiles to a comparable
amount of Lua heap, so a single module that size cannot fit the 96–160 KB
budget alongside anything else, and `fs.read` pays for the file twice (once in
the String, again in the Lua string it is copied into). The memory budget is
the limit that actually binds; this one is the backstop that keeps a
half-read file from ever reaching the parser.

Over the limit, the run stops with the file's name and the number — on the
device and in the simulator alike, which enforces the same cap so an oversized
module fails on the host instead of only on hardware. The fix is to split the
file into smaller `require`d modules. Every simulator memory report also prints
the largest single file the run read, as a percentage of the cap, so the trend
shows up long before it becomes a failure:

```
  largest single file       25.1 KB   51% of the 48.8 KB SD read cap (/scripts/adventure/dialogs.lua)
```

`--device x4|x3|WxH` picks the panel to preview (`x4` 480×800 is the
default, `x3` is 528×792), and the window's Device dropdown restarts the
script at the chosen size — the quickest way to see how a script (with or
without `engine/screenfit`) will look on another device.

## Precompiling scripts

### The three trees

Scripts live in one authored tree and are compiled into another; a third is
just scratch space the packer uses in between.

| Directory | Committed? | What it is |
|---|---|---|
| **`lua-scripts-src/`** | yes | The source of truth. Hand-authored `.lua`, sprites, data. The simulator and the editors (`tools/gamedev/`) work here, and nothing generated is ever written back into it. |
| **`lua-scripts-stage/`** | no (gitignored) | Intermediate. `pack_dialogs.py` writes the packed dialog core, arc modules and `.bin` here; `compile_tree.py` reads it as an overlay. Regenerated every build — safe to delete. |
| **`lua-scripts-build/`** | no (gitignored) | The finished card tree: `.luac` + assets. **This is what you copy to the SD card as `/scripts`.** |

The device only ever sees `lua-scripts-build/` copied onto the card. On the
card the folder is named `/scripts`, and every `require`/path inside the
scripts is written against that device path (`/scripts/engine/...`) — so the
host directory name is free to be descriptive without affecting anything at
runtime.

Compiling ahead of time wins twice: the device skips the parser (whose peak
runs ~1.28× the resident cost, and is what actually kills a load that would
otherwise have fitted), and `-s` drops the debug arrays permanently.

```bash
python tools/luac/build.py                  # build the host compiler, once
python tools/luac/compile_tree.py           # lua-scripts-src -> lua-scripts-build
```

A packed game (see [Memory](#memory)) adds its pack step before the compile,
and the generated files ride in as an overlay — the authored tree is never
written to:

```bash
python tools/gamedev/pack_dialogs.py lua-scripts-src/adventure/dialogs.lua \
    -o lua-scripts-stage/adventure --require-prefix adventure \
    --arc mushroom:fight_mushroom,fight_mushroom_eaten \
    --arc die_armed:fight_die_slash_chomp,fight_die_pierce_stomp \
    --arc die_bare:fight_die_fists_ \
    --arc fight:fight_ \
    --arc coda:storm,coda_,ghost,rest_,dragon_eats_,dragon_slay_scene \
    --arc deeds:eat_mushroom,contemplate,drink_elixir,swing_sword,count_gold,uncork_
python tools/luac/compile_tree.py --overlay lua-scripts-stage
```

Or just run `build_scripts.bat`, which is this pipeline. Arc order matters —
the first matching arc claims a node, so specific splinters (the mushroom
pair, the per-loadout deaths) are listed before the `fight_` catch-all. Cut
arcs along *player-choice* lines: nodes shared by every path through a fight
(the round nodes, gated by `when` conditions) belong together, because
splitting them would mean duplicating story.

Copy `lua-scripts-build/` to the card. `require` prefers `.luac` over `.lua` in the
same directory, and the browser lists both, so a card can carry either or both.

**The compiler must be built from `lib/Lua`, and only from there.** A system
`luac` produces bytecode the device rejects: `lundump.c`'s `checkHeader`
compares `sizeof(Instruction)`, `sizeof(lua_Integer)` and `sizeof(lua_Number)`
against the loading VM, and the firmware's `luaconf.h` hard-codes
`LUA_32BITS 1` — 4-byte integers and `float` numbers, against a stock build's
8 and `double`. Building the vendored sources gives the host tool that config
by construction; there is no flag to pass, and therefore none to get wrong.
`tools/luac/selftest.py` demonstrates the rejection using the Lua that `lupa`
already ships.

**The simulator runs source, not bytecode**, and always will: `lupa` embeds a
stock 64-bit Lua, so the header check rejects device chunks in the host
direction too. `lua-scripts-src/` stays the single source of truth; point the
simulator at it, and verify the compiled tree with `luac --check` instead of by
running it. Pointing the simulator at a `.luac` says so plainly rather than
failing inside a UTF-8 decoder.

See [`tools/luac/README.md`](../tools/luac/README.md).
