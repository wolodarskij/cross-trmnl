-- lib/patrol.lua — back-and-forth patrols with facing and line of sight,
-- for GO-style turn-based stealth. Works with a lib/tilemap grid.
--
--   local patrol = require("patrol")
--   local enemies = patrol.new({{path = {{1, 3}, {2, 3}, {3, 3}}}, ...})
--   patrol.step(enemies)                    -- one step each, ping-pong
--   patrol.seen(enemies, grid, 2, col, row) -- caught? (same tile or in sight)
--
-- Each enemy has .x/.y, and .fx/.fy — the direction of its last step.

local M = {}

-- specs: {{path = {{col, row}, ...}}, ...}; paths are walked back and forth.
function M.new(specs)
  local enemies = {}
  for _, spec in ipairs(specs) do
    local e = {path = spec.path, idx = 1, dir = 1}
    e.x, e.y = spec.path[1][1], spec.path[1][2]
    e.fx = spec.path[2][1] - e.x  -- initial facing: toward the next node
    e.fy = spec.path[2][2] - e.y
    enemies[#enemies + 1] = e
  end
  return enemies
end

function M.step(enemies)
  for _, e in ipairs(enemies) do
    local ni = e.idx + e.dir
    if ni < 1 or ni > #e.path then
      e.dir = -e.dir
      ni = e.idx + e.dir
    end
    local to = e.path[ni]
    e.fx, e.fy = to[1] - e.x, to[2] - e.y
    e.idx, e.x, e.y = ni, to[1], to[2]
  end
end

-- Tiles the enemy watches: up to `sight` tiles straight ahead, stopped by
-- solid tiles. Returns {{col, row}, ...}.
function M.vision(e, grid, sight)
  local tiles = {}
  local x, y = e.x, e.y
  for _ = 1, sight do
    x, y = x + e.fx, y + e.fy
    if grid:solid(x, y) then break end
    tiles[#tiles + 1] = {x, y}
  end
  return tiles
end

-- True if (col, row) is on an enemy or inside any enemy's line of sight.
function M.seen(enemies, grid, sight, col, row)
  for _, e in ipairs(enemies) do
    if e.x == col and e.y == row then return true end
    for _, t in ipairs(M.vision(e, grid, sight)) do
      if t[1] == col and t[2] == row then return true end
    end
  end
  return false
end

return M
