-- paint.lua — buttons demo. Arrows move the cursor, Confirm stamps a
-- square, Back exits. Shows an interactive input loop.

screen.clear()
screen.text(10, 20, "Arrows move   Confirm stamps   Back exits", {size = "small"})
screen.update("full")

local x = screen.width() // 2
local y = screen.height() // 2

while true do
  local btn = input.wait()  -- returns "up"/"down"/"left"/"right"/"confirm"; Back aborts
  if btn == "up" then
    y = y - 12
  elseif btn == "down" then
    y = y + 12
  elseif btn == "left" then
    x = x - 12
  elseif btn == "right" then
    x = x + 12
  elseif btn == "confirm" then
    screen.rect(x - 4, y - 4, 8, 8, true)
    screen.update("fast")
  end
end
