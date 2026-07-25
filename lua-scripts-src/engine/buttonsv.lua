-- engine/buttonsv.lua — vertical (stacked) button captions, loaded on demand.
--
-- Only the Power hint is drawn vertically, and plenty of scripts never show
-- one, so engine/buttons.lua pulls this in the first time it actually draws a
-- vertical caption. Nothing else references it.
--
--   local drawVertical = require("buttonsv")
--   drawVertical(cx, y, "Items")
--
-- Font metrics are approximations of the firmware's small font — there is no
-- text-metrics API — used to size the white box behind the letters. `h` must
-- cover real ink (SMALL_FONT line height is ~14-16px); undersizing clips
-- letters past the box. `w` is the horizontal advance, used for centering.
--
-- The metrics are packed into two strings rather than a table of 57 small
-- {w=,h=} tables. That table cost about 5.8 KB on the device — 91% of this
-- module's data — because every entry is a Table header plus a 2-slot node
-- array. Two strings cost a few hundred bytes and hold the same numbers.

-- One character per ASCII 32..126; the byte stored is the advance width + 48,
-- which keeps the string printable and greppable.
local WIDTHS =
  "777777777777777775777777777777777877867884787:887877788:886777777776765774574:777756577:7767777"

-- Characters with descenders, which need the taller 16px cell.
local TALL = "Qgjpqy"

local DEFAULT_W, DEFAULT_H = 7, 14
local GAP = 2   -- explicit space between stacked letters (inside the box)
local PAD = 4

-- Returns width, height for one character — two values rather than a table,
-- because this runs twice per character per draw and returning a table here
-- would allocate on every call. A space is a half-height gap that draws nothing.
local function cellSize(ch)
  if ch == " " then return 0, DEFAULT_H // 2 + GAP end
  local b = ch:byte()
  local w = DEFAULT_W
  if b and b >= 32 and b <= 126 then
    w = WIDTHS:byte(b - 31) - 48
  end
  return w, (TALL:find(ch, 1, true) and 16 or DEFAULT_H)
end

-- Vertical label: box = max letter width x (sum of letter heights + gaps + pad).
-- Each letter is centered on the max-width column axis. Measured in one pass and
-- drawn in a second, so no per-character table is built.
return function(cx, y, text)
  local n = #text
  local maxW, totalH = 0, 0
  for i = 1, n do
    local w, h = cellSize(text:sub(i, i))
    if w > maxW then maxW = w end
    totalH = totalH + h
    if i < n then totalH = totalH + GAP end
  end
  if maxW < 1 then maxW = DEFAULT_W end

  local boxW = maxW + PAD * 2 + GAP * 2
  local boxH = totalH + PAD * 2 + GAP
  local boxX = cx - boxW // 2
  local boxY = y - GAP
  screen.rect(boxX, boxY, boxW, boxH, true, false)  -- white background
  screen.rect(boxX, boxY, boxW, boxH, false)        -- black outline

  local cy = y
  for i = 1, n do
    local ch = text:sub(i, i)
    local w, h = cellSize(ch)
    if ch ~= " " then
      screen.text(cx - w // 2, cy, ch, {size = "small"})
    end
    cy = cy + h
    if i < n then cy = cy + GAP end
  end
end
