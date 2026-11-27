// "Hearth Monster": a monster that hunts the player through Level 0's rooms, with the senses set in Hearth Engine's
// Monster pathing window (Monsters/monsters.json: hearing and sight in metres, hunting speed). It hears footsteps
// within its hearing distance, sees the player within its sight distance and 60° of where it faces (walls block it),
// walks the shortest way through the doorways, and wanders otherwise. It reports where it is and what it's doing
// under /NoclipProtocol/Monsters so the editor shows it live. /NoclipProtocol/Monsters/Enabled = false stops them.
//
// With a Kind and a motions path it is a real creature: an Actor and a Simple Motion component on the same entity
// play its Tripo3D animations (idle, walk, run, an attack) as it moves; it strikes the player up close, can be shot
// (Weapons/Hit) and dies, and a Smiler drains sanity while it watches you and backs off from a flashlight or a flare.
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

        //! Set by the monster spawner before the entity activates: its own name (Monster pathing), its kind
        //! (senses and behaviour), and its motions, a product path prefix ("assets/models/monsters/smiler/smiler":
        //! the motions are <prefix>_idle.motion, _walk, _run and _slash).
        void Setup(const AZStd::string& name, const AZStd::string& kind, const AZStd::string& motions)
        {
            m_name = name;
            m_kind = kind;
            m_motions = motions;
            if (kind == "Smiler")
            {
                m_speed = 4.2f;   // hunting: a run; it wanders at 40 % (a walk)
                m_sight = 14.0f;
                m_hearing = 9.0f;
            }
        }
        bool IsDead() const { return m_health <= 0.0f; }
        float SinceDeath() const { return m_sinceDeath; }

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
        void Play(const char* motion, bool loop, float speed);

        AZStd::string m_kind;
        AZStd::string m_motions;
        AZStd::string m_playing;
        float m_health = 160.0f;
        float m_sinceDeath = 0.0f;
        float m_attack = -1.0f;      // seconds into an attack, or < 0
        bool m_struck = false;       // this attack's blow has landed (or missed)
        float m_attackCooldown = 0.0f;
        float m_lit = 0.0f;          // seconds in the player's flashlight
        float m_fleeing = 0.0f;      // seconds left backing off from light
        AZ::s64 m_hitsSeen = -1;
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
        AZ::s64 m_shotsSeen = -1;
        AZ::Vector3 m_heardAt = AZ::Vector3::CreateZero();
        float m_sinceHeard = 100.0f;
        CellPos m_wander;
        bool m_hasWander = false;
    };
} // namespace NoclipProtocol
