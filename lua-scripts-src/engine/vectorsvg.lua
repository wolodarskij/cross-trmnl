-- engine/vectorsvg.lua — the SVG subset of engine/vector.lua, loaded on demand.
--
-- Splitting this out is a memory decision, not a stylistic one: on the device
-- this reader costs about 11 KB of a 96 KB budget, and neither shipped game
-- draws an SVG. require("vector") no longer pays for it; the first access to
-- vector.svg / vector.svgFile pulls this module in.
--
--   local vector = require("vector")
--   vector.svgFile("/scripts/logo.svg", 20, 20, 200, 200)   -- loads this
--
-- This module returns an INSTALLER rather than a table: vector.lua calls it
-- with its own module table, so the code below keeps referring to M exactly as
-- it did when it lived in vector.lua, and reaches the primitives (M.line,
-- M.rect, M.polyline, M.polygon, M.circle) through it. Taking M as an argument
-- also avoids requiring vector.lua from inside vector.lua's own metamethod.
--
-- The five transform helpers are duplicated from vector.lua rather than
-- exported by it. They are one-liners, and exporting them would widen vector's
-- public API permanently to serve this one caller.

return function(M)

local MAX_PATH_PTS = 256

local function mapXY(t, x, y)
  return t.tx + x * t.sx, t.ty + y * t.sy
end

local function mapW(t, w) return w * t.sx end
local function mapH(t, h) return h * t.sy end
local function mapR(t, r) return r * ((t.sx + t.sy) * 0.5) end

local function compose(parent, child)
  return {
    tx = parent.tx + child.tx * parent.sx,
    ty = parent.ty + child.ty * parent.sy,
    sx = parent.sx * child.sx,
    sy = parent.sy * child.sy,
  }
end

------------------------------------------------------------------------
-- Tiny XML / number helpers for SVG
------------------------------------------------------------------------

local function trim(s)
  return (s:match("^%s*(.-)%s*$"))
end

local function tonumberSafe(s)
  if not s then return nil end
  return tonumber((s:gsub("[px%%]", "")))
end

local function attr(tag, name)
  -- name="value" or name='value'
  local v = tag:match("%s" .. name .. '%s*=%s*"([^"]*)"')
    or tag:match("%s" .. name .. "%s*=%s*'([^']*)'")
  return v
end

local function attrNum(tag, name, default)
  local v = tonumberSafe(attr(tag, name))
  if v == nil then return default end
  return v
end

local function wantsFill(tag)
  local f = attr(tag, "fill")
  if f == nil then return false end  -- stroke-only default for e-ink icons
  f = trim(f):lower()
  if f == "none" or f == "transparent" then return false end
  return true
end

local function wantsStroke(tag)
  local s = attr(tag, "stroke")
  if s == nil then return not wantsFill(tag) end
  s = trim(s):lower()
  return s ~= "none"
end

-- Parse "translate(a[,b]) scale(a[,b])" into tx,ty,sx,sy. Ignore other forms.
local function parseTransform(s)
  local t = { tx = 0, ty = 0, sx = 1, sy = 1 }
  if not s then return t end
  for kind, args in s:gmatch("(%w+)%s*%(([^)]*)%)") do
    local nums = {}
    for n in args:gmatch("[-+]?%d*%.?%d+") do
      nums[#nums + 1] = tonumber(n)
    end
    kind = kind:lower()
    if kind == "translate" then
      t.tx = t.tx + (nums[1] or 0)
      t.ty = t.ty + (nums[2] or 0)
    elseif kind == "scale" then
      local sx = nums[1] or 1
      local sy = nums[2] or sx
      t.sx = t.sx * sx
      t.sy = t.sy * sy
      t.tx = t.tx * sx
      t.ty = t.ty * sy
    end
  end
  return t
end

local function parsePoints(s)
  local pts = {}
  if not s then return pts end
  for n in s:gmatch("[-+]?%d*%.?%d+") do
    pts[#pts + 1] = tonumber(n)
    if #pts >= MAX_PATH_PTS * 2 then break end
  end
  return pts
end

-- Parse path d= into a flat polyline list of moves/lines (absolute coords).
-- Returns a list of subpaths, each a flat {x,y,...}. Unsupported cmds skipped.
local function parsePathD(d)
  local subpaths = {}
  if not d then return subpaths end
  local tokens = {}
  for tok in d:gmatch("[MmLlHhVvZz]|[-+]?%d*%.?%d+") do
    tokens[#tokens + 1] = tok
  end
  local i = 1
  local cx, cy, sx, sy = 0, 0, 0, 0
  local cur = nil
  local function push(x, y)
    if not cur then
      cur = {}
      subpaths[#subpaths + 1] = cur
    end
    if #cur >= MAX_PATH_PTS * 2 then return end
    cur[#cur + 1] = x
    cur[#cur + 1] = y
  end
  local function nextNum()
    local n = tonumber(tokens[i])
    if n then i = i + 1 end
    return n
  end
  while i <= #tokens do
    local t = tokens[i]
    i = i + 1
    if t == "M" or t == "m" then
      local rel = (t == "m")
      local x, y = nextNum(), nextNum()
      if not x or not y then break end
      if rel then x, y = cx + x, cy + y end
      cur = nil
      push(x, y)
      cx, cy, sx, sy = x, y, x, y
      -- subsequent pairs are implicit L/l
      while i <= #tokens and tonumber(tokens[i]) do
        local lx, ly = nextNum(), nextNum()
        if not lx or not ly then break end
        if rel then lx, ly = cx + lx, cy + ly end
        push(lx, ly)
        cx, cy = lx, ly
      end
    elseif t == "L" or t == "l" then
      local rel = (t == "l")
      while i <= #tokens and tonumber(tokens[i]) do
        local x, y = nextNum(), nextNum()
        if not x or not y then break end
        if rel then x, y = cx + x, cy + y end
        push(x, y)
        cx, cy = x, y
      end
    elseif t == "H" or t == "h" then
      local rel = (t == "h")
      while i <= #tokens and tonumber(tokens[i]) do
        local x = nextNum()
        if not x then break end
        if rel then x = cx + x end
        push(x, cy)
        cx = x
      end
    elseif t == "V" or t == "v" then
      local rel = (t == "v")
      while i <= #tokens and tonumber(tokens[i]) do
        local y = nextNum()
        if not y then break end
        if rel then y = cy + y end
        push(cx, y)
        cy = y
      end
    elseif t == "Z" or t == "z" then
      if cur and #cur >= 4 then
        push(sx, sy)
      end
      cx, cy = sx, sy
      cur = nil
    else
      -- Unknown command letter (C/Q/A/…): skip following numbers
      if t:match("^[A-Za-z]$") then
        while i <= #tokens and tonumber(tokens[i]) do i = i + 1 end
      end
      -- else: stray number already consumed as command — ignore
    end
  end
  return subpaths
end

------------------------------------------------------------------------
-- SVG element drawing (immediate; no DOM)
------------------------------------------------------------------------

local function drawSvgElement(tag, name, t)
  name = name:lower()
  local localT = parseTransform(attr(tag, "transform"))
  t = compose(t, localT)
  local fill = wantsFill(tag)
  local stroke = wantsStroke(tag)

  if name == "rect" then
    local x = attrNum(tag, "x", 0)
    local y = attrNum(tag, "y", 0)
    local w = attrNum(tag, "width", 0)
    local h = attrNum(tag, "height", 0)
    local px, py = mapXY(t, x, y)
    if fill then
      M.rect(px, py, mapW(t, w), mapH(t, h), true)
    end
    if stroke or not fill then
      M.rect(px, py, mapW(t, w), mapH(t, h), false)
    end
  elseif name == "circle" then
    local cx = attrNum(tag, "cx", 0)
    local cy = attrNum(tag, "cy", 0)
    local r = attrNum(tag, "r", 0)
    local px, py = mapXY(t, cx, cy)
    M.circle(px, py, mapR(t, r), fill)
  elseif name == "line" then
    local x1, y1 = mapXY(t, attrNum(tag, "x1", 0), attrNum(tag, "y1", 0))
    local x2, y2 = mapXY(t, attrNum(tag, "x2", 0), attrNum(tag, "y2", 0))
    M.line(x1, y1, x2, y2)
  elseif name == "polyline" or name == "polygon" then
    local pts = parsePoints(attr(tag, "points"))
    local mapped = {}
    for i = 1, #pts, 2 do
      local mx, my = mapXY(t, pts[i], pts[i + 1])
      mapped[#mapped + 1] = mx
      mapped[#mapped + 1] = my
    end
    if name == "polygon" and fill then
      M.polygon(mapped, true)
    else
      M.polyline(mapped, name == "polygon")
    end
  elseif name == "path" then
    local subs = parsePathD(attr(tag, "d"))
    for s = 1, #subs do
      local pts = subs[s]
      local mapped = {}
      for i = 1, #pts, 2 do
        local mx, my = mapXY(t, pts[i], pts[i + 1])
        mapped[#mapped + 1] = mx
        mapped[#mapped + 1] = my
      end
      if fill and #mapped >= 6 then
        M.polygon(mapped, true)
      else
        M.polyline(mapped, false)
      end
    end
  end
  -- g / svg / unknown: no geometry
end

local function parseViewBox(svgTag)
  local vb = attr(svgTag, "viewBox") or attr(svgTag, "viewbox")
  if vb then
    local nums = parsePoints(vb)
    if #nums >= 4 then
      return nums[1], nums[2], nums[3], nums[4]
    end
  end
  local w = attrNum(svgTag, "width", 100)
  local h = attrNum(svgTag, "height", 100)
  return 0, 0, w, h
end

-- Walk tag opens in document order; maintain a transform stack for <g>.
function M.svg(str, x, y, w, h)
  if not str or w < 1 or h < 1 then return false end

  -- Find root <svg ...>
  local svgOpen = str:match("<[Ss][Vv][Gg][^>]*>")
  if not svgOpen then return false end
  local minx, miny, vbw, vbh = parseViewBox(svgOpen)
  if vbw == 0 or vbh == 0 then return false end

  -- Map viewBox → destination box
  local root = {
    tx = x - minx * (w / vbw),
    ty = y - miny * (h / vbh),
    sx = w / vbw,
    sy = h / vbh,
  }

  -- Stack of transforms for nested <g>
  local stack = { root }
  local depth = 1

  -- Iterate all tags (open, close, self-close)
  for full in str:gmatch("<[^>]+>") do
    if full:match("^<%?") or full:match("^<!") then
      -- skip
    elseif full:match("^</") then
      local cname = full:match("^</%s*([%w:]+)")
      if cname and cname:lower() == "g" and depth > 1 then
        depth = depth - 1
      end
    else
      local name = full:match("^<%s*([%w:]+)")
      if name then
        local lname = name:lower()
        local selfClose = full:match("/%s*>$") ~= nil
        if lname == "g" then
          local parent = stack[depth]
          local child = parseTransform(attr(full, "transform"))
          depth = depth + 1
          stack[depth] = compose(parent, child)
          if selfClose then
            depth = depth - 1
          end
        elseif lname ~= "svg" then
          drawSvgElement(full, lname, stack[depth])
        end
      end
    end
  end
  return true
end

function M.svgFile(path, x, y, w, h)
  local body = fs.read(path)
  if not body then return false end
  return M.svg(body, x, y, w, h)
end

end
