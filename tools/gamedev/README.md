# gamedev tools

Editors for building Lua games on the engine in
[`lua-scripts-src/engine/`](../../lua-scripts-src/engine/) (see
[`docs/SCRIPTING.md`](../../docs/SCRIPTING.md)).

Requirements: `pip install pillow lupa`.

| Tool | What it does |
|---|---|
| `engine_editor.py` | **The umbrella editor** — open a game once and edit every part of it from one window. `python engine_editor.py --game ../../lua-scripts-src/adventure.lua` (or the folder, or any data file — it discovers the rest). It hosts the three editors below as tabs (**World** = tiles, **Story & Items** = dialog/item/variables, **Sprites**), showing only the ones a game has (adventure has all three; catgo has World + Sprites; a bare script has none). **Save all** / **Reload all** act on every tab and **Play ▶** runs the game in the simulator. Editing a story **variable** (add / rename / remove in the Story tab) also reaches the game's **wiring** script: the `vars = {…}` initial-value table and the `vars.name` reads move together with the dialog/item edits, so the three files never drift apart. Dynamic `vars[expr]` accesses can't be rewritten safely and are reported for you to check. Every panel still runs standalone (the rows below); the shell just puts them under one hood. |
| `sprite_editor.py` | Pixel editor for 1-bit BMP sprites. `python sprite_editor.py ../../lua-scripts-src/catgo/cat.bmp` — left-drag paints, right-drag erases, Ctrl+S saves. Point it at a **folder** (`python sprite_editor.py ../../lua-scripts-src/adventure`) to pick from every BMP there and add new ones. |
| `tile_editor.py` | Map editor driven by the game's own data module (no separate config): `python tile_editor.py` (pick the file in a dialog) or `--game ../../lua-scripts-src/adventure/world.lua`. The `tiles` table is the palette; Prev/Next switches rooms; Ctrl+S writes the map back into the Lua file in place. |
| `pack_dialogs.py` | Build-time packer for a finished dialog tree — the authored `dialogs.lua` is read, never written. Prose moves into a sidecar `.bin` that the engine reads back one page at a time (`fs.readRange` via `engine/textpack.lua`), text-only pages collapse to bare integers, and `--arc NAME:terms` groups split into modules `require`d only when a playthrough first reaches them (a term ending `_` matches as a prefix, others exactly). Output goes to a staging dir that `compile_tree.py --overlay` lays over the source tree, so the card gets the packed shape and the repo keeps the authored one. The generated tree is plain Lua — run it in the simulator with `--sd` and the same routes must produce byte-identical frames, which is the shipped equivalence proof. Adventure's invocation is in [SCRIPTING.md](../../docs/SCRIPTING.md#precompiling-scripts). |
| `dialog_editor.py` | Visual editor for dialog-tree modules (`engine/dialogtree.lua` format) **and their sibling `items.lua`**: `python dialog_editor.py` (file dialog), `--file .../dialogs.lua`, or `--items .../items.lua`. Opening either file auto-loads the other from the same folder (or use the **Open items…** button), so the two documents are edited together and Ctrl+S writes whichever changed. Speakers, Conversations, Scenes and **Items** live in separate tabs (conversations with add / reorder / delete entry buttons; items with name, sprite, an `owned` condition and per-action `label` / condition / **scene** rows); the always-visible pane below lists every story variable (counting uses in **both** files) with a header naming what's selected ("Scene: uncork_do", "Item: Reincarnation vial (4 of 5)"). The Story tab is **shape-aware** — it renders only the sections a scene uses (branch arms, choices, pages, ending), with `+ Add …` buttons for the rest, so nothing is a wall of empty boxes. Everything referenceable is a **dropdown, never free text**: "go to" / action-scene targets, speakers, item/ending sprite (from the folder's BMPs) and style; conditions are picked as `variable ▾ / operator ▾ / value` rows instead of typed, so a typo can't silently break a reference. When a dropdown needs something that doesn't exist yet, it offers `＋ New scene / speaker / variable…` to create it in place, and every scene dropdown has a `→` button to jump straight to its target. Anything the form doesn't model (inline branch effects, `mapset`) is preserved untouched and editable on the Source (Lua) tab, which has vertical + horizontal scrollbars and live-checks validity as you type. The **Story variables** pane lists each variable with its usage count and, when one is selected, every place it's used across both files (double-click to jump there); it also adds / renames / removes variables. **Renames propagate across both files**: renaming a scene, variable, speaker/character, or conversation rewrites every reference — including item `action.node` targets and item `owned`/`when` conditions — so nothing is left dangling (conversation renames also flag the game's external `ACTORS` map). Undo/redo (Ctrl+Z / Ctrl+Y) covers every structural change in both documents; Reload re-reads from disk after manual edits; switching a left tab opens its last-edited (or first) item; both **"used by"** and **"goes to"** are rows of jump buttons; Check story reports dangling targets and unknown speakers. Ctrl+S rewrites each changed file, keeping its header comment and (for the story) node order. |

A game is data plus a thin wiring script:

1. **World** — `mygame/world.lua` returning `{tiles, rooms, start}`
   (see `adventure/world.lua` or `catgo/levels.lua` for the shape). Tiles
   declare `name` (for this editor), `solid`, and either a `sprite` name or
   a `draw` function.
2. **Story** — `mygame/dialogs.lua`, a dialog tree (`engine/dialogtree.lua`
   format): conversations, choices gated on story variables and items,
   variable writes, map edits and endings — all data.
3. **Inventory** — `mygame/items.lua`: what appears in the item menu and
   which dialog node each deed runs.
4. **Sprites** — 1-bit BMPs in `mygame/`, edited with `sprite_editor.py`.
5. **Wiring** — `mygame.lua` (~70 lines): builds `game.new` from the world,
   maps tile chars to dialog actors, feeds items to `menu.show`.

The quickest way to edit all five is `python engine_editor.py --game
../../lua-scripts-src/mygame.lua`, which opens whichever parts exist as tabs and
can Play the game without leaving. Or run each editor above on its own file.

Test on a PC with the simulator next door: `python ../lua-sim/sim.py
../../lua-scripts-src/mygame.lua`. On the card: copy `engine/` once, plus
each game's `.lua` and asset folder — or run `build_scripts.bat` and copy the
compiled `lua-scripts-build/` instead (smaller, faster to load).
