-- meminfo.lua — how much memory this device gives a script, and what this
-- script is using of it. Run it from the script browser to read the numbers
-- the firmware derives its budget from.
--
-- The budget is not a fixed number. It is the free heap at launch, minus a
-- reserve held back for rendering, the SD layer and the failure screen, then
-- clamped to a policy band. "Held back" below is that subtraction as actually
-- applied: when it does not match the reserve in Settings, the band decided
-- the limit rather than the heap — and those two want opposite fixes.
--
-- The free-heap figure is whatever was free at *this* launch. Every script
-- reaches the sandbox the same way, so it is the number any game would see,
-- but it moves with what ran before it.
--
-- Deliberately no require(): a memory read-out that pulled in the engine
-- would be reporting its own weight.

local used, peak, limit, freeHeap = device.mem()

local W = screen.width()
local H = screen.height()
local MARGIN = 30

local function kb(n)
  return string.format("%.1f KB", n / 1024)
end

local rows = {}
local function row(label, value)
  rows[#rows + 1] = {label, value}
end

-- The simulator has no real heap and reports 0. Say so, rather than doing
-- arithmetic on a zero and presenting the result as a measurement.
if freeHeap and freeHeap > 0 then
  row("Free heap at launch", kb(freeHeap))
  row("Held back", kb(freeHeap - limit))
else
  row("Free heap at launch", "n/a (simulator)")
end
row("Allowed a script", kb(limit))
row("Used right now", kb(used))
row("Peak this run", kb(peak))

screen.clear()
screen.text(W // 2, 60, "Script memory", {size = "large", bold = true, align = "center"})

local y = 130
for _, r in ipairs(rows) do
  screen.text(MARGIN, y, r[1])
  screen.text(W - MARGIN, y, r[2], {align = "right"})
  y = y + 36
end

screen.line(MARGIN, y + 4, W - MARGIN, y + 4)
y = y + 24

-- The note only explains a row that is actually on screen.
if freeHeap and freeHeap > 0 then
  screen.text(MARGIN, y, "Held back = free heap - allowed.", {size = "small"})
  screen.text(MARGIN, y + 22, "If that is not the reserve set in Settings,", {size = "small"})
  screen.text(MARGIN, y + 44, "the limit was clamped, not derived.", {size = "small"})
else
  screen.text(MARGIN, y, "The simulator has no device heap; its limit", {size = "small"})
  screen.text(MARGIN, y + 22, "is the host equivalent of the device budget.", {size = "small"})
end

screen.text(W // 2, H - 40, "Press Back to exit", {size = "small", align = "center"})
screen.update("full")

input.wait()
