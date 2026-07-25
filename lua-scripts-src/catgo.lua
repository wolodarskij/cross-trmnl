-- catgo.lua — "NINE LIVES", a turn-based stealth puzzle in the spirit of
-- Hitman GO, very loosely after a certain Stephen King short story.
-- You are the cat. Reach the old man. Do not be seen.
--
--   Arrows step one tile; every step you take, Halston takes one too.
--   Confirm waits a turn (you will need this). Dotted tiles are watched —
--   end your turn on one and the gun finds you. Back asks before exit.
--
-- Built on the shared engine in /scripts/engine/ (game, patrol, dialog,
-- tilemap). Levels live in catgo/levels.lua; sprites in /scripts/catgo/.
-- The turn loop is game-specific: GO rules don't fit the explore loop.

local game = require("game")
local patrol = require("patrol")
local dialog = require("dialog")
local tilemap = require("tilemap")
local data = require("catgo.levels")

local SPR = "/scripts/catgo/"
local SIGHT = 2  -- how many tiles ahead an enemy watches

local enemies, moves = {}, 0

local G = game.new{
  sprites = SPR,
  tiles = data.tiles,
  rooms = data.levels,
  start = {room = 1, x = 0, y = 0},  -- real cat position set per level
  footer = { confirm = "Wait" },
  playerSprite = function() return "cat" end,
}

local function resetLevel(li)
  G:enterRoom(li, "none")
  G.px, G.py = G.grid:find("C")
  moves = 0
  enemies = patrol.new(data.levels[li].enemies)
end

local function drawBoard(li)
  G:draw()
  screen.text(screen.width() - 10, 6, "L" .. li .. "   Moves " .. moves, {size = "small", align = "right"})

  for _, e in ipairs(enemies) do                               -- watched tiles
    for _, t in ipairs(patrol.vision(e, G.grid, SIGHT)) do
      local cx = G.mapX + t[1] * G.tile + G.tile // 2
      local cy = G.mapY + t[2] * G.tile + G.tile // 2
      screen.rect(cx - 4, cy - 4, 8, 8, true)
    end
  end
  for _, e in ipairs(enemies) do
    local x, y = G.mapX + e.x * G.tile, G.mapY + e.y * G.tile
    tilemap.sprite(SPR, "hitman", x, y, G.tile, G.tile)
    local cx, cy = x + G.tile // 2, y + G.tile // 2            -- facing tick
    screen.line(cx + e.fx * (G.tile // 2 - 6), cy + e.fy * (G.tile // 2 - 6),
                cx + e.fx * (G.tile // 2 + 2), cy + e.fy * (G.tile // 2 + 2), 3)
  end
end

local function playLevel(li)
  dialog.show(data.levels[li].story)
  resetLevel(li)
  drawBoard(li)
  screen.update("full")
  while true do
    local btn = input.wait()
    if btn == "back" then
      local pick = dialog.choose({who = "* * *", text = "Exit the game?"}, {"Yes", "No"})
      if pick == 1 then device.exit() end
      drawBoard(li)
      screen.update("full")
    else
    local turn = false
    local nx, ny = tilemap.step(G.px, G.py, btn)
    if nx and not G.grid:solid(nx, ny) then
      G.px, G.py = nx, ny
      turn = true
    elseif btn == "confirm" then
      turn = true  -- wait in place; Halston still moves
    end
    if turn then
      moves = moves + 1
      if G.grid:at(G.px, G.py) == "X" then return end          -- made it
      patrol.step(enemies)
      if patrol.seen(enemies, G.grid, SIGHT, G.px, G.py) then
        dialog.show(data.caught)
        resetLevel(li)
        drawBoard(li)
        screen.update("full")
      else
        drawBoard(li)
        screen.update(moves % 20 == 0 and "full" or "fast")
      end
    end
    end
  end
end

for li = 1, #data.levels do
  playLevel(li)
end
dialog.show(data.ending)
G:endScreen("cat", "NINE LIVES")
