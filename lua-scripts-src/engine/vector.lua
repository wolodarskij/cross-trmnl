-- engine/vector.lua — simple vector drawing + constrained SVG subset.
--
--   local vector = require("vector")
--
-- Immediate helpers (monochrome e-ink via screen.line / rect / pixel):
--   vector.line(x1, y1, x2, y2)
--   vector.rect(x, y, w, h [, fill])
--   vector.polyline(pts [, closed])   -- flat {x,y,...} or {{x,y},...}
--   vector.polygon(pts [, fill])      -- closed; fill draws a bounding rect
--   vector.circle(cx, cy, r [, fill]) -- mid-point / chord approx
--
-- Command lists scaled into a box (coords in unit space when used via shape):
--   vector.path(cmds, x, y, w, h)
--   cmds: { "line", x1,y1,x2,y2 }, { "rect", x,y,w,h [,fill] },
--         { "poly"|"polyline", {x,y,...} [,closed] }, { "circle", cx,cy,r [,fill] },
--         { "group", {tx=,ty=,sx=,sy=}, { ...nested cmds... } }
--
-- Tile-friendly shapes (normalized 0–1 coords inside the tile):
--   local tree = vector.shape{ { "poly", {...} }, { "rect", ... } }
--   draw = function(x, y, size) tree:draw(x, y, size) end
--
-- SVG subset (viewBox / width+height; rect, circle, line, polyline, polygon,
-- path M/L/H/V/Z; g with translate/scale only):
--   vector.svg(str, x, y, w, h)
--   vector.svgFile(path, x, y, w, h)
-- Both still work exactly as before, but the reader now lives in
-- engine/vectorsvg.lua and is only loaded when one of them is first touched —
-- it costs about 11 KB on the device, and most scripts never draw an SVG.
-- Ship engine/vectorsvg.lua alongside this file if your script uses them.

local M = {}

local MAX_PATH_PTS = 256

------------------------------------------------------------------------
-- Immediate primitives
------------------------------------------------------------------------

function M.line(x1, y1, x2, y2)
  screen.line(x1 // 1, y1 // 1, x2 // 1, y2 // 1)
end

function M.rect(x, y, w, h, fill)
  if w < 1 or h < 1 then return end
  screen.rect(x // 1, y // 1, w // 1, h // 1, fill)
end

-- Normalize points to a flat list of numbers.
local function flatten(pts)
  if type(pts[1]) == "table" then
    local out = {}
    for i = 1, #pts do
      out[#out + 1] = pts[i][1]
      out[#out + 1] = pts[i][2]
    end
    return out
  end
  return pts
end

function M.polyline(pts, closed)
  pts = flatten(pts)
  local n = #pts
  if n < 4 then return end
  for i = 1, n - 2, 2 do
    M.line(pts[i], pts[i + 1], pts[i + 2], pts[i + 3])
  end
  if closed then
    M.line(pts[n - 1], pts[n], pts[1], pts[2])
  end
end

function M.polygon(pts, fill)
  pts = flatten(pts)
  if #pts < 6 then return end
  if fill then
    local minx, miny, maxx, maxy = pts[1], pts[2], pts[1], pts[2]
    for i = 3, #pts, 2 do
      local x, y = pts[i], pts[i + 1]
      if x < minx then minx = x end
      if y < miny then miny = y end
      if x > maxx then maxx = x end
      if y > maxy then maxy = y end
    end
    M.rect(minx, miny, maxx - minx + 1, maxy - miny + 1, true)
  else
    M.polyline(pts, true)
  end
end

-- Mid-point circle (outline) or filled disc via horizontal spans.
function M.circle(cx, cy, r, fill)
  cx, cy, r = cx // 1, cy // 1, r // 1
  if r < 1 then
    screen.pixel(cx, cy)
    return
  end
  if fill then
    local x, y, d = r, 0, 1 - r
    while x >= y do
      screen.line(cx - x, cy + y, cx + x, cy + y)
      screen.line(cx - x, cy - y, cx + x, cy - y)
      screen.line(cx - y, cy + x, cx + y, cy + x)
      screen.line(cx - y, cy - x, cx + y, cy - x)
      y = y + 1
      if d < 0 then
        d = d + 2 * y + 1
      else
        x = x - 1
        d = d + 2 * (y - x) + 1
      end
    end
  else
    local x, y, d = r, 0, 1 - r
    while x >= y do
      screen.pixel(cx + x, cy + y)
      screen.pixel(cx - x, cy + y)
      screen.pixel(cx + x, cy - y)
      screen.pixel(cx - x, cy - y)
      screen.pixel(cx + y, cy + x)
      screen.pixel(cx - y, cy + x)
      screen.pixel(cx + y, cy - x)
      screen.pixel(cx - y, cy - x)
      y = y + 1
      if d < 0 then
        d = d + 2 * y + 1
      else
        x = x - 1
        d = d + 2 * (y - x) + 1
      end
    end
  end
end

------------------------------------------------------------------------
-- Transform stack (translate + scale) for path / SVG drawing
------------------------------------------------------------------------

local function identity()
  return { tx = 0, ty = 0, sx = 1, sy = 1 }
end

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
-- Command list drawing
------------------------------------------------------------------------

local function drawCmds(cmds, t)
  for i = 1, #cmds do
    local c = cmds[i]
    local kind = c[1]
    if kind == "line" then
      local x1, y1 = mapXY(t, c[2], c[3])
      local x2, y2 = mapXY(t, c[4], c[5])
      M.line(x1, y1, x2, y2)
    elseif kind == "rect" then
      local x, y = mapXY(t, c[2], c[3])
      M.rect(x, y, mapW(t, c[4]), mapH(t, c[5]), c[6])
    elseif kind == "poly" or kind == "polyline" then
      local pts = c[2]
      local flat = flatten(pts)
      local mapped = {}
      for j = 1, #flat, 2 do
        local mx, my = mapXY(t, flat[j], flat[j + 1])
        mapped[#mapped + 1] = mx
        mapped[#mapped + 1] = my
      end
      local closed = c[3]
      if kind == "poly" then closed = true end
      if c[4] then
        M.polygon(mapped, true)
      else
        M.polyline(mapped, closed)
      end
    elseif kind == "circle" then
      local cx, cy = mapXY(t, c[2], c[3])
      M.circle(cx, cy, mapR(t, c[4]), c[5])
    elseif kind == "group" then
      local g = c[2] or {}
      local nested = compose(t, {
        tx = g.tx or 0, ty = g.ty or 0,
        sx = g.sx or 1, sy = g.sy or 1,
      })
      drawCmds(c[3] or {}, nested)
    end
  end
end

-- Draw cmds with unit coords mapped into the pixel box (x,y,w,h).
function M.path(cmds, x, y, w, h)
  drawCmds(cmds, { tx = x, ty = y, sx = w, sy = h })
end

-- Shape object: normalized 0–1 commands, :draw(x, y, size) or :draw(x, y, w, h).
function M.shape(cmds)
  local s = { cmds = cmds }
  function s:draw(x, y, w, h)
    if h == nil then h = w end
    M.path(self.cmds, x, y, w, h)
  end
  return s
end

------------------------------------------------------------------------
-- SVG subset — loaded on demand
------------------------------------------------------------------------
-- The reader lives in engine/vectorsvg.lua and costs about 11 KB on the
-- device. Nothing above needs it, and most scripts only ever use the
-- primitives and shapes, so it is pulled in the first time svg / svgFile is
-- reached and never loaded otherwise. After the first touch the functions are
-- real fields on M, so this metamethod stops being consulted.

local svgLoaded = false

setmetatable(M, {
  __index = function(_, key)
    if svgLoaded or (key ~= "svg" and key ~= "svgFile") then return nil end
    svgLoaded = true
    require("vectorsvg")(M)   -- installs M.svg and M.svgFile
    return rawget(M, key)
  end,
})

return M
