#!/usr/bin/env python3
"""Engine editor — every part of a game on the Lua engine, under one hood.

    python engine_editor.py                                   # pick a game
    python engine_editor.py --game ../../lua-scripts-src/adventure.lua
    python engine_editor.py --game ../../lua-scripts-src/adventure   (the folder)

Open a game once and this hosts the existing editors as tabs of a single window:

  • World      — the tile-map editor (world.lua / levels.lua)      [tile_editor]
  • Story & Items — the dialog-tree + inventory + variables editor [dialog_editor]
  • Sprites    — a picker over the game's BMPs + the pixel editor   [sprite_editor]

Only the tabs a game actually has are shown (adventure has all three; catgo has
World + Sprites; a bare script has none). "Save all" / "Reload all" act on every
tab, and "Play ▶" runs the game in the simulator.

Editing a story **variable** (add / rename / remove in the Story tab) also
reaches the game's *wiring* script: the `vars = {…}` initial-value table and the
`vars.name` reads move together with the dialog/item edits, so the three files
never drift apart. Dynamic `vars[expr]` accesses can't be rewritten safely and
are reported for you to check. The wiring edit is buffered and written together
with the story save, so nothing is persisted half-done.

Requires: pip install pillow lupa (same as the individual editors).
"""

import argparse
import os
import subprocess
import sys
import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk

import dialog_editor
import sprite_editor
import tile_editor
import wiring

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SIM = os.path.join(REPO, "tools", "lua-sim", "sim.py")

WORLD_NAMES = ("world.lua", "levels.lua")


class Game:
    """The files that make up one game, discovered from any of its parts."""

    def __init__(self, name, scripts_root, game_dir, wiring_path):
        self.name = name
        self.scripts_root = scripts_root
        self.dir = game_dir
        self.wiring = wiring_path
        self.world = self._first(WORLD_NAMES)
        self.dialogs = self._first(("dialogs.lua",))
        self.items = self._first(("items.lua",))
        self.bmps = ([f for f in sorted(os.listdir(game_dir)) if f.lower().endswith(".bmp")]
                     if game_dir and os.path.isdir(game_dir) else [])

    def _first(self, names):
        if not self.dir:
            return None
        for n in names:
            p = os.path.join(self.dir, n)
            if os.path.isfile(p):
                return p
        return None

    @property
    def has_anything(self):
        return bool(self.world or self.dialogs or self.items or self.bmps)


def discover_game(path):
    """Resolve a game from a folder, a wiring `.lua`, or one of its data files."""
    path = os.path.abspath(path)
    DATA = set(WORLD_NAMES) | {"dialogs.lua", "items.lua"}

    if os.path.isdir(path):
        game_dir = path
        scripts_root = os.path.dirname(path)
        name = os.path.basename(path.rstrip(os.sep))
    else:
        parent = os.path.dirname(path)
        base = os.path.basename(path)
        stem = base[:-4] if base.endswith(".lua") else base
        sibling_dir = os.path.join(parent, stem)
        if base in DATA:                      # a data file inside the game folder
            game_dir = parent
            scripts_root = os.path.dirname(parent)
            name = os.path.basename(parent)
        elif os.path.isdir(sibling_dir):      # a wiring file next to its folder
            game_dir = sibling_dir
            scripts_root = parent
            name = stem
        else:                                 # a bare script (life.lua): nothing to edit
            game_dir = None
            scripts_root = parent
            name = stem

    wiring_path = os.path.join(scripts_root, name + ".lua")
    if not os.path.isfile(wiring_path):
        wiring_path = path if (not os.path.isdir(path) and path.endswith(".lua")
                               and os.path.basename(path) not in DATA) else None
    return Game(name, scripts_root, game_dir, wiring_path)


class EngineEditor:
    def __init__(self, root):
        self.root = root
        self.game = None
        self.panels = []          # [(label, panel)]
        self.active = None
        self.wiring_src = None    # in-memory buffer for the wiring script
        self.wiring_dirty = False
        self.state_name = None    # the wiring state-table identifier, e.g. "vars"

        bar = tk.Frame(root)
        bar.pack(fill="x")
        tk.Button(bar, text="Open game…", command=self.open_dialog).pack(side="left", padx=2, pady=2)
        tk.Button(bar, text="Save all (Ctrl+S)", command=self.save_all).pack(side="left", padx=2)
        tk.Button(bar, text="Reload all", command=self.reload_all).pack(side="left", padx=2)
        self.play_btn = tk.Button(bar, text="Play ▶", command=self.play)
        self.play_btn.pack(side="left", padx=8)
        self.game_label = tk.Label(bar, text="(no game)", anchor="e", fg="#444")
        self.game_label.pack(side="right", padx=6)

        self.body = tk.Frame(root)
        self.body.pack(fill="both", expand=True)
        self.nb = ttk.Notebook(self.body)
        self.empty = tk.Label(self.body, fg="#666", justify="center",
                              font=("Segoe UI", 11))

        self.status = tk.Label(root, text="", anchor="w", relief="sunken")
        self.status.pack(side="bottom", fill="x")

        root.bind("<Control-s>", lambda e: self.save_all())
        self.nb.bind("<<NotebookTabChanged>>", lambda e: self._on_tab())

    # -- opening a game -------------------------------------------------------

    def open_dialog(self):
        if not self._confirm_discard():
            return
        p = filedialog.askopenfilename(
            title="Open a game (wiring .lua or a data file)",
            initialdir=os.path.join(REPO, "lua-scripts-src"),
            filetypes=[("Lua files", "*.lua"), ("All files", "*")])
        if p:
            self.open_game(p)

    def open_game(self, path):
        try:
            game = discover_game(path)
        except Exception as e:
            messagebox.showerror("Open game", f"{path}\n\n{e}")
            return
        self._teardown()
        self.game = game
        self.root.title(f"engine_editor — {game.name}")
        self.game_label.config(text=game.name + (f"  ({game.dir})" if game.dir else ""))

        # wiring buffer + state-table identifier (for variable propagation)
        self.wiring_src, self.wiring_dirty, self.state_name = None, False, None
        if game.wiring and os.path.isfile(game.wiring):
            self.wiring_src = open(game.wiring, encoding="utf-8").read()
            st = wiring.find_state_table(self.wiring_src)
            self.state_name = st.name if st else None

        self.play_btn.config(state=("normal" if game.wiring else "disabled"))

        self._build_tabs()
        if self.panels:
            self.empty.pack_forget()
            self.nb.pack(fill="both", expand=True)
            self.nb.select(0)
            self._on_tab()
            self.set_status(f"opened {game.name} — {len(self.panels)} editor(s)")
        else:
            self.nb.pack_forget()
            msg = (f"Nothing editable in “{game.name}”.\n\n"
                   "No world.lua / levels.lua, dialogs.lua, items.lua or sprites "
                   "were found next to it.")
            if game.wiring:
                msg += "\n\nYou can still Play ▶ it."
            self.empty.config(text=msg)
            self.empty.pack(fill="both", expand=True, padx=40, pady=40)
            self.set_status(f"opened {game.name} — nothing to edit")

    def _build_tabs(self):
        g = self.game
        if g.world:
            p = tile_editor.TileEditor(self.nb, g.world, embedded=True)
            self._add_tab("World", p)
        if g.dialogs or g.items:
            p = dialog_editor.DialogEditor(
                self.nb, g.dialogs or g.items,
                on_var_change=self._on_var_change, on_saved=self._flush_wiring, embedded=True)
            self._add_tab("Story & Items", p)
        if g.bmps:
            p = sprite_editor.SpritePanel(self.nb, g.dir)
            self._add_tab("Sprites", p)

    def _add_tab(self, label, panel):
        self.nb.add(panel.frame, text=label)
        self.panels.append((label, panel))

    def _teardown(self):
        if self.active:
            self.active.deactivate()
            self.active = None
        for _label, panel in self.panels:
            try:
                panel.frame.destroy()
            except tk.TclError:
                pass
        self.panels = []

    # -- tab focus / accelerators ---------------------------------------------

    def _current_panel(self):
        if not self.panels:
            return None
        try:
            idx = self.nb.index(self.nb.select())
        except tk.TclError:
            return None
        return self.panels[idx][1] if 0 <= idx < len(self.panels) else None

    def _on_tab(self):
        panel = self._current_panel()
        if panel is self.active:
            return
        if self.active:
            self.active.deactivate()
        self.active = panel
        if panel:
            panel.activate()

    # -- save / reload / play -------------------------------------------------

    def _dirty(self):
        return self.wiring_dirty or any(p.is_dirty() for _l, p in self.panels)

    def _confirm_discard(self):
        if self.game and self._dirty():
            return messagebox.askyesno("Unsaved changes",
                                       "Discard unsaved changes in the current game?")
        return True

    def _flush_wiring(self):
        """Write the wiring buffer to disk if it changed (called when the Story
        tab saves, so story + wiring land together)."""
        if self.wiring_dirty and self.game and self.game.wiring:
            with open(self.game.wiring, "w", encoding="utf-8", newline="\n") as f:
                f.write(self.wiring_src)
            self.wiring_dirty = False

    def save_all(self):
        saved = []
        for label, panel in self.panels:
            if panel.is_dirty():
                if panel.save() is not False:
                    saved.append(label)
        self._flush_wiring()          # covers add-var seeding (story tab not dirtied)
        self.set_status("saved: " + ", ".join(saved) if saved
                        else "nothing changed to save")

    def reload_all(self):
        if not self.game:
            return
        if self._dirty() and not messagebox.askyesno(
                "Reload all", "Discard unsaved changes and re-read every file from disk?"):
            return
        self.open_game(self.game.wiring or self.game.dir
                       or (self.game.world or self.game.dialogs))

    def play(self):
        if not (self.game and self.game.wiring):
            return
        if self._dirty():
            ans = messagebox.askyesnocancel("Play", "Save changes before playing?")
            if ans is None:
                return
            if ans:
                self.save_all()
        try:
            subprocess.Popen([sys.executable, SIM, self.game.wiring], cwd=REPO)
            self.set_status(f"launched simulator on {os.path.basename(self.game.wiring)}")
        except Exception as e:
            messagebox.showerror("Play", f"Could not launch the simulator:\n\n{e}")

    # -- variable → wiring propagation ----------------------------------------

    def _on_var_change(self, action, a, b):
        """Called by the Story tab after a variable add/rename/remove. Mirror the
        change into the wiring buffer and return a note for the status line."""
        if not (self.game and self.game.wiring and self.wiring_src is not None):
            return None
        if self.state_name is None:
            return "wiring: no vars table found — left untouched"

        if action == "rename":
            res = wiring.rename_in_wiring(self.wiring_src, self.state_name, a, b)
        elif action == "remove":
            res = wiring.remove_in_wiring(self.wiring_src, self.state_name, a)
        elif action == "add":
            initial = simpledialog.askstring(
                "Wiring initial value",
                f"Initial value for '{a}' in the wiring {self.state_name} table\n"
                f"(a Lua literal like 0, false, \"\" — blank to leave it undeclared):",
                parent=self.root)
            if not initial:
                return "wiring: left undeclared"
            res = wiring.seed_in_wiring(self.wiring_src, self.state_name, a, initial.strip())
        else:
            return None

        self.wiring_src = res.src
        if res.changed:
            self.wiring_dirty = True
        note = f"wiring: {res.changed} edit(s)"
        if res.warnings:
            note += " — " + "; ".join(res.warnings)
        return note

    def set_status(self, text):
        self.status.config(text=text)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--game", help="a game: its wiring .lua, its folder, or a data file; "
                                   "omit to pick one in a dialog")
    args = ap.parse_args()
    root = tk.Tk()
    root.geometry("1100x780")
    root.title("engine_editor")
    app = EngineEditor(root)
    game = args.game
    if not game:
        root.withdraw()
        game = filedialog.askopenfilename(
            title="Open a game (wiring .lua or a data file)",
            initialdir=os.path.join(REPO, "lua-scripts-src"),
            filetypes=[("Lua files", "*.lua"), ("All files", "*")])
        if not game:
            return
        root.deiconify()
    app.open_game(game)
    root.mainloop()


if __name__ == "__main__":
    main()
