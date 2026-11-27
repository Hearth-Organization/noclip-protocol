#pragma once

#include <AzCore/std/containers/unordered_set.h>

#include "Level0Layout.h"
#include "../Base/BaseCatalog.h"

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Component/Component.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Color.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>
#include <AzFramework/Spawnable/Spawnable.h>
#include <AzFramework/Spawnable/SpawnableEntitiesInterface.h>

namespace NoclipProtocol
{
    //! Endless Level 0: generates chunks of rooms around the followed entity (the player) while you play,
    //! and unloads chunks that fall far behind. Same seed = same world for every player.
    //!
    //! Each chunk is built from four tile prefabs (wall box, floor slab, ceiling slab, ceiling light).
    //! Box pieces are stretched to size with their Non-uniform Scale component when they spawn.
    class EndlessLevelComponent
        : public AZ::Component
        , public AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(EndlessLevelComponent, "{6C2D7E0B-41F3-4A4E-9C6B-2E7D1B9F5A31}");

        static void Reflect(AZ::ReflectContext* context);
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided);

        //! The seed actually in use (useful for co-op later and for bug reports).
        AZ::u64 GetSeed() const { return m_settings.m_seed; }
        //! The layout settings in use (seed included), for monsters finding their way.
        const Level0::Settings& GetSettings() const { return m_settings; }
        //! 0 = Level 0, 1 = Level 1, 2 = Level 2, 3 = the intro facility, 4 = a catalog level (see GetLevelNumber).
        int GetStyle() const { return m_styleField; }
        //! The level's number in the catalog (0-99; the intro is -1).
        int GetLevelNumber() const { return m_settings.m_catalog >= 0 ? m_settings.m_catalog : (m_styleField <= 2 ? m_styleField : -1); }

    protected:
        void Activate() override;
        void Deactivate() override;

        // AZ::TickBus
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;


    private:
        enum Piece : int
        {
            PieceWall = 0,
            PiecePillar,
            PieceFloor,
            PieceCeiling,
            PieceLight,
            PieceLightFlicker,
            PieceLightDead,
            PieceDecalWet,
            PieceDecalMold,
            PieceDecalWorn,
            PieceLightDying,
            PieceProp,
            PieceNoclip,
            PieceFrame,   // Breach 4-A's hazard-striped frame
            PieceScreen,  // glowing terminal screen
            PieceCount
        };

        struct Chunk
        {
            int m_x = 0;
            int m_y = 0;
            AZStd::vector<AzFramework::EntitySpawnTicket> m_tickets;
            int m_pieces = 0; // walls, pillars, lights, stains... spawned for this chunk
            AZStd::vector<AZ::Vector3> m_noclipSpots;
            AZStd::vector<AZ::Vector3> m_litLights; // working and dying lights (what lights the player up)
            struct LivePickup
            {
                AZ::Vector3 m_at;
                int m_kind;
                int m_index;
                AzFramework::EntitySpawnTicket m_ticket;
            };
            AZStd::vector<LivePickup> m_pickups; // supplies still lying here
            AZStd::vector<AzFramework::EntitySpawnTicket> m_clutter; // one per clutter kind in use (empty = none here)
            int m_shift = 0;                     // how many times this chunk has rearranged itself (low sanity)
        };

        static AZ::u64 Key(int x, int y);
        bool LoadPieceAssets();
        bool PieceAssetsReady() const;
        void SpawnChunk(int chunkX, int chunkY);
        void DespawnChunk(Chunk& chunk);
        void SpawnBox(AzFramework::EntitySpawnTicket& ticket, const Level0::Box& box, bool stretch, float yawRadians = 0.0f,
            bool hum = false, int dress = -1);

        // Catalog levels (style 4): one O3DE level plays any of Levels 3-99. The level's own materials go on the tile
        // pieces as they spawn (dress 0 wall and pillar, 1 floor, 2 ceiling) and its colour and brightness on the lights (3).
        AZ::Data::AssetId m_dressMaterials[3];
        AZ::Color m_lightTint = AZ::Color::CreateOne();
        float m_lightIntensityScale = 1.0f;
        float m_lightRadiusScale = 1.0f;
        void SetUpCatalogLevel();
        bool IsOutdoors() const { return m_settings.m_catalog >= 0 && m_settings.m_layout == 3; }
        void DressEntity(AZ::EntityId id, int dress);
        void UpdateHums(float deltaTime);

        // Base building (Base::State): the placed pieces' own spawns, so each can be removed again, and their lights
        // (switched off in a blackout).
        enum BuildPiece : int { BuildPole = 0, BuildFloodlight, BuildFlare, BuildBeacon, BuildBeaconLight, BuildUvLamp, BuildPieceCount };
        AZ::Data::Asset<AzFramework::Spawnable> m_buildAssets[BuildPieceCount];
        AZ::Data::Asset<AzFramework::Spawnable> m_kindAssets[Base::KindCount]; // the pieces with their own prefab (BaseCatalog.h)
        struct BuiltPiece
        {
            AZStd::vector<AzFramework::EntitySpawnTicket> m_body;
            AZStd::vector<AzFramework::EntitySpawnTicket> m_light;
            bool m_lightOn = false;
        };
        AZStd::vector<BuiltPiece> m_built; // one per Base::State::m_placed entry
        bool m_buildEnabled = false;
        void StartBuilding();
        void DoBuildRequests();
        void SpawnPieceLight(int index); // the piece's light (again, when the power comes back)
        void UpdateBase(float deltaTime, const AZ::Vector3& at); // the base ticking over, the breach seam, delivered crates
        // Breach 4-A sealed: a glowing seam that opens a crack for a delivery.
        AZ::Data::Asset<AzFramework::Spawnable> m_seamAsset;
        AzFramework::EntitySpawnTicket m_seamTicket;
        AZStd::vector<AZ::EntityId> m_seamEntities;
        float m_seamShown = -1.0f;
        AzFramework::EntitySpawnTicket m_crateTicket; // a delivered Foundation crate waiting in front of the breach
        bool m_crateUp = false;
        int m_useTarget = -1;                         // what [E] is at: a placed piece, -2 the crate, -1 nothing

        // Supplies to gather (scrap, wire, fuel, Almond Water, documents, samples): [E] picks one up.
        AZ::Data::Asset<AzFramework::Spawnable> m_pickupAssets[6]; // + Foundation documents, odd samples
        // Clutter (ClutterCatalog.h): everything that has ended up in the Backrooms.
        AZStd::vector<AZ::Data::Asset<AzFramework::Spawnable>> m_clutterAssets;
        void LoadClutterAssets();
        AZStd::unordered_set<AZ::u64> m_collected; // chunk key ^ index: picked up this run, so they don't come back
        AZ::u64 m_promptPickup = 0;                 // which pickup the "[E] Pick up" prompt is for (0: none)
        bool m_pickupPrompt = false;
        void UpdatePickups(const AZ::Vector3& at);

        // Settings (editable in the Editor)
        AZ::EntityId m_followEntity;
        AZ::u64 m_seed = 1;
        bool m_randomSeedEachRun = true;
        int m_loadRadius = 1;   // chunks around the player that must exist (1 = 3x3)
        int m_unloadRadius = 2; // chunks farther than this get removed
        int m_chunksPerFrame = 1;
        AZStd::string m_wallPrefab = "prefabs/level0/l0_wall.spawnable";
        AZStd::string m_pillarPrefab = "prefabs/level0/l0_pillar.spawnable";
        AZStd::string m_floorPrefab = "prefabs/level0/l0_floor.spawnable";
        AZStd::string m_ceilingPrefab = "prefabs/level0/l0_ceiling.spawnable";
        AZStd::string m_lightPrefab = "prefabs/level0/l0_light.spawnable";
        AZStd::string m_lightFlickerPrefab = "prefabs/level0/l0_lightflicker.spawnable";
        AZStd::string m_lightDeadPrefab = "prefabs/level0/l0_lightdead.spawnable";
        AZStd::string m_decalWetPrefab = "prefabs/level0/l0_decalwet.spawnable";
        AZStd::string m_decalMoldPrefab = "prefabs/level0/l0_decalmold.spawnable";
        AZStd::string m_decalWornPrefab = "prefabs/level0/l0_decalworn.spawnable";
        AZStd::string m_lightDyingPrefab = "prefabs/level0/l0_lightdying.spawnable";
        AZStd::string m_propPrefab;       // Level 1 crate / Level 2 pipe (empty = no props)
        int m_styleField = 0;             // 0 = Level 0, 1 = Level 1, 2 = Level 2, 3 = the intro facility
        AZStd::string m_noclipPrefab = "prefabs/levels/noclipspot.spawnable";
        bool m_mainMenu = false;          // this level is the main menu's backdrop (the player stays put, the HUD shows the menu)
        bool m_outdoorLevel = false;      // this is the Outdoors level: a sun, a sky and fog (catalog levels laid out outdoors)
        bool m_outdoorSet = false;        // its sun, sky and fog have been set for this level
        float m_outdoorTries = 0.0f;
        void SetUpOutdoors(float deltaTime);
        AZ::Data::Asset<AzFramework::Spawnable> m_landmarkAssets[3]; // tree, house, street lamp (outdoors)
        int m_landmarkShare[2] = { 70, 20 };                           // % trees, % houses (the rest street lamps)
        AZStd::string m_framePrefab;      // breach frame (empty = none)
        AZStd::string m_screenPrefab;     // terminal screen (empty = none)
        bool m_terminalPrompt = false;    // the intro's "Access terminal" prompt is showing
        bool m_accepted = false;          // the intro's deployment has been accepted (Breach 4-A unsealed)
        float m_sealedToastAt = -100.0f;  // when the "sealed" message was last shown
        AZStd::string m_noclipTarget;     // level a noclip spot drops you into (empty = the next one: 0 -> 1 -> 2 -> 0)
        bool m_noclipping = false;
        void CheckNoclipSpots();
        void PublishPlayerState(); // light level, base distance and bearing, level name: for the HUD and sanity
        // Feature switches (for finding problems and for performance)
        bool m_enablePillars = true;
        bool m_enableLightBulbs = true;     // real point lights (off = glowing panels only)
        bool m_enableFlicker = true;        // off = flickering lights become working lights
        bool m_enableDeadLights = true;     // off = dead lights become missing
        bool m_enableStains = true;
        bool m_facedPlayer = false;
        bool m_enableDyingLights = true;   // off = dying lights become working lights
        float m_darkZoneShareField = 0.4f;
        float m_decalChanceField = 0.3f;
        float m_workingLightsField = 0.66f;           // share of lit-area lights that work
        float m_workingLightsRayTracedField = 0.17f;  // same, when ray-traced bounce light is on (the owner dialed it back, Nov 26: 0.33 -> 0.22 -> 0.17)
        int m_chunkCellsField = 6;
        float m_lightFractionField = 0.35f;
        float m_openFractionField = 0.2f;
        float m_roomChanceField = 0.35f;     // chance a chunk gets one big open room
        float m_pillarChanceField = 0.6f;    // chance of a pillar on an open corner
        int m_spawnRoomRadiusField = 3;      // forward base (spawn room): cells each way, 3 = 7 x 7 cells = 28 m
        float m_darkZoneScaleField = 3.5f;   // dark zones are about this many cells across
        bool m_enableRoomShifting = false;   // coming soon: rooms that rearrange when nobody is looking
        Level0::Settings m_settings;

        // Runtime state
        AZ::Data::Asset<AzFramework::Spawnable> m_pieceAssets[PieceCount];
        AZStd::unordered_map<AZ::u64, Chunk> m_chunks;

        // Shifting rooms: at low sanity, chunks the player can't see rearrange themselves (Level0Layout's shift).
        AZStd::unordered_map<AZ::u64, int> m_chunkShifts; // chunk key -> times it has shifted this run (kept when unloaded)
        float m_shiftClock = 0.0f;
        int m_shiftsDone = 0;
        bool m_clutterOff = false;      // noclip_clutter 0 (testing)
        bool ChunkInView(int chunkX, int chunkY, const AZ::Vector3& eye, const AZ::Vector3& forward) const;
        void UpdateShifting(float deltaTime, const AZ::Vector3& eye, int playerX, int playerY);

        // Light hum: only some lights hum, each starting at its own moment so the loops don't line up into one drone.
        struct PendingHum
        {
            AZ::EntityId m_entity;
            float m_delay = 0.0f;
            int m_tries = 0;
        };
        AZStd::vector<PendingHum> m_pendingHums;
        float m_humShareField = 0.5f; // share of working and dying lights that hum
        bool m_assetsMissing = false;
        bool m_announced = false;
        AZ::s64 m_shiftsSeen = 0;      // /NoclipProtocol/Level0/Shifts (Hearth Blocks "Shift the rooms")
        int m_spawnedPieces = 0;      // counts SpawnBox calls, for the editor's "9 chunks · 412 pieces"
        AZStd::string m_statsShown;   // what was last written to /Hearth/Viewport/Stats
        void PublishStats();
        void DrawChunkBorders() const;
    };
} // namespace NoclipProtocol
