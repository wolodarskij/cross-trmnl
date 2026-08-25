#!/usr/bin/env python3
"""Tile-map editor for the Lua game engine (engine/tilemap.lua maps).

    python tile_editor.py                 # pick the data module in a file dialog
    python tile_editor.py --game ../../lua-scripts-src/adventure/world.lua
    python tile_editor.py --game ../../lua-scripts-src/catgo/levels.lua

Reads the game's own data module (the single source of truth): the `tiles`
table becomes the palette (name / solid / sprite), and each entry of `rooms`
(or `levels`) becomes an editable map. Requires lupa (pip install lupa).

Prev/Next switches rooms. Click a palette entry to select it; left-drag
paints it, right-drag paints floor ('.'). Save (Ctrl+S) writes every edited
room's map lines back into the Lua file in place. "Copy as Lua" puts the quoted
lines on the clipboard.

`TileEditor` builds into any parent frame and exposes the panel protocol
(dirty / save / reload / title / activate / deactivate) so the unified
engine_editor.py can host it as a tab; run standalone it owns the window.
"""

import argparse
import os
import re
import tkinter as tk
from tkinter import filedialog

from PIL import Image, ImageTk

try:
    from lupa import lua54
except ImportError:
    raise SystemExit("lupa is required: pip install lupa")


def load_game(path):
    """Evaluate the Lua data module and pull out tiles + rooms."""
    lua = lua54.LuaRuntime()
    # draw functions in data modules reference `screen` at call time only,
    # but give them a dummy so nothing explodes if one runs at load time
    lua.execute("screen = setmetatable({}, {__index = function() return function() end end})")
    path = os.path.abspath(path)
    # adventure/world.lua → scripts/; engine modules live in scripts/engine/
    scripts_root = os.path.dirname(os.path.dirname(path))
    engine_dir = os.path.join(scripts_root, "engine")
    # Drop Lua's package.require (same approach as tools/lua-sim/sim.py), then
    # install a sandbox require that finds engine/ next to the game data.
    g = lua.globals()
    for name in ("require", "package"):
        g[name] = None
    loaded = {}

    def lua_require(name):
        name = str(name)
        if name in loaded:
            return loaded[name]
        rel = name.replace(".", "/") + ".lua"
        for base in (engine_dir, scripts_root):
            cand = os.path.join(base, *rel.split("/"))
            if os.path.isfile(cand):
                with open(cand, encoding="utf-8") as f:
                    result = lua.execute(f.read())
                loaded[name] = True if result is None else result
                return loaded[name]
        raise RuntimeError(
            f"module '{name}' not found (looked in {engine_dir} and {scripts_root})"
        )

    g.require = lua_require
    with open(path, encoding="utf-8") as f:
        data = lua.execute(f.read())
    if data is None:
        raise SystemExit(f"{path} did not return a table")

    tiles = {}
    for ch, spec in data["tiles"].items():
        tiles[str(ch)] = {
            "name": spec["name"] or "",
            "solid": bool(spec["solid"]),
            "sprite": (str(spec["sprite"]) + ".bmp") if spec["sprite"] else None,
        }

    rooms_tbl = data["rooms"] or data["levels"]
    if rooms_tbl is None:
        raise SystemExit(f"{path} has neither `rooms` nor `levels`")
    rooms = []
    for entry in rooms_tbl.values():
        rooms.append({
            "name": str(entry["name"] or f"room {len(rooms) + 1}"),
            "map": [str(row) for row in entry["map"].values()],
        })
    return tiles, rooms


def save_room(path, room_index, lines):
    """Rewrite the N-th `map = {...}` block in the Lua file (0-based index)."""
    src = open(path, encoding="utf-8").read()
    blocks = list(re.finditer(r"map = \{.*?\}", src, re.S))
    if room_index >= len(blocks):
        raise SystemExit(f"cannot find map block #{room_index + 1} in {path}")
    m = blocks[room_index]
    indent = " " * 8
    body = "\n".join(f'{indent}"{row}",' for row in lines)
    new_block = "map = {\n" + body + "\n      }"
    out = src[:m.start()] + new_block + src[m.end():]
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(out)


class TileEditor:
    def __init__(self, parent, game_path, embedded=False):
        self.frame = tk.Frame(parent)
        self.top = self.frame.winfo_toplevel()
        self.embedded = embedded
        self.default = "."
        self.zoom = 36

        bar = tk.Frame(self.frame)
        bar.pack(fill="x")
        if not embedded:
            tk.Button(bar, text="Open...", command=self.open_dialog).pack(side="left", padx=2, pady=2)
        tk.Button(bar, text="< Prev", command=lambda: self.switch(-1)).pack(side="left", padx=2)
        tk.Button(bar, text="Next >", command=lambda: self.switch(1)).pack(side="left", padx=2)
        tk.Button(bar, text="Save (Ctrl+S)", command=self.save).pack(side="left", padx=8)
        tk.Button(bar, text="Copy as Lua", command=self.copy_lua).pack(side="left", padx=2)
        self.room_label = tk.Label(bar, text="", anchor="w")
        self.room_label.pack(side="left", padx=8)
        self.status = tk.Label(bar, text="", anchor="e")
        self.status.pack(side="right", padx=6)

        main = tk.Frame(self.frame)
        main.pack(fill="both", expand=True)
        self.pal = tk.Frame(main)
        self.pal.pack(side="left", anchor="n", padx=4, pady=4)

        self.canvas = tk.Canvas(main, bg="white", highlightthickness=0)
        self.canvas.pack(side="left", padx=4, pady=4)
        self.canvas.bind("<Button-1>", lambda e: self.paint(e, None))
        self.canvas.bind("<B1-Motion>", lambda e: self.paint(e, None))
        self.canvas.bind("<Button-3>", lambda e: self.paint(e, self.default))
        self.canvas.bind("<B3-Motion>", lambda e: self.paint(e, self.default))
        self.canvas.bind("<Motion>", self.hover)
        self._accel = []
        self.open_game(game_path)

    # -- panel protocol -------------------------------------------------------

    def title(self):
        room = self.rooms[self.room_i]
        return (f"{os.path.basename(self.game_path)} — "
                f"[{self.room_i + 1}/{len(self.rooms)}] {room['name']}")

    @property
    def dirty(self):
        return bool(self._dirty_rooms)

    def is_dirty(self):
        return bool(self._dirty_rooms)

    def activate(self):
        self._accel.append(("<Control-s>", self.top.bind("<Control-s>", lambda e: self.save(), "+")))

    def deactivate(self):
        for seq, fid in self._accel:
            self.top.unbind(seq, fid)
        self._accel = []

    def reload(self):
        self.open_game(self.game_path)

    # -- game / room state ----------------------------------------------------

    def open_game(self, game_path):
        self.game_path = os.path.abspath(game_path)
        self.tiles, self.rooms = load_game(self.game_path)
        self.sprites_dir = os.path.dirname(self.game_path)
        self.room_i = 0
        self._dirty_rooms = set()

        self.thumbs = {}
        for ch, spec in self.tiles.items():
            if spec["sprite"]:
                p = os.path.join(self.sprites_dir, spec["sprite"])
                if os.path.exists(p):
                    img = Image.open(p).convert("L").resize((self.zoom, self.zoom), Image.NEAREST)
                    self.thumbs[ch] = ImageTk.PhotoImage(img)

        for w in self.pal.winfo_children():
            w.destroy()
        tk.Label(self.pal, text="palette").pack()
        self.selected = tk.StringVar(value=next(iter(self.tiles)))
        for ch, spec in sorted(self.tiles.items()):
            row = tk.Frame(self.pal)
            row.pack(fill="x")
            tk.Radiobutton(row, variable=self.selected, value=ch, indicatoron=False, width=3,
                           text=ch, font=("Consolas", 12, "bold")).pack(side="left")
            if ch in self.thumbs:
                tk.Label(row, image=self.thumbs[ch]).pack(side="left", padx=2)
            solid = " (solid)" if spec["solid"] else ""
            tk.Label(row, text=spec["name"] + solid).pack(side="left", padx=2)
        self.load_room()

    def open_dialog(self):
        path = ask_game_file(initialdir=os.path.dirname(self.game_path))
        if path:
            self.open_game(path)

    def load_room(self):
        room = self.rooms[self.room_i]
        self.grid = [list(row) for row in room["map"]]
        self.rows, self.cols = len(self.grid), len(self.grid[0])
        self.canvas.config(width=self.cols * self.zoom, height=self.rows * self.zoom)
        self.room_label.config(text=f"[{self.room_i + 1}/{len(self.rooms)}] {room['name']}")
        if not self.embedded:
            self.top.title(f"tile_editor — {self.title()}")
        self.redraw()

    def stash(self):
        """Keep the current room's edits in memory before switching away."""
        self.rooms[self.room_i]["map"] = ["".join(r) for r in self.grid]

    def switch(self, d):
        self.stash()
        self.room_i = (self.room_i + d) % len(self.rooms)
        self.load_room()

    def cell(self, event):
        c, r = event.x // self.zoom, event.y // self.zoom
        if 0 <= c < self.cols and 0 <= r < self.rows:
            return c, r
        return None

    def paint(self, event, ch):
        cr = self.cell(event)
        if not cr:
            return
        ch = ch or self.selected.get()
        c, r = cr
        if self.grid[r][c] != ch:
            self.grid[r][c] = ch
            self._dirty_rooms.add(self.room_i)
            self.redraw()

    def hover(self, event):
        cr = self.cell(event)
        star = " *unsaved*" if self.dirty else ""
        if cr:
            ch = self.grid[cr[1]][cr[0]]
            name = self.tiles.get(ch, {}).get("name", "?")
            self.status.config(text=f"{cr[0]},{cr[1]}  '{ch}' {name}{star}")
        else:
            self.status.config(text=star)

    def save(self):
        """Persist every edited room, not just the current one."""
        self.stash()
        for i in sorted(self._dirty_rooms):
            save_room(self.game_path, i, self.rooms[i]["map"])
        n = len(self._dirty_rooms)
        self._dirty_rooms = set()
        self.status.config(text=f"saved {n} room(s) into {os.path.basename(self.game_path)}")
        return True

    def copy_lua(self):
        lua = "\n".join(f'        "{"".join(r)}",' for r in self.grid)
        self.frame.clipboard_clear()
        self.frame.clipboard_append(lua)
        self.status.config(text="Lua map lines copied to clipboard")

    def redraw(self):
        z = self.zoom
        self.canvas.delete("all")
        for r in range(self.rows):
            for c in range(self.cols):
                ch = self.grid[r][c]
                x, y = c * z, r * z
                if ch in self.thumbs:
                    self.canvas.create_image(x, y, image=self.thumbs[ch], anchor="nw")
                elif ch == self.default:
                    self.canvas.create_rectangle(x + z // 2 - 1, y + z // 2 - 1,
                                                 x + z // 2 + 1, y + z // 2 + 1, fill="black")
                elif self.tiles.get(ch, {}).get("solid") and not self.tiles.get(ch, {}).get("sprite"):
                    self.canvas.create_rectangle(x + 3, y + 3, x + z - 3, y + z - 3, fill="black")
                else:
                    self.canvas.create_rectangle(x + 3, y + 3, x + z - 3, y + z - 3)
                    self.canvas.create_text(x + z // 2, y + z // 2, text=ch, font=("Consolas", z // 2))
        for i in range(self.cols + 1):
            self.canvas.create_line(i * z, 0, i * z, self.rows * z, fill="#ddd")
        for i in range(self.rows + 1):
            self.canvas.create_line(0, i * z, self.cols * z, i * z, fill="#ddd")


def ask_game_file(initialdir=None):
    return filedialog.askopenfilename(
        title="Open game data module",
        initialdir=initialdir or os.getcwd(),
        filetypes=[("Lua data modules", "*.lua"), ("All files", "*")])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", help="the game's Lua data module (world.lua / levels.lua); "
                                   "omit to pick one in a file dialog")
    args = ap.parse_args()
    root = tk.Tk()
    game = args.game
    if not game:
        root.withdraw()
        game = ask_game_file()
        if not game:
            return
        root.deiconify()
    ed = TileEditor(root, game)
    ed.frame.pack(fill="both", expand=True)
    ed.activate()
    root.mainloop()


if __name__ == "__main__":
    main()
