#!/usr/bin/env python3
"""Story-variable edits for a game's *wiring* script (e.g. adventure.lua).

The dialog editor already renames/removes story variables across dialogs.lua and
items.lua (pure data). The wiring script is different — it is *code*: it declares
the variables' initial values in a `local vars = {...}` table and reads them as
`vars.name` field accesses. When a variable is renamed here, both of those have
to move together, and the dynamic `vars[k]` form (a computed key) must be left
alone because a regex cannot know what it resolves to.

These helpers do exactly that and nothing more:

  find_state_table(src)      -> StateTable | None   (the `vars` identifier + its {…})
  rename_in_wiring(src, …)   -> WiringEdit          (vars.old -> vars.new, key too)
  remove_in_wiring(src, …)   -> WiringEdit          (drop the initializer key)
  seed_in_wiring(src, …)     -> WiringEdit          (add `name = <initial>`)

Everything is a pure string->string transform with a report, so it is unit
tested with no display and the caller (engine_editor.py) can show the warnings.
The state table is located from the engine contract `dtree.run(_, _, _, STATE)`;
if that can't be found and resolved to a bare identifier, every op is a no-op
and says so — we never guess at rewriting executable code.

A small Lua-aware scanner underlies all of this so that braces, strings (quoted
and long-bracket) and comments never fool the brace matching or the key search.
Keys are assumed to be identifiers (`name = value`), which is how every state
and data table in this project is written; the `["name"]` string-key form is not
rewritten (it is reported as a manual-review item if it ever appears).
"""

import re
from collections import namedtuple

IDENT = r"[A-Za-z_][A-Za-z0-9_]*"

# name:       the state-table identifier, e.g. "vars"
# init_open:  index of the initializer '{', or None if there is no `local name = {…}`
# init_close: index of the matching '}', or None
StateTable = namedtuple("StateTable", "name init_open init_close")

# src:      the (possibly) rewritten source
# changed:  number of edits actually applied
# warnings: human-readable notes for the user (dynamic uses, missing table, …)
WiringEdit = namedtuple("WiringEdit", "src changed warnings")


# ---- a just-enough Lua scanner ----------------------------------------------

def _long_bracket_level(src, i):
    """If src[i:] opens a long bracket ('[[', '[=[', '[==[', …) return its level
    (number of '='); otherwise None."""
    if i >= len(src) or src[i] != "[":
        return None
    j = i + 1
    while j < len(src) and src[j] == "=":
        j += 1
    if j < len(src) and src[j] == "[":
        return j - (i + 1)
    return None


def _skip_long_bracket(src, i, level):
    """Index just past a long-bracket string/comment that starts at src[i]."""
    close = "]" + "=" * level + "]"
    k = src.find(close, i)
    return len(src) if k == -1 else k + len(close)


def _skip_atom(src, i):
    """If src[i] begins a Lua comment or string literal, return the index just
    past it; otherwise None. Handles line/block comments, quoted strings with
    backslash escapes, and long-bracket strings."""
    if src.startswith("--", i):
        j = i + 2
        level = _long_bracket_level(src, j)
        if level is not None:
            return _skip_long_bracket(src, j, level)
        nl = src.find("\n", j)
        return len(src) if nl == -1 else nl        # line comment ends at newline
    level = _long_bracket_level(src, i)
    if level is not None:
        return _skip_long_bracket(src, i, level)
    ch = src[i:i + 1]
    if ch in ('"', "'"):
        j = i + 1
        while j < len(src):
            if src[j] == "\\":
                j += 2
                continue
            if src[j] == ch:
                return j + 1
            j += 1
        return len(src)
    return None


def _matching_brace(src, i):
    """src[i] is '{'; return the index of the matching '}', or None."""
    depth, j = 0, i
    while j < len(src):
        a = _skip_atom(src, j)
        if a is not None:
            j = a
            continue
        c = src[j]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return j
        j += 1
    return None


def _split_call_args(src, start):
    """`start` is the index just after a call's '('. Return the list of
    top-level argument substrings, or None if the ')' is never found."""
    depth, j, cur, args = 0, start, start, []
    while j < len(src):
        a = _skip_atom(src, j)
        if a is not None:
            j = a
            continue
        c = src[j]
        if c == ")" and depth == 0:
            args.append(src[cur:j])
            return args
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
        elif c == "," and depth == 0:
            args.append(src[cur:j])
            cur = j + 1
        j += 1
    return None


# ---- locating the state table -----------------------------------------------

def find_state_table(src):
    """The story-state table for a wiring script, discovered from the engine
    contract `dtree.run(G, dialogs, start, STATE)`. Returns a StateTable (whose
    init span is None when there is no `local STATE = {…}` literal to edit), or
    None when no state identifier can be resolved — in which case callers leave
    the file untouched."""
    name = None
    for m in re.finditer(r"\bdtree\.run\s*\(", src):
        args = _split_call_args(src, m.end())
        if args and len(args) >= 4:
            cand = args[3].strip()
            if re.fullmatch(IDENT, cand):
                name = cand
                break
    if name is None:
        return None

    init_open = init_close = None
    dm = re.search(r"\blocal\s+" + re.escape(name) + r"\s*=\s*", src)
    if dm and dm.end() < len(src) and src[dm.end()] == "{":
        close = _matching_brace(src, dm.end())
        if close is not None:
            init_open, init_close = dm.end(), close
    return StateTable(name, init_open, init_close)


def _dynamic_use_count(src, name):
    """How many `name[...]` computed-key accesses appear (cannot be renamed)."""
    return len(re.findall(r"(?<![\w.])" + re.escape(name) + r"\s*\[", src))


# ---- editing the initializer literal ----------------------------------------

def _iter_top_level_keys(body):
    """Yield (key, key_start, key_end) for every `key =` entry at depth 0 of a
    table-literal body (the text between its braces). String-key and positional
    entries are skipped."""
    depth, i, n = 0, 0, len(body)
    while i < n:
        a = _skip_atom(body, i)
        if a is not None:
            i = a
            continue
        c = body[i]
        if c in "{([":
            depth += 1
            i += 1
            continue
        if c in "})]":
            depth -= 1
            i += 1
            continue
        if depth == 0 and (c.isalpha() or c == "_"):
            m = re.match(IDENT, body[i:])
            word = m.group(0)
            j = i + len(word)
            k = j
            while k < n and body[k] in " \t":
                k += 1
            if k < n and body[k] == "=" and body[k:k + 2] != "==":
                yield word, i, j
            i = j
            continue
        i += 1


def _entry_span(body, key_start):
    """Given the start of a `key = value` entry, return (start, end) covering the
    whole entry including its trailing comma — so it can be cut cleanly."""
    # walk past `key`, whitespace, `=`
    i = key_start
    i += len(re.match(IDENT, body[i:]).group(0))
    while i < len(body) and body[i] in " \t":
        i += 1
    i += 1  # the '='
    # consume the value up to a depth-0 comma or the end of the body
    depth = 0
    while i < len(body):
        a = _skip_atom(body, i)
        if a is not None:
            i = a
            continue
        c = body[i]
        if c in "{([":
            depth += 1
        elif c in "})]":
            depth -= 1
        elif c == "," and depth == 0:
            return key_start, i + 1        # include the comma
        i += 1
    return key_start, len(body)


def rename_in_wiring(src, name, old, new):
    """Rename story variable `old`->`new` in a wiring script: the `vars.old`
    field accesses and, if present, the `old =` key in the `local vars = {…}`
    initializer. Dynamic `vars[...]` accesses are left alone and reported."""
    st = find_state_table(src)
    if st is None:
        return WiringEdit(src, 0, ["state table not found — wiring left untouched"])
    warnings = []
    changed = 0

    # 1) field accesses: name.old  ->  name.new  (whole-word, right table only)
    field = re.compile(r"(?<![\w.])" + re.escape(name) + r"\." + re.escape(old) + r"\b")
    src, n = field.subn(name + "." + new, src)
    changed += n

    # 2) the initializer key (depth-0 only), recomputed after the field edits
    st = find_state_table(src)
    if st and st.init_open is not None:
        body = src[st.init_open + 1:st.init_close]
        for key, ks, ke in _iter_top_level_keys(body):
            if key == old:
                body = body[:ks] + new + body[ke:]
                src = src[:st.init_open + 1] + body + src[st.init_close:]
                changed += 1
                break

    dyn = _dynamic_use_count(src, name)
    if dyn:
        warnings.append(f"{dyn} dynamic {name}[…] access(es) left unchanged — "
                        f"check by hand whether they use '{old}'")
    return WiringEdit(src, changed, warnings)


def remove_in_wiring(src, name, key):
    """Drop `key` from the `local vars = {…}` initializer. Any `vars.key` code
    references are reported (not deleted): removing them changes game logic, so
    that is the author's call."""
    st = find_state_table(src)
    if st is None:
        return WiringEdit(src, 0, ["state table not found — wiring left untouched"])
    changed = 0
    if st.init_open is not None:
        body = src[st.init_open + 1:st.init_close]
        for k, ks, _ke in _iter_top_level_keys(body):
            if k == key:
                s, e = _entry_span(body, ks)
                # tidy the whitespace the cut leaves behind
                while s > 0 and body[s - 1] in " \t":
                    s -= 1
                if e < len(body) and body[e] == "\n" and s > 0 and body[s - 1] == "\n":
                    e += 1
                body = body[:s] + body[e:]
                src = src[:st.init_open + 1] + body + src[st.init_close:]
                changed += 1
                break
    warnings = []
    refs = len(re.findall(r"(?<![\w.])" + re.escape(name) + r"\." + re.escape(key) + r"\b", src))
    if refs:
        warnings.append(f"{refs} `{name}.{key}` reference(s) remain in the wiring "
                        f"code — remove or adjust that logic yourself")
    dyn = _dynamic_use_count(src, name)
    if dyn:
        warnings.append(f"{dyn} dynamic {name}[…] access(es) may use '{key}' — check by hand")
    return WiringEdit(src, changed, warnings)


def seed_in_wiring(src, name, key, initial):
    """Insert `key = <initial>` into the `local vars = {…}` initializer (at depth
    0). `initial` is a raw Lua literal ('false', '0', '""'). No-op with a warning
    if the key is already present or there is no initializer literal to edit."""
    st = find_state_table(src)
    if st is None:
        return WiringEdit(src, 0, ["state table not found — wiring left untouched"])
    if st.init_open is None:
        return WiringEdit(src, 0, [f"no `local {name} = {{…}}` literal to seed into"])
    body = src[st.init_open + 1:st.init_close]
    for k, _ks, _ke in _iter_top_level_keys(body):
        if k == key:
            return WiringEdit(src, 0, [f"'{key}' already declared in {name}"])

    entry = f"{key} = {initial}"
    inner = body.strip()
    if not inner:
        new_body = " " + entry + " "                       # {} -> { key = init }
    elif inner.endswith(","):
        new_body = body.rstrip() + " " + entry + ","       # keep trailing-comma style
    else:
        # match the existing single-line vs multi-line shape
        if "\n" in body:
            indent = re.match(r"[ \t]*", body.lstrip("\n")).group(0) or "  "
            new_body = body.rstrip() + ",\n" + indent + entry + "\n"
        else:
            new_body = body.rstrip() + ", " + entry
    src = src[:st.init_open + 1] + new_body + src[st.init_close:]
    return WiringEdit(src, 1, [])
