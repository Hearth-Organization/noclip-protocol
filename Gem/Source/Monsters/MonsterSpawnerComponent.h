// Monster spawner: brings Level 0's monsters in out of the player's sight and takes them away again.
//
// Smilers, Facelings, SCP-096 and SCP-106 (Prefabs/Monsters/<Kind>.prefab: an Actor with its Tripo3D model, a Simple
// Motion component, a collider and a Hearth Monster) come in once the player has been in Level 0 a while (each kind
// its own wait and number at a time, in Kinds()): never near the spawn room and the base, always 28-45 m away. A dead one sinks away and is removed; one left far behind
// is taken away. /NoclipProtocol/Monsters/Spawn = false stops new ones. monster_spawn <kind> brings one in 10 m
// ahead of the player at once (testing).
//
// Header-only like the weapons: created on the player's runtime "Noclip UI" entity.
#pragma once

#include "HearthMonsterComponent.h"
#include "../Level/EndlessLevelComponent.h"

#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Asset/AssetManagerBus.h>
#include <AzCore/Component/Component.h>
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/std/smart_ptr/make_shared.h>
#include <AzCore/std/smart_ptr/shared_ptr.h>
#include <AzCore/std/smart_ptr/unique_ptr.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Spawnable/Spawnable.h>
#include <AzFramework/Spawnable/SpawnableEntitiesInterface.h>

#include <cmath>
#include <cstdlib>

namespace NoclipProtocol
{
    class MonsterSpawnerComponent
        : public AZ::Component
        , public AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(MonsterSpawnerComponent, "{5E1C7A93-2B64-4F0D-9A3E-71C8D2B4F6A5}");

        static void Reflect(AZ::ReflectContext* context)
        {
            if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
            {
                serialize->Class<MonsterSpawnerComponent, AZ::Component>()->Version(1);
            }
        }

        AZ::EntityId m_player;

        void Activate() override { AZ::TickBus::Handler::BusConnect(); }
        void Deactivate() override
        {
            AZ::TickBus::Handler::BusDisconnect();
            m_live.clear();
        }

        //! monster_spawn: one now, 10 m ahead of the player.
        static void Request(const AZStd::string& kind) { Pending() = kind; }

    private:
        struct Kind
        {
            const char* m_name;
            const char* m_spawnable;
            const char* m_motions;
            int m_most;          // at most this many at a time
            float m_firstAfter;  // seconds in Level 0 before the first
            float m_between;     // seconds between arrivals (plus up to the same again)
        };
        static const Kind* Kinds(int& count)
        {
            static const Kind kinds[] = {
                { "Smiler", "prefabs/monsters/smiler.spawnable", "assets/models/monsters/smiler/smiler", 2, 45.0f, 60.0f },
                // Facelings drift about in small numbers; SCP-096 and SCP-106 are rare, one at a time, later on.
                { "Faceling", "prefabs/monsters/faceling.spawnable", "assets/models/monsters/faceling/faceling", 3, 40.0f, 45.0f },
                { "Scp096", "prefabs/monsters/scp096.spawnable", "assets/models/monsters/scp096/scp096", 1, 300.0f, 1200.0f },
                { "Scp106", "prefabs/monsters/scp106.spawnable", "assets/models/monsters/scp106/scp106", 1, 600.0f, 1500.0f },
            };
            count = static_cast<int>(sizeof(kinds) / sizeof(kinds[0]));
            return kinds;
        }
        static AZStd::string& Pending()
        {
            static AZStd::string pending;
            return pending;
        }

        struct Live
        {
            AZStd::shared_ptr<AzFramework::EntitySpawnTicket> m_ticket; // shared: the component must stay copyable for serialization
            AZStd::shared_ptr<AZ::EntityId> m_id = AZStd::make_shared<AZ::EntityId>(); // filled in when it spawns
            int m_kind = 0;
        };

        static const Level0::Settings* LevelSettings(int& style)
        {
            const Level0::Settings* found = nullptr;
            AZ::ComponentApplicationBus::Broadcast(&AZ::ComponentApplicationRequests::EnumerateEntities, [&](AZ::Entity* entity)
            {
                if (!found && entity->GetState() == AZ::Entity::State::Active)
                {
                    if (auto* level = entity->FindComponent<EndlessLevelComponent>())
                    {
                        found = &level->GetSettings();
                        style = level->GetStyle();
                    }
                }
            });
            return found;
        }

        void Spawn(int kind, const AZ::Vector3& at, float yaw)
        {
            int count = 0;
            const Kind& k = Kinds(count)[kind];
            if (!m_assets[kind].IsReady())
            {
                return;
            }
            Live live;
            live.m_kind = kind;
            live.m_ticket = AZStd::make_shared<AzFramework::EntitySpawnTicket>(m_assets[kind]);
            const AZStd::string name = AZStd::string::format("%s %d", k.m_name, ++m_serial);
            const AZStd::string kindName = k.m_name, motions = k.m_motions;
            AZStd::shared_ptr<AZ::EntityId> entityOut = live.m_id;
            AzFramework::SpawnAllEntitiesOptionalArgs args;
            args.m_preInsertionCallback = [at, yaw, name, kindName, motions, entityOut](AzFramework::EntitySpawnTicket::Id,
                                                                                       AzFramework::SpawnableEntityContainerView view)
            {
                for (AZ::Entity* entity : view)
                {
                    if (auto* monster = entity->FindComponent<HearthMonsterComponent>())
                    {
                        monster->Setup(name, kindName, motions);
                        *entityOut = entity->GetId();
                        if (auto* transform = entity->FindComponent<AzFramework::TransformComponent>())
                        {
                            AzFramework::TransformComponentConfiguration config;
                            config.m_localTransform = AZ::Transform::CreateFromQuaternionAndTranslation(AZ::Quaternion::CreateRotationZ(yaw), at);
                            config.m_worldTransform = config.m_localTransform;
                            transform->SetConfiguration(config);
                        }
                    }
                }
            };
            AzFramework::SpawnableEntitiesInterface::Get()->SpawnAllEntities(*live.m_ticket, AZStd::move(args));
            m_live.push_back(AZStd::move(live));
            AZ_Printf("Noclip", "monster: %s at %.1f %.1f", name.c_str(), at.GetX(), at.GetY());
        }

        void OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time) override
        {
            int count = 0;
            const Kind* kinds = Kinds(count);
            if (m_assets.empty())
            {
                m_assets.resize(count);
                for (int i = 0; i < count; ++i)
                {
                    m_nextIn.push_back(kinds[i].m_firstAfter);
                }
            }
            for (int i = 0; i < count; ++i)
            {
                // Looked up until found: a monster's prefab may still be processing when the level starts.
                if (!m_assets[i].GetId().IsValid())
                {
                    AZ::Data::AssetId id;
                    AZ::Data::AssetCatalogRequestBus::BroadcastResult(id, &AZ::Data::AssetCatalogRequests::GetAssetIdByPath, kinds[i].m_spawnable,
                                                                      azrtti_typeid<AzFramework::Spawnable>(), false);
                    if (id.IsValid())
                    {
                        m_assets[i] = AZ::Data::AssetManager::Instance().GetAsset<AzFramework::Spawnable>(id, AZ::Data::AssetLoadBehavior::PreLoad);
                    }
                }
            }

            auto* registry = AZ::SettingsRegistry::Get();
            bool paused = false, spawn = true;
            if (registry)
            {
                registry->Get(paused, "/NoclipProtocol/Paused");
                registry->Get(spawn, "/NoclipProtocol/Monsters/Spawn");
            }
            m_check -= deltaTime;
            const AZStd::string pending = Pending();
            if (paused || (m_check > 0.0f && pending.empty()))
            {
                return;
            }
            const float step = AZStd::max(m_check + 0.5f, deltaTime);
            m_check = 0.5f;
            int style = -1;
            const Level0::Settings* settings = LevelSettings(style);
            AZ::Vector3 playerAt = AZ::Vector3::CreateZero();
            AZ::Transform playerTm = AZ::Transform::CreateIdentity();
            AZ::TransformBus::EventResult(playerTm, m_player, &AZ::TransformBus::Events::GetWorldTM);
            playerAt = playerTm.GetTranslation();

            // Take away the dead and the left-behind.
            for (size_t i = 0; i < m_live.size();)
            {
                AZ::Entity* entity = nullptr;
                AZ::ComponentApplicationBus::BroadcastResult(entity, &AZ::ComponentApplicationRequests::FindEntity, (*m_live[i].m_id));
                auto* monster = entity ? entity->FindComponent<HearthMonsterComponent>() : nullptr;
                AZ::Vector3 at = AZ::Vector3::CreateZero();
                AZ::TransformBus::EventResult(at, (*m_live[i].m_id), &AZ::TransformBus::Events::GetWorldTranslation);
                const bool gone = !settings || style != 0 || (monster && monster->IsDead() && monster->SinceDeath() > 1.5f) ||
                                  (monster && at.GetDistance(playerAt) > 90.0f);
                if (gone && (*m_live[i].m_id).IsValid())
                {
                    AzFramework::SpawnableEntitiesInterface::Get()->DespawnAllEntities(*m_live[i].m_ticket);
                    m_live.erase(m_live.begin() + i);
                    continue;
                }
                ++i;
            }
            if (!settings || style != 0)
            {
                m_inLevel = 0.0f;
                return;
            }
            m_inLevel += step;
            const float cell = settings->m_cellSize;
            auto snap = [cell](float v) { return (std::floor(v / cell) + 0.5f) * cell; };

            if (!pending.empty())
            {
                Pending().clear();
                for (int i = 0; i < count; ++i)
                {
                    if (pending == kinds[i].m_name || pending == AZStd::string(kinds[i].m_name).substr(0, pending.size()))
                    {
                        const AZ::Vector3 ahead = playerTm.GetBasisY().GetNormalized() * 10.0f;
                        const AZ::Vector3 at(snap(playerAt.GetX() + ahead.GetX()), snap(playerAt.GetY() + ahead.GetY()), playerAt.GetZ());
                        Spawn(i, at, std::atan2(-(playerAt.GetX() - at.GetX()), playerAt.GetY() - at.GetY()));
                    }
                }
                return;
            }
            if (!spawn)
            {
                return;
            }
            for (int i = 0; i < count; ++i)
            {
                m_nextIn[i] -= step;
                int alive = 0;
                for (const Live& live : m_live)
                {
                    alive += live.m_kind == i ? 1 : 0;
                }
                if (alive >= kinds[i].m_most || m_nextIn[i] > 0.0f || m_inLevel < kinds[i].m_firstAfter)
                {
                    continue;
                }
                // Somewhere out of the way: 28-45 m off, and well clear of the spawn room and the base around it.
                for (int attempt = 0; attempt < 12; ++attempt)
                {
                    const float angle = static_cast<float>(std::rand() % 6283) / 1000.0f;
                    const float radius = 28.0f + static_cast<float>(std::rand() % 1700) / 100.0f;
                    const AZ::Vector3 at(snap(playerAt.GetX() + std::cos(angle) * radius), snap(playerAt.GetY() + std::sin(angle) * radius),
                                         playerAt.GetZ());
                    if (AZ::Vector3(at.GetX(), at.GetY(), 0.0f).GetLength() < 30.0f)
                    {
                        continue;
                    }
                    Spawn(i, at, angle);
                    break;
                }
                m_nextIn[i] = kinds[i].m_between * (1.0f + static_cast<float>(std::rand() % 100) / 100.0f);
            }
        }

        AZStd::vector<AZ::Data::Asset<AzFramework::Spawnable>> m_assets;
        AZStd::vector<float> m_nextIn;
        AZStd::vector<Live> m_live;
        float m_check = 0.0f;
        float m_inLevel = 0.0f;
        int m_serial = 0;
    };
} // namespace NoclipProtocol
