// Dying (storyboard Death1-6, approved Nov 27 2026: Battlefield 1's prologue, the Foundation's way).
//
// When health reaches 0: 1 the hit (the view drops, the HUD drains, "VITALS LOST"); 2 the camera leaves the body and rises
// over where you fell; 3 the card: rank, number, name, born-died, service, family, time in the Backrooms, what killed you
// and where, what you left behind; 4 the camera rises over the level like a map, the base beacon pulses gold, a grey X
// where you fell, "NEXT AGENT DISPATCHED"; 5 it dives back down to the base (or Breach 4-A) with the next agent's card;
// 6 you're the next agent: randomly a man or a woman, with their own family (Agents.h). The base, stockpile and research
// stay; the dead agent's backpack stays where they fell (a backpack on the floor, marked) until the next one gets it back.
//
// The camera goes through the ceiling by pushing its near clip plane past the ceiling slab. The HUD draws the overlays
// from /NoclipProtocol/Death/ (NoclipHudComponent). Any key during the card skips ahead.
//
// Header-only like the weapons: created on the player's runtime "Noclip UI" entity.
#pragma once

#include "Agents.h"
#include "FirstPersonControllerComponent.h"
#include "../Base/BaseBuilding.h"
#include "../Level/EndlessLevelComponent.h"
#include "../Level/LevelCatalog.h"

#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Asset/AssetManagerBus.h>
#include <AzCore/Component/Component.h>
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Console/IConsole.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzFramework/Components/CameraBus.h>
#include <AzFramework/Components/TransformComponent.h>
#include <AzFramework/Spawnable/Spawnable.h>
#include <AzFramework/Spawnable/SpawnableEntitiesInterface.h>

#include <cmath>

namespace NoclipProtocol
{
    class DeathSequenceComponent
        : public AZ::Component
        , public AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(DeathSequenceComponent, "{8B4E2D61-7C3A-4F59-9E1B-3D6A2C8F5B47}");

        static void Reflect(AZ::ReflectContext* context)
        {
            if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
            {
                serialize->Class<DeathSequenceComponent, AZ::Component>()->Version(1);
            }
        }

        AZ::EntityId m_player;
        AZ::EntityId m_camera;

        enum Phase : int { Alive = 0, Hit, PullOut, Card, Beacon, NextAgent };

        void Activate() override
        {
            m_phase = Alive;
            m_gearSpawned = false;
            if (auto* r = AZ::SettingsRegistry::Get())
            {
                r->Set("/NoclipProtocol/Death/Phase", static_cast<AZ::s64>(0));
                r->Set("/NoclipProtocol/Player/Dead", false);
                // The first agent of a run.
                AZ::s64 number = 0;
                if (!r->Get(number, AZStd::string(Agents::Root) + "/Number"))
                {
                    // The first agent of a run: Agt. 4417, randomly a man or a woman too.
                    Agents::Agent before;
                    before.m_number = 4416;
                    double now = 0.0;
                    r->Get(now, "/NoclipProtocol/Hud/time");
                    Agents::Store(Agents::New(before, now));
                }
            }
            AZ::TickBus::Handler::BusConnect();
        }

        void Deactivate() override
        {
            AZ::TickBus::Handler::BusDisconnect();
            if (m_gearTicket && m_gearTicket->IsValid())
            {
                AzFramework::SpawnableEntitiesInterface::Get()->DespawnAllEntities(*m_gearTicket);
            }
            m_gearTicket.reset();
        }

    private:
        static float Ease(float t) { t = AZStd::clamp(t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

        const EndlessLevelComponent* Level() const
        {
            const EndlessLevelComponent* found = nullptr;
            AZ::ComponentApplicationBus::Broadcast(&AZ::ComponentApplicationRequests::EnumerateEntities, [&](AZ::Entity* entity)
            {
                if (!found && entity->GetState() == AZ::Entity::State::Active)
                {
                    found = entity->FindComponent<EndlessLevelComponent>();
                }
            });
            return found;
        }

        static AZStd::string Killer(const AZStd::string& kind)
        {
            static const char* const names[][2] = {
                { "Smiler", "A Smiler" }, { "Faceling", "A Faceling" }, { "Scp096", "SCP-096" }, { "Scp106", "SCP-106" },
                { "Scp173", "SCP-173" }, { "Hound", "A Hound" }, { "Wretch", "A Wretch" }, { "Skinstealer", "A Skin-stealer" },
                { "Staff3008", "SCP-3008 staff" }, { "Scp939", "SCP-939" }, { "Clump", "A Clump" }, { "Deathmoth", "A Deathmoth" },
                { "Partygoer", "A Partygoer" }, { "Dancer", "A Dancer" }, { "Scp049", "SCP-049" }, { "Scarecrow", "The Scarecrow" },
                { "Diver", "The Diver" }, { "Burnt", "A Burnt one" }, { "Patient", "A Patient" }, { "Nurse", "The Nurse" },
                { "Clown", "The Clown" }, { "Miner", "A Miner" }, { "Frostwraith", "A Frost wraith" }, { "Farmthing", "The Farm thing" },
                { "Crawler", "The Crawler" }, { "Doll", "A Doll" }, { "Mannequin", "A Mannequin" }, { "Watcher", "The Watcher" },
                { "Plush", "The Plush" } };
            for (const auto& n : names)
            {
                if (kind == n[0])
                {
                    return n[1];
                }
            }
            return kind.empty() ? AZStd::string("Unknown causes") : kind;
        }

        AZ::Vector3 Where() const
        {
            AZ::Vector3 at = AZ::Vector3::CreateZero();
            AZ::TransformBus::EventResult(at, m_player, &AZ::TransformBus::Events::GetWorldTranslation);
            return at;
        }

        void SetCamera(const AZ::Vector3& at, float yaw, float pitch, float roll = 0.0f)
        {
            const AZ::Quaternion q = AZ::Quaternion::CreateRotationZ(yaw) * AZ::Quaternion::CreateRotationX(pitch) *
                                     AZ::Quaternion::CreateRotationY(roll);
            AZ::TransformBus::Event(m_camera, &AZ::TransformBus::Events::SetWorldTM, AZ::Transform::CreateFromQuaternionAndTranslation(q, at));
            // Above the ceiling, clip it away so the floor below shows (the near plane just past the ceiling slab).
            const float overCeiling = at.GetZ() - m_floorZ - m_ceiling;
            const float nearClip = overCeiling > -0.2f ? overCeiling + 0.45f : m_nearClip;
            Camera::CameraRequestBus::Event(m_camera, &Camera::CameraRequests::SetNearClipDistance, nearClip);
        }

        void Begin()
        {
            auto* r = AZ::SettingsRegistry::Get();
            m_phase = Hit;
            m_clock = 0.0f;
            m_fell = Where();
            AZ::TransformBus::EventResult(m_eye, m_camera, &AZ::TransformBus::Events::GetWorldTranslation);
            AZ::TransformBus::EventResult(m_cameraLocal, m_camera, &AZ::TransformBus::Events::GetLocalTM);
            AZ::Transform body = AZ::Transform::CreateIdentity();
            AZ::TransformBus::EventResult(body, m_player, &AZ::TransformBus::Events::GetWorldTM);
            m_yaw0 = body.GetRotation().GetEulerRadians().GetZ();
            m_floorZ = m_fell.GetZ();
            Camera::CameraRequestBus::EventResult(m_nearClip, m_camera, &Camera::CameraRequests::GetNearClipDistance);
            Camera::CameraRequestBus::EventResult(m_fov, m_camera, &Camera::CameraRequests::GetFovRadians);
            m_ceiling = 3.0f;
            m_level = -1;
            AZStd::string levelName = "Sublevel 4";
            if (const EndlessLevelComponent* level = Level())
            {
                m_ceiling = level->GetSettings().m_ceilingHeight;
                m_level = level->GetLevelNumber();
                const Level0::Point base = Level0::SpawnPoint(level->GetSettings());
                m_base = AZ::Vector3(base.m_x, base.m_y, m_floorZ);
                if (m_level == 0)
                {
                    m_base = AZ::Vector3(Level0::Level0Breach::ArriveX, Level0::Level0Breach::ArriveY, m_floorZ);
                }
                if (m_level >= 0 && m_level < Catalog::LevelCount)
                {
                    levelName = AZStd::string::format("Level %d, %s", m_level, Catalog::Levels[m_level].m_name);
                }
            }
            // Who died.
            const Agents::Agent agent = Agents::Load();
            double now = 0.0;
            AZStd::string killer;
            if (r)
            {
                r->Get(now, "/NoclipProtocol/Hud/time");
                r->Get(killer, "/NoclipProtocol/Player/LastHitBy");
                r->Set("/NoclipProtocol/Player/Dead", true);
                r->Set("/NoclipProtocol/Monsters/Enabled", false); // everything holds still for the camera
                r->Set("/NoclipProtocol/Monsters/Spawn", false);
            }
            // The backpack stays where they fell.
            Base::State& base = Base::Get();
            for (int i = 0; i < 6; ++i)
            {
                m_gear[i] = base.m_pack[i];
                base.m_pack[i] = 0;
            }
            const int carried = m_gear[0] + m_gear[1] + m_gear[2] + m_gear[4] + m_gear[5];
            const int minutes = static_cast<int>((now - agent.m_since) / 60.0);
            const AZStd::string facts =
                AZStd::string::format("SERVICE\t%d years \xc2\xb7 MTF Epsilon-6\n", static_cast<int>(agent.m_service)) +
                "FAMILY\t" + agent.m_family + "\n" +
                AZStd::string::format("IN THE BACKROOMS\t%d h %02d min \xc2\xb7 deepest: Level %d\n", minutes / 60, minutes % 60,
                                      static_cast<int>(AZStd::max<AZ::s64>(agent.m_deepest, AZStd::max(m_level, 0)))) +
                "KILLED BY\t" + Killer(killer) + " \xc2\xb7 " + levelName + "\n" +
                AZStd::string::format("LEFT BEHIND\tBackpack %d / %d \xc2\xb7 marked on your compass", carried, Base::State::PackMax);
            AZStd::string name = agent.m_first + " " + agent.m_last;
            for (char& c : name) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
            if (r)
            {
                r->Set("/NoclipProtocol/Death/Card/Rank", agent.m_rank);
                r->Set("/NoclipProtocol/Death/Card/Number", AZStd::string::format("%d", static_cast<int>(agent.m_number)));
                r->Set("/NoclipProtocol/Death/Card/Name", name);
                r->Set("/NoclipProtocol/Death/Card/Years", AZStd::string::format("%d \xe2\x80\x94 2026", static_cast<int>(agent.m_born)));
                r->Set("/NoclipProtocol/Death/Card/Facts", facts);
                r->Set("/NoclipProtocol/Death/Level", levelName);
                r->Set("/NoclipProtocol/Death/BaseLabel", AZStd::string(m_level == 0 ? "FORWARD BASE \xc2\xb7 LEVEL 0" : "BREACH 4-A \xc2\xb7 LEVEL 0"));
            }
            // The next one, ready for their card.
            m_next = Agents::New(agent, now);
            AZStd::string nextName = m_next.m_first + " " + m_next.m_last;
            for (char& c : nextName) { c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
            if (r)
            {
                r->Set("/NoclipProtocol/Death/Next/Rank", m_next.m_rank);
                r->Set("/NoclipProtocol/Death/Next/Number", AZStd::string::format("%d", static_cast<int>(m_next.m_number)));
                r->Set("/NoclipProtocol/Death/Next/Name", nextName);
                r->Set("/NoclipProtocol/Death/Next/Years", AZStd::string::format("%d \xe2\x80\x94", static_cast<int>(m_next.m_born)));
                r->Set("/NoclipProtocol/Death/Next/Facts",
                       AZStd::string::format("SERVICE\t%d year%s \xc2\xb7 MTF Epsilon-6\n", static_cast<int>(m_next.m_service), m_next.m_service == 1 ? "" : "s") +
                       "FAMILY\t" + m_next.m_family + "\n" +
                       AZStd::string::format("ORDERS\tRecover Agt. %d's gear. Hold the forward base.", static_cast<int>(agent.m_number)));
                // Where the gear lies (kept over level loads for the next agent).
                r->Set("/NoclipProtocol/Death/Gear/Level", static_cast<AZ::s64>(m_level));
                r->Set("/NoclipProtocol/Death/Gear/X", static_cast<double>(m_fell.GetX()));
                r->Set("/NoclipProtocol/Death/Gear/Y", static_cast<double>(m_fell.GetY()));
                r->Set("/NoclipProtocol/Death/Gear/Z", static_cast<double>(m_floorZ));
                r->Set("/NoclipProtocol/Death/Gear/Agent", agent.m_number);
                r->Set("/NoclipProtocol/Death/Gear/Pending", true);
                for (int i = 0; i < 6; ++i)
                {
                    r->Set(AZStd::string::format("/NoclipProtocol/Death/Gear/Pack%d", i), static_cast<AZ::s64>(m_gear[i]));
                }
            }
            m_gearSpawned = false; // put the backpack down
            AZ_Printf("Noclip", "death: Agt. %d %s killed by %s in %s", static_cast<int>(agent.m_number), name.c_str(), Killer(killer).c_str(),
                      levelName.c_str());
        }

        void Finish()
        {
            auto* r = AZ::SettingsRegistry::Get();
            Agents::Store(m_next);
            m_phase = Alive;
            Camera::CameraRequestBus::Event(m_camera, &Camera::CameraRequests::SetNearClipDistance, m_nearClip);
            AZ::TransformBus::Event(m_camera, &AZ::TransformBus::Events::SetLocalTM, m_cameraLocal);
            if (r)
            {
                r->Set("/NoclipProtocol/Death/Phase", static_cast<AZ::s64>(0));
                r->Set("/NoclipProtocol/Player/Dead", false);
                r->Set("/NoclipProtocol/Player/Respawn", true);
                double now = 0.0;
                r->Get(now, "/NoclipProtocol/Hud/time");
                r->Set("/NoclipProtocol/Death/BootUntil", now + 4.0);
                r->Set("/NoclipProtocol/Agent/NumberText", AZStd::string::format("%d", static_cast<int>(m_next.m_number)));
                r->Set("/NoclipProtocol/Player/LastHitBy", AZStd::string());
                r->Set("/NoclipProtocol/Monsters/Enabled", true);
                r->Set("/NoclipProtocol/Monsters/Spawn", true);
            }
            if (m_level == 0)
            {
                // Out of Breach 4-A (or at the beacon) in the spawn room, facing in.
                AZ::Entity* entity = nullptr;
                AZ::ComponentApplicationBus::BroadcastResult(entity, &AZ::ComponentApplicationRequests::FindEntity, m_player);
                if (auto* player = entity ? entity->FindComponent<FirstPersonControllerComponent>() : nullptr)
                {
                    player->Teleport(AZ::Vector3(m_base.GetX(), m_base.GetY(), m_floorZ + 0.05f), 0.0f, 0.0f, true, true);
                }
            }
            else
            {
                // Every new agent comes through Breach 4-A into Level 0.
                if (r)
                {
                    r->Set("/NoclipProtocol/Level/Arrived", AZStd::string("breach"));
                }
                AZ::TickBus::QueueFunction([]()
                {
                    if (auto* console = AZ::Interface<AZ::IConsole>::Get())
                    {
                        console->PerformCommand("LoadLevel levels/level0/level0.spawnable");
                    }
                });
            }
        }

        // The dead agent's backpack: on the floor where they fell, until the next agent walks up to it.
        void UpdateGear(double now)
        {
            auto* r = AZ::SettingsRegistry::Get();
            bool pending = false;
            if (!r || !r->Get(pending, "/NoclipProtocol/Death/Gear/Pending") || !pending)
            {
                return;
            }
            AZ::s64 level = -2, agentNumber = 0;
            double x = 0, y = 0, z = 0;
            r->Get(level, "/NoclipProtocol/Death/Gear/Level");
            r->Get(x, "/NoclipProtocol/Death/Gear/X");
            r->Get(y, "/NoclipProtocol/Death/Gear/Y");
            r->Get(z, "/NoclipProtocol/Death/Gear/Z");
            r->Get(agentNumber, "/NoclipProtocol/Death/Gear/Agent");
            const EndlessLevelComponent* here = Level();
            if (!here || here->GetLevelNumber() != level)
            {
                return;
            }
            const AZ::Vector3 gear(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
            if (!m_gearSpawned)
            {
                m_gearSpawned = true;
                AZ::Data::AssetId id;
                AZ::Data::AssetCatalogRequestBus::BroadcastResult(id, &AZ::Data::AssetCatalogRequests::GetAssetIdByPath,
                    "prefabs/clutter/backpack.spawnable", azrtti_typeid<AzFramework::Spawnable>(), false);
                if (id.IsValid())
                {
                    m_gearAsset = AZ::Data::AssetManager::Instance().GetAsset<AzFramework::Spawnable>(id, AZ::Data::AssetLoadBehavior::PreLoad);
                    m_gearTicket = AZStd::make_shared<AzFramework::EntitySpawnTicket>(m_gearAsset);
                    AzFramework::SpawnAllEntitiesOptionalArgs args;
                    args.m_preInsertionCallback = [gear](AzFramework::EntitySpawnTicket::Id, AzFramework::SpawnableEntityContainerView view)
                    {
                        if (!view.empty())
                        {
                            if (auto* t = (*view.begin())->FindComponent<AzFramework::TransformComponent>())
                            {
                                AzFramework::TransformComponentConfiguration config;
                                config.m_localTransform = AZ::Transform::CreateFromQuaternionAndTranslation(AZ::Quaternion::CreateRotationZ(0.7f), gear);
                                config.m_worldTransform = config.m_localTransform;
                                t->SetConfiguration(config);
                            }
                        }
                    };
                    AzFramework::SpawnableEntitiesInterface::Get()->SpawnAllEntities(*m_gearTicket, AZStd::move(args));
                }
            }
            const AZ::Vector3 at = Where();
            const float dx = gear.GetX() - at.GetX(), dy = gear.GetY() - at.GetY();
            const float distance = std::sqrt(dx * dx + dy * dy);
            m_objectiveClock -= 0.1f;
            if (m_objectiveClock <= 0.0f)
            {
                m_objectiveClock = 1.0f;
                r->Set("/NoclipProtocol/Hud/game.objective", AZStd::string::format("Recover Agt. %d's gear", static_cast<int>(agentNumber)));
                r->Set("/NoclipProtocol/Hud/game.progress", AZStd::string::format("Their backpack \xc2\xb7 %.0f m", distance));
            }
            if (distance < 1.8f)
            {
                // Picked up: back into this agent's pack, as much as fits.
                Base::State& base = Base::Get();
                for (int i = 0; i < 6; ++i)
                {
                    AZ::s64 n = 0;
                    r->Get(n, AZStd::string::format("/NoclipProtocol/Death/Gear/Pack%d", i));
                    const int room = Base::State::PackMax - base.Carried();
                    const int take = AZStd::min(static_cast<int>(n), AZStd::max(room, 0));
                    base.m_pack[i] += take;
                }
                r->Set("/NoclipProtocol/Death/Gear/Pending", false);
                r->Set("/NoclipProtocol/Hud/toast", AZStd::string::format("RECOVERED AGT. %d'S GEAR", static_cast<int>(agentNumber)));
                r->Set("/NoclipProtocol/Hud/toast.until", now + 3.0);
                r->Set("/NoclipProtocol/Hud/game.objective", AZStd::string("Survive, scavenge, find the way deeper"));
                r->Set("/NoclipProtocol/Hud/game.progress", AZStd::string("Look for a tear in the walls"));
                if (m_gearTicket && m_gearTicket->IsValid())
                {
                    AzFramework::SpawnableEntitiesInterface::Get()->DespawnAllEntities(*m_gearTicket);
                }
            }
        }

        void OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time) override
        {
            auto* r = AZ::SettingsRegistry::Get();
            if (!r || deltaTime <= 0.0f)
            {
                return;
            }
            double now = 0.0, health = 1.0;
            bool menu = false;
            r->Get(now, "/NoclipProtocol/Hud/time");
            r->Get(health, "/NoclipProtocol/Hud/player.health");
            r->Get(menu, "/NoclipProtocol/Menu/Active");
            if (m_phase == Alive)
            {
                if (menu)
                {
                    return;
                }
                // Deepest level this agent has reached.
                m_deepestClock -= deltaTime;
                if (m_deepestClock <= 0.0f)
                {
                    m_deepestClock = 2.0f;
                    if (const EndlessLevelComponent* level = Level())
                    {
                        AZ::s64 deepest = 0;
                        const AZStd::string key = AZStd::string(Agents::Root) + "/Deepest";
                        r->Get(deepest, key);
                        if (level->GetLevelNumber() > deepest)
                        {
                            r->Set(key, static_cast<AZ::s64>(level->GetLevelNumber()));
                        }
                    }
                }
                UpdateGear(now);
                if (health <= 0.0)
                {
                    Begin();
                }
                return;
            }

            bool skip = false;
            if (r->Get(skip, "/NoclipProtocol/Death/Skip") && skip)
            {
                r->Set("/NoclipProtocol/Death/Skip", false);
                if (m_phase == Card || m_phase == NextAgent)
                {
                    m_clock = 1e6f;
                }
            }
            static const float lengths[6] = { 0.0f, 1.6f, 3.2f, 4.5f, 3.8f, 3.4f };
            m_clock += deltaTime;
            const float t = AZStd::clamp(m_clock / lengths[m_phase], 0.0f, 1.0f);
            const float e = Ease(t);
            const float above = m_ceiling + 6.0f; // over the ceiling, looking down on the room
            const float high = 55.0f;              // the level as a map
            const float down = -AZ::Constants::HalfPi + 0.001f;
            switch (m_phase)
            {
            case Hit:
            {
                // The view drops and tips over.
                const AZ::Vector3 at = m_eye + AZ::Vector3(0.0f, 0.0f, -1.2f * e);
                SetCamera(at, m_yaw0, -0.35f * e, 0.45f * e);
                break;
            }
            case PullOut:
            {
                // Out of the body, straight up to just under the ceiling, turning slowly, looking down.
                const AZ::Vector3 at(m_fell.GetX(), m_fell.GetY(), m_floorZ + 0.4f + (m_ceiling - 0.6f) * e);
                SetCamera(at, m_yaw0 + 0.6f * e, down);
                break;
            }
            case Card:
            {
                // Still rising, through the ceiling.
                const AZ::Vector3 at(m_fell.GetX(), m_fell.GetY(), m_floorZ + m_ceiling - 0.2f + (above - m_ceiling + 0.2f) * e);
                SetCamera(at, m_yaw0 + 0.6f + 0.4f * t, down);
                break;
            }
            case Beacon:
            {
                // Up to a map's height, sliding over to the base; the beacon pulses (HUD).
                const AZ::Vector3 from(m_fell.GetX(), m_fell.GetY(), m_floorZ + above);
                const AZ::Vector3 to(m_base.GetX(), m_base.GetY(), m_floorZ + high);
                const AZ::Vector3 at = from.Lerp(to, e);
                const float yaw = (m_yaw0 + 1.0f) * (1.0f - e);
                SetCamera(at, yaw, down);
                // Where the beacon and the fallen agent are on screen (in half-screen-heights from the centre).
                const float span = (at.GetZ() - m_floorZ) * std::tan(m_fov * 0.5f);
                auto place = [&](const AZ::Vector3& p, const char* key)
                {
                    const float dx = p.GetX() - at.GetX(), dy = p.GetY() - at.GetY();
                    const float c = std::cos(-yaw), s = std::sin(-yaw);
                    r->Set(AZStd::string::format("/NoclipProtocol/Death/%sU", key), static_cast<double>((dx * c - dy * s) / span));
                    r->Set(AZStd::string::format("/NoclipProtocol/Death/%sV", key), static_cast<double>((dx * s + dy * c) / span));
                };
                place(m_base, "Base");
                place(m_fell, "Mark");
                break;
            }
            case NextAgent:
            {
                // Down onto the base, levelling out at eye height.
                const AZ::Vector3 from(m_base.GetX(), m_base.GetY(), m_floorZ + high);
                const AZ::Vector3 to(m_base.GetX(), m_base.GetY(), m_floorZ + 1.6f);
                SetCamera(from.Lerp(to, e), 0.0f, down * (1.0f - Ease((t - 0.6f) / 0.4f)));
                break;
            }
            default:
                break;
            }
            r->Set("/NoclipProtocol/Death/Phase", static_cast<AZ::s64>(m_phase));
            r->Set("/NoclipProtocol/Death/T", static_cast<double>(t));
            if (m_clock >= lengths[m_phase])
            {
                m_clock = 0.0f;
                if (m_phase == NextAgent)
                {
                    Finish();
                }
                else
                {
                    m_phase = static_cast<Phase>(m_phase + 1);
                }
            }
        }

        Phase m_phase = Alive;
        float m_clock = 0.0f;
        AZ::Vector3 m_fell = AZ::Vector3::CreateZero();
        AZ::Vector3 m_eye = AZ::Vector3::CreateZero();
        AZ::Vector3 m_base = AZ::Vector3::CreateZero();
        AZ::Transform m_cameraLocal = AZ::Transform::CreateIdentity();
        float m_floorZ = 0.0f;
        float m_ceiling = 3.0f;
        float m_nearClip = 0.1f;
        float m_fov = 1.0f;
        float m_yaw0 = 0.0f;
        int m_level = -1;
        int m_gear[6] = { 0, 0, 0, 0, 0, 0 };
        Agents::Agent m_next;
        float m_deepestClock = 0.0f;
        float m_objectiveClock = 0.0f;
        bool m_gearSpawned = false;
        AZ::Data::Asset<AzFramework::Spawnable> m_gearAsset;
        AZStd::shared_ptr<AzFramework::EntitySpawnTicket> m_gearTicket;
    };
} // namespace NoclipProtocol
