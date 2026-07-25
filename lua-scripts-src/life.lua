-- life.lua — Conway's Game of Life. Edit the grid with the cursor, then let
-- it run. Shows a two-mode input loop and per-generation "fast" refreshes.
--
--   EDIT mode: arrows move the cursor, Confirm toggles a cell,
--              Power starts the simulation.
--   RUN mode:  any button pauses back to EDIT mode. Back exits (always).
--
-- The grid wraps around the edges (toroidal), so gliders fly forever.

local CELL = 20                 -- pixels per cell
local TOP = 44                  -- reserved for the status line
local STEP_MS = 150             -- delay between generations

local W = screen.width()
local H = screen.height()
local COLS = W // CELL
local ROWS = (H - TOP) // CELL
local GRID_X = (W - COLS * CELL) // 2
local GRID_Y = TOP

-- Two flat 1-based arrays, swapped each generation (no per-step allocation).
local grid, nextGrid = {}, {}
for i = 1, COLS * ROWS do
  grid[i] = 0
  nextGrid[i] = 0
end

local function idx(col, row)  -- col/row are 0-based, wrapping
  return (row % ROWS) * COLS + (col % COLS) + 1
end

local function seed()
  -- a glider...
  local c, r = 3, 3
  for _, p in ipairs({{1, 0}, {2, 1}, {0, 2}, {1, 2}, {2, 2}}) do
    grid[idx(c + p[1], r + p[2])] = 1
  end
  -- ...and a blinker
  c, r = COLS // 2, ROWS // 2
  for d = -1, 1 do
    grid[idx(c + d, r)] = 1
  end
end

local function step()
  for row = 0, ROWS - 1 do
    for col = 0, COLS - 1 do
      local n = grid[idx(col - 1, row - 1)] + grid[idx(col, row - 1)] + grid[idx(col + 1, row - 1)]
              + grid[idx(col - 1, row)]                               + grid[idx(col + 1, row)]
              + grid[idx(col - 1, row + 1)] + grid[idx(col, row + 1)] + grid[idx(col + 1, row + 1)]
      local i = idx(col, row)
      if grid[i] == 1 then
        nextGrid[i] = (n == 2 or n == 3) and 1 or 0
      else
        nextGrid[i] = (n == 3) and 1 or 0
      end
    end
  end
  grid, nextGrid = nextGrid, grid
end

local function draw(mode, gen, curCol, curRow)
  screen.clear()
  if mode == "edit" then
    screen.text(10, 8, "Arrows move  Confirm toggle  Power run", {size = "small"})
  else
    screen.text(10, 8, "Gen " .. gen .. "   any button pauses", {size = "small"})
  end
  screen.rect(GRID_X - 1, GRID_Y - 1, COLS * CELL + 2, ROWS * CELL + 2)
  for row = 0, ROWS - 1 do
    local y = GRID_Y + row * CELL
    for col = 0, COLS - 1 do
      if grid[idx(col, row)] == 1 then
        screen.rect(GRID_X + col * CELL + 1, y + 1, CELL - 2, CELL - 2, true)
      end
    end
  end
  if curCol then  -- cursor: a hollow box around the current cell
    screen.rect(GRID_X + curCol * CELL, GRID_Y + curRow * CELL, CELL, CELL)
    screen.rect(GRID_X + curCol * CELL + 3, GRID_Y + curRow * CELL + 3, CELL - 6, CELL - 6)
  end
end

seed()
local col, row = 3, 3
local gen = 0

draw("edit", gen, col, row)
screen.update("full")

while true do
  -- EDIT mode -----------------------------------------------------------
  local btn = input.wait()
  if btn == "up" then
    row = (row - 1) % ROWS
  elseif btn == "down" then
    row = (row + 1) % ROWS
  elseif btn == "left" then
    col = (col - 1) % COLS
  elseif btn == "right" then
    col = (col + 1) % COLS
  elseif btn == "confirm" then
    local i = idx(col, row)
    grid[i] = 1 - grid[i]
  elseif btn == "power" then
    -- RUN mode ----------------------------------------------------------
    repeat
      step()
      gen = gen + 1
      draw("run", gen)
      screen.update(gen % 30 == 0 and "full" or "fast")
      device.sleep(STEP_MS)
    until input.poll() ~= nil
  end
  draw("edit", gen, col, row)
  screen.update("fast")
end
