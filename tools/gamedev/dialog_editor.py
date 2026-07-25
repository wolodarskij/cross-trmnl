#!/usr/bin/env python3
"""Dialog-tree editor for the Lua game engine (engine/dialogtree.lua files).

    python dialog_editor.py                    # pick the dialog module in a file dialog
    python dialog_editor.py --file ../../lua-scripts-src/adventure/dialogs.lua

A dialog module returns a table with `speakers`, per-actor `talk` lists, and
named nodes. The editor shows that structure as a tree on the left and the
selected piece in a visual form on the right — no Lua syntax needed:

  * dialog text is edited with real line breaks, next to the speaker's portrait
  * speakers and "go to" targets are dropdowns
  * conditions and effects use plain lists: `mushrooms >= 3, not sword`,
    `gold, elixir = false`
  * choices are label / condition / target rows

Everything the form does not cover (branch, mapset, exotic keys) is preserved
untouched and remains editable on the Source tab, which shows the selected
piece as a Lua snippet. Save (Ctrl+S) rewrites the file, keeping its leading
comment block and the original node order.

The sibling item file (`items.lua`) can be opened at the same time — its
inventory items reference dialog scenes (`action.node`) and share the story
variables, so the editor loads both, edits items on their own tab, and keeps
the cross references in sync when anything is renamed.

Extras: jump buttons for goto/next targets and incoming "used by" references,
node new/rename/delete (rename updates every reference in both files), and
Validate (unknown targets, unknown speakers, the story variables in use).
Requires lupa + pillow (pip install lupa pillow).
"""

import argparse
import copy
import os
import re
import tkinter as tk
from tkinter import filedialog, messagebox, simpledialog, ttk

try:
    from PIL import Image, ImageTk
except ImportError:
    Image = None

try:
    from lupa import lua54
except ImportError:
    raise SystemExit("lupa is required: pip install lupa")

LUA_KEYWORDS = {
    "and", "break", "do", "else", "elseif", "end", "false", "for", "function",
    "goto", "if", "in", "local", "nil", "not", "or", "repeat", "return",
    "then", "true", "until", "while",
}
IDENT = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

# Canonical key order inside a node/entry; unknown keys go after these.
KEY_ORDER = ["speaker", "portrait", "who", "left", "right", "label", "when",
             "set", "add", "mapset", "show", "sleep", "text", "pages",
             "prompt", "ending", "style", "sprite", "title", "sub", "room",
             "branch", "next", "choices", "goto"]

# Key order for an item file (items.lua): item = {name, sprite, owned, actions};
# each action = {label, when, node}.
ITEM_KEY_ORDER = ["name", "sprite", "owned", "actions", "label", "when", "node"]

# Keys the visual form owns; everything else is preserved and editable in Source.
FORM_KEYS = {"speaker", "when", "set", "add", "pages", "prompt", "choices",
             "next", "goto", "ending", "show", "sleep", "branch"}

END_LABEL = "(end dialog)"
INHERIT = "(inherit)"
NEW_SCENE = "＋ New scene…"
NEW_SPEAKER = "＋ New speaker…"
NEW_VAR = "＋ New variable…"
ENDING_STYLES = ["(plain)", "mushroom_crawl"]


# ---- Lua table <-> Python ----------------------------------------------------
# A Lua table becomes {"named": dict, "array": list}; scalars stay scalars.

def lua_to_py(obj):
    if type(obj).__name__ != "_LuaTable":
        if isinstance(obj, float) and obj.is_integer():
            return int(obj)
        return obj
    named, array = {}, []
    keys = list(obj.keys())
    n = 0
    while (n + 1) in keys or float(n + 1) in keys:
        array.append(lua_to_py(obj[n + 1]))
        n += 1
    for k in keys:
        if isinstance(k, (int, float)) and 1 <= k <= n:
            continue
        named[str(k)] = lua_to_py(obj[k])
    return {"named": named, "array": array}


def table(named=None, array=None):
    return {"named": named or {}, "array": array or []}


def is_table(v):
    return isinstance(v, dict) and set(v) == {"named", "array"}


def lua_key(k):
    if IDENT.match(k) and k not in LUA_KEYWORDS:
        return k
    return '["' + k.replace("\\", "\\\\").replace('"', '\\"') + '"]'


def lua_str(s):
    return '"' + (s.replace("\\", "\\\\").replace('"', '\\"')
                   .replace("\n", "\\n").replace("\t", "\\t")) + '"'


def sort_keys(named, key_order=KEY_ORDER):
    known = [k for k in key_order if k in named]
    return known + [k for k in named if k not in key_order]


def serialize(v, indent=0, key_order=KEY_ORDER):
    """Python model → Lua source. Tables emit named keys first, then array part."""
    if v is True:
        return "true"
    if v is False:
        return "false"
    if isinstance(v, (int, float)):
        return repr(v)
    if isinstance(v, str):
        return lua_str(v)
    if not is_table(v):
        raise TypeError(f"cannot serialize {v!r}")
    pad, inner = "  " * indent, "  " * (indent + 1)
    parts = [f"{lua_key(k)} = {serialize(v['named'][k], indent + 1, key_order)}"
             for k in sort_keys(v["named"], key_order)]
    parts += [serialize(item, indent + 1, key_order) for item in v["array"]]
    if not parts:
        return "{}"
    one_line = "{" + ", ".join(parts) + "}"
    if len(one_line) + len(pad) <= 78 and "\n" not in one_line:
        return one_line
    return "{\n" + "".join(f"{inner}{p},\n" for p in parts) + pad + "}"


# ---- friendly condition / effect syntax --------------------------------------
#   {sword = true, vial = false, ["mushrooms>="] = 3, room = 2}
#   <->  "sword, not vial, mushrooms >= 3, room = 2"

def cond_to_text(t):
    if not is_table(t):
        return ""
    parts = []
    for k, v in t["named"].items():
        if k.endswith(">="):
            parts.append(f"{k[:-2]} >= {v}")
        elif v is True:
            parts.append(k)
        elif v is False:
            parts.append(f"not {k}")
        elif isinstance(v, str):
            parts.append(f'{k} = "{v}"')
        else:
            parts.append(f"{k} = {v}")
    return ", ".join(parts)


def _value(tok):
    low = tok.lower()
    if low in ("true", "yes"):
        return True
    if low in ("false", "no"):
        return False
    try:
        return int(tok)
    except ValueError:
        pass
    try:
        return float(tok)
    except ValueError:
        pass
    return tok.strip("\"'")


def text_to_cond(text):
    """→ condition table, or None for an empty string. Raises ValueError."""
    text = text.strip()
    if not text:
        return None
    named = {}
    for part in text.split(","):
        part = part.strip()
        if not part:
            continue
        if part.startswith("not "):
            key = part[4:].strip()
            if not key:
                raise ValueError(f"bad condition {part!r}")
            named[key] = False
        elif ">=" in part:
            key, _, val = part.partition(">=")
            named[key.strip() + ">="] = _value(val.strip())
        elif "=" in part:
            key, _, val = part.partition("=")
            named[key.strip()] = _value(val.strip())
        else:
            named[part] = True
    return table(named=named)


# ---- loading -----------------------------------------------------------------

def fresh_lua():
    lua = lua54.LuaRuntime()
    g = lua.globals()
    for name in ("require", "package", "os", "io"):
        g[name] = None
    g.require = lambda name: lua.table()  # dialog data should not need modules
    return lua


def parse_lua_snippet(text):
    """Parse a Lua table constructor; returns the Python model or raises."""
    return lua_to_py(fresh_lua().execute("return " + text))


def read_header(src):
    """Leading comment block of a Lua source file (blank lines within kept)."""
    header = []
    for line in src.splitlines():
        if line.startswith("--") or (header and not line.strip()):
            header.append(line)
        else:
            break
    while header and not header[-1].strip():
        header.pop()
    return header


def classify(raw):
    """Raw lua_to_py table → which kind of module this is."""
    return "items" if raw["array"] and not raw["named"] else "dialog"


def load_module(path):
    """→ (header lines, kind, raw model dict) for either a dialog or item file."""
    src = open(path, encoding="utf-8").read()
    data = fresh_lua().execute(src)
    if data is None:
        raise SystemExit(f"{path} did not return a table")
    raw = lua_to_py(data)
    return read_header(src), classify(raw), raw


def load_items(path):
    """→ (header lines, list of item tables) for an items.lua module."""
    header, kind, raw = load_module(path)
    if kind != "items":
        raise SystemExit(f"{path}: expected an item list (a returned array)")
    return header, raw["array"]


def load_file(path):
    """→ (header comment lines, node order from source, model dict)."""
    src = open(path, encoding="utf-8").read()
    header = read_header(src)

    data = fresh_lua().execute(src)
    if data is None:
        raise SystemExit(f"{path} did not return a table")
    model = lua_to_py(data)
    if model["array"]:
        raise SystemExit(f"{path}: top level should be named keys only")

    # Original T.<id> assignment order, so saves don't shuffle the file.
    order = re.findall(r"^\s*(?:T|M|D)\.([A-Za-z_][A-Za-z0-9_]*)\s*=", src, re.M)
    seen = set()
    order = [k for k in order if not (k in seen or seen.add(k))]
    for k in model["named"]:
        if k not in order:
            order.append(k)
    return header, order, model["named"]


# ---- reference graph ---------------------------------------------------------

def walk_tables(v, path=""):
    if not is_table(v):
        return
    yield path, v
    for k in v["named"]:
        yield from walk_tables(v["named"][k], f"{path}.{k}" if path else k)
    for i, item in enumerate(v["array"]):
        yield from walk_tables(item, f"{path}[{i + 1}]")


# Scene-reference keys: dialogs use goto/next, item actions use node.
REF_KEYS = ("goto", "next", "node")


def walk_all(model, items=None):
    """(path, table) over every table in the dialog model and, if given, the
    item list. Item paths are prefixed `items[N]` so they map back to `item:N`."""
    for top, v in model.items():
        yield from walk_tables(v, top)
    for i, it in enumerate(items or []):
        yield from walk_tables(it, f"items[{i + 1}]")


def find_refs(model, items=None):
    refs = {}
    for path, t in walk_all(model, items):
        for key in REF_KEYS:
            tgt = t["named"].get(key)
            if isinstance(tgt, str):
                refs.setdefault(tgt, []).append(path)
    return refs


def rename_refs(model, old, new, items=None):
    n = 0
    for _, t in walk_all(model, items):
        for key in REF_KEYS:
            if t["named"].get(key) == old:
                t["named"][key] = new
                n += 1
    return n


def rename_speaker_refs(model, old, new):
    """Every `speaker = old` (nodes, pages, prompts, talk lists) → new."""
    n = 0
    for _, v in model.items():
        for _, t in walk_tables(v):
            if t["named"].get("speaker") == old:
                t["named"]["speaker"] = new
                n += 1
    return n


def _rewrite_var_key(named, old, new):
    """Rename var key `old`→`new` (and `old>=`→`new>=`) in one clause dict,
    preserving position. Returns the number of keys changed."""
    changed = 0
    for suffix in ("", ">="):
        ok, nk = old + suffix, new + suffix
        if ok in named:
            named[nk] = named.pop(ok)
            changed += 1
    return changed


# Condition/effect keys that hold story-variable clauses. Item `owned` is the
# same shape as `when`, so it counts as a use of every variable it names.
COND_KEYS = ("when", "set", "add", "owned")


def rename_var(model, old, new, items=None):
    n = 0
    for _, t in walk_all(model, items):
        for key in COND_KEYS:
            cond = t["named"].get(key)
            if is_table(cond):
                n += _rewrite_var_key(cond["named"], old, new)
    return n


def remove_var(model, name, items=None):
    """Delete every clause using `name` (both `name` and `name>=`)."""
    n = 0
    for _, t in walk_all(model, items):
        for key in COND_KEYS:
            cond = t["named"].get(key)
            if is_table(cond):
                for k in (name, name + ">="):
                    if k in cond["named"]:
                        del cond["named"][k]
                        n += 1
    return n


def collect_vars(model, items=None):
    out = set()
    for _, t in walk_all(model, items):
        for key in COND_KEYS:
            cond = t["named"].get(key)
            if is_table(cond):
                for k in cond["named"]:
                    if not k.startswith(("near_", "room")):
                        out.add(k.rstrip(">=").rstrip("<="))
    return sorted(out)


def var_uses(model, name, items=None):
    """→ [(path, kind)] for every when/set/add/owned clause using `name`."""
    uses = []
    for path, t in walk_all(model, items):
        for kind in COND_KEYS:
            cond = t["named"].get(kind)
            if is_table(cond) and (name in cond["named"] or name + ">=" in cond["named"]):
                uses.append((path, kind))
    return uses


def page_text(page):
    """A page body is `text = "..."` or a list of line strings."""
    if not is_table(page):
        return str(page)
    if "text" in page["named"]:
        return str(page["named"]["text"])
    return "\n".join(str(x) for x in page["array"])


def when_summary(t):
    text = cond_to_text(t)
    return text if text else "always"


# ---- editor ------------------------------------------------------------------

class DialogEditor:
    def __init__(self, parent, path, on_var_change=None, on_saved=None, embedded=False):
        self.frame = tk.Frame(parent)
        self.root = self.frame            # widget parent for children / after / winfo
        self.top = self.frame.winfo_toplevel()
        self.on_var_change = on_var_change  # shell hook: propagate var edits to wiring
        self.on_saved = on_saved            # shell hook: flush the wiring buffer with us
        self.embedded = embedded
        self._accel = []                  # (seq, funcid) bound on the toplevel
        self.thumbs = {}       # portrait name -> PhotoImage
        self.collectors = []   # form field -> model writers
        self.undo_stack = []   # snapshots of (model, order) BEFORE each change
        self.redo_stack = []
        self._lint_job = None  # pending after() id for live source validation
        self.known_vars = set()   # story vars offered in condition dropdowns
        self._scene_combos = []   # goto/next ref-combos on the current form
        self._speaker_combos = [] # speaker ref-combos on the current form
        self._last_sel = {}       # left-tab index -> last-selected iid
        # The companion item file (items.lua): a parallel document.
        self.items = None         # list of item tables, or None if not loaded
        self.items_path = None
        self.items_header = []
        self.items_dirty = False
        self._dialog_saved = None  # last text written for each slot (churn guard)
        self._items_saved = None

        bar = tk.Frame(self.frame)
        bar.pack(fill="x")
        buttons = [("Reload", self.reload), ("Save (Ctrl+S)", self.save),
                   ("Undo (Ctrl+Z)", self.undo), ("Redo (Ctrl+Y)", self.redo),
                   ("Check story", self.validate)]
        if not embedded:  # the shell owns opening a game; standalone keeps its own openers
            buttons = [("Open...", self.open_dialog), ("Open items…", self.open_items)] + buttons
        for text, cmd in buttons:
            tk.Button(bar, text=text, command=cmd).pack(side="left", padx=2, pady=2)
        self.status = tk.Label(self.frame, text="", anchor="w")
        self.status.pack(side="bottom", fill="x", padx=4)

        main = tk.PanedWindow(self.frame, sashrelief="raised")
        main.pack(fill="both", expand=True)

        # Left: Speakers / Conversations / Scenes tabs + the variables pane.
        left = tk.Frame(main)
        main.add(left, width=270)
        self.left_tabs = ttk.Notebook(left)
        self.left_tabs.pack(fill="both", expand=True)

        sp_tab = tk.Frame(self.left_tabs)
        self.left_tabs.add(sp_tab, text="Speakers")
        tk.Label(sp_tab, fg="#666", anchor="nw", justify="left", wraplength=220,
                 text="Who can speak and which portrait each one uses.\n\n"
                      "Edit the table on the right.").pack(fill="x", padx=8, pady=8)

        conv_tab = tk.Frame(self.left_tabs)
        self.left_tabs.add(conv_tab, text="Conversations")
        self.conv_tree = ttk.Treeview(conv_tab, show="tree", selectmode="browse")
        self.conv_tree.pack(fill="both", expand=True)
        self.conv_tree.bind("<<TreeviewSelect>>", lambda e: self.on_select())
        cbar = tk.Frame(conv_tab)
        cbar.pack(fill="x")
        for text, cmd in (("+ Conv", self.add_conversation), ("+ Entry", self.add_entry),
                          ("Rename", self.rename_conversation),
                          ("↑", lambda: self.move_entry(-1)), ("↓", lambda: self.move_entry(1)),
                          ("Delete", self.delete_selected)):
            tk.Button(cbar, text=text, command=cmd).pack(side="left", padx=1, pady=2)

        scene_tab = tk.Frame(self.left_tabs)
        self.left_tabs.add(scene_tab, text="Scenes")
        self.scene_tree = ttk.Treeview(scene_tab, show="tree", selectmode="browse")
        self.scene_tree.pack(fill="both", expand=True)
        self.scene_tree.bind("<<TreeviewSelect>>", lambda e: self.on_select())
        sbar = tk.Frame(scene_tab)
        sbar.pack(fill="x")
        for text, cmd in (("+ Scene", self.new_node), ("Rename", self.rename_node),
                          ("Delete", self.delete_selected)):
            tk.Button(sbar, text=text, command=cmd).pack(side="left", padx=1, pady=2)

        item_tab = tk.Frame(self.left_tabs)
        self.left_tabs.add(item_tab, text="Items")
        self.item_placeholder = tk.Frame(item_tab)
        tk.Label(self.item_placeholder, fg="#666", anchor="nw", justify="left", wraplength=220,
                 text="Inventory items (a sibling items.lua). Their actions point at "
                      "scenes and share the story variables.\n\nOpen one to edit it here.").pack(
            fill="x", padx=8, pady=8)
        tk.Button(self.item_placeholder, text="Open items file", command=self.open_items).pack(
            anchor="w", padx=8)
        self.item_body = tk.Frame(item_tab)
        self.item_tree = ttk.Treeview(self.item_body, show="tree", selectmode="browse")
        self.item_tree.pack(fill="both", expand=True)
        self.item_tree.bind("<<TreeviewSelect>>", lambda e: self.on_select())
        ibar = tk.Frame(self.item_body)
        ibar.pack(fill="x")
        for text, cmd in (("+ Item", self.add_item),
                          ("↑", lambda: self.move_item(-1)), ("↓", lambda: self.move_item(1)),
                          ("Delete", self.delete_selected)):
            tk.Button(ibar, text=text, command=cmd).pack(side="left", padx=1, pady=2)

        self.left_tabs.bind("<<NotebookTabChanged>>", lambda e: self.on_left_tab())

        vars_box = tk.LabelFrame(left, text="Story variables")
        vars_box.pack(fill="x", pady=(4, 0))
        vbar = tk.Frame(vars_box)
        vbar.pack(fill="x")
        for text, cmd in (("+ Var", self.add_var), ("Rename", self.rename_var_ui),
                          ("Remove", self.remove_var_ui)):
            tk.Button(vbar, text=text, command=cmd).pack(side="left", padx=1, pady=1)
        self.vars_list = tk.Listbox(vars_box, height=7, activestyle="none",
                                    font=("Consolas", 9), exportselection=False)
        self.vars_list.pack(fill="both", expand=True)
        self.vars_list.bind("<<ListboxSelect>>", lambda e: self.show_var_uses())
        tk.Label(vars_box, text="used in (double-click to open):",
                 anchor="w", fg="#666").pack(fill="x")
        self.uses_list = tk.Listbox(vars_box, height=5, activestyle="none",
                                    font=("Consolas", 9), exportselection=False)
        self.uses_list.pack(fill="both", expand=True)
        self.uses_list.bind("<Double-Button-1>", lambda e: self.open_var_use())
        self._var_names = []   # index -> variable name, parallel to vars_list
        self._use_iids = []    # index -> iid, parallel to uses_list

        right = tk.Frame(main)
        main.add(right)
        self.title_label = tk.Label(right, text="", anchor="w", font=("Segoe UI", 12, "bold"))
        self.title_label.pack(fill="x", padx=6, pady=(4, 0))
        self.refs_label = tk.Label(right, text="", anchor="w", fg="#666", justify="left")
        self.refs_label.pack(fill="x", padx=6)
        self.links = tk.Frame(right)
        self.links.pack(fill="x", padx=6)

        self.tabs = ttk.Notebook(right)
        self.tabs.pack(fill="both", expand=True, padx=4, pady=4)

        # Form tab: a scrollable frame the form is built into.
        form_holder = tk.Frame(self.tabs)
        self.tabs.add(form_holder, text="Story")
        self.form_canvas = tk.Canvas(form_holder, highlightthickness=0)
        vsb = ttk.Scrollbar(form_holder, orient="vertical", command=self.form_canvas.yview)
        self.form_canvas.configure(yscrollcommand=vsb.set)
        vsb.pack(side="right", fill="y")
        self.form_canvas.pack(side="left", fill="both", expand=True)
        self.form = tk.Frame(self.form_canvas)
        self._form_win = self.form_canvas.create_window((0, 0), window=self.form, anchor="nw")
        self.form.bind("<Configure>",
                       lambda e: self.form_canvas.configure(scrollregion=self.form_canvas.bbox("all")))
        self.form_canvas.bind("<Configure>",
                              lambda e: self.form_canvas.itemconfigure(self._form_win, width=e.width))
        # Mousewheel + accelerators are bound on the toplevel in activate() and
        # removed in deactivate(), so an embedded panel never steals events from
        # a sibling tab. Standalone, main() calls activate() once.

        source_holder = tk.Frame(self.tabs)
        self.tabs.add(source_holder, text="Source (Lua)")
        self.text = tk.Text(source_holder, wrap="none", font=("Consolas", 11), undo=True)
        src_vsb = ttk.Scrollbar(source_holder, orient="vertical", command=self.text.yview)
        src_hsb = ttk.Scrollbar(source_holder, orient="horizontal", command=self.text.xview)
        self.text.configure(yscrollcommand=src_vsb.set, xscrollcommand=src_hsb.set)
        self.text.grid(row=0, column=0, sticky="nsew")
        src_vsb.grid(row=0, column=1, sticky="ns")
        src_hsb.grid(row=1, column=0, sticky="ew")
        tk.Label(source_holder, text="Ctrl+Enter applies the snippet",
                 anchor="w", fg="#666").grid(row=2, column=0, columnspan=2, sticky="ew")
        source_holder.rowconfigure(0, weight=1)
        source_holder.columnconfigure(0, weight=1)
        self.text.bind("<KeyRelease>", self.schedule_lint)
        self.tabs.bind("<<NotebookTabChanged>>", self.on_tab_change)
        self.open_file(path)

    # -- panel protocol -------------------------------------------------------

    def title(self):
        names = [os.path.basename(p) for p in (self.path, self.items_path) if p]
        return "Story & Items — " + (" + ".join(names) if names else "(empty)")

    def is_dirty(self):
        return bool(self.dirty or self.items_dirty)

    def activate(self):
        b = self.top.bind
        self._accel = [
            ("<MouseWheel>", self.top.bind("<MouseWheel>", self._on_wheel, "+")),
            ("<Control-s>", b("<Control-s>", lambda e: self.save(), "+")),
            ("<Control-Return>", b("<Control-Return>", lambda e: (self.apply(), "break")[1], "+")),
            ("<Control-z>", b("<Control-z>", self.on_ctrl_z, "+")),
            ("<Control-y>", b("<Control-y>", self.on_ctrl_y, "+")),
        ]

    def deactivate(self):
        for seq, fid in self._accel:
            self.top.unbind(seq, fid)
        self._accel = []

    def _on_wheel(self, event):
        widget = self.root.winfo_containing(event.x_root, event.y_root)
        w = widget
        while w is not None:
            if w is self.form_canvas:
                self.form_canvas.yview_scroll(-1 if event.delta > 0 else 1, "units")
                return
            w = w.master

    # -- file in/out ----------------------------------------------------------

    def asset_dir(self):
        """Folder that holds the portraits/sprites and the sibling file."""
        p = self.path or self.items_path
        return os.path.dirname(p) if p else "."

    def sibling(self, name):
        """Path to a same-folder companion (dialogs.lua / items.lua), or None."""
        p = os.path.join(self.asset_dir(), name)
        return p if os.path.exists(p) else None

    def open_file(self, path):
        """Primary open: load `path`, reset the session, and auto-load its
        sibling (items.lua for a dialog, dialogs.lua for an item file)."""
        path = os.path.abspath(path)
        try:
            header, kind, raw = load_module(path)
        except Exception as e:  # bad Lua after a manual edit — keep the session
            messagebox.showerror("Load failed", f"{path}\n\n{e}")
            return
        # Fresh session: clear both slots first.
        self.path = self.items_path = None
        self.header, self.order, self.model = [], [], {}
        self.items, self.items_header = None, []
        self.dirty = self.items_dirty = False
        self._dialog_saved = self._items_saved = None
        self.current = None
        self.thumbs = {}
        self.undo_stack, self.redo_stack = [], []
        if kind == "items":
            self.set_items(path, header, raw["array"])
            partner = self.sibling("dialogs.lua")
            if partner and os.path.abspath(partner) != path:
                self.load_dialog(partner)
        else:
            self.set_dialog(path, header, load_file(path)[1], raw["named"])
            partner = self.sibling("items.lua")
            if partner and os.path.abspath(partner) != path:
                self.load_items_slot(partner)
        self.known_vars = set(collect_vars(self.model, self.items))
        self.retitle()
        self.rebuild_lists()
        self.refresh_vars()
        self.set_status(self.loaded_summary())

    def loaded_summary(self):
        bits = []
        if self.path:
            bits.append(f"{len(self.node_ids())} scenes, "
                        f"{len(self.model.get('talk', table())['named'])} conversations")
        if self.items is not None:
            bits.append(f"{len(self.items)} items ({os.path.basename(self.items_path)})")
        return "loaded " + "; ".join(bits) if bits else "loaded (empty)"

    def retitle(self):
        if self.embedded:            # the shell owns the window title
            return
        names = [os.path.basename(p) for p in (self.path, self.items_path) if p]
        self.top.title("dialog_editor — " + " + ".join(names) if names else "dialog_editor")

    def set_dialog(self, path, header, order, model):
        self.path, self.header, self.order, self.model = path, header, order, model
        self._dialog_saved = self.render_dialog()

    def set_items(self, path, header, items):
        self.items_path, self.items_header, self.items = path, header, items
        self._items_saved = self.render_items()

    def load_dialog(self, path):
        header, order, model = load_file(path)
        self.set_dialog(os.path.abspath(path), header, order, model)

    def load_items_slot(self, path):
        header, items = load_items(path)
        self.set_items(os.path.abspath(path), header, items)

    def open_dialog(self):
        p = filedialog.askopenfilename(title="Open dialog or item module",
                                       initialdir=self.asset_dir(),
                                       filetypes=[("Lua modules", "*.lua"), ("All files", "*")])
        if p:
            self.open_file(p)

    def open_items(self):
        """Load just the items slot, leaving any open dialog in place."""
        p = filedialog.askopenfilename(title="Open item module",
                                       initialdir=self.asset_dir(),
                                       filetypes=[("Lua item modules", "*.lua"), ("All files", "*")])
        if not p:
            return
        try:
            self.load_items_slot(p)
        except Exception as e:
            messagebox.showerror("Load failed", f"{p}\n\n{e}")
            return
        self.items_dirty = False
        self.known_vars |= set(collect_vars({}, self.items))
        self.current = None
        self.retitle()
        self.rebuild_lists()
        self.refresh_vars()
        self.set_status(self.loaded_summary())

    def reload(self):
        if (self.dirty or self.items_dirty) and not messagebox.askyesno(
                "Reload", "Discard unsaved changes and re-read from disk?"):
            return
        primary = self.path or self.items_path
        if primary:
            self.open_file(primary)

    def render_dialog(self):
        out = list(self.header)
        if not out:
            out = [f"-- {os.path.basename(self.path)} — dialog tree (engine/dialogtree.lua format)."]
        out += ["", "local T = {}", ""]
        for key in self.order:
            if key not in self.model:
                continue
            out.append(f"T.{key} = " + serialize(self.model[key]) + "\n")
        out.append("return T")
        return "\n".join(out) + "\n"

    def render_items(self):
        out = list(self.items_header)
        if not out:
            out = [f"-- {os.path.basename(self.items_path)} — inventory items (data)."]
        out += ["", "return " + serialize(table(array=self.items), key_order=ITEM_KEY_ORDER)]
        return "\n".join(out) + "\n"

    def save(self):
        if not self.apply():
            return False
        written = []
        if self.path is not None:
            text = self.render_dialog()
            if text != self._dialog_saved:
                with open(self.path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(text)
                self._dialog_saved = text
                written.append(os.path.basename(self.path))
        if self.items_path is not None:
            text = self.render_items()
            if text != self._items_saved:
                with open(self.items_path, "w", encoding="utf-8", newline="\n") as f:
                    f.write(text)
                self._items_saved = text
                written.append(os.path.basename(self.items_path))
        self.dirty = self.items_dirty = False
        self.set_status("saved " + ", ".join(written) if written else "nothing changed to save")
        if self.on_saved:                # let the shell persist the wiring buffer with us
            self.on_saved()
        return True

    # -- portraits ------------------------------------------------------------

    def speaker_ids(self):
        return list(self.model.get("speakers", table())["named"])

    def speaker_portrait(self, speaker_id):
        sp = self.model.get("speakers", table())["named"].get(speaker_id)
        if is_table(sp):
            return sp["named"].get("portrait")
        return None

    def portrait_thumb(self, name, size=48):
        if not name or Image is None:
            return None
        key = (name, size)
        if key in self.thumbs:
            return self.thumbs[key]
        p = os.path.join(self.asset_dir(), name + ".bmp")
        if not os.path.exists(p):
            return None
        img = Image.open(p).convert("L").resize((size, size), Image.NEAREST)
        self.thumbs[key] = ImageTk.PhotoImage(img)
        return self.thumbs[key]

    # -- tree -----------------------------------------------------------------

    def node_ids(self):
        return [k for k in self.model if k not in ("speakers", "talk")]

    def item_name(self, idx):
        """Display name of item #idx (1-based); falls back to its position."""
        if self.items and 1 <= idx <= len(self.items):
            it = self.items[idx - 1]
            if is_table(it):
                return str(it["named"].get("name") or f"item {idx}")
        return f"item {idx}"

    def talk_entry_label(self, entry, idx):
        when = entry["named"].get("when") if is_table(entry) else None
        return f"{idx}. " + ("if " + when_summary(when) if when else "otherwise")

    def rebuild_lists(self, select=None):
        # Items tab: show the tree once a file is loaded, else the placeholder.
        self.item_tree.delete(*self.item_tree.get_children())
        if self.items is None:
            self.item_body.pack_forget()
            self.item_placeholder.pack(fill="both", expand=True)
        else:
            self.item_placeholder.pack_forget()
            self.item_body.pack(fill="both", expand=True)
            for i in range(len(self.items)):
                self.item_tree.insert("", "end", iid=f"item:{i + 1}",
                                      text=f"{i + 1}. {self.item_name(i + 1)}")
        self.conv_tree.delete(*self.conv_tree.get_children())
        if "talk" in self.model:
            for actor, lst in sorted(self.model["talk"]["named"].items()):
                a = self.conv_tree.insert("", "end", iid=f"talk:{actor}", text=actor, open=True)
                if is_table(lst):
                    for i, entry in enumerate(lst["array"]):
                        self.conv_tree.insert(a, "end", iid=f"talk:{actor}:{i + 1}",
                                              text=self.talk_entry_label(entry, i + 1))
        self.scene_tree.delete(*self.scene_tree.get_children())
        for key in self.order:
            if key in self.model and key not in ("speakers", "talk"):
                self.scene_tree.insert("", "end", iid=f"node:{key}", text=key)
        if select:
            tree, tab = self.tree_of(select)
            if tree is not None and tree.exists(select):
                self._nav_guard = True
                try:
                    self.left_tabs.select(tab)
                    tree.selection_set(select)
                    tree.see(select)
                finally:
                    self._nav_guard = False

    def tree_of(self, iid):
        """(tree widget, left-tab index) that owns an iid."""
        if iid.startswith("talk:"):
            return self.conv_tree, 1
        if iid.startswith("node:"):
            return self.scene_tree, 2
        if iid.startswith("item:"):
            return self.item_tree, 3
        return None, 0

    def active_tree(self):
        tab = self.left_tabs.index(self.left_tabs.select())
        return {1: self.conv_tree, 2: self.scene_tree, 3: self.item_tree}.get(tab)

    def selection(self):
        tree = self.active_tree()
        if tree is None:
            return "speakers"
        sel = tree.selection()
        return sel[0] if sel else None

    def on_left_tab(self):
        if getattr(self, "_nav_guard", False):
            return  # navigate()/rebuild_lists drive the selection explicitly
        tab = self.left_tabs.index(self.left_tabs.select())
        if tab == 0:
            self.on_select()  # the Speakers tab IS the selection
            return
        tree = self.active_tree()
        want = self._last_sel.get(tab)
        if not (want and tree.exists(want)):
            kids = tree.get_children()
            want = kids[0] if kids else None
        if want is None:
            return
        if tree.selection() != (want,):
            tree.selection_set(want)
            tree.see(want)
        self.on_select()   # render explicitly (don't depend on the virtual event)

    PATH_TALK = re.compile(r"^talk\.([A-Za-z_][A-Za-z0-9_]*)(?:\[(\d+)\])?")
    PATH_ITEM = re.compile(r"^items\[(\d+)\]")

    def path_to_iid(self, path):
        """A walk_tables path → a selectable iid (node:/talk:/item:/speakers)."""
        if path.startswith("speakers"):
            return "speakers"
        m = self.PATH_ITEM.match(path)
        if m:
            return f"item:{m.group(1)}"
        m = self.PATH_TALK.match(path)
        if m:
            return f"talk:{m.group(1)}:{m.group(2)}" if m.group(2) else f"talk:{m.group(1)}"
        top = re.split(r"[.\[]", path, 1)[0]
        return f"node:{top}"

    def navigate(self, iid):
        """Select and render any piece by iid, switching tabs as needed."""
        if not iid:
            return
        self._nav_guard = True
        try:
            if iid == "speakers":
                self.left_tabs.select(0)
                self.on_select()
                return
            tree, tab = self.tree_of(iid)
            if tree is None or not tree.exists(iid):
                return self.set_status(f"'{self.use_label(iid)}' is gone")
            self.left_tabs.select(tab)
            if tree.selection() != (iid,):
                tree.selection_set(iid)
                tree.see(iid)
            self.on_select()
        finally:
            self._nav_guard = False

    def get_piece(self, iid):
        if iid == "speakers":
            return self.model.get("speakers")
        if iid and iid.startswith("talk:"):
            rest = iid[5:].split(":")
            lst = self.model["talk"]["named"].get(rest[0])
            if lst is None:
                return None
            if len(rest) == 1:
                return lst
            try:
                return lst["array"][int(rest[1]) - 1]
            except (IndexError, ValueError):
                return None
        if iid and iid.startswith("node:"):
            return self.model.get(iid[5:])
        if iid and iid.startswith("item:"):
            idx = int(iid[5:])
            if self.items and 1 <= idx <= len(self.items):
                return self.items[idx - 1]
            return None
        return None

    def set_piece(self, iid, value):
        if iid == "speakers":
            self.model["speakers"] = value
        elif iid.startswith("talk:"):
            rest = iid[5:].split(":")
            if len(rest) == 1:
                self.model["talk"]["named"][rest[0]] = value
            else:
                self.model["talk"]["named"][rest[0]]["array"][int(rest[1]) - 1] = value
        elif iid.startswith("node:"):
            self.model[iid[5:]] = value
        elif iid.startswith("item:"):
            self.items[int(iid[5:]) - 1] = value

    def header_text(self, iid):
        if iid == "speakers":
            return "Speakers"
        if iid.startswith("talk:"):
            parts = iid[5:].split(":")
            if len(parts) == 1:
                return f"Conversation: {parts[0]}"
            total = len(self.model["talk"]["named"][parts[0]]["array"])
            return f"Conversation: {parts[0]} — entry {parts[1]} of {total}"
        if iid.startswith("node:"):
            return f"Scene: {iid[5:]}"
        if iid.startswith("item:"):
            idx = int(iid[5:])
            return f"Item: {self.item_name(idx)} ({idx} of {len(self.items)})"
        return ""

    def on_select(self):
        if self.current is not None and not self.apply():
            return  # keep editing until the current piece is valid
        iid = self.selection()
        piece = self.get_piece(iid)
        if piece is None:
            return
        self.current = iid
        self._force = set()   # collapsed empty sections re-collapse on a new selection
        if iid.startswith("talk:"):
            self._last_sel[1] = iid
        elif iid.startswith("node:"):
            self._last_sel[2] = iid
        self.title_label.config(text=self.header_text(iid))
        self.show_source(piece)
        self.build_form(iid, piece)
        self.show_links(iid, piece)

    def show_links(self, iid, piece):
        self.refs_label.config(text="")
        for w in self.links.winfo_children():
            w.destroy()
        name = iid.split(":", 1)[-1]

        # incoming — who references this piece (buttons, like "goes to")
        incoming, seen_in = [], set()
        for path in find_refs(self.model, self.items).get(name, []):
            r = self.path_to_iid(path)
            if r not in seen_in:
                seen_in.add(r)
                incoming.append(r)
        row1 = tk.Frame(self.links)
        row1.pack(fill="x")
        tk.Label(row1, text="used by:").pack(side="left")
        if incoming:
            for r in incoming[:12]:
                tk.Button(row1, text=self.use_label(r), padx=2,
                          command=lambda i=r: self.navigate(i)).pack(side="left", padx=2)
        else:
            tk.Label(row1, text="—", fg="#888").pack(side="left")

        # outgoing — scenes this piece jumps to
        targets, seen = [], set()
        for _, t in walk_tables(piece):
            for key in REF_KEYS:
                tgt = t["named"].get(key)
                if isinstance(tgt, str) and tgt not in seen:
                    seen.add(tgt)
                    targets.append(tgt)
        if targets:
            row2 = tk.Frame(self.links)
            row2.pack(fill="x")
            tk.Label(row2, text="goes to:").pack(side="left")
            for tgt in targets:
                tk.Button(row2, text=tgt, padx=2,
                          command=lambda t=tgt: self.jump(t)).pack(side="left", padx=2)

    def jump(self, node_id):
        if self.scene_tree.exists(f"node:{node_id}"):
            self.navigate(f"node:{node_id}")
        else:
            self.set_status(f"scene '{node_id}' does not exist (Check story lists all)")

    # -- source tab -----------------------------------------------------------

    def show_source(self, piece):
        self.text.delete("1.0", "end")
        self.text.insert("1.0", serialize(piece))
        self.text.edit_modified(False)

    def on_tab_change(self, _event):
        # Entering the Source tab: re-serialize what the form holds.
        if self.current is None:
            return
        if self.tabs.index(self.tabs.select()) == 1:
            if self.collect_form():
                self.show_source(self.get_piece(self.current))
        else:
            # Entering the form tab: apply pending source edits first.
            if self.text.edit_modified():
                if self.apply_source():
                    self.build_form(self.current, self.get_piece(self.current))

    def apply_source(self):
        try:
            piece = parse_lua_snippet(self.text.get("1.0", "end"))
        except Exception as e:
            self.set_status(f"Lua error: {e}")
            return False
        if piece != self.get_piece(self.current):
            self.remember()
            self.set_piece(self.current, piece)
            self.dirty = True
            self.refresh_vars()
        self.text.edit_modified(False)
        return True

    def apply(self):
        """Fold whatever the user last edited (form or source) into the model."""
        if self.current is None:
            return True
        if self.tabs.index(self.tabs.select()) == 1 and self.text.edit_modified():
            ok = self.apply_source()
        else:
            ok = self.collect_form()
        if ok:
            self.show_links(self.current, self.get_piece(self.current))
        return ok

    # -- the visual form -------------------------------------------------------

    def clear_form(self):
        self.collectors = []
        self._scene_combos = []
        self._speaker_combos = []
        for w in self.form.winfo_children():
            w.destroy()

    def section(self, title):
        f = tk.LabelFrame(self.form, text=title, padx=6, pady=4)
        f.pack(fill="x", padx=6, pady=4)
        return f

    # -- typo-proof reference dropdowns ---------------------------------------

    def scene_values(self):
        return [END_LABEL] + sorted(self.node_ids()) + [NEW_SCENE]

    def speaker_values(self, allow_inherit=True):
        return ([INHERIT] if allow_inherit else []) + self.speaker_ids() + [NEW_SPEAKER]

    def bmp_names(self):
        d = self.asset_dir()
        try:
            return sorted(f[:-4] for f in os.listdir(d) if f.lower().endswith(".bmp"))
        except OSError:
            return []

    def scene_combo(self, parent, current, width=22):
        """A readonly goto/next picker with a jump ("→") button. '＋ New scene…'
        creates and links one. Returns (var, frame); the caller packs the frame."""
        frame = tk.Frame(parent)
        var = tk.StringVar(value=current or END_LABEL)
        combo = ttk.Combobox(frame, textvariable=var, values=self.scene_values(),
                             width=width, state="readonly")
        combo.pack(side="left")
        prev = {"v": var.get()}

        def is_real():
            return var.get() not in (END_LABEL, NEW_SCENE, "")

        def on_pick(_e=None):
            if var.get() == NEW_SCENE:
                name = self.create_scene_inline()
                var.set(name or prev["v"])
            prev["v"] = var.get()
            openbtn.config(state="normal" if is_real() else "disabled")

        def open_target():
            if is_real() and self.collect_form():
                self.jump(var.get())
        combo.bind("<<ComboboxSelected>>", on_pick)
        openbtn = tk.Button(frame, text="→", padx=3, command=open_target,
                            state="normal" if is_real() else "disabled")
        openbtn.pack(side="left", padx=1)
        self._scene_combos.append((combo, var))
        return var, frame

    def speaker_combo(self, parent, current, allow_inherit=True, on_change=None):
        """Create (but do not pack) a readonly speaker picker. Returns (var, combo)."""
        cur = current if current else (INHERIT if allow_inherit else "")
        var = tk.StringVar(value=cur)
        combo = ttk.Combobox(parent, textvariable=var,
                             values=self.speaker_values(allow_inherit),
                             width=16, state="readonly")
        prev = {"v": var.get()}

        def on_pick(_e=None):
            if var.get() == NEW_SPEAKER:
                sid = self.create_speaker_inline()
                var.set(sid or prev["v"])
            prev["v"] = var.get()
            if on_change:
                on_change()
        combo.bind("<<ComboboxSelected>>", on_pick)
        self._speaker_combos.append((combo, var, allow_inherit))
        return var, combo

    def refresh_scene_combos(self):
        for combo, var in self._scene_combos:
            keep = var.get()
            combo.configure(values=self.scene_values())
            var.set(keep)

    def refresh_speaker_combos(self):
        for combo, var, allow in self._speaker_combos:
            keep = var.get()
            combo.configure(values=self.speaker_values(allow))
            var.set(keep)

    def create_scene_inline(self):
        name = simpledialog.askstring("New scene", "Scene id (letters/digits/underscore):")
        if not name:
            return None
        if not IDENT.match(name) or name in self.model:
            self.set_status("invalid or duplicate scene id")
            return None
        if not self.collect_form():
            return None
        self.remember()
        self.model[name] = table(named={"pages": table(array=[table(named={"text": ""})])})
        self.order.append(name)
        self.dirty = True
        self.rebuild_lists()          # Scenes tab shows it; keep the current form
        self.refresh_scene_combos()
        self.set_status(f"created scene '{name}' — edit it from the Scenes tab")
        return name

    def create_speaker_inline(self):
        sid = simpledialog.askstring("New speaker", "Speaker id (letters/digits/underscore):")
        if not sid:
            return None
        if not IDENT.match(sid) or sid in self.speaker_ids():
            self.set_status("invalid or duplicate speaker id")
            return None
        disp = simpledialog.askstring("New speaker", f"Display name for '{sid}':",
                                      initialvalue=sid) or sid
        if not self.collect_form():
            return None
        self.remember()
        self.model.setdefault("speakers", table())["named"][sid] = table(named={"name": disp})
        self.dirty = True
        self.refresh_speaker_combos()
        self.set_status(f"created speaker '{sid}'")
        return sid

    def speaker_row(self, parent, label, current, allow_inherit=True):
        row = tk.Frame(parent)
        row.pack(fill="x", pady=2)
        tk.Label(row, text=label, width=10, anchor="w").pack(side="left")
        pic = tk.Label(row)

        def update_pic(*_a):
            name = self.resolve_speaker(var.get())
            thumb = self.portrait_thumb(self.speaker_portrait(name), 32)
            pic.configure(image=thumb or "")
            pic.image = thumb
            inh = self._inherit_speaker
            hint.config(text=f"(uses {inh})" if var.get() == INHERIT and inh else "")

        var, combo = self.speaker_combo(row, current, allow_inherit, on_change=update_pic)
        combo.pack(side="left")
        pic.pack(side="left", padx=6)
        hint = tk.Label(row, fg="#888")
        hint.pack(side="left")
        var.trace_add("write", update_pic)
        update_pic()
        return var

    def resolve_speaker(self, value):
        """Map an (inherit) selection to the effective speaker for the thumbnail."""
        if value and value not in (INHERIT, NEW_SPEAKER):
            return value
        return getattr(self, "_inherit_speaker", None)

    # -- condition builder (typo-proof when / set / add) ----------------------

    WHEN_OPS = ["is set", "is not set", "≥", "= number", "= text"]
    SET_OPS = ["← true", "← false", "← number", "← text"]

    def _clause_of(self, mode, key, value):
        """(key, value) from the model → (var, op, value_text) for the widgets."""
        if mode == "add":
            return key, "+=", str(value)
        if mode == "set":
            if value is True:
                return key, "← true", ""
            if value is False:
                return key, "← false", ""
            if isinstance(value, str):
                return key, "← text", value
            return key, "← number", str(value)
        # when
        if key.endswith(">="):
            return key[:-2], "≥", str(value)
        if value is True:
            return key, "is set", ""
        if value is False:
            return key, "is not set", ""
        if isinstance(value, str):
            return key, "= text", value
        return key, "= number", str(value)

    def _dict_of(self, mode, var, op, value_text):
        """(var, op, value_text) from the widgets → {key: value} for the model."""
        if op == "≥":
            return {var + ">=": int(value_text or 0)}
        if op in ("is set", "← true"):
            return {var: True}
        if op in ("is not set", "← false"):
            return {var: False}
        if op in ("= text", "← text"):
            return {var: value_text}
        # numeric: = number, ← number, +=
        return {var: int(value_text or 0)}

    def var_values(self, current_keys):
        opts = set(self.known_vars)
        opts.add("room")
        for k in current_keys:
            base = k[:-2] if k.endswith(">=") else k
            opts.add(base)
        return sorted(opts) + [NEW_VAR]

    def cond_builder(self, parent, mode, current):
        """Rows of [variable ▾][operator ▾][value]. Returns read() -> dict|None."""
        box = tk.Frame(parent)
        box.pack(fill="x")
        ops = {"when": self.WHEN_OPS, "set": self.SET_OPS, "add": ["+="]}[mode]
        keys_now = list(current["named"].keys()) if is_table(current) else []
        rows = []

        def add_row(var="", op=None, val=""):
            row = tk.Frame(box)
            row.pack(fill="x", pady=1)
            vvar = tk.StringVar(value=var)
            vcombo = ttk.Combobox(row, textvariable=vvar, width=16, state="readonly",
                                  values=self.var_values(keys_now))
            vcombo.pack(side="left")
            ovar = tk.StringVar(value=op or ops[0])
            ttk.Combobox(row, textvariable=ovar, width=10, state="readonly",
                         values=ops).pack(side="left", padx=2)
            e = tk.Entry(row, width=12)
            e.insert(0, val)
            e.pack(side="left")
            entry = {"var": vvar, "op": ovar, "val": e, "row": row}

            def on_var(_e=None):
                if vvar.get() == NEW_VAR:
                    name = simpledialog.askstring("New variable",
                                                  "Variable name (letters/digits/underscore):")
                    if name and IDENT.match(name):
                        self.known_vars.add(name)
                        vvar.set(name)
                        vcombo.configure(values=self.var_values(keys_now))
                    else:
                        vvar.set(var)
            vcombo.bind("<<ComboboxSelected>>", on_var)
            tk.Button(row, text="✕",
                      command=lambda: (rows.remove(entry), row.destroy())).pack(side="left", padx=2)
            rows.append(entry)

        for k, v in (current["named"].items() if is_table(current) else []):
            add_row(*self._clause_of(mode, k, v))
        tk.Button(box, text="+ add", command=lambda: add_row()).pack(anchor="w")

        def read():
            out = {}
            for entry in rows:
                var = entry["var"].get().strip()
                if not var or var == NEW_VAR:
                    continue
                out.update(self._dict_of(mode, var, entry["op"].get(), entry["val"].get().strip()))
            return table(named=out) if out else None
        return read

    def build_form(self, iid, piece):
        self.clear_form()
        # Speaker inherited from an enclosing conversation, for (inherit) resolution.
        self._inherit_speaker = None
        if iid.startswith("talk:"):
            actor = iid[5:].split(":")[0]
            lst = self.model["talk"]["named"].get(actor)
            if is_table(lst):
                self._inherit_speaker = lst["named"].get("speaker")
        if iid == "speakers":
            self.build_speakers_form(piece)
            return
        if not is_table(piece):
            tk.Label(self.form, text="Nothing to edit here.").pack()
            return
        if iid.startswith("item:"):
            self.build_item_form(iid, piece)
            return
        if iid.startswith("talk:") and ":" not in iid[5:]:
            self.build_talklist_form(iid, piece)
            return
        self.build_node_form(iid, piece)

    # .. speakers ..

    def build_speakers_form(self, piece):
        info = tk.Label(self.form, text="Who can speak, and which portrait file (BMP in the "
                                        "same folder) each one uses.", anchor="w", fg="#666")
        info.pack(fill="x", padx=6, pady=2)
        box = self.section("Speakers")
        rows = []

        def add_row(sid="", name="", portrait="", existing=False):
            row = tk.Frame(box)
            row.pack(fill="x", pady=2)
            if existing:
                # id is a reference key elsewhere: rename via a propagating button.
                tk.Label(row, text=sid, width=10, anchor="w",
                         font=("Consolas", 10, "bold")).pack(side="left")
                tk.Button(row, text="✎", padx=2,
                          command=lambda: self.rename_speaker_ui(sid)).pack(side="left")
                e_id = None
            else:
                e_id = tk.Entry(row, width=10)
                e_id.insert(0, sid)
                e_id.pack(side="left")
            tk.Label(row, text="name").pack(side="left", padx=(8, 2))
            e_name = tk.Entry(row, width=18)
            e_name.insert(0, name)
            e_name.pack(side="left")
            tk.Label(row, text="portrait").pack(side="left", padx=(8, 2))
            p_vals = ["(none)"] + self.bmp_names()
            if portrait and portrait not in p_vals:
                p_vals.append(portrait)
            p_var = tk.StringVar(value=portrait or "(none)")
            ttk.Combobox(row, textvariable=p_var, values=p_vals, width=14,
                         state="readonly").pack(side="left")
            pic = tk.Label(row)
            pic.pack(side="left", padx=6)

            def update_pic(*_a):
                thumb = self.portrait_thumb(
                    p_var.get() if p_var.get() != "(none)" else None, 32)
                pic.configure(image=thumb or "")
                pic.image = thumb
            p_var.trace_add("write", update_pic)
            update_pic()
            tk.Button(row, text="✕",
                      command=lambda: self.remove_speaker(sid, row, entry, rows)
                      if existing else (row.destroy(), rows.remove(entry))).pack(side="left")
            entry = {"sid": sid, "e_id": e_id, "e_name": e_name, "p_var": p_var}
            rows.append(entry)

        for sid, sp in piece["named"].items():
            if is_table(sp):
                add_row(sid, str(sp["named"].get("name", "")),
                        str(sp["named"].get("portrait", "")), existing=True)
        tk.Button(box, text="+ Add speaker", command=add_row).pack(anchor="w", pady=2)

        def collect():
            named = {}
            for e in rows:
                sid = e["sid"] if e["e_id"] is None else e["e_id"].get().strip()
                if not sid:
                    continue
                entry = table(named={"name": e["e_name"].get().strip() or sid})
                if e["p_var"].get() and e["p_var"].get() != "(none)":
                    entry["named"]["portrait"] = e["p_var"].get()
                named[sid] = entry
            piece["named"] = named
        self.collectors.append(collect)

    # .. talk list (the per-actor greeting dispatch) ..

    def build_talklist_form(self, iid, piece):
        actor = iid[5:]
        tk.Label(self.form, text=f"When the player talks to '{actor}', the first entry whose "
                                 "condition holds is played.\nSelect an entry under this "
                                 "conversation to edit it; the buttons below the list "
                                 "add / reorder / delete entries.",
                 anchor="w", justify="left", fg="#666").pack(fill="x", padx=6, pady=2)
        box = self.section("Conversation")
        var = self.speaker_row(box, "speaker", piece["named"].get("speaker"))

        lst = self.section("Entries (checked top to bottom)")
        for i, entry in enumerate(piece["array"]):
            row = tk.Frame(lst)
            row.pack(fill="x")
            tk.Label(row, text=self.talk_entry_label(entry, i + 1), anchor="w").pack(side="left")

        def collect():
            v = var.get()
            if v and v != INHERIT:
                piece["named"]["speaker"] = v
            else:
                piece["named"].pop("speaker", None)
        self.collectors.append(collect)

    # .. inventory item ..

    def build_item_form(self, iid, piece):
        named = piece["named"]
        tk.Label(self.form, text="An inventory item. It shows in the Items menu while its "
                                 "“owned” condition holds; each action runs a dialog scene.\n"
                                 "Use {variable} in the name to show a live count "
                                 "(e.g. “Red mushroom x{mushrooms}”).",
                 anchor="w", justify="left", fg="#666").pack(fill="x", padx=6, pady=2)

        head = self.section("Item")
        nrow = tk.Frame(head)
        nrow.pack(fill="x", pady=2)
        tk.Label(nrow, text="name", width=8, anchor="w").pack(side="left")
        name_e = tk.Entry(nrow)
        name_e.insert(0, str(named.get("name", "")))
        name_e.pack(side="left", fill="x", expand=True)

        srow = tk.Frame(head)
        srow.pack(fill="x", pady=2)
        tk.Label(srow, text="sprite", width=8, anchor="w").pack(side="left")
        spr = named.get("sprite")
        spr_vals = ["(none)"] + self.bmp_names()
        if spr and spr not in spr_vals:
            spr_vals.append(spr)
        spr_var = tk.StringVar(value=spr or "(none)")
        ttk.Combobox(srow, textvariable=spr_var, values=spr_vals, width=16,
                     state="readonly").pack(side="left")
        pic = tk.Label(srow)
        pic.pack(side="left", padx=6)

        def update_pic(*_a):
            thumb = self.portrait_thumb(spr_var.get() if spr_var.get() != "(none)" else None, 32)
            pic.configure(image=thumb or "")
            pic.image = thumb
        spr_var.trace_add("write", update_pic)
        update_pic()

        tk.Label(head, text="owned when… (blank = always in the menu)", fg="#666",
                 anchor="w").pack(fill="x")
        owned_read = self.cond_builder(head, "when", named.get("owned"))

        # actions -------------------------------------------------------------
        acts_box = self.section("Actions (each runs a scene when chosen)")
        action_rows = []

        def rebuild():
            self.build_form(self.current, self.get_piece(self.current))

        def add_action_row(action):
            row = tk.Frame(acts_box, bd=1, relief="groove")
            row.pack(fill="x", pady=2)
            entry = {"row": row, "action": action}
            l1 = tk.Frame(row)
            l1.pack(fill="x", padx=2, pady=1)
            tk.Label(l1, text="label", width=8, anchor="w").pack(side="left")
            lab = tk.Entry(l1)
            lab.insert(0, str(action["named"].get("label", "")))
            lab.pack(side="left", fill="x", expand=True)
            tk.Label(l1, text="runs").pack(side="left", padx=4)
            tvar, tframe = self.scene_combo(l1, action["named"].get("node"), width=18)
            tframe.pack(side="left")

            def move(d):
                if not self.collect_form():
                    return
                i = action_rows.index(entry)
                j = i + d
                if 0 <= j < len(action_rows):
                    a = self.get_piece(self.current)["named"]["actions"]["array"]
                    a[i], a[j] = a[j], a[i]
                    rebuild()
            tk.Button(l1, text="↑", command=lambda: move(-1)).pack(side="left")
            tk.Button(l1, text="↓", command=lambda: move(1)).pack(side="left")
            tk.Button(l1, text="✕",
                      command=lambda: (action_rows.remove(entry), row.destroy())).pack(side="left", padx=2)
            l2 = tk.Frame(row)
            l2.pack(fill="x", padx=2, pady=1)
            tk.Label(l2, text="only if", width=8, anchor="w", fg="#666").pack(side="left")
            when_read = self.cond_builder(l2, "when", action["named"].get("when"))
            entry.update(label=lab, node=tvar, when=when_read)
            action_rows.append(entry)

        for a in (named.get("actions") or table())["array"]:
            if is_table(a):
                add_action_row(a)
        tk.Button(acts_box, text="+ Add action",
                  command=lambda: add_action_row(table(named={"label": ""}))).pack(anchor="w", pady=2)

        def collect():
            named["name"] = name_e.get()
            if spr_var.get() and spr_var.get() != "(none)":
                named["sprite"] = spr_var.get()
            else:
                named.pop("sprite", None)
            owned = owned_read()
            if owned is None:
                named.pop("owned", None)
            else:
                named["owned"] = owned
            arr = []
            for entry in action_rows:
                act = entry["action"]
                act["named"]["label"] = entry["label"].get()
                tgt = entry["node"].get()
                if tgt and tgt not in (END_LABEL, NEW_SCENE):
                    act["named"]["node"] = tgt
                else:
                    act["named"].pop("node", None)
                w = entry["when"]()
                if w is None:
                    act["named"].pop("when", None)
                else:
                    act["named"]["when"] = w
                arr.append(act)
            named["actions"] = table(array=arr)
            # keep the left-tree label in step with a renamed item
            try:
                if self.item_tree.exists(iid):
                    self.item_tree.item(iid, text=f"{iid[5:]}. {named.get('name') or iid}")
            except tk.TclError:
                pass
        self.collectors.append(collect)

    # .. node / talk entry ..

    def page_speaker_combo(self, combo_parent, pic_parent, current, node_speaker_getter):
        """Speaker picker for a dialog page/prompt; resolves the inherit chain
        (page → node → conversation) for the live portrait."""
        pic = tk.Label(pic_parent)

        def resolve(v):
            if v and v not in (INHERIT, NEW_SPEAKER):
                return v
            nv = node_speaker_getter()
            if nv and nv not in (INHERIT, NEW_SPEAKER):
                return nv
            return self._inherit_speaker

        def update_pic(*_a):
            thumb = self.portrait_thumb(self.speaker_portrait(resolve(var.get())), 48)
            pic.configure(image=thumb or "")
            pic.image = thumb

        var, combo = self.speaker_combo(combo_parent, current, True, on_change=update_pic)
        var.trace_add("write", update_pic)
        return var, combo, pic, update_pic

    @staticmethod
    def arm_summary(arm):
        n = arm["named"]
        bits = []
        if is_table(n.get("set")):
            bits.append("sets " + ", ".join(n["set"]["named"]))
        if is_table(n.get("add")):
            bits.append("adjusts " + ", ".join(n["add"]["named"]))
        if is_table(n.get("pages")):
            k = len(n["pages"]["array"])
            bits.append(f"{k} page" + ("s" if k != 1 else ""))
        if n.get("mapset"):
            bits.append("edits map")
        if n.get("ending"):
            bits.append("ending")
        if n.get("next"):
            bits.append(f"→ {n['next']}")
        return " · ".join(bits) or "(no effect)"

    @staticmethod
    def is_goto_arm(arm):
        return set(arm["named"]).issubset({"when", "goto"})

    def build_node_form(self, iid, piece):
        named = piece["named"]
        force = getattr(self, "_force", set())

        def rebuild():
            self.build_form(self.current, self.get_piece(self.current))

        def reveal(section):
            if not self.collect_form():
                return
            self._force = force | {section}
            rebuild()

        head = self.section("Scene")
        sp_var = self.speaker_row(head, "speaker", named.get("speaker"))
        tk.Label(head, text="play this only if…", fg="#666", anchor="w").pack(fill="x")
        when_read = self.cond_builder(head, "when", named.get("when"))
        tk.Label(head, text="then set…", fg="#666", anchor="w").pack(fill="x")
        set_read = self.cond_builder(head, "set", named.get("set"))
        tk.Label(head, text="then add…", fg="#666", anchor="w").pack(fill="x")
        add_read = self.cond_builder(head, "add", named.get("add"))

        has_pages = "pages" in named or "pages" in force
        has_choices = "choices" in named or "choices" in force
        has_branch = "branch" in named or "branch" in force
        has_ending = "ending" in named or "ending" in force

        # pages ---------------------------------------------------------------
        page_rows = []
        if has_pages:
            pages_box = self.section("Dialog pages")

            def add_page_row(page):
                row = tk.Frame(pages_box, bd=1, relief="groove")
                entry = {"row": row, "page": page}
                who = page["named"].get("speaker") if is_table(page) else None
                pic_holder = tk.Frame(row)
                pic_holder.pack(side="left", padx=4, pady=4)
                side = tk.Frame(row)
                side.pack(side="left", fill="x", expand=True, pady=2)
                top = tk.Frame(side)
                top.pack(fill="x")
                var, combo, pic, _upd = self.page_speaker_combo(
                    top, pic_holder, who, lambda: sp_var.get())
                combo.pack(side="left")
                pic.pack()

                def move(d):
                    if not self.collect_form():
                        return
                    i = page_rows.index(entry)
                    j = i + d
                    if 0 <= j < len(page_rows):
                        p = self.get_piece(self.current)["named"]["pages"]["array"]
                        p[i], p[j] = p[j], p[i]
                        rebuild()

                def remove():
                    page_rows.remove(entry)
                    row.destroy()
                tk.Button(top, text="↑", command=lambda: move(-1)).pack(side="right")
                tk.Button(top, text="↓", command=lambda: move(1)).pack(side="right")
                tk.Button(top, text="✕", command=remove).pack(side="right", padx=2)

                txt = tk.Text(side, height=4, width=44, font=("Consolas", 10), wrap="word", undo=True)
                txt.insert("1.0", page_text(page))
                txt.pack(fill="x", pady=2, padx=2)
                entry["var"], entry["text"] = var, txt
                page_rows.append(entry)
                return row

            for page in (named.get("pages") or table())["array"]:
                add_page_row(page).pack(fill="x", pady=2)
            tk.Button(pages_box, text="+ Add page",
                      command=lambda: add_page_row(table(named={"text": ""})).pack(fill="x", pady=2)
                      ).pack(anchor="w", pady=2)

        # choices -------------------------------------------------------------
        choice_rows = []
        prompt_e = prompt_sp = None
        if has_choices:
            choices_box = self.section("Choices (what the player can answer)")
            prompt = named.get("prompt")
            qf = tk.Frame(choices_box)
            qf.pack(fill="x")
            prompt_sp = self.speaker_row(qf, "asked by",
                                         prompt["named"].get("speaker") if is_table(prompt) else None)
            tk.Label(choices_box, text="question", fg="#666", anchor="w").pack(fill="x")
            prompt_e = tk.Text(choices_box, height=2, font=("Consolas", 10), wrap="word")
            prompt_e.insert("1.0", page_text(prompt) if prompt is not None else "")
            prompt_e.pack(fill="x", pady=2)

            def add_choice_row(choice):
                row = tk.Frame(choices_box, bd=1, relief="groove")
                row.pack(fill="x", pady=2)
                entry = {"row": row, "choice": choice}
                l1 = tk.Frame(row)
                l1.pack(fill="x", padx=2, pady=1)
                tk.Label(l1, text="answer", width=8, anchor="w").pack(side="left")
                lab = tk.Entry(l1)
                lab.insert(0, str(choice["named"].get("label", "")))
                lab.pack(side="left", fill="x", expand=True)
                tk.Label(l1, text="→").pack(side="left", padx=4)
                tgt = choice["named"].get("goto") or choice["named"].get("next")
                tvar, tcombo = self.scene_combo(l1, tgt, width=18)
                tcombo.pack(side="left")
                tk.Button(l1, text="✕",
                          command=lambda: (choice_rows.remove(entry), row.destroy())).pack(side="left", padx=2)
                l2 = tk.Frame(row)
                l2.pack(fill="x", padx=2, pady=1)
                tk.Label(l2, text="only if", width=8, anchor="w", fg="#666").pack(side="left")
                when_read = self.cond_builder(l2, "when", choice["named"].get("when"))
                entry.update(label=lab, target=tvar, when=when_read)
                choice_rows.append(entry)

            for ch in (named.get("choices") or table())["array"]:
                if is_table(ch):
                    add_choice_row(ch)
            tk.Button(choices_box, text="+ Add choice",
                      command=lambda: add_choice_row(table(named={"label": ""}))).pack(anchor="w", pady=2)

        # branch --------------------------------------------------------------
        arm_rows = []
        if has_branch:
            branch_box = self.section("Branch (first matching arm wins)")
            branch = named.get("branch") or table()

            def add_arm_row(arm):
                row = tk.Frame(branch_box, bd=1, relief="groove")
                row.pack(fill="x", pady=3)
                entry = {"row": row, "arm": arm}
                top = tk.Frame(row)
                top.pack(fill="x", padx=2, pady=1)
                tk.Label(top, text="when", width=6, anchor="w", fg="#666").pack(side="left")
                when_read = self.cond_builder(top, "when", arm["named"].get("when"))

                def move(d):
                    if not self.collect_form():
                        return
                    i = arm_rows.index(entry)
                    j = i + d
                    if 0 <= j < len(arm_rows):
                        a = self.get_piece(self.current)["named"]["branch"]["array"]
                        a[i], a[j] = a[j], a[i]
                        rebuild()
                ctl = tk.Frame(row)
                ctl.pack(fill="x", padx=2)
                tk.Button(ctl, text="↑", command=lambda: move(-1)).pack(side="right")
                tk.Button(ctl, text="↓", command=lambda: move(1)).pack(side="right")
                tk.Button(ctl, text="✕ delete arm",
                          command=lambda: (arm_rows.remove(entry), row.destroy())).pack(side="right", padx=4)

                if self.is_goto_arm(arm):
                    tk.Label(ctl, text="go to", anchor="w").pack(side="left")
                    tvar, tcombo = self.scene_combo(ctl, arm["named"].get("goto"), width=18)
                    tcombo.pack(side="left")
                    entry.update(kind="goto", when=when_read, target=tvar)
                else:
                    tk.Label(ctl, text=self.arm_summary(arm), fg="#333", anchor="w").pack(side="left")
                    tk.Label(row, text="(pages / effects for this arm are edited on the Source tab)",
                             fg="#999", anchor="w").pack(fill="x", padx=6)
                    entry.update(kind="inline", when=when_read)
                arm_rows.append(entry)

            for arm in branch["array"]:
                if is_table(arm):
                    add_arm_row(arm)
            tk.Button(branch_box, text="+ Add arm (goto)",
                      command=lambda: add_arm_row(table(named={}))).pack(anchor="w", pady=2)

        # flow (only when neither choices nor branch decide it) ---------------
        goto_key = "goto" if "goto" in named else "next"
        nvar = None
        if not has_choices and not has_branch:
            flow = self.section("After this scene")
            nrow = tk.Frame(flow)
            nrow.pack(fill="x", pady=2)
            tk.Label(nrow, text="go to", width=10, anchor="w").pack(side="left")
            nvar, ncombo = self.scene_combo(nrow, named.get(goto_key), width=24)
            ncombo.pack(side="left")
        else:
            flow = self.section("Timing")
        srow = tk.Frame(flow)
        srow.pack(fill="x", pady=2)
        tk.Label(srow, text="pause (ms)", width=10, anchor="w").pack(side="left")
        sleep_e = tk.Entry(srow, width=8)
        if named.get("sleep") is not None:
            sleep_e.insert(0, str(named["sleep"]))
        sleep_e.pack(side="left")
        show_var = tk.BooleanVar(value=bool(named.get("show")))
        tk.Checkbutton(srow, text="redraw the map during this scene (show)",
                       variable=show_var).pack(side="left", padx=10)

        # ending --------------------------------------------------------------
        ends = None
        if has_ending:
            end_box = self.section("Ending screen (clear the title for none)")
            e_named = named.get("ending")["named"] if is_table(named.get("ending")) else {}
            ends = {}
            for field, width in (("title", 30), ("sub", 44)):
                r = tk.Frame(end_box)
                r.pack(fill="x", pady=1)
                tk.Label(r, text=field, width=10, anchor="w").pack(side="left")
                e = tk.Entry(r, width=width)
                if e_named.get(field) is not None:
                    e.insert(0, str(e_named[field]))
                e.pack(side="left")
                ends[field] = e
            rs = tk.Frame(end_box)
            rs.pack(fill="x", pady=1)
            tk.Label(rs, text="sprite", width=10, anchor="w").pack(side="left")
            spr = e_named.get("sprite")
            spr_vals = ["(none)"] + self.bmp_names()
            if spr and spr not in spr_vals:
                spr_vals.append(spr)
            spr_var = tk.StringVar(value=spr or "(none)")
            ttk.Combobox(rs, textvariable=spr_var, values=spr_vals, width=16,
                         state="readonly").pack(side="left")
            tk.Label(rs, text="style", width=8, anchor="e").pack(side="left")
            sty_var = tk.StringVar(value=e_named.get("style") or "(plain)")
            ttk.Combobox(rs, textvariable=sty_var, values=ENDING_STYLES, width=16,
                         state="readonly").pack(side="left")
            ends["sprite"], ends["style"] = spr_var, sty_var

        # add-section buttons for what's absent -------------------------------
        adder = tk.Frame(self.form)
        adder.pack(fill="x", padx=6, pady=4)
        if not has_pages:
            tk.Button(adder, text="+ Add pages", command=lambda: reveal("pages")).pack(side="left", padx=2)
        if not has_choices and not has_branch:
            tk.Button(adder, text="+ Add choices", command=lambda: reveal("choices")).pack(side="left", padx=2)
            tk.Button(adder, text="+ Add branch", command=lambda: reveal("branch")).pack(side="left", padx=2)
        if not has_ending:
            tk.Button(adder, text="+ Add ending screen", command=lambda: reveal("ending")).pack(side="left", padx=2)

        # anything the form doesn't cover -------------------------------------
        extra = [k for k in named if k not in FORM_KEYS]
        if extra:
            tk.Label(self.form, fg="#666", anchor="w", justify="left",
                     text="Also in this scene (kept as-is, edit on the Source tab): "
                          + ", ".join(extra)).pack(fill="x", padx=8, pady=4)

        # collector ------------------------------------------------------------
        def collect():
            def put(key, read):
                t = read()
                if t is None:
                    named.pop(key, None)
                else:
                    named[key] = t

            v = sp_var.get()
            if v and v not in (INHERIT, NEW_SPEAKER):
                named["speaker"] = v
            else:
                named.pop("speaker", None)
            put("when", when_read)
            put("set", set_read)
            put("add", add_read)

            if has_pages:
                arr = []
                for entry in page_rows:
                    page = entry["page"] if is_table(entry["page"]) else table()
                    page["named"]["text"] = entry["text"].get("1.0", "end").rstrip("\n")
                    page["array"] = []
                    who = entry["var"].get()
                    if who and who not in (INHERIT, NEW_SPEAKER):
                        page["named"]["speaker"] = who
                    else:
                        page["named"].pop("speaker", None)
                    arr.append(page)
                named["pages"] = table(array=arr) if arr else None
                if not arr:
                    named.pop("pages", None)

            if has_choices:
                carr = []
                for entry in choice_rows:
                    ch = entry["choice"]
                    ch["named"]["label"] = entry["label"].get()
                    tgt = entry["target"].get()
                    ckey = "goto" if "goto" in ch["named"] or "next" not in ch["named"] else "next"
                    if tgt and tgt not in (END_LABEL, NEW_SCENE):
                        ch["named"][ckey] = tgt
                    else:
                        ch["named"].pop("goto", None)
                        ch["named"].pop("next", None)
                    w = entry["when"]()
                    if w is None:
                        ch["named"].pop("when", None)
                    else:
                        ch["named"]["when"] = w
                    carr.append(ch)
                if carr:
                    named["choices"] = table(array=carr)
                    p = named.get("prompt") if is_table(named.get("prompt")) else table()
                    ptext = prompt_e.get("1.0", "end").rstrip("\n")
                    if ptext or (prompt_sp.get() not in (INHERIT, NEW_SPEAKER)):
                        p["named"]["text"] = ptext
                        p["array"] = []
                        if prompt_sp.get() and prompt_sp.get() not in (INHERIT, NEW_SPEAKER):
                            p["named"]["speaker"] = prompt_sp.get()
                        else:
                            p["named"].pop("speaker", None)
                        named["prompt"] = p
                    else:
                        named.pop("prompt", None)
                else:
                    named.pop("choices", None)
                    named.pop("prompt", None)

            if has_branch:
                barr = []
                for entry in arm_rows:
                    arm = entry["arm"]
                    w = entry["when"]()
                    if w is None:
                        arm["named"].pop("when", None)
                    else:
                        arm["named"]["when"] = w
                    if entry["kind"] == "goto":
                        tgt = entry["target"].get()
                        if tgt and tgt not in (END_LABEL, NEW_SCENE):
                            arm["named"]["goto"] = tgt
                        else:
                            arm["named"].pop("goto", None)
                    barr.append(arm)
                named["branch"] = table(array=barr) if barr else None
                if not barr:
                    named.pop("branch", None)

            if nvar is not None:
                tgt = nvar.get()
                if tgt and tgt not in (END_LABEL, NEW_SCENE):
                    named[goto_key] = tgt
                else:
                    named.pop(goto_key, None)

            s = sleep_e.get().strip()
            if s:
                named["sleep"] = int(s)
            else:
                named.pop("sleep", None)
            if show_var.get():
                named["show"] = True
            else:
                named.pop("show", None)

            if ends is not None and ends["title"].get().strip():
                e = named.get("ending") if is_table(named.get("ending")) else table()
                e["named"]["title"] = ends["title"].get().strip()
                sub = ends["sub"].get().strip()
                if sub:
                    e["named"]["sub"] = sub
                else:
                    e["named"].pop("sub", None)
                if ends["sprite"].get() != "(none)":
                    e["named"]["sprite"] = ends["sprite"].get()
                else:
                    e["named"].pop("sprite", None)
                if ends["style"].get() != "(plain)":
                    e["named"]["style"] = ends["style"].get()
                else:
                    e["named"].pop("style", None)
                named["ending"] = e
            elif ends is not None:
                named.pop("ending", None)
        self.collectors.append(collect)

    def collect_form(self):
        before = self.snapshot()
        try:
            for c in self.collectors:
                c()
        except ValueError as e:
            self.set_status(f"condition error: {e}")
            return False
        except tk.TclError:
            return True  # widgets already destroyed (rebuild in progress)
        if (self.model, self.order, self.items) != before:
            self.remember(before)
            if (self.model, self.order) != before[:2]:
                self.dirty = True
            if self.items != before[2]:
                self.items_dirty = True
            self.refresh_vars()
        return True

    # -- undo / redo -----------------------------------------------------------

    def snapshot(self):
        """Deep copy of everything undo/redo restores: both document slots."""
        return copy.deepcopy((self.model, self.order, self.items))

    def mark_dirty(self, before):
        """Set the per-slot dirty flags by diffing against a prior snapshot."""
        if (self.model, self.order) != before[:2]:
            self.dirty = True
        if self.items != before[2]:
            self.items_dirty = True

    def remember(self, before=None):
        """Push the pre-change state (deep copy) onto the undo stack."""
        snap = before if before is not None else self.snapshot()
        self.undo_stack.append(snap)
        del self.undo_stack[:-50]
        self.redo_stack.clear()

    def _restore(self, snap):
        self.model, self.order, self.items = snap
        self.dirty = True
        self.items_dirty = self.items is not None
        self.current = None
        self.clear_form()
        self.text.delete("1.0", "end")
        self.text.edit_modified(False)
        self.rebuild_lists()
        self.refresh_vars()

    def undo(self):
        if not self.undo_stack:
            return self.set_status("nothing to undo")
        self.redo_stack.append(self.snapshot())
        self._restore(self.undo_stack.pop())
        self.set_status(f"undone ({len(self.undo_stack)} left)")

    def redo(self):
        if not self.redo_stack:
            return self.set_status("nothing to redo")
        self.undo_stack.append(self.snapshot())
        self._restore(self.redo_stack.pop())
        self.set_status("redone")

    def on_ctrl_z(self, event):
        if event.widget is self.text:
            return  # the source editor has its own text undo
        self.undo()
        return "break"

    def on_ctrl_y(self, event):
        if event.widget is self.text:
            return
        self.redo()
        return "break"

    # -- live source validity --------------------------------------------------

    def schedule_lint(self, _event=None):
        if self._lint_job:
            self.root.after_cancel(self._lint_job)
        self._lint_job = self.root.after(600, self.lint_source)

    def lint_source(self):
        self._lint_job = None
        if self.tabs.index(self.tabs.select()) != 1:
            return
        try:
            parse_lua_snippet(self.text.get("1.0", "end"))
            self.set_status("Lua OK")
        except Exception as e:
            self.set_status(f"Lua error: {e}")

    # -- variables pane --------------------------------------------------------

    def refresh_vars(self, keep=None):
        counts = {}
        for _, t in walk_all(self.model, self.items):
            for key in COND_KEYS:
                cond = t["named"].get(key)
                if is_table(cond):
                    for k in cond["named"]:
                        if not k.startswith(("near_", "room")):
                            name = k.rstrip(">=").rstrip("<=")
                            counts[name] = counts.get(name, 0) + 1
        # Include declared-but-unused vars (freshly added via + Var).
        for name in self.known_vars:
            counts.setdefault(name, 0)
        self.vars_list.delete(0, "end")
        self._var_names = sorted(counts)
        for name in self._var_names:
            self.vars_list.insert("end", f"{name}  ×{counts[name]}")
        self.uses_list.delete(0, "end")
        self._use_iids = []
        if keep in self._var_names:
            i = self._var_names.index(keep)
            self.vars_list.selection_set(i)
            self.show_var_uses()

    def selected_var(self):
        sel = self.vars_list.curselection()
        return self._var_names[sel[0]] if sel else None

    def show_var_uses(self):
        name = self.selected_var()
        self.uses_list.delete(0, "end")
        self._use_iids = []
        if not name:
            return
        for path, kind in var_uses(self.model, name, self.items):
            iid = self.path_to_iid(path)
            self.uses_list.insert("end", f"{self.use_label(iid)} — {kind}")
            self._use_iids.append(iid)

    def use_label(self, iid):
        if iid == "speakers":
            return "speakers"
        if iid.startswith("talk:"):
            return "talk " + iid[5:].replace(":", " #")
        if iid.startswith("item:"):
            return "item " + self.item_name(int(iid[5:]))
        return iid[5:] if iid.startswith("node:") else iid

    def open_var_use(self):
        sel = self.uses_list.curselection()
        if sel and sel[0] < len(self._use_iids):
            self.navigate(self._use_iids[sel[0]])

    def _fire_var_change(self, action, a, b, base_status):
        """Report a variable edit; when embedded, let the shell propagate it to
        the wiring script and fold its note into the status line."""
        extra = None
        if self.on_var_change:
            try:
                extra = self.on_var_change(action, a, b)
            except Exception as e:
                extra = f"wiring update failed: {e}"
        self.set_status(base_status + ("; " + extra if extra else ""))

    def add_var(self):
        name = simpledialog.askstring("New variable",
                                      "Variable name (letters/digits/underscore):")
        if not name:
            return
        if not IDENT.match(name):
            return self.set_status("variable names must be plain (letters, digits, _)")
        self.known_vars.add(name)
        self.refresh_vars(keep=name)
        self._fire_var_change("add", name, None,
                              f"variable '{name}' available in condition dropdowns")

    def rename_var_ui(self):
        old = self.selected_var()
        if not old:
            return self.set_status("select a variable to rename")
        new = simpledialog.askstring("Rename variable", f"New name for '{old}':",
                                     initialvalue=old)
        if not new or new == old:
            return
        if not IDENT.match(new):
            return self.set_status("variable names must be plain (letters, digits, _)")
        if not self.apply():
            return
        before = self.snapshot()
        self.remember(before)
        n = rename_var(self.model, old, new, self.items)
        self.known_vars.discard(old)
        self.known_vars.add(new)
        self.mark_dirty(before)
        self.current = None
        self.rebuild_lists()
        self.refresh_vars(keep=new)
        self._fire_var_change("rename", old, new, f"renamed variable; {n} clause(s) updated")

    def remove_var_ui(self):
        name = self.selected_var()
        if not name:
            return self.set_status("select a variable to remove")
        uses = var_uses(self.model, name, self.items)
        if uses and not messagebox.askyesno(
                "Remove variable",
                f"'{name}' is used in {len(uses)} place(s). Remove the variable and "
                f"delete those conditions/effects?"):
            return
        if not self.apply():
            return
        before = self.snapshot()
        self.remember(before)
        n = remove_var(self.model, name, self.items)
        self.known_vars.discard(name)
        self.mark_dirty(before)
        self.current = None
        self.rebuild_lists()
        self.refresh_vars()
        self._fire_var_change("remove", name, None,
                              f"removed variable '{name}' ({n} clause(s) deleted)")

    # -- item ops -------------------------------------------------------------

    def add_item(self):
        if self.items is None:
            return self.set_status("open an items file first (Open items…)")
        if not self.apply():
            return
        self.remember()
        self.items.append(table(named={"name": "New item",
                                       "actions": table(array=[])}))
        self.items_dirty = True
        self.current = None
        self.rebuild_lists(select=f"item:{len(self.items)}")

    def move_item(self, d):
        iid = self.selection()
        if not iid or not iid.startswith("item:"):
            return self.set_status("select an item to move")
        i = int(iid[5:]) - 1
        j = i + d
        if not (0 <= j < len(self.items)):
            return
        if not self.apply():
            return
        self.remember()
        self.items[i], self.items[j] = self.items[j], self.items[i]
        self.items_dirty = True
        self.current = None
        self.rebuild_lists(select=f"item:{j + 1}")

    # -- speaker rename / remove (propagating) --------------------------------

    def rename_speaker_ui(self, old):
        new = simpledialog.askstring("Rename speaker", f"New id for '{old}':",
                                     initialvalue=old)
        if not new or new == old:
            return
        if not IDENT.match(new) or new in self.speaker_ids():
            return self.set_status("invalid or duplicate speaker id")
        if not self.apply():
            return
        self.remember()
        sp = self.model["speakers"]["named"]
        sp[new] = sp.pop(old)
        n = rename_speaker_refs(self.model, old, new)
        self.dirty = True
        self.current = None
        self.rebuild_lists()
        self.navigate("speakers")
        self.set_status(f"renamed speaker; {n} reference(s) updated")

    def remove_speaker(self, sid, row, entry, rows):
        used = sum(1 for _, v in self.model.items() for _, t in walk_tables(v)
                   if t["named"].get("speaker") == sid)
        if used and not messagebox.askyesno(
                "Remove speaker",
                f"'{sid}' is still used by {used} line(s). Remove it anyway? "
                "Those lines will fall back to the narrator."):
            return
        row.destroy()
        if entry in rows:
            rows.remove(entry)
        # the actual removal happens on collect (rebuilds from remaining rows)
        self.dirty = True

    # -- conversation / scene ops ---------------------------------------------

    def rename_conversation(self):
        iid = self.selection()
        if not iid or not iid.startswith("talk:"):
            return self.set_status("select a conversation to rename")
        old = iid[5:].split(":")[0]
        new = simpledialog.askstring("Rename conversation",
                                     f"New actor id for '{old}':", initialvalue=old)
        if not new or new == old:
            return
        talk = self.model["talk"]["named"]
        if not IDENT.match(new) or new in talk:
            return self.set_status("invalid or duplicate actor id")
        if not self.apply():
            return
        self.remember()
        # preserve order
        talk_new = {}
        for k, v in talk.items():
            talk_new[new if k == old else k] = v
        self.model["talk"]["named"] = talk_new
        self.dirty = True
        self.current = None
        self.rebuild_lists(select=f"talk:{new}")
        self.set_status(f"renamed conversation to '{new}' — also update ACTORS in "
                        "adventure.lua (the game maps tile chars to this id)")

    def add_conversation(self):
        name = simpledialog.askstring("New conversation",
                                      "Actor id (as used by the game's ACTORS map):")
        if not name:
            return
        if not IDENT.match(name):
            return self.set_status("actor ids must be plain names (letters, digits, _)")
        talk = self.model.setdefault("talk", table())
        if name in talk["named"]:
            return self.set_status(f"conversation '{name}' already exists")
        self.remember()
        talk["named"][name] = table(
            array=[table(named={"pages": table(array=[table(named={"text": ""})])})])
        self.dirty = True
        self.rebuild_lists(select=f"talk:{name}:1")

    def add_entry(self):
        iid = self.selection()
        if not iid or not iid.startswith("talk:"):
            return self.set_status("select a conversation first")
        actor = iid[5:].split(":")[0]
        if not self.apply():
            return
        self.remember()
        lst = self.model["talk"]["named"][actor]
        lst["array"].append(table(named={"pages": table(array=[table(named={"text": ""})])}))
        self.dirty = True
        self.current = None
        self.rebuild_lists(select=f"talk:{actor}:{len(lst['array'])}")

    def move_entry(self, d):
        iid = self.selection()
        parts = iid[5:].split(":") if iid and iid.startswith("talk:") else []
        if len(parts) != 2:
            return self.set_status("select a conversation entry to move")
        actor, i = parts[0], int(parts[1]) - 1
        arr = self.model["talk"]["named"][actor]["array"]
        j = i + d
        if not (0 <= j < len(arr)):
            return
        if not self.apply():
            return
        self.remember()
        arr[i], arr[j] = arr[j], arr[i]
        self.dirty = True
        self.current = None
        self.rebuild_lists(select=f"talk:{actor}:{j + 1}")

    def new_node(self):
        name = simpledialog.askstring("New scene", "Scene id (letters/digits/underscore):")
        if not name:
            return
        if not IDENT.match(name):
            return self.set_status("scene ids must be plain names (letters, digits, _)")
        if name in self.model:
            return self.set_status(f"'{name}' already exists")
        self.remember()
        self.model[name] = table(named={"pages": table(array=[table(named={"text": ""})])})
        self.order.append(name)
        self.dirty = True
        self.rebuild_lists(select=f"node:{name}")

    def rename_node(self):
        iid = self.selection()
        if not iid or not iid.startswith("node:"):
            return self.set_status("select a scene to rename")
        old = iid[5:]
        new = simpledialog.askstring("Rename scene", f"New id for '{old}':", initialvalue=old)
        if not new or new == old:
            return
        if not IDENT.match(new) or new in self.model:
            return self.set_status("invalid or duplicate id")
        before = self.snapshot()
        self.remember(before)
        self.model[new] = self.model.pop(old)
        self.order[self.order.index(old)] = new
        n = rename_refs(self.model, old, new, self.items)
        self.mark_dirty(before)
        self.current = None
        self.rebuild_lists(select=f"node:{new}")
        self.set_status(f"renamed; {n} reference(s) updated "
                        + ("(incl. items)" if self.items is not None else ""))

    def delete_selected(self):
        iid = self.selection()
        if iid and iid.startswith("talk:"):
            parts = iid[5:].split(":")
            actor = parts[0]
            if len(parts) == 2:                      # one entry
                i = int(parts[1]) - 1
                arr = self.model["talk"]["named"][actor]["array"]
                if not messagebox.askyesno("Delete entry",
                                           f"Delete entry {i + 1} of '{actor}'?"):
                    return
                self.remember()
                del arr[i]
            else:                                    # the whole conversation
                n = len(self.model["talk"]["named"][actor]["array"])
                if not messagebox.askyesno("Delete conversation",
                                           f"Delete conversation '{actor}' ({n} entr"
                                           f"{'y' if n == 1 else 'ies'})?"):
                    return
                self.remember()
                del self.model["talk"]["named"][actor]
        elif iid and iid.startswith("node:"):
            name = iid[5:]
            used = find_refs(self.model, self.items).get(name, [])
            msg = f"Delete scene '{name}'?"
            if used:
                labels = [self.use_label(self.path_to_iid(p)) for p in used[:8]]
                msg += f"\n\nStill used by: {', '.join(labels)}"
            if not messagebox.askyesno("Delete scene", msg):
                return
            self.remember()
            del self.model[name]
            self.order.remove(name)
            self.dirty = True
        elif iid and iid.startswith("item:"):
            idx = int(iid[5:])
            if self.items is None or not (1 <= idx <= len(self.items)):
                return
            if not messagebox.askyesno("Delete item",
                                       f"Delete item '{self.item_name(idx)}'?"):
                return
            self.remember()
            del self.items[idx - 1]
            self.items_dirty = True
        else:
            return self.set_status("select a conversation entry, scene or item to delete")
        if not (iid.startswith("node:") or iid.startswith("item:")):
            self.dirty = True
        self.current = None
        self.rebuild_lists()
        self.clear_form()
        self.text.delete("1.0", "end")
        self.refresh_vars()

    def validate(self):
        if not self.apply():
            return
        problems = []
        ids = set(self.node_ids())
        for tgt, paths in find_refs(self.model, self.items).items():
            if tgt not in ids:
                where = self.use_label(self.path_to_iid(paths[0]))
                problems.append(f"missing scene '{tgt}' (from {where})")
        speakers = set(self.speaker_ids())
        if speakers:
            for top, v in self.model.items():
                for path, t in walk_tables(v, top):
                    sp = t["named"].get("speaker")
                    if isinstance(sp, str) and sp not in speakers:
                        problems.append(f"unknown speaker '{sp}' at {path}")
        text = ("OK — no problems." if not problems else "\n".join(problems[:30]))
        text += "\n\nstory variables: " + ", ".join(collect_vars(self.model, self.items))
        messagebox.showinfo("Check story", text)

    def set_status(self, text):
        star = "*unsaved*  " if (self.dirty or self.items_dirty) else ""
        self.status.config(text=star + text)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--file", help="dialog-tree or item Lua module; omit to pick one in a file dialog")
    ap.add_argument("--items", help="also open this item module (a sibling items.lua is auto-loaded)")
    args = ap.parse_args()
    root = tk.Tk()
    root.geometry("1040x760")
    path = args.file
    if not path and not args.items:
        root.withdraw()
        path = filedialog.askopenfilename(title="Open dialog or item module",
                                          filetypes=[("Lua modules", "*.lua"), ("All files", "*")])
        if not path:
            return
        root.deiconify()
    ed = DialogEditor(root, path or args.items)
    ed.frame.pack(fill="both", expand=True)
    ed.activate()
    if args.items and path:            # explicit item file alongside an explicit dialog
        ed.load_items_slot(args.items)
        ed.items_dirty = False
        ed.known_vars |= set(collect_vars({}, ed.items))
        ed.retitle()
        ed.rebuild_lists()
        ed.refresh_vars()
    root.mainloop()


if __name__ == "__main__":
    main()
