-- adventure/dialogs.lua — the whole story of "The Sword of Ember Peak" as a
-- dialog tree (engine/dialogtree.lua format). Loaded with
-- require("adventure.dialogs"). No code here — conversations, deeds, world
-- changes and endings are all data; adventure.lua only wires it up.
--
-- Speakers (name + portrait) are declared once below. Talk lists put each
-- greeting's `when` next to its pages. Named nodes remain for topic loops
-- and Items deeds.
--
-- Story variables: mushrooms, elixir, sword, vial, gold, isMushroom,
-- elixirOffered, dragonSlain, dragonWounds, reincarnated, wizardSlain,
-- catRevealed, ghostTold, readNote, dragonCoda, dragonSteps, dragonForm,
-- dragonBig.

local T = {}

T.speakers = {
  elder = {portrait = "elder", name = "Elder"},
  dragon = {portrait = "dragon", name = "Dragon"},
  wizard = {portrait = "wizard", name = "Wizard"},
  you = {name = "You"},
  note = {name = "The Note"},
  cat = {portrait = "cat", name = "The Cat"},
  ghost = {portrait = "ghost", name = "Dragon's Ghost"},
  n = {name = "* * *"},
}

T.talk = {
  elder = {
    speaker = "elder",
    {
      when = {isMushroom = true},
      pages = {{text = "AAAH! A talking mushroom!\n...Gerald? Is that you?"}},
    },
    {
      when = {catRevealed = true},
      pages = {
        {
          text = "A cat. Right. Sure.\nLook, the gold is real and\nthe barns are fine, so I'm\nnot asking questions.",
        },
      },
    },
    {
      when = {dragonSlain = true},
      pages = {
        {
          text = "You did it! The mountain is\nquiet! Also you look awful.\nSorry. Someone had to say it.",
        },
      },
    },
    {
      when = {sword = true},
      pages = {
        {text = "THE SWORD! You actually\npulled it out! Ha!"},
        {
          text = "A hundred years of big lads\ngrunting at that rock, and it\npops out for you. Wonderful.\nGo on. Ember Peak's that way.",
        },
      },
    },
    {
      pages = {
        {
          text = "A knight! Excellent. We're\nfresh out of heroes and we've\ngot a dragon. Ask me things.",
        },
      },
      next = "elder_topics",
    },
  },
  hoard = {
    {
      pages = {
        {
          speaker = "n",
          text = "A big pile of gold and a few\nbones. Nobody is guarding it.\nNobody at all.",
        },
      },
      next = "hoard_choice",
    },
  },
  cat = {
    speaker = "cat",
    {
      set = {catRevealed = true},
      pages = {
        {text = "Meow.\nBeing a higher lifeform is\nvery pleasant, thank you."},
        {
          text = "Tell your elders the dragon\nis handled. Also that they\nworship me now. Offerings go\non the usual rock.",
        },
      },
      ending = {
        sprite = "cat",
        title = "~ THE END ~",
        sub = "The dragon purrs on the warm rocks.",
      },
    },
  },
  dragon = {
    speaker = "dragon",
    {
      when = {isMushroom = true},
      pages = {{text = "A walking mushroom. Is that\nvegetarian, or does it still\ncount?"}},
      next = "fight_mushroom",
    },
    {
      when = {reincarnated = true},
      pages = {
        {
          text = "The wizard, knight. Kill him\nand every spell he ever cast\ngoes with him. Mine included.",
        },
      },
      next = "dragon_face",
    },
    {
      when = {dragonSlain = false, sword = true},
      pages = {
        {text = "Oh good. The little sword,\nand a little arm to hold it."},
        {
          speaker = "n",
          text = "The sword has done this many\ntimes before. You have not.\nThis is going to be a thing.",
        },
      },
      next = "dragon_face",
    },
    {
      pages = {
        {
          text = "I'm bored of eating peasants.\nBring me something spicier.\nOr at least something sharp.",
        },
      },
      next = "dragon_face",
    },
  },
  wizard = {
    speaker = "wizard",
    {
      when = {isMushroom = true},
      pages = {
        {
          text = "Fascinating! A walking\ningredient. Do NOT come any\ncloser to the cauldron.",
        },
      },
    },
    {
      when = {readNote = true},
      pages = {
        {
          text = "...Why are you looking at me\nlike that, knight?\n(He backs toward his cauldron.)",
        },
        {
          speaker = "you",
          text = "I read your note, wizard.\nThe curse. The mushrooms.\nThe whole shopping list.",
        },
      },
      next = "wizard_confront_choice",
    },
    {
      when = {elixir = true},
      pages = {{text = "Drink it AT the stone.\nIt wears off fast. Very fast."}},
    },
    {
      when = {sword = true},
      pages = {
        {
          text = "You pulled it? Nice work.\nSay hello to the dragon for\nme. We're old friends.\nSort of.",
        },
      },
    },
    {
      when = {elixirOffered = true, ["mushrooms>="] = 3},
      set = {elixir = true},
      add = {mushrooms = -3},
      pages = {
        {
          text = "Three red mushrooms. Grated\nfly. One human toenail. One\nsad little sigh. Look, I\ndon't write these recipes.",
        },
        {speaker = "n", text = "You received the\nSTRENGTH ELIXIR!"},
      },
    },
    {
      pages = {
        {
          text = "A visitor! Mind the cauldron,\nit bites. What do you want,\ntin man?",
        },
      },
      next = "wizard_topics",
    },
  },
  note = {
    {
      set = {readNote = true},
      pages = {
        {
          speaker = "note",
          text = "'Day 400. Dragon shape still\nholding nicely. While it sits\non my gold, nobody comes\npoking at my real work.'",
        },
        {
          speaker = "note",
          text = "'Mushroom crop coming along\nwell. Three per elixir, and\nthe locals pick them for me.\nFor FREE. Idiots.'",
        },
        {
          speaker = "n",
          text = "Signed with a little cauldron\ndoodle. You think about every\nmushroom you have cheerfully\nhanded over.",
        },
      },
    },
  },
  stone_empty = {
    {
      pages = {
        {
          speaker = "n",
          text = "A stone with a sword-shaped\nhole in it. Job done.",
        },
      },
    },
  },
  intro = {
    {
      pages = {
        {
          speaker = "n",
          text = "THE SWORD OF EMBER PEAK\n\nOne dragon. One sword stuck\nin a rock. One wizard with a\nmushroom habit.\n\nGo be a hero. Try not to\nturn into a mushroom.",
        },
      },
    },
  },
  stone = {
    {
      when = {isMushroom = true},
      pages = {
        {
          speaker = "n",
          text = "You lean on the stone.\nYou have no hands.\nYou are a mushroom.",
        },
      },
    },
    {when = {elixir = true}, ["goto"] = "stone_pull"},
    {
      pages = {
        {
          speaker = "n",
          text = "You pull. You go red in the\nface. The sword does not\nmove. Maybe strength is\nsomething you can buy.",
        },
      },
    },
  },
}

T.elder_topics = {
  speaker = "elder",
  prompt = {text = "What will you ask?"},
  choices = {
    {label = "About That Dragon", ["goto"] = "elder_dragon"},
    {label = "About That Sword", ["goto"] = "elder_sword_info"},
    {label = "Bye Then"},
  },
}

T.elder_dragon = {
  speaker = "elder",
  pages = {
    {
      text = "There's a dragon on Ember\nPeak, two woods east. It took\nour gold and burned down two\nbarns. TWO barns!",
    },
  },
  next = "elder_topics",
}

T.elder_sword_info = {
  speaker = "elder",
  pages = {
    {
      text = "See that stone? There's a\nsword in it. Hundred years,\nnobody has got it out yet.\nPlenty have gone red trying.",
    },
    {
      text = "The wizard over in Whisperwood\nsells a strength elixir. He's\na bit odd, but his stuff works.",
    },
  },
  next = "elder_topics",
}

T.wizard_confront_choice = {
  speaker = "wizard",
  prompt = {
    speaker = "n",
    text = "The cauldron bubbles. The\nwizard is watching your\nsword hand.",
  },
  choices = {
    {
      label = "Draw The Sword",
      when = {sword = true},
      ["goto"] = "wizard_death",
    },
    {label = "Back Away Slowly"},
  },
}

T.wizard_death = {
  set = {wizardSlain = true},
  mapset = {room = 2, {5, 6, "v"}},
  show = true,
  pages = {
    {
      speaker = "n",
      text = "One swing. That's all it\ntakes. The wizard goes off\nlike a damp firework and\nleaves a smell of burnt hair.",
    },
  },
  branch = {
    {
      when = {reincarnated = true},
      mapset = {
        room = 3,
        {6, 4, "C"},
        {7, 4, "."},
        {6, 5, "."},
        {7, 5, "."},
        {4, 4, "."},
      },
      pages = {
        {
          speaker = "n",
          text = "Somewhere east, something\nbarn-sized shrinks to about\nthe size of a loaf of bread.\nEvery spell he cast is off.",
        },
        {speaker = "n", text = "Go say hello to the princess."},
      },
    },
    {
      pages = {
        {
          speaker = "n",
          text = "Every spell he cast is off.\nSadly there's nothing alive\nleft on Ember Peak to un-curse.\nHe did drop a small vial.",
        },
      },
    },
  },
}

T.wizard_topics = {
  speaker = "wizard",
  prompt = {text = "What will you ask?"},
  choices = {
    {label = "About That Dragon", ["goto"] = "wizard_dragon"},
    {label = "About That Sword", ["goto"] = "wizard_sword_check"},
    {label = "Bye Then"},
  },
}

T.wizard_dragon = {
  speaker = "wizard",
  pages = {{text = "The Ember Peak dragon? Oh, we\ngo back. Way back. Next\nquestion, please."}},
  next = "wizard_topics",
}

T.wizard_sword_check = {
  speaker = "wizard",
  branch = {
    {
      when = {["mushrooms>="] = 3},
      set = {elixir = true},
      add = {mushrooms = -3},
      pages = {
        {
          text = "THREE red mushrooms! Lovely.\n(Chop. Simmer. A pinch of\nthunder. One rude word.)",
        },
        {speaker = "n", text = "You received the\nSTRENGTH ELIXIR!"},
      },
    },
    {
      set = {elixirOffered = true},
      pages = {
        {
          text = "Nobody pulls that sword on\ntheir own. Strength, though,\nyou can drink. Bring me THREE\nred mushrooms from the woods.",
        },
      },
      next = "wizard_topics",
    },
  },
}

T.stone_pull = {
  set = {sword = true, elixir = false},
  mapset = {room = 1, {8, 6, "R"}},
  pages = {
    {
      speaker = "n",
      text = "You drink the elixir. Your\narms go all buzzy. You take\nhold of the hilt.",
    },
    {speaker = "n", text = "SHINK.\n\nThe sword is yours."},
  },
}

T.dragon_eats_scene = {
  mapset = {room = 3, {6, 4, "M"}, {7, 4, "."}, {6, 5, "."}, {7, 5, "."}},
  show = true,
  sleep = 1500,
  next = "dragon_eats_end",
}

T.dragon_eats_end = {
  sleep = 1500,
  pages = {
    {
      speaker = "n",
      text = "Silence. Then a rumble.\nThen a sneeze. The dragon\nstarts to CHANGE.",
    },
  },
  ending = {
    sprite = "mushroom",
    title = "THE END(?)",
    sub = "Ember Peak has a hat now.",
  },
}

T.dragon_slay_scene = {
  set = {stormPending = true, stormY = 4, stormX = 6},
  mapset = {room = 3, {6, 4, "K"}, {7, 4, "k"}, {6, 5, "k"}, {7, 5, "k"}, {4, 4, "G"}},
  show = true,
}

-- ---------------------------------------------------------------------------
-- Combat. The dragon cycles Chomp -> Stomp -> Fireball -> Wait forever, and
-- telegraphs each move in the prompt, so every round is a fixed node: the
-- phase is known, so each answer's outcome is known. One set of round nodes
-- serves all three loadouts — the choices are gated on `sword`, so armed
-- fighters see Slash/Pierce and bare hands see Kick/Hit.
--
--   Chomp    breaks on Pierce      Stomp   breaks on Slash
--   Fireball can only be dodged    Wait    is the punish (and the only exit)
--
-- Dodging always survives; it just does no damage. Three wounds kill the
-- dragon; `dragonWounds` resets at the start of every fight.
-- ---------------------------------------------------------------------------

T.dragon_face = {
  prompt = {speaker = "dragon", text = "Well? It's cold up here and\nI'm bored."},
  choices = {
    {label = "Fight!", ["goto"] = "fight_start"},
    {label = "Not Yet"},
  },
}

T.fight_start = {
  locked = true,          -- no backing out from here until the fight resolves
  set = {dragonWounds = 0},
  pages = {
    {
      speaker = "n",
      text = "It uncoils. The dragon does\nchomp, then stomp, then\nfireball, then a breather.\nAlways in that order.\nTake notes.",
    },
  },
  next = "fight_chomp",
}

T.fight_chomp = {
  prompt = {speaker = "n", text = "The head snaps forward.\n\nCHOMP!"},
  choices = {
    {label = "Slash", when = {sword = true}, ["goto"] = "fight_die_slash_chomp"},
    {label = "Pierce", when = {sword = true}, ["goto"] = "fight_wound_chomp"},
    {label = "Kick", when = {sword = false}, ["goto"] = "fight_die_fists_chomp"},
    {label = "Hit", when = {sword = false}, ["goto"] = "fight_die_fists_chomp"},
    {label = "Dodge", ["goto"] = "fight_dodge_chomp"},
    {label = "Run Away", ["goto"] = "fight_die_flee"},
  },
}

T.fight_stomp = {
  prompt = {speaker = "n", text = "It rises on its hind legs.\n\nSTOMP!"},
  choices = {
    {label = "Slash", when = {sword = true}, ["goto"] = "fight_wound_stomp"},
    {label = "Pierce", when = {sword = true}, ["goto"] = "fight_die_pierce_stomp"},
    {label = "Kick", when = {sword = false}, ["goto"] = "fight_die_fists_stomp"},
    {label = "Hit", when = {sword = false}, ["goto"] = "fight_die_fists_stomp"},
    {label = "Dodge", ["goto"] = "fight_dodge_stomp"},
    {label = "Run Away", ["goto"] = "fight_die_flee"},
  },
}

T.fight_fire = {
  prompt = {speaker = "n", text = "The throat glows white.\n\nFIREBALL!"},
  choices = {
    {label = "Slash", when = {sword = true}, ["goto"] = "fight_die_fire"},
    {label = "Pierce", when = {sword = true}, ["goto"] = "fight_die_fire"},
    {label = "Kick", when = {sword = false}, ["goto"] = "fight_die_fire"},
    {label = "Hit", when = {sword = false}, ["goto"] = "fight_die_fire"},
    {label = "Dodge", ["goto"] = "fight_dodge_fire"},
    {label = "Run Away", ["goto"] = "fight_die_flee"},
  },
}

T.fight_wait = {
  prompt = {speaker = "n", text = "It stops to catch its breath.\nNothing is coming at you.\n\nWAIT."},
  choices = {
    {label = "Slash", when = {sword = true}, ["goto"] = "fight_wound_wait"},
    {label = "Pierce", when = {sword = true}, ["goto"] = "fight_wound_wait"},
    {label = "Kick", when = {sword = false}, ["goto"] = "fight_flail_wait"},
    {label = "Hit", when = {sword = false}, ["goto"] = "fight_flail_wait"},
    {label = "Dodge", ["goto"] = "fight_idle_wait"},
    {label = "Run Away", ["goto"] = "fight_flee"},
  },
}

T.fight_wound_chomp = {
  add = {dragonWounds = 1},
  pages = {
    {
      speaker = "n",
      text = "You hold the point steady and\nlet it come. The dragon bites\ndown on your sword. That one\nhurts, and not for you.",
    },
  },
  branch = {
    {when = {["dragonWounds>="] = 3}, ["goto"] = "fight_win"},
    {["goto"] = "fight_stomp"},
  },
}

T.fight_wound_stomp = {
  add = {dragonWounds = 1},
  pages = {
    {
      speaker = "n",
      text = "The foot comes down. You\nslash the back of the ankle\non the way past. The leg\nfolds.",
    },
  },
  branch = {
    {when = {["dragonWounds>="] = 3}, ["goto"] = "fight_win"},
    {["goto"] = "fight_fire"},
  },
}

T.fight_wound_wait = {
  add = {dragonWounds = 1},
  pages = {
    {
      speaker = "n",
      text = "It's busy breathing in. You\nare busy stabbing. This is\nthe best deal you'll get all\nday.",
    },
  },
  branch = {
    {when = {["dragonWounds>="] = 3}, ["goto"] = "fight_win"},
    {["goto"] = "fight_chomp"},
  },
}

T.fight_dodge_chomp = {
  pages = {
    {speaker = "n", text = "Teeth snap shut on the spot\nyou just left. Nobody gets\nhurt. Nobody gets anywhere."},
  },
  next = "fight_stomp",
}

T.fight_dodge_stomp = {
  pages = {
    {speaker = "n", text = "The ground jumps. You roll\nout of the way and hit\nabsolutely nothing."},
  },
  next = "fight_fire",
}

T.fight_dodge_fire = {
  pages = {
    {
      speaker = "n",
      text = "You get behind a rock. The\nrock is orange for a while.\nYou are fine.",
    },
  },
  next = "fight_wait",
}

T.fight_idle_wait = {
  pages = {{speaker = "n", text = "You dodge nothing at all,\nvery well."}},
  next = "fight_chomp",
}

T.fight_flail_wait = {
  pages = {
    {
      speaker = "n",
      text = "You punch a dragon that isn't\neven looking at you.\nNothing happens. Again.",
    },
  },
  next = "fight_chomp",
}

-- Deaths. Each wrong answer says what was wrong with it, then falls through
-- to the one shared screen — so the joke lives in exactly one place.

T.fight_die_flee = {
  speaker = "n",
  pages = {
    {
      text = "You turn your back on a\ndragon mid-attack. Dragons\nreally enjoy it when you do\nthat.",
    },
    {text = "Not even your bravery could\nsave you from death."},
  },
  next = "fight_death",
}

T.fight_die_fire = {
  speaker = "n",
  pages = {
    {
      text = "You put your guard up.\nFire does not care about\nyour guard.",
    },
    {text = "You cannot block fire.\nYou can only dodge it."},
  },
  next = "fight_death",
}

T.fight_die_slash_chomp = {
  speaker = "n",
  pages = {
    {
      text = "Your sword slides off the\nneck. That's the entire\npoint of scales.",
    },
    {text = "Chomp."},
  },
  next = "fight_death",
}

T.fight_die_pierce_stomp = {
  speaker = "n",
  pages = {
    {
      text = "You go hunting for a weak spot.\nA giant foot coming down does\nnot have one.",
    },
    {text = "Then the foot lands."},
  },
  next = "fight_death",
}

T.fight_die_fists_chomp = {
  speaker = "n",
  pages = {
    {text = "You hit a dragon with your\nbare hands. The dragon is\nnot impressed."},
    {text = "Chomp."},
  },
  next = "fight_death",
}

T.fight_die_fists_stomp = {
  speaker = "n",
  pages = {
    {text = "You hit a dragon with your\nbare hands. The dragon is\nnot impressed."},
    {text = "Then it stands on you."},
  },
  next = "fight_death",
}

T.fight_death = {
  ending = {sprite = "dragon", title = "YOU DIED", sub = "Become better."},
}

T.fight_flee = {
  pages = {
    {
      speaker = "dragon",
      text = "Off already? Fine. Thank you\nfor your bravery, knight.\nThere was almost some.",
    },
    {speaker = "n", text = "You walk away. It lets you."},
  },
}

T.fight_win = {
  set = {dragonSlain = true},
  pages = {
    {
      speaker = "n",
      text = "Third hit. That's the one it\nnotices. The dragon leans\nover, thinks about it, and\nstops.",
    },
    {
      speaker = "n",
      text = "The mountain goes quiet.\nThere is a very large pile of\ngold at your feet and nobody\nis watching it.",
    },
  },
  next = "dragon_slay_scene",
}

T.fight_mushroom = {
  locked = true,          -- entered directly, so it needs its own mark
  prompt = {speaker = "n", text = "You check your options. One\nstalk. One cap. No arms, no\nplan, no chance."},
  choices = {
    {label = "Mushroom", ["goto"] = "fight_mushroom_eaten"},
    {label = "Mushroom", ["goto"] = "fight_mushroom_eaten"},
    {label = "Mushroom", ["goto"] = "fight_mushroom_eaten"},
  },
}

T.fight_mushroom_eaten = {
  pages = {{speaker = "n", text = "Chomp."}},
  next = "dragon_eats_scene",
}

T.storm = {
  set = {stormPending = false, stormY = 4, storm = true, stormX = 6},
  pages = {
    {
      speaker = "n",
      text = "A storm rolls over Ember\nPeak. Rain, sideways, cold,\nlots of it. You need to get\nunder something. Now.",
    },
    {
      speaker = "n",
      text = "You can see about five paces.\nThere's the gold pile. There's\nthe dragon, still steaming.\nOr you can lie down in the\nmud, if you like.",
    },
  },
}

T.rest_gold = {
  set = {dragonSteps = 0, gold = true, dragonCoda = true, storm = false},
  mapset = {room = 3, {6, 4, "."}, {7, 4, "."}, {6, 5, "."}, {7, 5, "."}},
  show = true,
  pages = {
    {
      speaker = "n",
      text = "You burrow into the gold. It\nis surprisingly warm. You\ndream about flying, counting\ncoins, and keeping every\nsingle one of them.",
    },
    {
      speaker = "n",
      text = "Morning. Ten fingers, no\nheadache, feeling great. The\ncarcass is gone. The storm\ntook the whole thing. Tidy.",
    },
    {
      speaker = "n",
      text = "You feel fantastic, actually.\nGo tell the elder: the\nmountain is free!",
    },
  },
}

T.coda_turn = {
  set = {dragonForm = true},
  show = true,
  pages = {
    {
      speaker = "n",
      text = "Three steps later your gloves\ndon't fit. You're not\nbothered. Claws are better\nfor counting coins anyway.\n\nYou keep walking.",
    },
  },
}

T.coda_big = {
  set = {dragonBig = true},
  show = true,
  sleep = 1500,
  pages = {
    {
      speaker = "n",
      text = "Your shadow sprouts wings.\nThen it gets a second floor.\nThe news can wait.\nThe gold cannot.\n\nCongratulations. You are the\nmountain's new problem.",
    },
  },
  ending = {
    sprite = "dragon",
    title = "THE END(?)",
    sub = "The village has a NEW dragon problem.",
  },
}

T.rest_dirt = {
  set = {storm = false},
  pages = {
    {
      speaker = "n",
      text = "You lie down right there in\nthe mud. It's cool. It's\nsoft. It's a bit TOO soft.\nThe mushrooms have been\nwaiting all game for this.",
    },
  },
  ending = {
    style = "mushroom_crawl",
    title = "THE END(?)",
    sub = "The mushrooms got their own back.",
  },
}

T.rest_corpse = {
  set = {storm = false},
  pages = {
    {
      speaker = "n",
      text = "You do the practical thing.\nThe gross, famous, practical\nthing. It's warm in there and\nyou sleep beautifully.",
    },
  },
  next = "ghost",
}

T.ghost = {
  speaker = "ghost",
  set = {ghostTold = true, storm = false},
  mapset = {room = 3, {11, 8, "."}, {11, 9, "."}},
  pages = {
    {text = "Comfortable?\n\nGood. Listen, knight."},
    {
      text = "I don't like being dead. So\nyou're going to bring me\nback. Something further up\nthe ladder. Less bitey.",
    },
    {
      text = "First, go read the note in my\ncrypt. You should know whose\nerrands you've been running.\n\n(A stone grinds open, east.)",
    },
  },
}

T.hoard_choice = {
  prompt = {speaker = "n", text = "You could carry all of it.\nNobody would even know."},
  choices = {{label = "Take The Gold", ["goto"] = "hoard_take"}, {label = "Leave It"}},
}

T.hoard_take = {
  set = {gold = true},
  pages = {
    {
      speaker = "n",
      text = "You fill your pack. The coins\nare weirdly warm. Something\nlights up behind your ribs\nand won't go out.",
    },
    {
      speaker = "n",
      text = "By the time you reach the\ndoor your gloves don't fit.\nYou turn around and go back\nto the pile. Obviously.\nIt's YOURS.",
    },
  },
  ending = {
    sprite = "dragon",
    title = "THE END(?)",
    sub = "The village has a NEW dragon problem.",
  },
}

T.uncork_vial = {
  branch = {
    {
      when = {dragonSlain = true, reincarnated = false},
      ["goto"] = "uncork_do",
    },
    {
      pages = {
        {
          speaker = "n",
          text = "The light inside knocks on\nthe glass. There's nobody\naround here to bring back.",
        },
      },
    },
  },
}

T.uncork_do = {
  set = {reincarnated = true, vial = false},
  branch = {
    {
      when = {wizardSlain = true},
      mapset = {
        room = 3,
        {6, 4, "C"},
        {7, 4, "."},
        {6, 5, "."},
        {7, 5, "."},
        {4, 4, "."},
      },
      pages = {
        {
          speaker = "n",
          text = "You uncork the vial. The\nlight shoots off to Ember\nPeak. The wizard is gone, so\nnothing is left to make it\ndragon-shaped.",
        },
        {
          speaker = "n",
          text = "What climbs out of the gold\nis small. And whiskered.",
        },
      },
    },
    {
      mapset = {
        room = 3,
        {6, 4, "D"},
        {7, 4, "d"},
        {6, 5, "d"},
        {7, 5, "d"},
        {4, 4, "."},
      },
      pages = {
        {
          speaker = "n",
          text = "You uncork the vial. Light\nspills out everywhere and\nheads straight for the hoard\non Ember Peak.",
        },
        {
          speaker = "dragon",
          text = "...I was DEAD. I was on my\nway somewhere BETTER. The\ncurse yanked me right back\ninto THIS. You absolute\nlunatic.",
        },
        {
          speaker = "dragon",
          text = "Fine, the truth: I'm not a\ndragon. I'm a princess. That\nsmiling wizard in Whisperwood\ndid this to me.",
        },
        {
          speaker = "dragon",
          text = "Go stab him with that sword.\nEvery spell he cast dies with\nhim. Do a princess a favor.",
        },
      },
    },
  },
}

T.eat_mushroom = {
  set = {isMushroom = true},
  add = {mushrooms = -1},
  pages = {
    {
      speaker = "n",
      text = "You eat it raw. It tastes of\ndirt and bad decisions. Your\nskin goes spongy. You appear\nto have a cap now.",
    },
    {
      speaker = "n",
      text = "You are now a mushroom.\nA mushroom with boots.\nThe quest continues anyway.",
    },
  },
}

T.contemplate = {
  pages = {
    {
      speaker = "n",
      text = "You look at the mushroom.\nThe mushroom looks back at you.",
    },
  },
}

T.drink_elixir = {
  branch = {
    {when = {near_S = true}, ["goto"] = "stone_pull"},
    {
      pages = {
        {
          speaker = "n",
          text = "Not here. Save it for the\nstone. Drink it with your\nhand already on the hilt.",
        },
      },
    },
  },
}

T.swing_sword = {
  pages = {
    {
      speaker = "n",
      text = "Whhht. Whhht.\nThe sword hums. It would\nlike a villain, please.",
    },
  },
}

T.count_gold = {
  pages = {
    {
      speaker = "n",
      text = "You lose count three times.\nIt is a very good hoard.",
    },
  },
}

return T
