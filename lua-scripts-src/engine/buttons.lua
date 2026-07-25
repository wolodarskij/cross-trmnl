-- engine/buttons.lua — Xteink-aligned button action hints.
--
-- Placement (every screen that shows these hints):
--   Back     bottom-left, 5% width inset
--   Confirm  immediately after Back on the same baseline
--   Power    right edge, vertical, starting 15% from the top
--
--   local buttons = require("buttons")
--   buttons.draw{back = "Exit", confirm = "Talk", power = "Items"}
--
-- Actions are always Title Case. Set buttons.showButtonNames = true to
-- prefix with "Back — "/"Confirm — "/"Power — ".
--
-- The stacked Power caption is rendered by engine/buttonsv.lua, required only
-- when such a caption is actually drawn: it carries a font-metrics table that
-- costs about 5 KB on the device, and a script with no Power hint should not
-- pay for it. Ship engine/buttonsv.lua if your script uses `power`.

local M = {}

M.showButtonNames = false

-- "hide in gold" → "Hide In Gold" (never leave action words lowercase).
function M.titleCase(s)
  return (tostring(s):gsub("(%S)(%S*)", function(a, rest)
    return a:upper() .. rest:lower()
  end))
end

local function resolve(v)
  if type(v) == "function" then v = v() end
  if v == nil or v == false or v == "" then return nil end
  return M.titleCase(v)
end

function M.hint(btn, action, showNames)
  if showNames == nil then showNames = M.showButtonNames end
  action = resolve(action)
  if not action then return nil end
  if showNames then return btn .. " — " .. action end
  return action
end

-- Approximate UI_10 advance (no text-metrics API).
local function approxWidth(s)
  return #tostring(s) * 7
end

-- Draw Back / Confirm / Power in fixed device positions.
-- opts: {back=, confirm=, power=, showButtonNames=} — each string|fn|nil
function M.draw(opts)
  opts = opts or {}
  local showNames = opts.showButtonNames
  if showNames == nil then showNames = M.showButtonNames end

  local W, H = screen.width(), screen.height()
  -- Bottom-align captions in the footer band (game keeps the map above this).
  local yBot = H - 24
  local x = (W * 5) // 100
  screen.rect(0, H - 36, W, 36, true, false)  -- white footer strip

  local back = M.hint("Back", opts.back, showNames)
  if back then
    screen.text(x, yBot, back, {size = "small"})
    x = x + approxWidth(back) + 28
  end

  local confirm = M.hint("Confirm", opts.confirm, showNames)
  if confirm then
    screen.text(x, yBot, confirm, {size = "small"})
  end

  local power = M.hint("Power", opts.power, showNames)
  if power then
    -- Stacked captions need a small font-metrics table, so they live in
    -- engine/buttonsv.lua and are pulled in only when one is actually drawn —
    -- a script with no Power hint never pays for them. require() caches, so
    -- this costs a table lookup after the first draw.
    -- Center axis near the right edge (box sits inside the screen).
    require("buttonsv")(W - 12, (H * 15) // 100, power)
  end
end

return M
