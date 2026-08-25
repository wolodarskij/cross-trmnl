-- engine/dialogtree.lua — a data-driven dialog & story runner.
--
-- The whole story — conversations, deeds, world changes, endings — lives in
-- one tree of nodes (usually a game data module); the game supplies a table
-- of story variables and a `game` object (engine/game.lua). Nothing here is
-- game-specific.
--
--   local dtree = require("dialogtree")
--   dtree.run(G, trees, trees.talk.elder, vars)
--
-- Speakers (declared once on the tree):
--   trees.speakers = {
--     elder = {name = "Elder", portrait = "elder"},
--     n     = {name = "* * *"},   -- narrator; no portrait
--     you   = {name = "You"},
--   }
-- Nodes and talk lists set `speaker = "elder"`; pages inherit name/portrait.
-- A page may override with `speaker = "you"` or explicit `who` / `right` / `left`.
--
-- A node:
--   some_node = {
--     speaker = "elder",              -- default speaker for pages / prompt
--     portrait = "elder",             -- legacy: still honored if no speaker
--     pages = {{text = "line"}, {speaker = "you", text = "..."}},
--     set   = {quest = true},
--     add   = {mushrooms = -3},
--     mapset = {room = 3, {6,4,"G"}, {7,4,"."}},
--     show  = true,
--     sleep = 1500,
--     ending = {sprite = "cat", title = "~ THE END ~", sub = "..."},
--     locked = true,                  -- Back stops closing these windows
--     branch = {                      -- first matching when; goto OR inline node
--       {when = {...}, ["goto"] = "id"},
--       {when = {...}, pages = {...}, set = {...}},
--       {["goto"] = "fallback"},
--     },
--     next  = "id",
--     choices = {
--       {label = "Tell me of the sword", ["goto"] = "elder_sword"},
--       {label = "Farewell"},
--     },
--   }
--
-- Talk lists (actor greetings): ordered entries with `when` beside the text.
-- List-level `speaker` is inherited by inline entries.
--   trees.talk.elder = {
--     speaker = "elder",
--     { when = {isMushroom = true}, pages = {{text = "AAAH!"}} },
--     { pages = {{text = "Ah, a knight!"}}, next = "elder_topics" },
--   }
--
-- Conditions (`when`):
--   {sword = true, reincarnated = false}  equality
--   {["mushrooms>="] = 3}                 numeric minimum
--   {near_S = true}                       tile char adjacent to the player
--   {room = 3}                            current room index
--
-- Undismissable windows (`locked`): normally Back closes a dialog and stops
-- the walk. `locked = true` on a node makes its pages/choices refuse Back —
-- the player has to answer — and it STAYS on for the rest of that walk, so
-- marking where a fight begins covers every node it leads to. `locked =
-- false` releases it early. The flag is local to run(), never stored in
-- vars, so it always lifts when the walk ends: it cannot strand a player.
--
-- Screen contract: while the walk runs, dialog windows own the display —
-- run() never repaints the world between nodes (each full e-ink repaint is a
-- visible flash of a screen the player is not in). The CALLER repaints once
-- after run() returns, whatever ended the walk. `show = true` nodes are the
-- exception: an authored scene beat that deliberately flashes to the map.

local M = {}

local function checkOne(key, want, vars, G)
  local minKey = key:match("^(.-)>=$")
  if minKey then
    return (vars[minKey] or 0) >= want
  end
  local nearCh = key:match("^near_(.)$")
  if nearCh and G then
    return (G:adjacentTo(nearCh) ~= nil) == want
  end
  if key == "room" and G then
    return G.room == want
  end
  local v = vars[key]
  if v == nil then v = false end
  return v == want
end

function M.check(when, vars, G)
  if not when then return true end
  for key, want in pairs(when) do
    if not checkOne(key, want, vars, G) then return false end
  end
  return true
end

-- First matching entry of a dispatch / talk / branch list, or nil.
local function pickEntry(list, vars, G)
  for _, e in ipairs(list) do
    if M.check(e.when, vars, G) then return e end
  end
end

-- Entry is a full inline node (not a bare goto redirect).
local function isInlineNode(e)
  return e.pages or e.choices or e.prompt or e.branch
      or e.mapset or e.ending or e.show or e.sleep
      or e.next ~= nil
      or ((e.set or e.add) and not e["goto"])
end

local function applyMutations(node, vars)
  if node.set then
    for k, v in pairs(node.set) do vars[k] = v end
  end
  if node.add then
    for k, v in pairs(node.add) do vars[k] = (vars[k] or 0) + v end
  end
end

local function lookupSpeaker(speakers, id)
  if id and speakers then return speakers[id] end
end

-- Resolve who / right from the speaker registry, as a VIEW of the page rather
-- than an edit to it.
--
-- These pages belong to the story module, which require() caches for the whole
-- run — so writing resolved fields into them made the data grow permanently as
-- you played. A page holding one key rehashes from 1 hash slot to 4 the first
-- time it is shown, and nothing ever undoes it. Building a small throwaway view
-- instead keeps the story data exactly as authored: the copy is garbage by the
-- time the next page is drawn, where the old keys lived until the script quit.
--
-- Only the fields the renderer reads are carried over; `text` is copied by
-- reference, so a view costs one small table, not a duplicate of the prose.
--
-- Packed trees (tools/gamedev/pack_dialogs.py) arrive with text as an integer
-- ref into a sidecar .bin, and a page whose only field was text collapsed to
-- the bare ref. Both resolve here, into the view — the transient side — so the
-- authored/packed distinction never leaks past this function. `resolve` is the
-- tree's own T.text; unpacked games have none, and every check short-circuits.
local function viewPage(page, speakers, nodeSpeaker, nodePortrait, resolve)
  if type(page) == "number" then page = {text = page} end
  local sp = lookupSpeaker(speakers, page.speaker)
            or lookupSpeaker(speakers, nodeSpeaker)
  local text = page.text
  if resolve and type(text) == "number" then text = resolve(text) end
  local view = {text = text, left = page.left}
  -- Pages may also be a plain array of line strings (dialog.lua accepts both).
  for i = 1, #page do view[i] = page[i] end

  view.who = page.who or (sp and sp.name) or "* * *"

  if page.right ~= nil then
    view.right = page.right
  elseif page.speaker then
    -- explicit page speaker: only its own portrait (nil for narrator / you)
    if sp and sp.portrait then view.right = sp.portrait end
  elseif sp and sp.portrait then
    view.right = sp.portrait
  elseif nodePortrait then
    view.right = nodePortrait
  end
  return view
end

local function viewPages(pages, speakers, nodeSpeaker, nodePortrait, resolve)
  local out = {}
  for i, page in ipairs(pages) do
    out[i] = viewPage(page, speakers, nodeSpeaker, nodePortrait, resolve)
  end
  return out
end

-- Run the tree starting from a node id, an inline node, or a talk/dispatch list.
function M.run(G, trees, start, vars)
  local speakers = trees.speakers or {}
  local current = start
  local inheritedSpeaker = nil
  local talked = false
  local locked = false   -- sticky within this walk; see the header

  while current do
    local node

    if type(current) == "string" then
      node = trees[current]
      if not node then error("dialog node '" .. tostring(current) .. "' not found") end
      inheritedSpeaker = nil
    elseif type(current) == "table" and current[1] ~= nil then
      -- talk / dispatch list (array entries; optional .speaker)
      inheritedSpeaker = current.speaker
      local e = pickEntry(current, vars, G)
      if not e then break end
      if isInlineNode(e) then
        node = e
      else
        if e.set or e.add then applyMutations(e, vars) end
        current = e["goto"]
        inheritedSpeaker = nil
        -- re-loop with the string id
        node = nil
      end
    else
      -- bare inline node table
      node = current
    end

    if node then
      local effSpeaker = node.speaker or inheritedSpeaker
      local legacyPortrait = node.portrait
      if node.locked ~= nil then locked = node.locked end

      applyMutations(node, vars)
      if node.mapset then
        for _, cell in ipairs(node.mapset) do
          G:setTile(node.mapset.room or G.room, cell[1], cell[2], cell[3])
        end
      end
      if node.pages then
        -- keep = true: the walk owns the screen until it ends. The next thing
        -- drawn is either another dialog window or the caller's post-walk
        -- world repaint — a map flash between two windows serves neither.
        local ok = G:say(viewPages(node.pages, speakers, effSpeaker, legacyPortrait, trees.text),
                         nil, locked, true)
        talked = true
        if ok == false then
          -- Back dismissed the dialog; stop the tree walk.
          break
        end
      end
      if node.show then G:refresh("full") end
      if node.sleep then device.sleep(node.sleep) end
      if node.ending then
        if G.playEnding then G:playEnding(node.ending)
        else G:endScreen(node.ending.sprite, node.ending.title, node.ending.sub) end
      end

      if node.choices then
        local open = {}
        for _, c in ipairs(node.choices) do
          if M.check(c.when, vars, G) then open[#open + 1] = c end
        end
        local prompt = viewPage(node.prompt or {}, speakers, effSpeaker, legacyPortrait, trees.text)
        local labels = {}
        for i, o in ipairs(open) do labels[i] = o.label end
        local pick = G:ask(prompt, labels, nil, locked)
        if not pick then
          break
        end
        local c = open[pick]
        applyMutations(c, vars)
        current = c["goto"]
        inheritedSpeaker = nil
      elseif node.branch then
        local e = pickEntry(node.branch, vars, G)
        if not e then
          current = nil
        elseif isInlineNode(e) then
          current = e
          inheritedSpeaker = e.speaker or effSpeaker
        else
          if e.set or e.add then applyMutations(e, vars) end
          current = e["goto"]
          inheritedSpeaker = nil
        end
      else
        current = node.next
        inheritedSpeaker = nil
      end
    end
  end
  return talked
end

return M
