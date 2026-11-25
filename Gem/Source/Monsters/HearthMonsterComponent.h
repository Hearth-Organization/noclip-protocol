// "Hearth Monster": a monster that hunts the player through Level 0's rooms, with the senses set in Hearth Engine's
// Monster pathing window (Monsters/monsters.json: hearing and sight in metres, hunting speed). It hears footsteps
// within its hearing distance, sees the player within its sight distance and 60° of where it faces (walls block it),
// walks the shortest way through the doorways, and wanders otherwise. It reports where it is and what it's doing
// under /NoclipProtocol/Monsters so the editor shows it live. /NoclipProtocol/Monsters/Enabled = false stops them.
#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/unordered_set.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>

namespace NoclipProtocol
{
    namespace Level0
    {
        struct Settings;
    }

    class HearthMonsterComponent
        : public AZ::Component
        , public AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(HearthMonsterComponent, "{8F3B2C61-4D7E-4A9B-B1E5-3C0D9A6F7E12}");
        static void Reflect(AZ::ReflectContext* context);

        void Activate() override;
        void Deactivate() override;
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

    private:
        struct CellPos
        {
            int x = 0;
            int y = 0;
            bool operator==(const CellPos& o) const { return x == o.x && y == o.y; }
        };

        void LoadSenses();
        const Level0::Settings* Settings() const;
        void EnsureChunk(int chunkX, int chunkY);
        bool Open(const CellPos& from, int dx, int dy);
        bool LineOfSight(const AZ::Vector3& a, const AZ::Vector3& b);
        AZStd::vector<CellPos> FindPath(const CellPos& from, const CellPos& to);
        CellPos Cell(const AZ::Vector3& at) const;
        AZ::Vector3 Centre(const CellPos& cell, float z) const;
        void Report(const AZ::Vector3& at, const AZStd::string& state);

        AZStd::string m_name;
        float m_hearing = 7.0f;
        float m_sight = 12.0f;
        float m_speed = 2.5f;
        float m_cell = 4.0f;

        AZStd::unordered_set<AZ::u64> m_chunksBuilt;
        AZStd::unordered_set<AZ::u64> m_blocked; // wall segments between cells
        AZStd::vector<CellPos> m_path;
        float m_repath = 0.0f;
        AZ::s64 m_stepsSeen = -1;
        AZ::Vector3 m_heardAt = AZ::Vector3::CreateZero();
        float m_sinceHeard = 100.0f;
        CellPos m_wander;
        bool m_hasWander = false;
    };
} // namespace NoclipProtocol
