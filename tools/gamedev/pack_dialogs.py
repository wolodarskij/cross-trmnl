#!/usr/bin/env python3
"""Pack a dialog-tree module: prose to a sidecar .bin, nodes into lazy arcs.

The authored dialogs.lua is the source of truth and is never modified — this
reads it (through Lua, like the editor and simulator do) and generates a
memory-lean equivalent next to a text sidecar:

    out/dialogs.lua        core: talk lists + always-needed nodes + lazy-arc
                           metatable + T.text resolver (engine/textpack.lua)
    out/dialogs_<arc>.lua  nodes moved out of the core, loaded on first visit
    out/dialogs.bin        every page's text, concatenated; pages hold integer
                           refs (offset*4096 + len) resolved at display time

Memory effect on the device: prose costs RAM only while a page is on screen
(read back by fs.readRange), a page that held only text is one integer instead
of a table, and an arc the player never reaches is never loaded at all.

The generated tree is plain Lua — the simulator runs it exactly like the
authored one, which is how the two are proven equivalent (identical frames on
identical routes).

    python tools/gamedev/pack_dialogs.py lua-scripts-src/adventure/dialogs.lua \\
        -o lua-scripts-stage/adventure --require-prefix adventure \\
        --arc fight:fight_ --arc coda:storm,coda_,ghost,rest_,dragon_eats_,dragon_slay_scene

Arc terms: `name_` (trailing underscore) matches as a prefix, anything else
matches exactly. Loading an arc too eagerly can never break the game — it only
costs the memory it would have cost anyway — so arcs are safe to cut coarsely.
"""

import argparse
import os
import sys

try:
    from lupa import lua54
except ImportError:
    sys.exit("lupa is required: pip install lupa")

# The whole transform runs in Lua: the data already lives there, table
# semantics (array vs hash parts, key types) are native instead of bridged,
# and # on a Lua string counts bytes — which is what the .bin offsets must be.
TRANSFORM = r"""
local T, arcs, binRel, reqPrefix, srcName = ...

-- ---- serializer -----------------------------------------------------------
-- Deterministic: array part in order (entry order is semantics — pickEntry
-- stops at the first match), hash keys sorted. %q plus the gsub keeps
-- multi-line strings on one line.
local KEYWORDS = {
  ["and"]=1,["break"]=1,["do"]=1,["else"]=1,["elseif"]=1,["end"]=1,["false"]=1,
  ["for"]=1,["function"]=1,["goto"]=1,["if"]=1,["in"]=1,["local"]=1,["nil"]=1,
  ["not"]=1,["or"]=1,["repeat"]=1,["return"]=1,["then"]=1,["true"]=1,
  ["until"]=1,["while"]=1,
}

local function quote(s)
  return (string.format("%q", s):gsub("\\\n", "\\n"))
end

local function keyStr(k)
  if type(k) == "string" and k:match("^[%a_][%w_]*$") and not KEYWORDS[k] then
    return k .. " = "
  elseif type(k) == "string" then
    return "[" .. quote(k) .. "] = "
  else
    return "[" .. string.format("%d", k) .. "] = "
  end
end

local function valStr(v, indent)
  local t = type(v)
  if t == "string" then return quote(v) end
  if t == "boolean" then return tostring(v) end
  if t == "number" then
    assert(math.type(v) == "integer", srcName .. ": non-integer number in data: " .. tostring(v))
    return string.format("%d", v)
  end
  if t == "table" then
    local pad = string.rep("  ", indent + 1)
    local parts = {}
    local n = #v
    for i = 1, n do
      parts[#parts + 1] = pad .. valStr(v[i], indent + 1) .. ","
    end
    local keys = {}
    for k in pairs(v) do
      if not (type(k) == "number" and k >= 1 and k <= n and math.type(k) == "integer") then
        assert(type(k) == "string", srcName .. ": non-string hash key: " .. tostring(k))
        keys[#keys + 1] = k
      end
    end
    table.sort(keys)
    for _, k in ipairs(keys) do
      parts[#parts + 1] = pad .. keyStr(k) .. valStr(v[k], indent + 1) .. ","
    end
    if #parts == 0 then return "{}" end
    return "{\n" .. table.concat(parts, "\n") .. "\n" .. string.rep("  ", indent) .. "}"
  end
  error(srcName .. ": unsupported value type '" .. t .. "' — dialogs data must be pure (no functions)")
end

-- ---- text extraction ------------------------------------------------------
local chunks, binLen = {}, 0
local seen = {}  -- text -> ref; identical lines share one record

local function refFor(text)
  local ref = seen[text]
  if ref then return ref end
  local len = #text
  assert(len < 4096, srcName .. ": a page text is " .. len .. " bytes; the ref format stops at 4095")
  ref = binLen * 4096 + len
  chunks[#chunks + 1] = text
  binLen = binLen + len
  seen[text] = ref
  return ref
end

-- A page whose only field is text collapses to the bare ref; otherwise the
-- table stays and only its text becomes a ref. Prompts are pages here too.
local function packPage(page)
  if type(page) ~= "table" or type(page.text) ~= "string" then return page end
  local only = true
  for k in pairs(page) do
    if k ~= "text" then only = false break end
  end
  local ref = refFor(page.text)
  if only then return ref end
  page.text = ref
  return page
end

local function packNode(node)
  if type(node) ~= "table" then return end
  if node.pages then
    for i, p in ipairs(node.pages) do node.pages[i] = packPage(p) end
  end
  if node.prompt then node.prompt = packPage(node.prompt) end
  if node.branch then
    for _, e in ipairs(node.branch) do packNode(e) end
  end
end

-- Walk in sorted order so the .bin layout is deterministic run to run.
local names = {}
for k in pairs(T) do names[#names + 1] = k end
table.sort(names)
for _, name in ipairs(names) do
  local v = T[name]
  if name ~= "speakers" and type(v) == "table" then
    if name == "talk" then
      local actors = {}
      for a in pairs(v) do actors[#actors + 1] = a end
      table.sort(actors)
      for _, a in ipairs(actors) do
        for _, entry in ipairs(v[a]) do packNode(entry) end
      end
    else
      packNode(v)
    end
  end
end

-- ---- arc split ------------------------------------------------------------
local function arcOf(name)
  for _, arc in ipairs(arcs) do
    for _, term in ipairs(arc.terms) do
      if term:sub(-1) == "_" and name:sub(1, #term) == term then return arc end
      if name == term then return arc end
    end
  end
end

local arcNodes = {}   -- arc.name -> {nodeName -> node}
local coreNames = {}
for _, name in ipairs(names) do
  local arc = (name ~= "speakers" and name ~= "talk") and arcOf(name) or nil
  if arc then
    arcNodes[arc.name] = arcNodes[arc.name] or {}
    arcNodes[arc.name][name] = T[name]
  else
    coreNames[#coreNames + 1] = name
  end
end

-- ---- emit -----------------------------------------------------------------
local head = "-- GENERATED by tools/gamedev/pack_dialogs.py from " .. srcName .. "\n" ..
             "-- DO NOT EDIT: edit the source and re-pack. Prose lives in " .. binRel .. ".\n"

local core = {head, "local T = {}\n"}
core[#core + 1] = 'T.text = require("textpack").open("/scripts/' .. binRel .. '")\n'
for _, name in ipairs(coreNames) do
  core[#core + 1] = "T." .. name .. " = " .. valStr(T[name], 0) .. "\n"
end

if #arcs > 0 then
  core[#core + 1] = "-- Nodes matching these terms live in arc modules, required on first visit\n" ..
                    "-- and merged in. A route that never reaches an arc never pays for it.\n"
  core[#core + 1] = "local ARCS = {\n"
  for _, arc in ipairs(arcs) do
    local terms = {}
    for _, term in ipairs(arc.terms) do terms[#terms + 1] = quote(term) end
    core[#core + 1] = '  {' .. table.concat(terms, ", ") .. ', mod = "' ..
                      reqPrefix .. "." .. arc.module .. '"},\n'
  end
  core[#core + 1] = "}\n"
  core[#core + 1] = [[
setmetatable(T, {__index = function(t, k)
  for i = 1, #ARCS do
    local a = ARCS[i]
    for j = 1, #a do
      local term = a[j]
      if (term:sub(-1) == "_" and k:sub(1, #term) == term) or k == term then
        for name, node in pairs(require(a.mod)) do rawset(t, name, node) end
        return rawget(t, k)
      end
    end
  end
end})
]]
end
core[#core + 1] = "return T\n"

local out = {core = table.concat(core), bin = table.concat(chunks), arcs = {}}
for _, arc in ipairs(arcs) do
  local nodes = arcNodes[arc.name]
  assert(nodes and next(nodes), srcName .. ": arc '" .. arc.name .. "' matched no nodes")
  local buf = {head, "return {\n"}
  local ns = {}
  for n in pairs(nodes) do ns[#ns + 1] = n end
  table.sort(ns)
  for _, n in ipairs(ns) do
    buf[#buf + 1] = "  " .. n .. " = " .. valStr(nodes[n], 1) .. ",\n"
  end
  buf[#buf + 1] = "}\n"
  out.arcs[arc.module] = table.concat(buf)
end
return out
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="authored dialogs .lua module (read, never written)")
    ap.add_argument("-o", "--out", required=True, help="output directory (e.g. lua-scripts-stage/adventure)")
    ap.add_argument("--require-prefix", required=True,
                    help="require() prefix for arc modules (e.g. 'adventure' for /scripts/adventure/*)")
    ap.add_argument("--arc", action="append", default=[], metavar="NAME:TERMS",
                    help="split matching nodes into <base>_<NAME>.lua; TERMS comma-separated, "
                         "'x_' matches as prefix, anything else exactly")
    args = ap.parse_args()

    src = os.path.abspath(args.source)
    out = os.path.abspath(args.out)
    src_dir = os.path.dirname(src)
    # The authored tree is the source of truth; generated files must never
    # shadow it in place (same guard as compile_tree.py).
    if out == src_dir or out.startswith(src_dir + os.sep):
        sys.exit(f"refusing to write generated files into the source tree ({src_dir})")

    base = os.path.splitext(os.path.basename(src))[0]
    bin_rel = f"{args.require_prefix.replace('.', '/')}/{base}.bin"

    lua = lua54.LuaRuntime()
    with open(src, encoding="utf-8") as f:
        tree = lua.execute(f.read())
    if tree is None:
        sys.exit(f"{args.source}: module returned nothing")

    arcs = lua.table_from([], recursive=False)
    for i, spec in enumerate(args.arc, 1):
        name, _, terms = spec.partition(":")
        if not terms:
            sys.exit(f"--arc {spec}: expected NAME:term[,term...]")
        arcs[i] = lua.table_from({
            "name": name,
            "module": f"{base}_{name}",
            "terms": lua.table_from([t for t in terms.split(",") if t]),
        }, recursive=False)

    transform = lua.execute(f"return function(...)\n{TRANSFORM}\nend")
    result = transform(tree, arcs, bin_rel, args.require_prefix, os.path.basename(src))

    os.makedirs(out, exist_ok=True)
    written = []

    def emit(name, text, binary=False):
        path = os.path.join(out, name)
        if binary:
            with open(path, "wb") as f:
                f.write(text.encode("utf-8"))
        else:
            with open(path, "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
        written.append((name, os.path.getsize(path)))

    emit(f"{base}.lua", result["core"])
    emit(f"{base}.bin", result["bin"], binary=True)
    for module, text in sorted(dict(result["arcs"]).items()):
        emit(f"{module}.lua", text)

    src_size = os.path.getsize(src)
    total = sum(s for _, s in written)
    for name, size in written:
        print(f"  {name:<28} {size:>7,} B")
    print(f"  {'(authored source)':<28} {src_size:>7,} B -> {total:,} B generated, "
          f"prose {next(s for n, s in written if n.endswith('.bin')):,} B on card")
    print(f"  -> {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
