-- paint.lua — a small drawing pad. Shows an interactive input loop, the four
-- ink levels, a hand-rolled tools panel, and BMP round-tripping with
-- screen.save / screen.image.
--
--   Arrows       move the cursor — half the brush size per press (1..8 px);
--                hold to drag at the full brush size per screen refresh
--   Confirm      stamp the brush in the foreground ink
--   Back         stamp the brush in the background ink (the eraser)
--   Power        open the tools panel (inks, brush, New / Open / Save)
--   Back (hold)  exit — only the hold ends the script. It is a firmware
--                invariant and cannot be intercepted, so save first.
--
-- Pictures live in /scripts/paint/*.bmp (1-bit, written by screen.save).
-- Open lists that folder, so any BMP dropped there can be edited too.
--
-- The panel is 1 bit per pixel; the two grays are ordered dither keyed on
-- absolute screen position, so a stamp's look depends on where it lands and a
-- single light-gray pixel is inked only one time in four. The tools panel
-- draws every swatch with the real thing, so you see that before you stamp.
--
-- There is no way to read the framebuffer back from Lua, so a cursor cannot be
-- erased without destroying whatever it covered. Instead every stamp is
-- remembered as one packed integer and the picture is replayed from scratch on
-- each move — which is also why the cursor never leaves a trail. Save flattens
-- that: the picture goes to a BMP, the stamp list resets, and the file becomes
-- the layer the next stamps draw over.

local W, H = screen.width(), screen.height()
local TOP = 40                  -- status band; the canvas is everything below
local MAX_STAMPS = 2000         -- ~16 KB of Lua heap, and bounds redraw cost
local DIR = "/scripts/paint"    -- where pictures live

-- The firmware's four levels, lightest first (GfxRenderer.h `enum Color`).
local INKS = {"white", "lightgray", "darkgray", "black"}
local INK_NAME = {"White", "Light grey", "Dark grey", "Black"}

-- Brush = shape x size; sizes are the brush width in pixels, powers of two up
-- to 32. A square stamps as one rect; circle row widths are computed once per
-- size below, on a half-offset grid (cell centers at +-0.5, +-1.5, ...) so
-- even diameters stay round where the naive integer form collapses.
local SIZES = {1, 2, 4, 8, 16, 32}
local SHAPE_NAME = {"Square", "Circle"}

local CIRCLE = {}               -- CIRCLE[sizeIndex] = row widths, top to bottom
for si, n in ipairs(SIZES) do
  local rows, c, r2 = {}, (n + 1) / 2, n * n / 4
  for i = 1, n do
    local dy, count = i - c, 0
    for j = 1, n do
      local dx = j - c
      if dx * dx + dy * dy <= r2 then count = count + 1 end
    end
    rows[i] = count
  end
  CIRCLE[si] = rows
end

local fg, bg, shape, size = 4, 1, 1, 3   -- black on white, 4 px square
local stamps = {}   -- packed: x | y<<10 | (shape-1)<<20 | (size-1)<<21 | (ink-1)<<24
local full = false  -- MAX_STAMPS reached
local canvas = nil  -- BMP drawn under the stamps (set by Open and Save)
local curFile = nil -- name inside DIR that Save overwrites
local note = nil    -- one-shot band message (Saved / Opened / ...), cleared on input

local function drawShape(cx, cy, sh, si, ink)
  local n = SIZES[si]
  if sh == 1 then
    screen.rect(cx - n // 2, cy - n // 2, n, n, true, ink)
  else
    local rows = CIRCLE[si]
    local y0 = cy - n // 2
    for i = 1, n do
      local w = rows[i]
      screen.rect(cx - w // 2, y0 + i - 1, w, 1, true, ink)
    end
  end
end

local function redraw(cx, cy)
  screen.clear(INKS[bg])
  if canvas then screen.image(canvas, 0, TOP, W, H - TOP) end
  for i = 1, #stamps do
    local v = stamps[i]
    drawShape(v & 1023, (v >> 10) & 1023, ((v >> 20) & 1) + 1,
              ((v >> 21) & 7) + 1, INKS[((v >> 24) & 3) + 1])
  end

  -- Status band, kept white so it stays readable over any background.
  screen.rect(0, 0, W, TOP, true, "white")
  screen.line(0, TOP - 1, W, TOP - 1)
  local left = note or (INK_NAME[fg] .. " / " .. SHAPE_NAME[shape] .. " " .. SIZES[size]
                        .. (full and "  (full - save to continue)" or ""))
  screen.text(8, 10, left, {size = "small"})
  screen.text(W - 8, 10, "Power: tools", {size = "small", align = "right"})

  if cx then  -- cursor: two hollow rings clear of the brush, as in life.lua
    local n = SIZES[size]
    local x0, y0 = cx - n // 2 - 3, cy - n // 2 - 3
    screen.rect(x0, y0, n + 6, n + 6)
    screen.rect(x0 - 2, y0 - 2, n + 10, n + 10)
  end
end

-- Save the canvas region (not the band, not the cursor) as a 1-bit BMP. On
-- success the file becomes the layer under future stamps and the replay list
-- resets — so saving also compacts, and a long session never stays full.
local function doSave()
  fs.mkdir(DIR)
  if not curFile then
    local n = 1
    while fs.exists(DIR .. "/paint-" .. n .. ".bmp") do n = n + 1 end
    curFile = "paint-" .. n .. ".bmp"
  end
  local path = DIR .. "/" .. curFile
  redraw(nil)  -- replay without the cursor: the file must hold only the picture
  if not screen.save(path, 0, TOP, W, H - TOP) then return "Save failed" end
  canvas, stamps, full = path, {}, false
  return "Saved " .. curFile
end

-- Shared white panel with title rule; returns its top-left and width.
local function panel(title, ph)
  local pw = W - 60
  local px, py = 30, (H - ph) // 2
  screen.rect(px, py, pw, ph, true, "white")
  screen.rect(px, py, pw, ph)
  screen.text(W // 2, py + 16, title, {size = "large", bold = true, align = "center"})
  screen.line(px + 12, py + 56, px + pw - 12, py + 56)
  return px, py, pw
end

-- Pick a .bmp from DIR. Returns the name, or nil (and a band message) on
-- cancel / empty folder.
local function pickFile()
  local names = {}
  for _, n in ipairs(fs.list(DIR)) do
    if n:sub(-4) == ".bmp" then names[#names + 1] = n end
  end
  table.sort(names)
  if #names == 0 then return nil, "Nothing in " .. DIR end
  local visible = math.min(10, (H - 220) // 40)  -- panel must fit landscape too
  local sel, top = 1, 1
  local ph = 56 + visible * 40 + 60
  while true do
    local px, py = panel("OPEN", ph)
    for i = 0, math.min(visible, #names - top + 1) - 1 do
      local idx = top + i
      local y = py + 70 + i * 40
      if idx == sel then screen.text(px + 14, y, ">") end
      screen.text(px + 36, y, names[idx], {size = "small"})
    end
    screen.text(W // 2, py + ph - 30, "Confirm open   Back cancel",
                {size = "small", align = "center"})
    screen.update("fast")

    local btn = input.wait()
    if btn == "confirm" then return names[sel] end
    if btn == "back" or btn == "power" then return nil end
    if btn == "up" then
      sel = (sel - 2) % #names + 1
    elseif btn == "down" then
      sel = sel % #names + 1
    end
    if sel < top then top = sel end
    if sel > top + visible - 1 then top = sel - visible + 1 end
  end
end

-- The tools panel: value rows cycled with Left/Right, action rows fired with
-- Confirm. Returns "new" / "open" / "save", or nil for plain close. A short
-- Back is delivered to the script and never ends it, so a modal only has to
-- return from its own loop.
local function tools()
  local ROWS = {"Foreground", "Background", "Shape", "Size", "New", "Open", "Save"}
  local sel = 1
  local ph = 56 + #ROWS * 40 + 64
  while true do
    local px, py = panel("TOOLS", ph)
    for i = 1, #ROWS do
      local y = py + 70 + (i - 1) * 40
      if i == sel then screen.text(px + 14, y, ">") end
      screen.text(px + 36, y, ROWS[i], {size = "small"})
      local bx, by, bw, bh = px + 168, y - 4, 34, 26
      local label
      if i <= 3 then
        -- Every swatch shows the real thing: the inks over the current
        -- background, the brush as it stamps (capped to what the box holds).
        screen.rect(bx, by, bw, bh, true, INKS[bg])
        screen.rect(bx, by, bw, bh)
        if i == 1 then
          screen.rect(bx + 4, by + 4, bw - 8, bh - 8, true, INKS[fg])
          label = INK_NAME[fg]
        elseif i == 2 then
          label = INK_NAME[bg]
        else
          local psi = size
          while SIZES[psi] > bh - 6 do psi = psi - 1 end
          drawShape(bx + bw // 2, by + bh // 2, shape, psi, INKS[fg])
          label = SHAPE_NAME[shape]
        end
      elseif i == 4 then
        label = SIZES[size] .. " px"
      elseif i == 7 and curFile then
        label = curFile  -- what Save will overwrite
      end
      if label then screen.text(bx + bw + 12, y, label, {size = "small"}) end
    end
    screen.text(W // 2, py + ph - 52, "Up/Down row   Left/Right value",
                {size = "small", align = "center"})
    screen.text(W // 2, py + ph - 30, "Confirm select   Back closes   Back (hold) exits",
                {size = "small", align = "center"})
    screen.update("fast")

    local btn = input.wait()
    if btn == "back" or btn == "power" then return nil end
    if btn == "confirm" then
      if sel == 5 then return "new" end
      if sel == 6 then return "open" end
      if sel == 7 then return "save" end
      return nil  -- Confirm on a value row closes, as the old panel did
    end
    if btn == "up" then
      sel = (sel - 2) % #ROWS + 1
    elseif btn == "down" then
      sel = sel % #ROWS + 1
    elseif btn == "left" or btn == "right" then
      local d = (btn == "right") and 1 or -1
      if sel == 1 then
        fg = (fg - 1 + d) % #INKS + 1
      elseif sel == 2 then
        bg = (bg - 1 + d) % #INKS + 1
      elseif sel == 3 then
        shape = (shape - 1 + d) % #SHAPE_NAME + 1
      elseif sel == 4 then
        size = (size - 1 + d) % #SIZES + 1
      end
    end
  end
end

local x, y = W // 2, (TOP + H) // 2
local moves = 0

local DIRS = {up = {0, -1}, down = {0, 1}, left = {-1, 0}, right = {1, 0}}

local function move(d, px)
  x = math.min(math.max(x + d[1] * px, 6), W - 6)
  y = math.min(math.max(y + d[2] * px, TOP + 6), H - 6)
end

local function stamp(ink)
  if #stamps < MAX_STAMPS then
    stamps[#stamps + 1] = x | (y << 10) | ((shape - 1) << 20)
                            | ((size - 1) << 21) | ((ink - 1) << 24)
  else
    full = true
  end
end

redraw(x, y)
screen.update("full")

while true do
  local btn = input.wait()
  local mode = "fast"
  note = nil

  if DIRS[btn] then
    -- A tap moves half the brush (never less than 1 px, never more than 8);
    -- holding the button drags on at the full brush size, one step per screen
    -- refresh, so the panel's own update rate is what paces the drag.
    local d = DIRS[btn]
    local n = SIZES[size]
    move(d, math.min(math.max(n // 2, 1), 8))
    while true do
      moves = moves + 1
      redraw(x, y)
      screen.update(moves % 40 == 0 and "full" or "fast")
      if not input.down(btn) then break end
      move(d, n)
    end
  else
    if btn == "power" then
      local act = tools()
      if act == "new" then
        stamps, full, canvas, curFile = {}, false, nil, nil
        note = "New picture"
      elseif act == "open" then
        local name, err = pickFile()
        if name then
          stamps, full = {}, false
          canvas, curFile = DIR .. "/" .. name, name
          note = "Opened " .. name
        else
          note = err
        end
      elseif act == "save" then
        note = doSave()
      end
      mode = "full"  -- the panel covered the canvas; repaint it cleanly
    elseif btn == "confirm" then
      stamp(fg)
    elseif btn == "back" then
      stamp(bg)  -- the eraser; hold Back to exit instead
    end

    -- e-ink accumulates ghosting under partial refreshes; clear it
    -- periodically, as life.lua does between generations.
    moves = moves + 1
    if moves % 40 == 0 then mode = "full" end

    redraw(x, y)
    screen.update(mode)
  end
end
