#!/usr/bin/env python3
"""Noclip Protocol's 100 levels (the owner, Nov 27 2026: "Let's have 100 levels in total, pick some of the ones fans
would like to see and some unique ones"; monsters must not be too common; only one level is loaded at a time).

Each level is a row the game's endless-level generator reads (Levels/catalog.json, written by this script):
  number, name, kind ("fan": a level Backrooms fans know, "new": ours), layout (one of LAYOUTS), look (palette and
  materials, a short description for now), light (bright / normal / dim / dark / pitch), danger (1 calm .. 5 deadly),
  monsters (kinds from Gem/Source/Monsters, rare: never more than three about), and a note on what's special.

    python3 Levels/catalog.py            # writes Levels/catalog.json
"""
import json
import os

# The layout families the generator builds. Each is a set of settings for Level0Layout (cell size, wall height,
# how open, rooms, pillars) plus its own pieces.
LAYOUTS = {
    "rooms":     "Rooms and halls on a grid (Level 0's own: offices, lobbies, hotels, apartments)",
    "halls":     "Huge open bays with columns (warehouses, garages, pools, malls)",
    "corridors": "Narrow winding corridors (pipes, maintenance, bunkers)",
    "outdoor":   "Open ground under a sky or a ceiling too high to see (fields, streets, suburbs)",
    "caves":     "Rough organic tunnels and chambers (caves, mines, flesh)",
    "water":     "Flooded rooms and pools: wade, swim, never see the bottom",
    "stairs":    "Shafts, stairwells and drops: levels stacked on levels",
}

L = []


def add(number, name, kind, layout, look, light, danger, monsters, note=""):
    L.append(dict(number=number, name=name, kind=kind, layout=layout, look=look, light=light, danger=danger,
                  monsters=monsters, note=note))


# ---------------------------------------------------------------- the ones fans know (and already in the game)
add(0, "The Lobby", "fan", "rooms", "mono-yellow wallpaper, damp carpet, buzzing fluorescents", "normal", 2,
    ["Smiler", "Faceling", "Deathmoth", "Scp096", "Scp173"], "Where every expedition starts; the base is built here")
add(1, "Habitable Zone", "fan", "halls", "concrete warehouse, puddles, crates, strip lights", "normal", 2,
    ["Wretch", "Hound", "Skinstealer", "Clump", "Scp939", "Smiler"], "Supply crates turn up most often here")
add(2, "Pipe Dreams", "fan", "corridors", "hot concrete tunnels lined with pipes and valves, steam", "dim", 3,
    ["Wretch", "Deathmoth", "Scp106", "Smiler"], "Burning pipes hurt; valves can be shut")
add(3, "Electrical Station", "fan", "corridors", "narrow brick halls, humming transformers, sparking cables", "dim", 3,
    ["Hound", "Skinstealer", "Smiler"], "Live wires; the hum hides footsteps (monsters hear less, so do you)")
add(4, "Abandoned Office", "fan", "rooms", "empty office floors, cubicles, windows onto white nothing", "bright", 1,
    ["Faceling"], "Quiet: water coolers of Almond Water, good for a first outpost")
add(5, "Terror Hotel", "fan", "rooms", "1920s hotel halls, red carpet, boiler-room heat, numbered doors", "dim", 4,
    ["Skinstealer", "Wretch", "Smiler"], "Some room doors open onto other rooms of the same hotel")
add(6, "Lights Out", "fan", "rooms", "nothing to see: pitch black halls", "pitch", 5,
    ["Smiler", "Deathmoth"], "Flashlight only; Smilers everywhere you aren't looking")
add(7, "Thalassophobia", "fan", "water", "a small room, then an endless dark sea under a low ceiling", "dark", 4,
    ["Deathmoth"], "Swim through; something big moves below (never seen)")
add(8, "Cave System", "fan", "caves", "wet rock tunnels, stalactites, rope bridges", "dark", 4,
    ["Hound", "Scp939", "Clump"], "Echoes: shots carry twice as far")
add(9, "The Suburbs", "fan", "outdoor", "endless night-time suburbs, every house the same, streetlights", "dim", 4,
    ["Skinstealer", "Hound", "Faceling"], "Houses are rooms to search; never stay out after the streetlights die")
add(10, "Field of Wheat", "fan", "outdoor", "a wheat field to every horizon under an overcast sky, a few barns", "bright", 1,
    [], "Safe and calm; crops and water: farmland for the base")
add(11, "Endless City", "fan", "outdoor", "empty city blocks, skyscrapers, no people", "normal", 2,
    ["Wretch", "Faceling"], "Shops to loot; climbing a tower shows the city never ends")
add(12, "The Matrix", "fan", "corridors", "a corridor that is only a corridor, with doors to other levels", "normal", 1,
    [], "A crossroads: each door opens onto another level")
add(13, "Infinite Apartments", "fan", "rooms", "70s apartment block, the same flat over and over", "normal", 2,
    ["Faceling", "Skinstealer"], "Kitchens with food in the fridges")
add(37, "The Poolrooms", "fan", "water", "white tiles, warm shallow pools, bright sourceless light", "bright", 1,
    [], "Calm, almost no monsters, and still the most unsettling place you've been")

# ---------------------------------------------------------------- SCP places
add(14, "SCP-3008: Endless Store", "fan", "halls", "an endless flat-pack furniture store, showrooms and aisles", "bright", 3,
    ["Staff3008"], "Day: the staff ignore you. Night (lights go out): they hunt. Beds and food everywhere")
add(15, "Site-19 Ruins", "new", "corridors", "a Foundation site swallowed by the Backrooms: containment doors, red lights", "dim", 4,
    ["Scp173", "Scp106", "Scp939"], "Foundation lockers and dead MTF with gear; SCP items to recover")
add(16, "SCP-106's Pocket Dimension", "fan", "caves", "corroded black corridors that twist into each other", "dark", 5,
    ["Scp106"], "Where 106 drags you: one way out, 60 seconds")

# ---------------------------------------------------------------- the rest, ours (and lesser-known favourites)
more = [
    # number, name, kind, layout, look, light, danger, monsters, note
    (17, "The Parking Garage", "new", "halls", "multi-storey car park, dead cars, ramps that never reach the top", "dim", 3, ["Hound", "Wretch"], "Cars to search; a horn sounds somewhere and draws monsters"),
    (18, "Kindergarten", "new", "rooms", "bright childish murals, tiny chairs, a radio playing nursery songs", "bright", 3, ["Faceling"], "Toys move when you aren't looking"),
    (19, "Server Farm", "new", "halls", "rows of humming server racks, blue LEDs, cold air", "dim", 2, ["Deathmoth"], "Terminals that show the Foundation's lost camera feeds"),
    (20, "The Hospital Wing", "new", "rooms", "white hospital wards, curtains, gurneys, a nurses' station", "normal", 3, ["Faceling", "Skinstealer"], "Medkits are common here"),
    (21, "Flooded Basement", "new", "water", "knee-deep black water over a concrete basement", "dark", 3, ["Clump", "Wretch"], "Slow going; sounds of splashing behind you"),
    (22, "The Mall", "new", "halls", "a closed 90s shopping mall, fountain, escalators, shop fronts", "normal", 2, ["Wretch", "Faceling"], "Shops to loot; the music still plays"),
    (23, "Stairwell", "new", "stairs", "a concrete stairwell going down for ever, a door on every floor", "dim", 2, ["Wretch"], "Each door: a short room; the floor number counts on and on"),
    (24, "The Boiler Room", "new", "corridors", "furnaces, rusted boilers, catwalks over pits", "dim", 4, ["Clump", "Hound"], "Heat drains stamina"),
    (25, "Overgrown Office", "new", "rooms", "Level 4's offices taken over by vines and moss, birdsong from nowhere", "normal", 1, [], "Food can be grown here"),
    (26, "The Theatre", "new", "halls", "an old theatre, red seats, a stage, the curtain moves", "dim", 3, ["Faceling", "Smiler"], "Sitting in the front row starts the show"),
    (27, "Laundromat", "new", "rooms", "rows of washers spinning on their own, warm and humid", "normal", 1, [], "Clothes to swap; quiet place to rest"),
    (28, "Classroom Halls", "new", "rooms", "school corridors, lockers, classrooms, chalkboards", "normal", 2, ["Faceling", "Wretch"], "Chalkboards hold notes from earlier agents"),
    (29, "The Archive", "new", "halls", "endless shelves of Foundation files and boxes", "dim", 2, ["Deathmoth"], "Documents that unlock research"),
    (30, "Concrete Plains", "new", "outdoor", "flat grey concrete to the horizon, a hard white sky", "bright", 2, ["Hound"], "Nowhere to hide; you can see what's coming"),
    (31, "Motel", "new", "rooms", "a roadside motel at night, flickering neon VACANCY", "dim", 3, ["Skinstealer"], "Every room number is your own agent number"),
    (32, "The Bunker", "new", "corridors", "Cold War bunker, blast doors, bunk rooms, rations", "dim", 2, ["Wretch"], "Supplies; a working radio to call the Foundation"),
    (33, "Arcade", "new", "rooms", "dark arcade, cabinets glowing, carpets with neon patterns", "dim", 2, ["Smiler"], "Machines still work; one high score has your name"),
    (34, "Train Station", "new", "halls", "an underground station, a train that never leaves", "normal", 2, ["Wretch", "Faceling"], "Board the train: it goes to another level"),
    (35, "The Greenhouse", "new", "halls", "glass greenhouses, warm, plants that are not quite plants", "bright", 2, ["Clump"], "Herbs for medicine"),
    (36, "Attic", "new", "corridors", "low wooden attic beams, dust, old furniture under sheets", "dark", 3, ["Smiler", "Deathmoth"], "Crouch to move"),
    (38, "The Ballroom", "new", "halls", "a grand ballroom, chandeliers, a waltz playing, nobody dancing", "normal", 3, ["Faceling"], "Dancers appear if you stand still too long"),
    (39, "Snow Level", "new", "outdoor", "a frozen field under endless snowfall, buried lamp posts", "dim", 3, ["Hound"], "Cold drains health away from fire"),
    (40, "Red Rooms", "new", "rooms", "Level 0's rooms in deep red, a low hum", "dim", 4, ["Smiler", "Skinstealer"], "Sanity drains slowly here"),
    (41, "The Kitchen", "new", "rooms", "industrial kitchens, steel counters, hanging pans", "normal", 2, ["Wretch"], "Lots of food, but some of it moves"),
    (42, "Library of Babel", "new", "halls", "hexagonal library rooms, every book unreadable but one", "dim", 1, [], "Somewhere here is a book with the way out"),
    (43, "The Ship", "new", "corridors", "the corridors of a rusting cargo ship, the hull groans", "dim", 3, ["Clump", "Wretch"], "The floor tilts now and then"),
    (44, "Suburban Interior", "new", "rooms", "inside one house that goes on and on, family photos with no faces", "normal", 2, ["Faceling"], "The photos change"),
    (45, "Construction Site", "new", "outdoor", "half-built concrete floors, scaffolding, cranes in fog", "dim", 3, ["Hound", "Wretch"], "Falls hurt; building materials for the base"),
    (46, "The Gym", "new", "halls", "a school gym, bleachers, a basketball bouncing by itself", "normal", 2, ["Faceling"], ""),
    (47, "Subway Tunnels", "new", "corridors", "dark subway tunnels, live third rail, distant trains", "dark", 4, ["Hound", "Scp939"], "A train comes through every few minutes"),
    (48, "Pastel Rooms", "new", "rooms", "soft pastel rooms, too clean, a sweet smell", "bright", 2, [], "Peaceful until it isn't"),
    (49, "The Freezer", "new", "corridors", "walk-in freezers end to end, frost, hanging plastic", "dim", 3, ["Wretch"], "Cold; food that keeps"),
    (50, "Level Fun =)", "fan", "rooms", "a children's party that never ends, balloons, streamers", "bright", 5, ["Smiler", "Skinstealer"], "The Partygoers want you to stay. Don't."),
    (51, "Run For Your Life (Level !)", "fan", "corridors", "a red-lit corridor that only goes forward", "dim", 5, ["Hound", "Clump", "Smiler"], "Arrive running; stop and die"),
    (52, "The Hub", "fan", "corridors", "a hall of doors, each a way into a different level", "normal", 1, [], "A safe crossroads; outposts here link levels"),
    (53, "The Windows", "fan", "outdoor", "a courtyard of windows from every side, something watches back", "normal", 3, ["Faceling"], "Don't look through the windows for long"),
    (54, "Church", "new", "halls", "an empty church, pews, candles that relight themselves", "dim", 2, [], "Praying restores sanity a little"),
    (55, "Hall of Mirrors", "new", "rooms", "mirror-lined rooms, your reflection lags behind", "normal", 3, ["Skinstealer"], "One reflection is not yours"),
    (56, "Data Center Flooded", "new", "water", "server racks half under water, sparking", "dark", 4, ["Deathmoth"], "Water and power: careful"),
    (57, "Docks", "new", "outdoor", "night-time docks, containers, cranes, black water", "dim", 3, ["Hound", "Wretch"], "Containers to open: supplies or worse"),
    (58, "Factory Floor", "new", "halls", "assembly lines still running, making nothing", "normal", 3, ["Clump", "Wretch"], "Machines crush; scrap for the base"),
    (59, "The Maze", "new", "corridors", "tall hedges in a maze under a grey sky", "normal", 3, ["Hound"], "Walls of hedge you can't see over"),
    (60, "Prison Block", "new", "corridors", "cell blocks, open doors, an empty guard tower", "dim", 4, ["Skinstealer", "Wretch"], "Some cells lock behind you"),
    (61, "Observatory", "new", "halls", "a domed observatory, a telescope pointed at a sky with no stars", "dark", 1, [], "Looking through it shows the Foundation base"),
    (62, "Waterpark", "new", "water", "an abandoned indoor waterpark, slides into dark pools", "dim", 3, ["Clump"], "Slides are fast ways down"),
    (63, "Sewers", "new", "corridors", "brick sewer tunnels, slow brown water, rats", "dark", 3, ["Wretch", "Hound"], ""),
    (64, "The Void Bridge", "new", "outdoor", "a narrow bridge over a black void", "dark", 4, ["Deathmoth"], "Fall and you're in another level"),
    (65, "Doll Factory", "new", "halls", "shelves of dolls, a conveyor of doll heads", "dim", 4, ["Faceling", "Smiler"], "They turn their heads to follow you"),
    (66, "Radio Station", "new", "rooms", "a small radio station, on air, playing your own voice", "normal", 2, ["Scp939"], "939 learns voices here"),
    (67, "Sand Halls", "new", "halls", "Level 0's halls filled with drifting sand, hot and dry", "bright", 2, ["Hound"], "Water runs out fast"),
    (68, "Museum", "new", "halls", "a natural history museum, skeletons, dioramas, a giant whale", "dim", 2, ["Scp173"], "Statues everywhere: one of them is 173"),
    (69, "Butcher's", "new", "rooms", "cold-store rooms, hanging meat, tiled floors", "dim", 4, ["Clump", "Skinstealer"], ""),
    (70, "Forest", "new", "outdoor", "a dark pine forest, fog, the trees too evenly spaced", "dark", 3, ["Hound", "Skinstealer"], "Firewood and shelter"),
    (71, "Underground Lake", "new", "water", "a huge cave lake, a jetty, a rowing boat", "dark", 3, ["Clump"], "Row across to the far shore"),
    (72, "Clock Tower", "new", "stairs", "inside a clock tower, gears, a staircase around them", "dim", 2, [], "Time runs strange here: sanity holds still"),
    (73, "The Cinema", "new", "halls", "a multiplex, screens showing your expedition", "dark", 3, ["Smiler"], "The film shows where monsters are"),
    (74, "Bathroom Level", "new", "rooms", "endless public toilets and showers, dripping taps", "normal", 2, ["Wretch"], "Clean water"),
    (75, "Elevator Shaft", "new", "stairs", "a huge shaft of elevators going nowhere", "dark", 3, ["Deathmoth"], "Ride an elevator: another level"),
    (76, "The Farm", "new", "outdoor", "a farmhouse and barns at dusk, animals that aren't", "dim", 3, ["Skinstealer", "Hound"], "Livestock-like things to keep at the base?"),
    (77, "Bunker Command", "new", "rooms", "a Foundation command post, maps, a long-dead team", "dim", 2, ["Scp939"], "Orders and intel; an MTF radio"),
    (78, "The Carnival", "new", "outdoor", "an empty carnival at night, rides turning, music box tunes", "dim", 4, ["Smiler", "Faceling"], "Win a prize: a rare item"),
    (79, "Glass Halls", "new", "halls", "walls of glass, you can see far but not reach", "bright", 2, ["Hound"], ""),
    (80, "Asylum", "new", "corridors", "an old asylum, padded rooms, chalk on the walls", "dim", 5, ["Skinstealer", "Faceling", "Scp106"], "Sanity drains fast"),
    (81, "Hotel Pool", "new", "water", "a hotel's indoor pool, steam, deck chairs, a party long over", "normal", 2, [], "Rest here"),
    (82, "Toy Store", "new", "halls", "an enormous toy store, plush animals on every shelf", "bright", 3, ["Faceling"], ""),
    (83, "Mines", "new", "caves", "mine tunnels, rails, carts, lanterns, ore", "dark", 3, ["Clump", "Hound"], "Metal ore for the base"),
    (84, "The Meat Level", "new", "caves", "walls of flesh, warm and breathing", "dim", 5, ["Clump", "Wretch"], "Everything here is alive"),
    (85, "Airport Terminal", "new", "halls", "an empty airport, departure boards listing other levels", "bright", 1, [], "Boards show where each gate leads"),
    (86, "Rooftops", "new", "outdoor", "flat rooftops joined by planks, a city below in fog", "dim", 3, ["Deathmoth", "Hound"], "Don't fall"),
    (87, "Laboratory", "new", "rooms", "a Foundation lab, broken containment glass, notes", "normal", 3, ["Scp939", "Skinstealer"], "Research data; SCP-500 can turn up"),
    (88, "Cathedral of Pipes", "new", "halls", "an organ of pipes rising out of sight, playing", "dim", 3, ["Smiler"], "The music tells you where monsters are"),
    (89, "Swamp", "new", "outdoor", "a foggy swamp, dead trees, boardwalks", "dark", 4, ["Clump", "Hound"], ""),
    (90, "The White Room", "new", "rooms", "a perfectly white empty room that never ends", "bright", 2, [], "No sound, no shadow: sanity drains"),
    (91, "Graveyard", "new", "outdoor", "a graveyard at night, every stone an agent's name", "dark", 3, ["Wretch", "Scp106"], "One stone has your agent's name"),
    (92, "Space Station", "new", "corridors", "corridors of a station, stars outside the windows", "normal", 3, ["Deathmoth", "Skinstealer"], "Low gravity: jump far"),
    (93, "Beach", "new", "outdoor", "a grey beach under an endless overcast, the tide never comes in", "bright", 1, [], "Calm; driftwood and fish"),
    (94, "Motion (Level 94)", "fan", "outdoor", "a cartoon-like town where things move like animation", "bright", 3, ["Faceling"], "The world moves in frames"),
    (95, "Burnt Halls", "new", "rooms", "Level 0 after a fire: soot, ash, melted ceiling tiles", "dark", 3, ["Wretch", "Smiler"], ""),
    (96, "The Pit", "new", "stairs", "a ring of walkways around a bottomless pit", "dark", 4, ["Clump", "Deathmoth"], "Something climbs up the walls"),
    (97, "Childhood Home", "new", "rooms", "a house that looks like your agent's childhood home", "normal", 3, ["Skinstealer"], "Memories: sanity heals, then breaks"),
    (98, "The Static", "new", "rooms", "rooms flickering like a broken TV picture", "dim", 4, ["Smiler", "Faceling"], "Walls blink in and out"),
    (99, "The Exit?", "new", "rooms", "a plain office with a green EXIT sign over a door", "bright", 5, ["Scp096", "Scp173", "Scp106"], "The last level. Is it really the way out?"),
]
for row in more:
    add(*row)

L.sort(key=lambda r: r["number"])
assert len(L) == 100 and len({r["number"] for r in L}) == 100, (len(L), sorted(set(range(100)) - {r["number"] for r in L}))
assert all(r["layout"] in LAYOUTS for r in L)

if __name__ == "__main__":
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "catalog.json")
    with open(out, "w") as f:
        json.dump({"layouts": LAYOUTS, "levels": L}, f, indent=2)
        f.write("\n")
    fans = sum(r["kind"] == "fan" for r in L)
    print(f"wrote {len(L)} levels ({fans} fan favourites, {len(L) - fans} new) to Levels/catalog.json")
