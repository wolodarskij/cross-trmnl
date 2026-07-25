-- lib/tilemap.lua — ASCII tile maps, sprite drawing, 4-direction movement.
--
--   local tilemap = require("tilemap")
--   local grid = tilemap.new(lines, {solid = "#EW"})
--   grid:at(col, row)      -- tile char ("#" outside the map)
--   grid:solid(col, row)   -- true if the tile blocks movement
--   grid:find("C")         -- col, row of the first such char
--
-- Drawing (see tilemap.draw below) maps each tile char to either a sprite
-- name, a renderer function, or false (draw nothing).

local M = {}

-- Both an array (ipairs: up, down, left, right — a fixed scan order) and a
-- name map (DIRS[btn]). The array part is not convenience, it is correctness:
-- "first adjacent tile" walkers must scan deterministically, and pairs() order
-- follows the string-hash seed, which Lua re-randomizes every boot — so a
-- player standing between two interesting tiles would face a different one
-- each power-on. Iterate DIRS with ipairs, never pairs.
local UP, DOWN, LEFT, RIGHT = {0, -1}, {0, 1}, {-1, 0}, {1, 0}
M.DIRS = {UP, DOWN, LEFT, RIGHT, up = UP, down = DOWN, left = LEFT, right = RIGHT}

-- Target tile for a direction button, or nil if btn is not a direction.
function M.step(col, row, btn)
  local d = M.DIRS[btn]
  if not d then return nil end
  return col + d[1], row + d[2]
end

function M.new(lines, opts)
  opts = opts or {}
  local g = {lines = lines, cols = #lines[1], rows = #lines, solidChars = opts.solid or "#"}

  function g:at(col, row)
    if col < 0 or col >= self.cols or row < 0 or row >= self.rows then return "#" end
    return self.lines[row + 1]:sub(col + 1, col + 1)
  end

  function g:solid(col, row)
    return self.solidChars:find(self:at(col, row), 1, true) ~= nil
  end

  -- Change a tile (e.g. picked-up items, opened passages). Mutates the map
  -- lines in place, so the change persists if the same table is re-loaded.
  function g:set(col, row, ch)
    if col < 0 or col >= self.cols or row < 0 or row >= self.rows then return end
    local line = self.lines[row + 1]
    self.lines[row + 1] = line:sub(1, col) .. ch .. line:sub(col + 2)
  end

  function g:find(ch)
    for row = 0, self.rows - 1 do
      for col = 0, self.cols - 1 do
        if self:at(col, row) == ch then return col, row end
      end
    end
  end

  return g
end

-- Draw a sprite BMP from `dir`; if the file is missing, draw a labelled box
-- so the game stays visible without its assets.
function M.sprite(dir, name, x, y, w, h)
  if not screen.image(dir .. name .. ".bmp", x, y, w, h) then
    screen.rect(x + 2, y + 2, w - 4, h - 4)
    screen.text(x + w // 2, y + h // 2 - 10, name:sub(1, 1):upper(), {align = "left"})
  end
end

-- Draw the whole grid.
--   opts.x, opts.y   top-left of the map on screen
--   opts.tile        tile size in pixels
--   opts.sprites     sprite directory (e.g. "/scripts/mygame/")
--   opts.tiles       {[char] = "spritename" | function(x, y, size) | false}
--   opts.floor       optional function(x, y, size) for chars not in opts.tiles
function M.draw(g, opts)
  local tiles = opts.tiles or {}
  for row = 0, g.rows - 1 do
    local y = opts.y + row * opts.tile
    for col = 0, g.cols - 1 do
      local x = opts.x + col * opts.tile
      local r = tiles[g:at(col, row)]
      if r == nil then
        if opts.floor then opts.floor(x, y, opts.tile) end
      elseif type(r) == "string" then
        M.sprite(opts.sprites, r, x, y, opts.tile, opts.tile)
      elseif type(r) == "function" then
        r(x, y, opts.tile)
      end  -- false: draw nothing
    end
  end
end

return M
