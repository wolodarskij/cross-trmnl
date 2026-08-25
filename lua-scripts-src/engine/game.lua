-- engine/game.lua — the world/room shell shared by tile-based games.
--
-- A game provides data (usually from its own require-able data module) and
-- callbacks; the engine owns rooms, movement, rendering and the common
-- storytelling helpers.
--
--   local game = require("game")
--   local G = game.new{
--     sprites = "/scripts/mygame/",
--     tiles = {
--       ["#"] = {name = "wall", solid = true, draw = fn(x, y, size)},
--       ["E"] = {name = "elder", solid = true, sprite = "elder"},
--       ["D"] = {name = "dragon", solid = true, sprite = "dragon", big = 2},
--       ["m"] = {name = "mushroom", sprite = "mushroom"},  -- walkable
--     },
--     rooms = {{name = "Village", map = {...}, tiles = {...}? }, ...},
--     start = {room = 1, x = 6, y = 14},
--     -- Button hints via require("buttons") — same placement on every screen:
--     --   Back bottom-left (5% inset) · Confirm after Back
--     --   Power top-right, vertical, starting ~15% from top
--     footer = {
--       back = "Exit",                 -- action only; button name optional
--       confirm = function() ... end,  -- string or fn()->string|nil
--       power = "Items",
--     },
--     hud = {"optional top-right status"},  -- strings or fn()->string
--     playerSprite = function() return "player" end,
--   }
--   G:run{onUse = fn(ch), onPower = fn, onPickup = {m = fn(x, y)},
--         canMove = fn(nx,ny), onMoved = fn(nx,ny)}
--
-- buttons.showButtonNames (default false) toggles Back/Confirm/Power prefixes.
--
-- Tile `draw` may be a function(x, y, size) or per-room via room.tiles
-- overriding the global set. `big = 2` draws the sprite across 2x2 tiles
-- (anchor char at the top-left). A '.' on a map edge exits to the adjacent
-- room (rooms are a west-to-east chain).
--
-- Movement never rewrites map tiles: the player is drawn over the map, and
-- walkable scenery survives walking in and out. Exactly two things change
-- tiles — stepping on a char listed in `onPickup` (cleared to '.') and
-- `G:setTile` / dialog `mapset`. So never list a decorative char as an
-- onPickup key; that combination silently destroys the tile.

local tilemap = require("tilemap")
local buttons = require("buttons")

-- dialog is reached on demand rather than required here: it is only used by
-- :say / :ask, and a game that never opens a dialog should not compile the
-- renderer. Worth about 3 KB on the device for a map-only game; require()
-- caches, so a game that does talk pays one table lookup per call.
local function dialog()
  return require("dialog")
end

local M = {}

-- Prefer require("buttons").showButtonNames; this mirrors it for older call sites.
M.showButtonNames = false

function M.buttonHint(btn, action, showNames)
  return buttons.hint(btn, action, showNames)
end

function M.new(def)
  local footer = def.footer or {}
  if def.showButtonNames ~= nil then
    M.showButtonNames = def.showButtonNames
  end
  buttons.showButtonNames = M.showButtonNames
  local G = {
    sprites = def.sprites,
    tiles = def.tiles,
    rooms = def.rooms,
    hud = def.hud or {},
    footer = {
      back = footer.back or "Exit",
      confirm = footer.confirm,
      power = footer.power,
    },
    playerSprite = def.playerSprite or function() return "player" end,
    room = def.start.room,
    px = def.start.x,
    py = def.start.y,
  }

  -- Title band above the map; footer band below (Back / Confirm / toasts).
  -- Map tiles must never grow into the footer — that was overlapping captions.
  local TOP = 56
  local BOTTOM = 40

  local function layout()
    local room = G.rooms[G.room]
    G.cols = #room.map[1]
    G.rows = #room.map
    G.mapY = TOP
    G.tile = math.min(screen.width() // G.cols,
                      (screen.height() - TOP - BOTTOM) // G.rows)
    G.mapX = (screen.width() - G.cols * G.tile) // 2
    G.mapBottom = G.mapY + G.rows * G.tile
  end
  layout()

  -- solidity string for tilemap.new, derived from the tile definitions
  local function solidChars(tiles)
    local s = ""
    for ch, t in pairs(tiles) do
      if t.solid then s = s .. ch end
    end
    return s
  end

  -- tilemap.draw renderer table for a room (global tiles + room overrides)
  local function renderers(room)
    local out = {}
    local function add(ch, t)
      if t.sprite then
        if t.big then
          local big = t.big
          out[ch] = function(x, y, size)
            tilemap.sprite(G.sprites, t.sprite, x, y, size * big, size * big)
          end
        else
          out[ch] = t.sprite
        end
      elseif t.draw then
        out[ch] = t.draw
      end
    end
    for ch, t in pairs(G.tiles) do add(ch, t) end
    for ch, t in pairs(room.tiles or {}) do add(ch, t) end
    return out
  end

  function G:currentRoom() return self.rooms[self.room] end

  -- mode: refresh mode after entering ("full" default); "none" skips the
  -- refresh for games that overlay their own drawing before updating.
  function G:enterRoom(n, mode)
    self.room = n
    local room = self.rooms[n]
    local merged = {}
    for ch, t in pairs(self.tiles) do merged[ch] = t end
    for ch, t in pairs(room.tiles or {}) do merged[ch] = t end
    self.grid = tilemap.new(room.map, {solid = solidChars(merged)})
    layout()
    if mode ~= "none" then self:refresh(mode or "full") end
  end

  function G:drawFooter()
    buttons.showButtonNames = M.showButtonNames
    buttons.draw{
      back = self.footer.back,
      confirm = self.footer.confirm,
      power = self.footer.power,
    }
  end

  function G:draw()
    local room = self:currentRoom()
    screen.clear()
    screen.text(10, 8, room.name, {size = "medium", bold = true})
    local yHud = 6
    for _, line in ipairs(self.hud) do
      local text = line
      if type(line) == "function" then text = line() end
      if text and text ~= "" then
        screen.text(screen.width() - 10, yHud, text, {size = "small", align = "right"})
        yHud = yHud + 20
      end
    end
    screen.line(0, self.mapY - 6, screen.width(), self.mapY - 6)
    tilemap.draw(self.grid, {x = self.mapX, y = self.mapY, tile = self.tile,
                             sprites = self.sprites, tiles = renderers(room)})
    if self.overlay then self.overlay() end
    -- playerSprite may return a second value: a tile scale (2 = 2x2 tiles),
    -- drawn bottom-left anchored so a grown player still stands on its tile.
    local ps, big = self.playerSprite()
    local s = self.tile * (big or 1)
    tilemap.sprite(self.sprites, ps,
                   self.mapX + self.px * self.tile,
                   self.mapY + self.py * self.tile - (s - self.tile), s, s)
    self:drawFooter()
  end

  function G:refresh(mode)
    self:draw()
    screen.update(mode or "fast")
  end

  -- Event / pickup toasts: one small line, bottom-right of the footer band
  -- (same baseline as Back/Confirm — never on the map).
  function G:toast(text)
    self:draw()
    local W, H = screen.width(), screen.height()
    local xLeft = (W * 50) // 100
    local xRight = W - 8
    local maxW = xRight - xLeft
    local y = H - 24  -- bottom-aligned with button captions
    local cw, ch, pad = 7, 16, 3

    local line = tostring(text):gsub("\n", " ")
    local maxChars = math.max(4, maxW // cw)
    if #line > maxChars then
      line = line:sub(1, maxChars - 1) .. "…"
    end
    local tw = #line * cw
    local xPad = xRight - tw - pad
    if xPad < xLeft then xPad = xLeft end
    screen.rect(xPad, y - pad, xRight - xPad + pad, ch + pad * 2, true, false)
    screen.text(xRight, y, line, {size = "small", align = "right"})
    screen.update("fast")
  end

  -- Dialog with portraits: the player on the left, whoever they face right.
  -- Returns false if Back dismissed the dialog early. `locked` refuses Back,
  -- so the window can only be left with Confirm (and never returns false).
  -- Portraits are passed as defaults rather than written into the pages: the
  -- caller's tables often belong to a cached story module, and editing them
  -- would make that data grow permanently as the game is played.
  --
  -- `keep = true` skips the world repaint afterwards, for callers that will
  -- draw the next screen themselves — a dialog-tree walk shows window after
  -- window, and repainting the map between two windows costs a full e-ink
  -- flash to show a screen nobody asked for. A bare say keeps the repaint:
  -- with no caller taking over, the dialog would otherwise stay up forever.
  function G:say(pages, portrait, locked, keep)
    local ok = dialog().show(pages, {sprites = self.sprites, allowBack = not locked,
                                   left = self.playerSprite(), right = portrait})
    if not keep then self:refresh("full") end
    return ok
  end

  -- Returns the 1-based choice index, or nil if Back cancelled. `locked`
  -- refuses Back, so the player must pick one of the options.
  function G:ask(prompt, options, portrait, locked)
    return dialog().choose(prompt, options,
                         {sprites = self.sprites, allowBack = not locked,
                          left = self.playerSprite(), right = portrait})
  end

  -- Big sprite + title; Back (or Confirm) exits via device.exit().
  function G:endScreen(sprite, title, sub)
    screen.clear()
    local s = self.tile * 4
    tilemap.sprite(self.sprites, sprite, (screen.width() - s) // 2, screen.height() // 2 - s - 20, s, s)
    screen.text(screen.width() // 2, screen.height() // 2 + 30, title, {size = "large", bold = true, align = "center"})
    if sub then
      screen.text(screen.width() // 2, screen.height() // 2 + 80, sub, {align = "center"})
    end
    buttons.showButtonNames = M.showButtonNames
    buttons.draw{back = "Exit", confirm = "Exit"}
    screen.update("full")
    while true do
      local btn = input.wait()
      if btn == "back" or btn == "confirm" then device.exit() end
    end
  end

  -- Mushrooms creep in from the edges over several frames, then the title card.
  function G:mushroomCrawlEnding(ending)
    local cx = screen.width() // 2
    local cy = screen.height() // 2
    local s = math.max(self.tile * 2, 48)
    for frame = 1, 6 do
      screen.clear()
      local dist = (7 - frame) * (s * 3 // 4)
      tilemap.sprite(self.sprites, "mushroom", cx - dist - s, cy - s // 2, s, s)
      tilemap.sprite(self.sprites, "mushroom", cx + dist, cy - s // 2, s, s)
      tilemap.sprite(self.sprites, "mushroom", cx - s // 2, cy - dist - s, s, s)
      tilemap.sprite(self.sprites, "mushroom", cx - s // 2, cy + dist, s, s)
      tilemap.sprite(self.sprites, "player", cx - s // 2, cy - s // 2, s, s)
      screen.update("fast")
      device.sleep(400)
    end
    self:endScreen("mushroom", ending.title, ending.sub)
  end

  function G:playEnding(ending)
    if ending.style == "mushroom_crawl" then
      self:mushroomCrawlEnding(ending)
    else
      self:endScreen(ending.sprite, ending.title, ending.sub)
    end
  end

  -- Edit any room's map. Room maps are shared tables, so a change to another
  -- room is simply there when the player next enters it; changing the current
  -- room also updates the live grid (they alias the same lines).
  function G:setTile(roomN, x, y, ch)
    if roomN == self.room then
      self.grid:set(x, y, ch)
    else
      tilemap.new(self.rooms[roomN].map):set(x, y, ch)
    end
  end

  function G:adjacentTo(ch)
    for _, d in ipairs(tilemap.DIRS) do
      if self.grid:at(self.px + d[1], self.py + d[2]) == ch then
        return self.px + d[1], self.py + d[2]
      end
    end
  end

  -- First adjacent tile char in DIRS order (up, down, left, right), or nil if
  -- none of interest. ipairs: the scan order is part of the answer.
  function G:facingChar()
    for _, d in ipairs(tilemap.DIRS) do
      local ch = self.grid:at(self.px + d[1], self.py + d[2])
      if ch and ch ~= "." and ch ~= "#" then return ch end
    end
  end

  -- The standard explore loop. handlers:
  --   onUse(ch)      Confirm beside a tile char (first adjacent match wins);
  --                  if nothing adjacent handles it, called once with nil
  --   onPower()      the Power button (e.g. open a menu)
  --   onPickup[ch]   stepping onto char; tile is cleared first
  --   canMove(nx,ny) optional; return false to block a step (and room exits)
  --   onMoved(nx,ny) optional; after a successful in-room step
  function G:run(handlers)
    local onPickup = handlers.onPickup or {}
    local canMove = handlers.canMove
    local onMoved = handlers.onMoved
    self:enterRoom(self.room)
    while true do
      local btn = input.wait()
      if btn == "back" then
        local pick = self:ask({who = "* * *", text = "Exit the game?"}, {"Yes", "No"})
        if pick == 1 then device.exit() end
        self:refresh("full")
      elseif btn == "confirm" and handlers.onUse then
        local used = false
        for _, d in ipairs(tilemap.DIRS) do
          local ch = self.grid:at(self.px + d[1], self.py + d[2])
          if handlers.onUse(ch) then used = true; break end
        end
        if not used then handlers.onUse(nil) end
      elseif btn == "power" and handlers.onPower then
        handlers.onPower()
      else
        local nx, ny = tilemap.step(self.px, self.py, btn)
        if nx then
          if canMove and not canMove(nx, ny) then
            -- storm wall / custom clamp: stay put
          elseif nx < 0 and self.room > 1 then          -- exits: walk off the map edge
            self.px = self.cols - 1
            self:enterRoom(self.room - 1)
          elseif nx >= self.cols and self.room < #self.rooms
                 and self.grid:at(self.cols - 1, ny) ~= "#" then
            self.px = 0
            self:enterRoom(self.room + 1)
          elseif not self.grid:solid(nx, ny) then
            self.px, self.py = nx, ny
            local ch = self.grid:at(nx, ny)
            if onPickup[ch] then
              self.grid:set(nx, ny, ".")
              onPickup[ch](nx, ny)
            else
              self:refresh("fast")
            end
            if onMoved then onMoved(nx, ny) end
          end
        end
      end
    end
  end

  return G
end

return M
