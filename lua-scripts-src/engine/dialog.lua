-- lib/dialog.lua — full-screen, book-style dialog pages and choices.
--
-- Each page is {who = "Speaker", "line one", "line two", ...}, optionally
-- with portrait sprite names: left (usually the protagonist) and right
-- (whoever they face). Portraits need opts = {sprites = "/scripts/game/"}.
--
--   local dialog = require("dialog")
--   dialog.show({{who = "Elder", left = "player", right = "elder",
--                 "Hello, knight!"}}, {sprites = SPR})
--   local pick = dialog.choose("Rest where?", {"Gold", "Dirt"}, opts)
--
-- Confirm turns pages / picks; up/down move the chooser. Back closes the
-- dialog (show → false, choose → nil). The caller redraws afterwards.
--
-- A window can refuse to be closed: opts.allowBack = false makes Back do
-- nothing and hides its footer caption, so the only way out is Confirm.
-- Use it for windows the player must answer (a fight round); leave it unset
-- everywhere else. Only an explicit false locks — nil means allowed.
-- Button hints use require("buttons") placement on every page.

local tilemap = require("tilemap")
local buttons = require("buttons")

local M = {}

local PORTRAIT = 80

-- Portraits + speaker name + rule line; returns the y where body text starts.
--
-- left/right/who fall back to opts, so a caller can supply defaults for a whole
-- run of pages without writing them into the page tables — those tables usually
-- belong to a cached story module, and editing them makes it grow as you play.
local function drawTop(page, opts)
  local w = screen.width()
  local left = page.left or (opts and opts.left)
  local right = page.right or (opts and opts.right)
  local who = page.who or (opts and opts.who) or ""
  local hasPortraits = opts and opts.sprites and (left or right)
  if hasPortraits then
    if left then
      tilemap.sprite(opts.sprites, left, 40, 50, PORTRAIT, PORTRAIT)
    end
    if right then
      tilemap.sprite(opts.sprites, right, w - 40 - PORTRAIT, 50, PORTRAIT, PORTRAIT)
    end
    screen.text(w // 2, 150, who, {size = "large", bold = true, align = "center"})
    screen.line(60, 200, w - 60, 200)
    return 230
  end
  screen.text(w // 2, 90, who, {size = "large", bold = true, align = "center"})
  screen.line(60, 140, w - 60, 140)
  return 180
end

-- Page body: either page.text (lines joined with "\n" — cheapest in RAM,
-- preferred for data modules) or a list of line strings.
local function drawBody(page, y)
  local function line(ln)
    screen.text(40, y, ln)
    y = y + 36
  end
  if page.text then
    for ln in (page.text .. "\n"):gmatch("(.-)\n") do line(ln) end
  else
    for _, ln in ipairs(page) do line(ln) end
  end
  return y
end

-- Back caption for a window: nil when it refuses to close, so no affordance
-- is drawn for a button that does nothing (buttons.draw skips a nil hint).
local function backHint(opts)
  if opts and opts.allowBack == false then return nil end
  return "Close"
end

-- Returns true if all pages were shown, false if Back dismissed early
-- (never false when opts.allowBack == false).
function M.show(pages, opts)
  local canClose = not (opts and opts.allowBack == false)
  for _, page in ipairs(pages) do
    screen.clear()
    drawBody(page, drawTop(page, opts))
    buttons.draw{back = backHint(opts), confirm = "Continue"}
    screen.update("fast")
    while true do
      local btn = input.wait()
      if btn == "confirm" then break end
      if btn == "back" and canClose then return false end
    end
  end
  return true
end

-- Full-screen choice. Returns the 1-based index, or nil if Back closed it
-- (never nil when opts.allowBack == false — the player must pick something).
function M.choose(prompt, options, opts)
  if type(prompt) == "string" then prompt = {who = prompt} end
  local canClose = not (opts and opts.allowBack == false)
  local sel = 1
  while true do
    screen.clear()
    local y = drawBody(prompt, drawTop(prompt, opts))
    y = y + 12
    for i, label in ipairs(options) do
      if i == sel then screen.rect(44, y + 6, 10, 10, true) end
      screen.text(70, y, buttons.titleCase(label))
      y = y + 36
    end
    buttons.draw{back = backHint(opts), confirm = "Choose"}
    screen.update("fast")

    local btn = input.wait()
    if btn == "up" then
      sel = (sel - 2) % #options + 1
    elseif btn == "down" then
      sel = sel % #options + 1
    elseif btn == "confirm" then
      return sel
    elseif btn == "back" and canClose then
      return nil
    end
  end
end

return M
