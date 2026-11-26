#pragma once

// Level 0 layout: endless, chunked, deterministic.
//
// The world is an infinite grid of square cells (CellSize meters). Cells are grouped into chunks of
// ChunkCells x ChunkCells. Every chunk is generated only from (seed, chunk x, chunk y), so any player,
// at any time, gets the same rooms for the same seed.
//
// Chunks connect: the walls on the lines between chunks are decided by a hash of the line itself, and each
// side of every chunk always gets at least two doorways, so the whole world is one connected maze.
// Each chunk builds its own interior walls plus its south (-Y) and west (-X) border lines; the north and
// east lines belong to the neighbors, so no wall is ever built twice.
//
// This file has no engine dependencies so it can be unit-tested on its own.

#include <cstdint>
#include <vector>

namespace NoclipProtocol::Level0
{
    struct Settings
    {
        uint64_t m_seed = 1;
        int m_chunkCells = 6;          // cells per chunk side
        float m_cellSize = 4.0f;       // meters
        float m_ceilingHeight = 3.0f;  // meters
        float m_wallThickness = 0.2f;  // meters
        float m_openFraction = 0.2f;   // share of leftover interior maze walls knocked out
        float m_borderExtraOpen = 0.15f; // chance of extra doorways on chunk borders (beyond the guaranteed two)
        float m_roomChance = 0.35f;    // chance a chunk gets one big open room
        float m_lightFraction = 0.35f; // kept for compatibility; lighting now comes from zones (below)
        float m_pillarChance = 0.6f;   // chance of a pillar on an open interior corner
        float m_pillarSize = 0.8f;     // chunky square pillars, meters
        int m_spawnRoomRadius = 3;      // cells each way from the spawn cell: the big, lit first-base room (7 x 7 cells, 28 m)
        float m_darkZoneShare = 0.4f;  // share of the world inside dark zones (dead / missing lights)
        float m_darkZoneScale = 3.5f;  // dark zones are about this many cells across
        float m_decalChance = 0.3f;    // chance a cell gets a floor stain
        float m_workingLights = 0.66f; // share of lights in lit areas that work; lower = more dead and flickering (ray tracing uses 0.33)

        // Which level this is: 0 = Level 0 (yellow rooms), 1 = Level 1 (concrete warehouse / garage),
        // 2 = Level 2 (pipe tunnels), 3 = the intro: Site-[REDACTED] Sublevel 4 (one fixed facility, not endless).
        // ApplyStyle sets the rest of the settings to match.
        int m_style = 0;
        int m_lightSpacing = 1;        // a light every N cells (in a checker pattern when 2)
        float m_propChance = 0.0f;     // Level 1: crates per cell; Level 2: pipes per wall segment
        float m_pickupChance = 0.05f; // per cell: scrap, wire, fuel or Almond Water lying about (ApplyStyle: fewer in Level 2)
        float m_noclipChance = 0.08f;  // chance a chunk has a noclip spot (a tear you walk through into the next level)
        float m_clutterChance = 0.5f;  // per cell: something that ended up here (ApplyStyle sets it per level; 0 = none)
        float m_scatterChance = 0.3f;  // per cell: small things lying about (cans, papers, a backpack)
    };

    //! Level 1 and 2 presets: sizes, openness, pillars, rooms, lights and props for that level's look.
    //! Leaves the seed, dark zone share and working lights as they are. Level 0 (style 0) is unchanged.
    void ApplyStyle(Settings& settings);

    //! An axis-aligned box: center and full size, in world meters. Z is up; floor top is z = 0.
    struct Box
    {
        float m_cx, m_cy, m_cz;
        float m_sx, m_sy, m_sz;
    };

    struct Point
    {
        float m_x, m_y, m_z;
    };

    enum class LightState : int
    {
        Working = 0,
        Flickering,
        Dead,
        Dying, // looks working; goes out for good when the player walks under it
    };

    struct Light
    {
        Point m_position; // at the ceiling surface
        LightState m_state;
    };

    enum class DecalType : int
    {
        Wet = 0,
        Mold,
        Worn,
    };

    struct Decal
    {
        Point m_position; // on the floor
        float m_yawRadians;
        float m_size;      // meters across
        DecalType m_type;
    };

    struct ChunkLayout
    {
        int m_chunkX = 0;
        int m_chunkY = 0;
        std::vector<Box> m_walls;   // fixed one-cell segments, so wall textures never stretch
        std::vector<Box> m_pillars;
        Box m_floor{};
        Box m_ceiling{};
        std::vector<Light> m_lights; // cells with no light at all are simply absent
        std::vector<Decal> m_decals;
        std::vector<Box> m_props;    // Level 1 crates, Level 2 pipes (one stretched box each)
        std::vector<Point> m_noclipSpots; // on the floor, in the middle of a cell
        bool m_hasSlabs = true;           // false: no floor or ceiling (the empty chunks around the intro facility)
        std::vector<Box> m_frames;        // Breach 4-A's hazard-striped frame (columns and lintel)
        std::vector<Box> m_voids;         // the breach itself where it is not a way out (Level 0's end of Breach 4-A)
        std::vector<Box> m_screens;       // glowing terminal screens
        struct Pickup
        {
            Point m_at;  // on the floor
            int m_kind;  // 0 scrap, 1 wire, 2 fuel, 3 Almond Water
            int m_index; // its number in this chunk (with the chunk, a lasting id: collected ones stay gone)
        };
        std::vector<Pickup> m_pickups;    // supplies to gather for the forward base
        struct Clutter
        {
            Point m_at;     // on the floor: the prop's origin
            float m_yaw;    // radians, 0 = front facing +Y
            int m_kind;     // index into Clutter::Kinds (ClutterCatalog.h)
        };
        std::vector<Clutter> m_clutter;   // everything that has ended up in here: furniture, junk, lost belongings
    };

    //! The intro facility (style 3): where the player starts, the terminal, and Breach 4-A.
    struct Facility
    {
        static constexpr float StartX = 10.0f, StartY = 6.0f;      // corridor start, facing north (+Y)
        static constexpr float TerminalX = 19.4f, TerminalY = 16.0f; // kiosk in the briefing room, screen facing west
        static constexpr float BreachX = 10.0f, BreachY = 40.0f;    // Breach 4-A in the middle of the breach chamber
    };
    //! Breach 4-A's Level 0 end: against the spawn room's back (south) wall, where the Foundation punched through.
    struct Level0Breach
    {
        static constexpr float X = 2.0f, Y = -11.6f;     // the breach (the spawn room's south wall face is at y -11.9)
        static constexpr float ArriveX = 2.0f, ArriveY = -9.0f; // where you step out, facing north into the room
    };

    //! Builds the layout of one chunk. shift > 0: the same chunk after it has rearranged itself that many times
    //! (low sanity): new interior walls, rooms and clutter; its border doorways, lights, supplies and noclip spot
    //! stay, and its maze still reaches every cell, so the way back to the base and to the tears always stays open.
    ChunkLayout GenerateChunk(const Settings& settings, int chunkX, int chunkY, int shift = 0);

    //! Which chunk a world position falls in.
    void WorldToChunk(const Settings& settings, float x, float y, int& chunkX, int& chunkY);

    //! Center of the spawn cell (cell 0,0 of chunk 0,0), on the floor.
    Point SpawnPoint(const Settings& settings);

    //! Is the wall segment on a chunk border open (a doorway)?
    //! vertical == true: the line x = lineIndex (in cells), segment from cell y = cell to cell + 1.
    //! vertical == false: the line y = lineIndex, segment from cell x = cell to cell + 1.
    bool IsBorderOpen(const Settings& settings, bool vertical, int lineIndex, int cell);

    //! The way to face from (x, y) for the longest, widest clear view (yaw in radians, 0 = facing +Y,
    //! positive turns toward -X), so the player never spawns staring at a wall.
    float OpenestYaw(const Settings& settings, float x, float y);

    //! 0..1 "darkness" of the area around a world cell; above (1 - darkZoneShare) the lights are dead or gone.
    float DarkZoneValue(const Settings& settings, int cellX, int cellY);
} // namespace NoclipProtocol::Level0
