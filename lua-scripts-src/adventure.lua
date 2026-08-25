-- adventure.lua — "The Sword of Ember Peak", a small branching fantasy
-- adventure with four endings, some of them mushrooms.
--
--   Arrows walk. Confirm talks to / uses whatever you stand beside.
--   Power opens Items: Left/Right picks an item, Up/Down picks a deed,
--   Confirm does it. Back closes dialogs/Items; on the map it asks before exit.
--
-- This file is only wiring. The story is a dialog tree in
-- adventure/dialogs.lua, items are adventure/items.lua, the world is
-- adventure/world.lua — all running on the shared /scripts/engine/.

local game = require("game")
local menu = require("menu")
local dtree = require("dialogtree")
local tilemap = require("tilemap")
local world = require("adventure.world")
local dialogs = require("adventure.dialogs")
local items = require("adventure.items")

local SPR = "/scripts/adventure/"
local STORM_R = 5  -- Chebyshev radius lit during the thunderstorm

-- Story variables: everything the dialog tree reads and writes.
local vars = {mushrooms = 0}

local G

local function inStormRadius(col, row)
  local dx = col - (vars.stormX or 6)
  local dy = row - (vars.stormY or 4)
  if dx < 0 then dx = -dx end
  if dy < 0 then dy = -dy end
  return (dx > dy and dx or dy) <= STORM_R
end

local function syncStormFog()
  if vars.storm then
    G.overlay = function()
      local t = G.tile
      for row = 0, G.rows - 1 do
        for col = 0, G.cols - 1 do
          if not inStormRadius(col, row) then
            screen.rect(G.mapX + col * t, G.mapY + row * t, t, t, true)
          end
        end
      end
    end
  else
    G.overlay = nil
  end
end

local function runNode(start)
  dtree.run(G, dialogs, start, vars)
  syncStormFog()
  G:refresh("full")
end

-- Which dialog actor answers a Confirm press, by adjacent tile char.
local ACTORS = {E = "elder", W = "wizard", D = "dragon", d = "dragon",
                G = "hoard", C = "cat", S = "stone", R = "stone_empty", n = "note"}

local function peekAdjacent()
  for _, d in ipairs(tilemap.DIRS) do
    local ch = G.grid:at(G.px + d[1], G.py + d[2])
    if ch and ch ~= "." and ch ~= "#" then return ch end
  end
end

-- Action label only; game.footer adds "Confirm — …" when showButtonNames.
local function confirmHint()
  if vars.dragonCoda then return nil end
  local ch = peekAdjacent()
  if vars.storm then
    if ch == "G" then return "Hide In Gold" end
    if ch == "K" or ch == "k" then return "Hide In Carcass" end
    if not ch then return "Lie Down" end
    return nil
  end
  if not ch then return nil end
  if ch == "D" or ch == "d" then
    -- The dragon can always be fought; without the sword you just have worse
    -- answers to it (see the fight_* nodes in adventure/dialogs.lua).
    if not vars.dragonSlain then return "Fight" end
    return "Talk"
  end
  if ch == "E" or ch == "W" or ch == "C" then return "Talk" end
  if ch == "S" then
    if vars.elixir then return "Pull The Sword" end
    return "Examine"
  end
  if ch == "R" then return "Examine" end
  if ch == "n" then return "Read" end
  if ch == "G" then return "Inspect" end
  if ACTORS[ch] then return "Interact" end
  return nil
end

local function confirmRest(prompt, node)
  local pick = G:ask({who = "* * *", text = prompt}, {"Yes", "No"})
  if pick == 1 then
    runNode(node)
  else
    syncStormFog()
    G:refresh("full")
  end
end

local function showItems()
  if vars.dragonForm then
    G:toast("Your claws are full of gold.")
    return
  end
  if vars.dragonCoda then
    G:toast("No time. Go tell the elder!")
    return
  end
  local list = {}
  for _, it in ipairs(items) do
    if dtree.check(it.owned, vars, G) then
      local actions = {}
      for _, a in ipairs(it.actions) do
        if dtree.check(a.when, vars, G) then
          actions[#actions + 1] = {label = a.label, fn = function() runNode(a.node) end}
        end
      end
      local name = it.name:gsub("{(%w+)}", function(k) return tostring(vars[k] or 0) end)
      list[#list + 1] = {name = name, sprite = it.sprite, actions = actions}
    end
  end
  if not menu.show{title = "~ ITEMS ~", sprites = SPR, empty = "(no items)", items = list} then
    G:refresh("full")
  end
end

G = game.new{
  sprites = SPR,
  tiles = world.tiles,
  rooms = world.rooms,
  start = world.start,
  footer = {
    back = "Exit",
    confirm = confirmHint,
    power = "Items",
  },
  playerSprite = function()
    if vars.dragonBig then return "dragon", 2 end   -- the full-sized problem
    if vars.dragonForm then return "dragon" end
    if vars.isMushroom then return "mushroom" end
    if vars.sword then return "player_sword" end
    return "player"                                 -- incl. the coda's human morning
  end,
}

G:enterRoom(G.room)
runNode(dialogs.talk.intro)

G:run{
  onUse = function(ch)
    if vars.dragonCoda then return true end
    if vars.storm then
      if ch == "G" then
        confirmRest("Hide inside the gold pile?", "rest_gold")
        return true
      end
      if ch == "K" or ch == "k" then
        confirmRest("Crawl inside the dragon carcass?", "rest_corpse")
        return true
      end
      if ch == nil then
        confirmRest("Lie down in the mud?", "rest_dirt")
        return true
      end
      return false
    end
    local actor = ACTORS[ch]
    if actor then
      runNode(dialogs.talk[actor])
      return true
    end
  end,
  canMove = function(nx, ny)
    if vars.stormPending then
      vars.stormPending = false
      runNode("storm")
    end
    if vars.dragonCoda then
      if nx < 0 or ny < 0 or nx >= G.cols or ny >= G.rows then return false end
      return not G.grid:solid(nx, ny)
    end
    if not vars.storm then return true end
    if nx < 0 or ny < 0 or nx >= G.cols or ny >= G.rows then return false end
    return inStormRadius(nx, ny)
  end,
  onMoved = function()
    if vars.dragonCoda then
      vars.dragonSteps = (vars.dragonSteps or 0) + 1
      if vars.dragonSteps == 3 then
        runNode("coda_turn")                -- gloves stop fitting
      elseif vars.dragonSteps == 6 then
        runNode("coda_big")                 -- wings; carries the ending
      end
    end
  end,
  onPower = showItems,
  onPickup = {
    m = function()
      vars.mushrooms = vars.mushrooms + 1
      G:toast("Mushroom " .. vars.mushrooms .. "/3")
    end,
    v = function()
      vars.vial = true
      G:toast("Reincarnation vial")
    end,
  },
}
