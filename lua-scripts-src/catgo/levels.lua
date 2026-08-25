-- catgo/levels.lua — tile definitions and levels for catgo.lua.
-- Pure data; loaded with require("catgo.levels"). Edit maps with
-- tools/gamedev/tile_editor.py:
--   python tile_editor.py --game lua-scripts-src/catgo/levels.lua

local vector = require("vector")

local nodeDotShape = vector.shape{
  { "rect", 0.45, 0.45, 0.10, 0.10, true },
}

local furnitureShape = vector.shape{
  { "rect", 0.20, 0.20, 0.60, 0.60, true },
}

local doorShape = vector.shape{
  { "rect", 0.25, 0.15, 0.50, 0.70 },
  { "rect", 0.35, 0.25, 0.30, 0.50 },
}

local function nodeDot(x, y, size) nodeDotShape:draw(x, y, size) end
local function furniture(x, y, size) furnitureShape:draw(x, y, size) end
local function door(x, y, size) doorShape:draw(x, y, size) end

return {
  tiles = {
    ["."] = {name = "floor", draw = nodeDot},
    ["#"] = {name = "wall/furniture", solid = true, draw = furniture},
    ["C"] = {name = "cat start", draw = nodeDot},
    ["X"] = {name = "exit door", draw = door},
    ["D"] = {name = "Drogan", solid = true, sprite = "drogan"},
  },

  levels = {
    {
      name = "The Grounds",
      map = {
        "##########",
        "#........#",
        "#...C....#",
        "#........#",
        "#####.####",
        "#........#",
        "#........#",
        "#........#",
        "####.#####",
        "#........#",
        "#....X...#",
        "#........#",
        "##########",
      },
      enemies = { {path = {{1, 6}, {2, 6}, {3, 6}, {4, 6}, {5, 6}, {6, 6}, {7, 6}, {8, 6}}} },
      story = {
        {who = "* * *", "Sharam Pharmaceuticals tested", "Tri-Dormal-G on five thousand", "cats. Four thousand nine", "hundred ninety-nine died."},
        {who = "* * *", "You are the one that did not.", "Tonight: the Drogan mansion.", "The old man has hired a", "professional. His name is", "Halston. His gun never misses."},
        {who = "* * *", "Arrows step. Confirm waits.", "Halston moves when you move.", "Stay off the dotted tiles", "he watches. Reach the door."},
      },
    },
    {
      name = "The Hall",
      map = {
        "##########",
        "#C.......#",
        "########.#",
        "#........#",
        "#.###.####",
        "#........#",
        "########.#",
        "#........#",
        "#.###.####",
        "#........#",
        "########.#",
        "#.......X#",
        "##########",
      },
      enemies = {
        {path = {{1, 3}, {2, 3}, {3, 3}, {4, 3}, {5, 3}, {6, 3}}},
        {path = {{8, 7}, {7, 7}, {6, 7}, {5, 7}, {4, 7}, {3, 7}}},
      },
      story = {
        {who = "* * *", "The grounds are behind you.", "Somewhere upstairs a clock", "ticks. Halston knows you", "are inside. He reloads,", "though he has not fired."},
      },
    },
    {
      name = "The Study",
      map = {
        "##########",
        "#....D...#",
        "#....X...#",
        "#.##.##..#",
        "#........#",
        "#..#..#..#",
        "#........#",
        "#.##.##..#",
        "#........#",
        "#...#....#",
        "#........#",
        "#...C....#",
        "##########",
      },
      enemies = {
        {path = {{2, 4}, {3, 4}, {4, 4}, {5, 4}, {6, 4}, {7, 4}}},
        {path = {{7, 6}, {6, 6}, {5, 6}, {4, 6}, {3, 6}, {2, 6}}},
        {path = {{2, 8}, {3, 8}, {4, 8}, {5, 8}}},
      },
      story = {
        {who = "* * *", "Up the great stair. The study", "door stands ajar; a low fire", "burns. The old man dozes in", "his chair, dreaming of five", "thousand cats."},
      },
    },
  },

  ending = {
    {who = "The Cat", "...purr."},
    {who = "* * *", "Halston searched the house", "until dawn. He found an open", "window, a cold fire, and a", "very, very quiet chair."},
    {who = "* * *", "The papers never got the story", "straight. But you know how it", "goes: a cat settles its debts", "in person.", "", "~ THE END ~"},
  },

  caught = {
    {who = "* * *", "A dry cough of a silenced", "pistol. But a cat has nine", "lives, and you have eight", "left. Try that room again."},
  },
}
