#pragma once

#include "Level0Layout.h"

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Component/Component.h>
#include <AzCore/Component/EntityId.h>
#include <AzCore/Component/TickBus.h>
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

    protected:
        void Activate() override;
        void Deactivate() override;

        // AZ::TickBus
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

        //! The layout settings in use (seed included), for monsters finding their way.
        const Level0::Settings& GetSettings() const { return m_settings; }

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
            PieceCount
        };

        struct Chunk
        {
            int m_x = 0;
            int m_y = 0;
            AZStd::vector<AzFramework::EntitySpawnTicket> m_tickets;
            int m_pieces = 0; // walls, pillars, lights, stains... spawned for this chunk
        };

        static AZ::u64 Key(int x, int y);
        bool LoadPieceAssets();
        bool PieceAssetsReady() const;
        void SpawnChunk(int chunkX, int chunkY);
        void DespawnChunk(Chunk& chunk);
        void SpawnBox(AzFramework::EntitySpawnTicket& ticket, const Level0::Box& box, bool stretch, float yawRadians = 0.0f);

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
        bool m_assetsMissing = false;
        bool m_announced = false;
        AZ::s64 m_shiftsSeen = 0;      // /NoclipProtocol/Level0/Shifts (Hearth Blocks "Shift the rooms")
        int m_spawnedPieces = 0;      // counts SpawnBox calls, for the editor's "9 chunks · 412 pieces"
        AZStd::string m_statsShown;   // what was last written to /Hearth/Viewport/Stats
        void PublishStats();
        void DrawChunkBorders() const;
    };
} // namespace NoclipProtocol
