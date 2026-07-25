-- engine/screenfit.lua — draw for one device, run on any.
--
-- A game declares the device (or resolution) it was designed for; if the
-- real panel differs, the global `screen` is replaced by a proxy that
-- scales every coordinate. `screen.width()`/`screen.height()` keep
-- reporting the DESIGN resolution, so all layout math in the game and the
-- engine is untouched.
--
--   require("screenfit").apply{device = "x4"}
--   require("screenfit").apply{width = 480, height = 800, mode = "stretch"}
--
-- Settings (all optional):
--   device  "x4" (480x800, the default) or "x3" (528x792)
--   width/height  explicit design resolution instead of `device`
--   mode    "fit"      uniform scale, centered with margins (default)
--           "stretch"  scale x and y independently, fills the panel
--           "off"      never scale (declare-only)
--
-- Text uses the named sizes (small/medium/large) unscaled — panel sizes
-- are close enough that layout, not glyph size, is what needs fixing.
-- The untouched screen table stays available as `screenfit.raw`.

local M = {}

M.DEVICES = {
  x4 = {width = 480, height = 800},
  x3 = {width = 528, height = 792},
}

M.raw = nil  -- the real screen table, set by apply() when scaling kicks in

function M.apply(opts)
  opts = opts or {}
  local design = M.DEVICES[opts.device or "x4"] or M.DEVICES.x4
  local dw = opts.width or design.width
  local dh = opts.height or design.height
  local W, H = screen.width(), screen.height()
  if (W == dw and H == dh) or opts.mode == "off" then return false end

  local sx, sy, ox, oy = W / dw, H / dh, 0, 0
  if opts.mode ~= "stretch" then  -- "fit": uniform, centered
    local s = math.min(sx, sy)
    sx, sy = s, s
    ox = (W - dw * s) // 2
    oy = (H - dh * s) // 2
  end
  local function X(x) return math.floor(x * sx + ox) end
  local function Y(y) return math.floor(y * sy + oy) end

  local raw = screen
  M.raw = raw
  screen = {
    width = function() return dw end,
    height = function() return dh end,
    clear = raw.clear,
    update = raw.update,
    invert = raw.invert,
    text = function(x, y, str, o) return raw.text(X(x), Y(y), str, o) end,
    line = function(x1, y1, x2, y2, w)
      return raw.line(X(x1), Y(y1), X(x2), Y(y2), w or 1)
    end,
    rect = function(x, y, w, h, fill)
      if fill == nil then fill = false end
      return raw.rect(X(x), Y(y), math.floor(w * sx), math.floor(h * sy), fill)
    end,
    pixel = function(x, y, on)
      if on == nil then on = true end
      return raw.pixel(X(x), Y(y), on)
    end,
    image = function(path, x, y, w, h)
      return raw.image(path, X(x), Y(y), math.floor((w or dw) * sx), math.floor((h or dh) * sy))
    end,
  }
  return true
end

return M
