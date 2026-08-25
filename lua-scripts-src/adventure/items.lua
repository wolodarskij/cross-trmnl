-- adventure/items.lua — inventory definitions for adventure.lua.
-- Pure data; loaded with require("adventure.items").
--
-- Each item: `owned` decides when it appears in Items, `actions` are its
-- deeds — every deed points at a node in adventure/dialogs.lua, and may be
-- gated with the same `when` conditions dialog choices use. Names may embed
-- story variables as {var}.

return {
  {
    name = "Red mushroom x{mushrooms}",
    sprite = "mushroom",
    owned = {["mushrooms>="] = 1},
    actions = {
      {label = "Eat One", when = {isMushroom = false}, node = "eat_mushroom"},
      {label = "Look at it", node = "contemplate"},
    },
  },
  {
    name = "Strength elixir",
    sprite = "vial",
    owned = {elixir = true},
    actions = {{label = "Drink It", node = "drink_elixir"}},
  },
  {
    name = "The old sword",
    sprite = "sword",
    owned = {sword = true},
    actions = {{label = "Practice A Swing", node = "swing_sword"}},
  },
  {
    name = "Reincarnation vial",
    sprite = "vial",
    owned = {vial = true},
    actions = {{label = "Uncork It", node = "uncork_vial"}},
  },
  {
    name = "The dragon's hoard",
    sprite = "gold",
    owned = {gold = true},
    actions = {{label = "Count It", node = "count_gold"}},
  },
}
