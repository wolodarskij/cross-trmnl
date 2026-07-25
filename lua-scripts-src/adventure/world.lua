-- adventure/world.lua — tile definitions and room maps for adventure.lua.
-- Pure data (plus scenery draw functions); loaded with require("adventure.world").
-- Edit maps with tools/gamedev/tile_editor.py:
--   python tile_editor.py --game lua-scripts-src/adventure/world.lua

local vector = require("vector")

local tree = vector.shape{
  { "poly", {0.5, 0.08, 0.15, 0.65, 0.85, 0.65} },
  { "rect", 0.42, 0.65, 0.16, 0.25, true },
}

local rockShape = vector.shape{
  { "rect", 0.08, 0.12, 0.84, 0.75 },
  { "line", 0.08, 0.12, 0.90, 0.85 },
}

local note = vector.shape{
  { "rect", 0.25, 0.20, 0.50, 0.60 },
  { "line", 0.35, 0.35, 0.62, 0.35 },
  { "line", 0.35, 0.50, 0.62, 0.50 },
  { "line", 0.35, 0.65, 0.53, 0.65 },
}

local function drawTree(x, y, size) tree:draw(x, y, size) end
local function drawRock(x, y, size) rockShape:draw(x, y, size) end
local function drawNote(x, y, size) note:draw(x, y, size) end

local rock = {name = "rock", solid = true, draw = drawRock}

return {
  start = {room = 1, x = 6, y = 14},

  -- '#' scenery defaults to trees; rocky rooms override it below.
  tiles = {
    ["."] = {name = "floor"},
    ["#"] = {name = "tree", solid = true, draw = drawTree},
    ["P"] = {name = "player start"},
    ["E"] = {name = "elder", solid = true, sprite = "elder"},
    ["W"] = {name = "wizard", solid = true, sprite = "wizard"},
    ["D"] = {name = "dragon", solid = true, sprite = "dragon", big = 2},
    ["d"] = {name = "dragon body", solid = true},
    ["K"] = {name = "dragon carcass", solid = true, sprite = "dragon_dead", big = 2},
    ["k"] = {name = "carcass body", solid = true},
    ["S"] = {name = "sword in stone", solid = true, sprite = "swordstone"},
    ["R"] = {name = "bare stone", solid = true, sprite = "stone"},
    ["m"] = {name = "mushroom (pickup)", sprite = "mushroom"},
    ["v"] = {name = "reincarnation vial (pickup)", sprite = "vial"},
    ["G"] = {name = "the hoard", solid = true, sprite = "gold"},
    ["C"] = {name = "cat", solid = true, sprite = "cat"},
    ["M"] = {name = "giant mushroom", solid = true, sprite = "mushroom", big = 2},
    ["n"] = {name = "a note", solid = true, draw = drawNote},
  },

  rooms = {
    {
      name = "Greenhollow",
      map = {
        "############",
        "#..........#",
        "#....##....#",
        "#....##....#",
        "#..........#",
        "#.....E....#",
        "#.......S..#",
        "#..........#",
        "#...........",
        "#...........",
        "#..........#",
        "#..##..##..#",
        "#..##..##..#",
        "#..........#",
        "#.....P....#",
        "#..........#",
        "#..........#",
        "############",
      },
    },
    {
      name = "Whisperwood",
      map = {
        "############",
        "#.#..#...#.#",
        "#..m.......#",
        "#.#...#..#.#",
        "#....#.....#",
        "#.#........#",
        "#....W...#.#",
        "#..........#",
        "............",
        "............",
        "#..#.....#.#",
        "#.....#....#",
        "#.#..m...#.#",
        "#...#......#",
        "#.#.....#..#",
        "#....#..m..#",
        "#.#......#.#",
        "############",
      },
    },
    {
      name = "Ember Peak",
      tiles = {["#"] = rock},
      map = {
        "############",
        "##........##",
        "#..........#",
        "#..........#",
        "#.....Dd...#",
        "#.....dd...#",
        "#..........#",
        "#..........#",
        "...........#",
        "...........#",
        "#..........#",
        "#..######..#",
        "#..######..#",
        "#..........#",
        "#..........#",
        "##........##",
        "#..........#",
        "############",
      },
    },
    {
      name = "The Crypt",
      tiles = {["#"] = rock},
      map = {
        "############",
        "##........##",
        "#..........#",
        "#..........#",
        "#..........#",
        "#..........#",
        "#..........#",
        "#..........#",
        "............",
        "............",
        "#..........#",
        "#..........#",
        "#....##.n..#",
        "#....##....#",
        "#..........#",
        "#..........#",
        "##........##",
        "############",
      },
    },
  },
}
