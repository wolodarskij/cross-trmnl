#!/usr/bin/env python3
"""Pixel editor for 1-bit BMP sprites (the format screen.image() draws).

    python sprite_editor.py path/to/cat.bmp            # edit (or create 40x40)
    python sprite_editor.py new.bmp --size 20 --zoom 20
    python sprite_editor.py path/to/game_folder        # pick from every BMP there

Left-drag paints black, right-drag erases, I inverts, C clears,
Ctrl+S / the Save button writes the BMP. The right panel previews the
sprite at 1x and 2x on white, as the device will show it.

`Editor` and `SpritePanel` build into any parent frame and expose the small
panel protocol (dirty / save / reload / title / activate / deactivate), so the
unified engine_editor.py can host them as a tab; run standalone they own the
window.
"""

import argparse
import os
import tkinter as tk
from tkinter import messagebox, simpledialog

from PIL import Image, ImageTk

BLACK, WHITE = 0, 1


class Editor:
    """One BMP, built into `parent`."""

    def __init__(self, parent, path, size=40, zoom=14):
        self.frame = tk.Frame(parent)
        self.top = self.frame.winfo_toplevel()
        self.path = path
        self.zoom = zoom
        self._accel = []
        if os.path.exists(path):
            self.img = Image.open(path).convert("1")
        else:
            self.img = Image.new("1", (size, size), WHITE)
        self.w, self.h = self.img.size

        bar = tk.Frame(self.frame)
        bar.pack(fill="x")
        tk.Button(bar, text="Save (Ctrl+S)", command=self.save).pack(side="left", padx=2, pady=2)
        tk.Button(bar, text="Clear (C)", command=self.clear).pack(side="left", padx=2)
        tk.Button(bar, text="Invert (I)", command=self.invert).pack(side="left", padx=2)
        self.status = tk.Label(bar, text="", anchor="e")
        self.status.pack(side="right", padx=6)

        main = tk.Frame(self.frame)
        main.pack()
        self.canvas = tk.Canvas(main, width=self.w * zoom, height=self.h * zoom,
                                bg="white", highlightthickness=0)
        self.canvas.pack(side="left", padx=4, pady=4)

        side = tk.Frame(main)
        side.pack(side="left", anchor="n", padx=8, pady=8)
        tk.Label(side, text="preview").pack()
        self.preview1 = tk.Label(side, bd=1, relief="solid")
        self.preview1.pack(pady=4)
        self.preview2 = tk.Label(side, bd=1, relief="solid")
        self.preview2.pack(pady=4)

        self.canvas.bind("<Button-1>", lambda e: self.paint(e, BLACK))
        self.canvas.bind("<B1-Motion>", lambda e: self.paint(e, BLACK))
        self.canvas.bind("<Button-3>", lambda e: self.paint(e, WHITE))
        self.canvas.bind("<B3-Motion>", lambda e: self.paint(e, WHITE))
        self.canvas.bind("<Motion>", self.hover)
        # Single-letter shortcuts fire only while the canvas holds focus, so they
        # can't clobber typing elsewhere when embedded; clicking the canvas focuses it.
        self.canvas.bind("<Enter>", lambda e: self.canvas.focus_set())
        self.canvas.bind("c", lambda e: self.clear())
        self.canvas.bind("i", lambda e: self.invert())
        self.dirty = False
        self.redraw()

    # -- panel protocol -------------------------------------------------------

    def title(self):
        return f"{os.path.basename(self.path)} ({self.w}x{self.h})"

    def is_dirty(self):
        return self.dirty

    def activate(self):
        self._accel.append(("<Control-s>", self.top.bind("<Control-s>", lambda e: self.save(), "+")))

    def deactivate(self):
        for seq, fid in self._accel:
            self.top.unbind(seq, fid)
        self._accel = []

    def reload(self):
        if os.path.exists(self.path):
            self.img = Image.open(self.path).convert("1")
            self.w, self.h = self.img.size
            self.canvas.config(width=self.w * self.zoom, height=self.h * self.zoom)
            self.dirty = False
            self.redraw()

    # -- editing --------------------------------------------------------------

    def cell(self, event):
        x, y = event.x // self.zoom, event.y // self.zoom
        if 0 <= x < self.w and 0 <= y < self.h:
            return x, y
        return None

    def paint(self, event, val):
        c = self.cell(event)
        if c and self.img.getpixel(c) != val:
            self.img.putpixel(c, val)
            self.dirty = True
            self.redraw()

    def hover(self, event):
        c = self.cell(event)
        star = " *unsaved*" if self.dirty else ""
        self.status.config(text=f"{c[0]},{c[1]}{star}" if c else star)

    def clear(self):
        self.img.paste(WHITE, (0, 0, self.w, self.h))
        self.dirty = True
        self.redraw()

    def invert(self):
        self.img = Image.eval(self.img.convert("L"), lambda p: 255 - p).convert("1")
        self.dirty = True
        self.redraw()

    def save(self):
        self.img.save(self.path)
        self.dirty = False
        self.status.config(text=f"saved {os.path.basename(self.path)}")
        return True

    def redraw(self):
        z = self.zoom
        self.canvas.delete("all")
        for y in range(self.h):
            for x in range(self.w):
                if self.img.getpixel((x, y)) == BLACK:
                    self.canvas.create_rectangle(x * z, y * z, x * z + z, y * z + z,
                                                 fill="black", width=0)
        for i in range(0, self.w + 1):  # grid lines
            self.canvas.create_line(i * z, 0, i * z, self.h * z, fill="#ddd")
        for i in range(0, self.h + 1):
            self.canvas.create_line(0, i * z, self.w * z, i * z, fill="#ddd")

        p1 = ImageTk.PhotoImage(self.img.convert("L"))
        p2 = ImageTk.PhotoImage(self.img.convert("L").resize((self.w * 2, self.h * 2), Image.NEAREST))
        self.preview1.config(image=p1)
        self.preview1.image = p1
        self.preview2.config(image=p2)
        self.preview2.image = p2


class SpritePanel:
    """A folder of BMPs: a list on the left, the pixel Editor on the right.
    Switching sprites offers to save unsaved changes first."""

    def __init__(self, parent, bmp_dir, zoom=14):
        self.frame = tk.Frame(parent)
        self.dir = os.path.abspath(bmp_dir)
        self.zoom = zoom
        self.editor = None
        self._active = False

        left = tk.Frame(self.frame)
        left.pack(side="left", fill="y", padx=(2, 0), pady=2)
        tk.Label(left, text="sprites").pack(anchor="w")
        self.listbox = tk.Listbox(left, width=20, height=22, exportselection=False,
                                  font=("Consolas", 10))
        self.listbox.pack(fill="y", expand=True)
        self.listbox.bind("<<ListboxSelect>>", lambda e: self._on_pick())
        tk.Button(left, text="New sprite…", command=self.new_sprite).pack(fill="x", pady=(2, 0))

        self.holder = tk.Frame(self.frame)
        self.holder.pack(side="left", fill="both", expand=True)

        self._names = []
        self.refresh_list()
        if self._names:
            self.listbox.selection_set(0)
            self._load(self._names[0])

    def refresh_list(self, select=None):
        self._names = sorted(f for f in os.listdir(self.dir) if f.lower().endswith(".bmp"))
        self.listbox.delete(0, "end")
        for name in self._names:
            self.listbox.insert("end", name)
        if select in self._names:
            i = self._names.index(select)
            self.listbox.selection_clear(0, "end")
            self.listbox.selection_set(i)

    def _on_pick(self):
        sel = self.listbox.curselection()
        if not sel:
            return
        name = self._names[sel[0]]
        if self.editor and os.path.basename(self.editor.path) == name:
            return
        if not self._confirm_switch():
            self.refresh_list(select=os.path.basename(self.editor.path) if self.editor else None)
            return
        self._load(name)

    def _confirm_switch(self):
        if self.editor and self.editor.dirty:
            ans = messagebox.askyesnocancel(
                "Unsaved sprite", f"Save changes to {os.path.basename(self.editor.path)}?")
            if ans is None:
                return False
            if ans:
                self.editor.save()
        return True

    def _load(self, name):
        if self.editor:
            self.editor.deactivate()
        for w in self.holder.winfo_children():
            w.destroy()
        self.editor = Editor(self.holder, os.path.join(self.dir, name), zoom=self.zoom)
        self.editor.frame.pack(fill="both", expand=True)
        if self._active:
            self.editor.activate()

    def new_sprite(self):
        name = simpledialog.askstring("New sprite", "File name (e.g. hero.bmp):", parent=self.frame)
        if not name:
            return
        if not name.lower().endswith(".bmp"):
            name += ".bmp"
        path = os.path.join(self.dir, name)
        if os.path.exists(path):
            messagebox.showerror("New sprite", f"{name} already exists")
            return
        size = simpledialog.askinteger("New sprite", "Size (pixels):", parent=self.frame,
                                       initialvalue=40, minvalue=4, maxvalue=200) or 40
        Image.new("1", (size, size), WHITE).save(path)
        self.refresh_list(select=name)
        if not self._confirm_switch():
            return
        self._load(name)

    # -- panel protocol -------------------------------------------------------

    @property
    def dirty(self):
        return bool(self.editor and self.editor.dirty)

    def is_dirty(self):
        return self.dirty

    def title(self):
        return "Sprites"

    def save(self):
        return self.editor.save() if self.editor else True

    def reload(self):
        cur = os.path.basename(self.editor.path) if self.editor else None
        self.refresh_list(select=cur)
        if self.editor:
            self.editor.reload()

    def activate(self):
        self._active = True
        if self.editor:
            self.editor.activate()

    def deactivate(self):
        self._active = False
        if self.editor:
            self.editor.deactivate()


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", help="BMP file to edit (created if missing), or a folder of BMPs")
    ap.add_argument("--size", type=int, default=40, help="canvas size for new files (default 40)")
    ap.add_argument("--zoom", type=int, default=14, help="pixels per cell on screen (default 14)")
    args = ap.parse_args()
    root = tk.Tk()
    if os.path.isdir(args.path):
        panel = SpritePanel(root, args.path, zoom=args.zoom)
        root.title(f"sprite_editor — {os.path.basename(os.path.abspath(args.path))}/")
    else:
        panel = Editor(root, args.path, args.size, args.zoom)
        root.title(f"sprite_editor — {panel.title()}")
    panel.frame.pack(fill="both", expand=True)
    panel.activate()
    root.mainloop()


if __name__ == "__main__":
    main()
