-- engine/menu.lua — a generic item-carousel menu (inventory screens).
--
--   local menu = require("menu")
--   menu.show{
--     title = "~ ITEMS ~",
--     sprites = "/scripts/mygame/",
--     empty = "(empty pockets)",
--     items = {
--       {name = "Red mushroom x2", sprite = "mushroom",
--        actions = {{label = "Eat one", fn = eatOne}}},
--     },
--   }
--
-- Left/Right cycles items, Up/Down cycles the current item's actions,
-- Confirm runs the selected action (and closes the menu — actions draw
-- their own outcome), Power closes. Returns true if an action ran.
--
-- allowBack = false makes the menu refuse to close (Back and Power do
-- nothing, and neither caption is drawn) — for menus the player must act on.
-- Only an explicit false locks; nil means allowed, as inventories are.
-- Button hints use require("buttons") placement.

local tilemap = require("tilemap")
local buttons = require("buttons")

local M = {}

function M.show(def)
  local w = screen.width()
  local title = def.title or "~ ITEMS ~"
  local items = def.items or {}
  local canClose = def.allowBack ~= false
  local closeHint = canClose and "Close" or nil

  -- An empty menu always closes: with nothing to choose, refusing would
  -- strand the player.
  if #items == 0 then
    screen.clear()
    screen.text(w // 2, 60, title, {size = "large", bold = true, align = "center"})
    screen.line(60, 110, w - 60, 110)
    screen.text(w // 2, 200, def.empty or "(nothing)", {align = "center"})
    buttons.draw{back = "Close", power = "Close"}
    screen.update("fast")
    while true do
      local btn = input.wait()
      if btn == "back" or btn == "power" then return false end
    end
  end

  local ii, ai = 1, 1
  while true do
    local it = items[ii]
    local acts = type(it.actions) == "function" and it.actions() or it.actions
    if ai > #acts then ai = #acts end

    screen.clear()
    screen.text(w // 2, 60, title, {size = "large", bold = true, align = "center"})
    screen.line(60, 110, w - 60, 110)
    if it.sprite and def.sprites then
      tilemap.sprite(def.sprites, it.sprite, (w - 80) // 2, 140, 80, 80)
    end
    screen.text(w // 2, 240, "< " .. it.name .. " >", {bold = true, align = "center"})
    screen.text(w // 2, 274, ii .. " of " .. #items, {size = "small", align = "center"})
    local y = 330
    for i, a in ipairs(acts) do
      if i == ai then screen.rect(44, y + 6, 10, 10, true) end
      screen.text(70, y, buttons.titleCase(a.label))
      y = y + 36
    end
    screen.text(w // 2, screen.height() - 70, "Left/Right Item   Up/Down Deed",
                {size = "small", align = "center"})
    buttons.draw{back = closeHint, confirm = "Do", power = closeHint}
    screen.update("fast")

    local btn = input.wait()
    if (btn == "back" or btn == "power") and canClose then return false end
    if btn == "left" then
      ii = (ii - 2) % #items + 1
      ai = 1
    elseif btn == "right" then
      ii = ii % #items + 1
      ai = 1
    elseif btn == "up" then
      ai = (ai - 2) % #acts + 1
    elseif btn == "down" then
      ai = ai % #acts + 1
    elseif btn == "confirm" then
      acts[ai].fn()
      return true
    end
  end
end

return M
