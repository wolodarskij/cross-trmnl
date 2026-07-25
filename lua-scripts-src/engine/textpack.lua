-- engine/textpack.lua — read packed story text from a sidecar file.
--
-- A packed game keeps its prose on the card, not in memory: the pack step
-- (tools/gamedev/pack_dialogs.py) moves every page's text into one .bin and
-- leaves an integer ref (offset * 4096 + len) where the string was. This
-- module turns a ref back into its string with a single fs.readRange at the
-- moment the page is shown; the string lands in dialogtree's throwaway view,
-- so it is garbage by the time the next page is drawn. Text costs memory only
-- while it is on screen — the same deal screen.image already gives pictures.
--
-- Generated pack cores wire it up; nothing else needs to:
--   T.text = require("textpack").open("/scripts/adventure/dialogs.bin")
--
-- No cache on purpose: dialogtree builds views once per G:say, so each page is
-- resolved exactly once per showing, and a page turn on e-ink is human-paced.

local M = {}

function M.open(binPath)
  return function(ref)
    local s = fs.readRange(binPath, ref // 4096, ref % 4096)
    if not s then
      error("textpack: cannot read " .. binPath .. " (ref " .. tostring(ref) ..
            ") — is the game's .bin on the card next to its .luac files?")
    end
    return s
  end
end

return M
