#include "HearthMonsterComponent.h"
#include "../Level/EndlessLevelComponent.h"
#include "../Level/Level0Layout.h"
#include "../Player/FirstPersonControllerComponent.h"
#include "SimpleMotionComponentBus.h"
#include "MonsterSounds.h"
#include "../Hud/GameSettings.h"
#include <MiniAudio/MiniAudioPlaybackBus.h>
#include <MiniAudio/SoundAsset.h>
#include <AzCore/Asset/AssetManager.h>

#include <AzCore/Asset/AssetManagerBus.h>

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/IO/FileIO.h>
#include <AzCore/JSON/document.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/std/containers/queue.h>
#include <AzCore/std/containers/unordered_map.h>

#include <cmath>
#include <cstdlib>

namespace NoclipProtocol
{
    namespace
    {
        AZ::u64 MonsterEdgeKey(int vertical, int line, int cell)
        {
            return (static_cast<AZ::u64>(vertical) << 62) ^ (static_cast<AZ::u64>(static_cast<AZ::u32>(line + 0x100000)) << 31)
                ^ static_cast<AZ::u64>(static_cast<AZ::u32>(cell + 0x100000));
        }
        AZ::u64 MonsterCellKey(int x, int y)
        {
            return (static_cast<AZ::u64>(static_cast<AZ::u32>(x)) << 32) | static_cast<AZ::u32>(y);
        }
        AZ::EntityId MonsterPlayer()
        {
            AZ::EntityId player;
            AZ::ComponentApplicationBus::Broadcast(&AZ::ComponentApplicationRequests::EnumerateEntities, [&player](AZ::Entity* entity)
            {
                if (!player.IsValid() && entity->GetState() == AZ::Entity::State::Active && entity->FindComponent<FirstPersonControllerComponent>())
                {
                    player = entity->GetId();
                }
            });
            return player;
        }
    } // namespace

    void HearthMonsterComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<HearthMonsterComponent, AZ::Component>()->Version(2)->Field("Name", &HearthMonsterComponent::m_name)
                ->Field("Kind", &HearthMonsterComponent::m_kind)->Field("Motions", &HearthMonsterComponent::m_motions);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<HearthMonsterComponent>("Hearth Monster", "Hunts the player through Level 0 with the senses from Hearth Engine's Monster pathing")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Noclip Protocol")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HearthMonsterComponent::m_name, "Name",
                                  "Its name in Monster pathing (empty: the entity's name)")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HearthMonsterComponent::m_kind, "Kind", "Smiler, Hound... (senses in Monster pathing are looked up by kind too)")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HearthMonsterComponent::m_motions, "Motions",
                                  "Product path prefix of its motions, e.g. assets/models/monsters/smiler/smiler");
            }
        }
    }

    void HearthMonsterComponent::LoadSenses()
    {
        AZ::IO::FileIOBase* io = AZ::IO::FileIOBase::GetInstance();
        if (!io)
        {
            return;
        }
        AZStd::string text;
        for (const char* path : { "@projectroot@/Monsters/monsters.json", "@products@/monsters/monsters.json" })
        {
            AZ::IO::HandleType handle = AZ::IO::InvalidHandle;
            if (io->Open(path, AZ::IO::OpenMode::ModeRead | AZ::IO::OpenMode::ModeBinary, handle))
            {
                AZ::u64 size = 0;
                io->Size(handle, size);
                text.resize(size);
                io->Read(handle, text.data(), size);
                io->Close(handle);
                break;
            }
        }
        rapidjson::Document document;
        document.Parse(text.c_str());
        if (document.HasParseError() || !document.IsObject() || !document.HasMember("monsters") || !document["monsters"].IsArray())
        {
            return;
        }
        for (const auto& m : document["monsters"].GetArray())
        {
            if (m.IsObject() && m.HasMember("name") && m["name"].IsString() &&
                (m_name == m["name"].GetString() || m_kind == m["name"].GetString()))
            {
                m_hearing = m.HasMember("hearing") && m["hearing"].IsNumber() ? static_cast<float>(m["hearing"].GetDouble()) : m_hearing;
                m_sight = m.HasMember("sight") && m["sight"].IsNumber() ? static_cast<float>(m["sight"].GetDouble()) : m_sight;
                m_speed = m.HasMember("speed") && m["speed"].IsNumber() ? static_cast<float>(m["speed"].GetDouble()) : m_speed;
                m_turn = m.HasMember("turn") && m["turn"].IsNumber() ? AZ::DegToRad(static_cast<float>(m["turn"].GetDouble())) : m_turn;
            }
        }
    }

    void HearthMonsterComponent::Activate()
    {
        if (m_name.empty())
        {
            m_name = GetEntity()->GetName();
        }
        LoadSenses();
        LoadSounds();
        if (m_turn != 0.0f)
        {
            // It was placed facing the way it should look: turn the model so its face is that way.
            AZ::Quaternion q = AZ::Quaternion::CreateIdentity();
            AZ::TransformBus::EventResult(q, GetEntityId(), &AZ::TransformBus::Events::GetWorldRotationQuaternion);
            AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldRotationQuaternion,
                AZ::Quaternion::CreateRotationZ(m_turn) * q);
        }
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            AZStd::string list;
            registry->Get(list, "/NoclipProtocol/Monsters/List");
            if (("," + list + ",").find("," + m_name + ",") == AZStd::string::npos)
            {
                registry->Set("/NoclipProtocol/Monsters/List", list.empty() ? m_name : list + "," + m_name);
            }
        }
        AZ::TickBus::Handler::BusConnect();
    }

    void HearthMonsterComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            AZStd::string list;
            registry->Get(list, "/NoclipProtocol/Monsters/List");
            AZStd::string kept;
            size_t start = 0;
            while (start <= list.size())
            {
                const size_t comma = list.find(',', start);
                const AZStd::string name = list.substr(start, comma == AZStd::string::npos ? AZStd::string::npos : comma - start);
                if (!name.empty() && name != m_name)
                {
                    kept += kept.empty() ? name : "," + name;
                }
                if (comma == AZStd::string::npos)
                {
                    break;
                }
                start = comma + 1;
            }
            registry->Set("/NoclipProtocol/Monsters/List", kept);
        }
        m_chunksBuilt.clear();
        m_blocked.clear();
    }

    const Level0::Settings* HearthMonsterComponent::Settings() const
    {
        const Level0::Settings* found = nullptr;
        AZ::ComponentApplicationBus::Broadcast(&AZ::ComponentApplicationRequests::EnumerateEntities, [&found](AZ::Entity* entity)
        {
            if (!found)
            {
                if (auto* level = entity->FindComponent<EndlessLevelComponent>())
                {
                    found = &level->GetSettings();
                }
            }
        });
        return found;
    }

    HearthMonsterComponent::CellPos HearthMonsterComponent::Cell(const AZ::Vector3& at) const
    {
        return { static_cast<int>(std::floor(at.GetX() / m_cell)), static_cast<int>(std::floor(at.GetY() / m_cell)) };
    }

    AZ::Vector3 HearthMonsterComponent::Centre(const CellPos& cell, float z) const
    {
        return AZ::Vector3((cell.x + 0.5f) * m_cell, (cell.y + 0.5f) * m_cell, z);
    }

    void HearthMonsterComponent::EnsureChunk(int chunkX, int chunkY)
    {
        if (!m_chunksBuilt.insert(MonsterCellKey(chunkX, chunkY)).second)
        {
            return;
        }
        const Level0::Settings* settings = Settings();
        if (!settings)
        {
            return;
        }
        m_cell = settings->m_cellSize;
        const Level0::ChunkLayout layout = Level0::GenerateChunk(*settings, chunkX, chunkY);
        for (const Level0::Box& w : layout.m_walls)
        {
            const bool horizontal = w.m_sy < w.m_sx;
            const float length = (horizontal ? w.m_sx : w.m_sy) - settings->m_wallThickness;
            const int count = static_cast<int>(std::lround(length / m_cell));
            const int line = static_cast<int>(std::lround((horizontal ? w.m_cy : w.m_cx) / m_cell));
            const int first = static_cast<int>(std::lround(((horizontal ? w.m_cx : w.m_cy) - length * 0.5f) / m_cell));
            for (int i = 0; i < count; ++i)
            {
                m_blocked.insert(MonsterEdgeKey(horizontal ? 0 : 1, line, first + i));
            }
        }
        // Holes in the floor (stairs down, a drop into water): walled off for walking, so nothing floats across them.
        for (const Level0::ChunkLayout::Exit& exit : layout.m_exits)
        {
            if (exit.m_kind == 0)
            {
                continue;
            }
            const CellPos c = Cell(AZ::Vector3(exit.m_at.m_x, exit.m_at.m_y, 0.0f));
            m_blocked.insert(MonsterEdgeKey(0, c.y, c.x));
            m_blocked.insert(MonsterEdgeKey(0, c.y + 1, c.x));
            m_blocked.insert(MonsterEdgeKey(1, c.x, c.y));
            m_blocked.insert(MonsterEdgeKey(1, c.x + 1, c.y));
        }
    }

    bool HearthMonsterComponent::Open(const CellPos& from, int dx, int dy)
    {
        const Level0::Settings* settings = Settings();
        const int n = settings ? settings->m_chunkCells : 6;
        auto chunkOf = [n](int c) { return c >= 0 ? c / n : (c - n + 1) / n; };
        EnsureChunk(chunkOf(from.x), chunkOf(from.y));
        EnsureChunk(chunkOf(from.x + dx), chunkOf(from.y + dy));
        if (dx == 1) return !m_blocked.count(MonsterEdgeKey(1, from.x + 1, from.y));
        if (dx == -1) return !m_blocked.count(MonsterEdgeKey(1, from.x, from.y));
        if (dy == 1) return !m_blocked.count(MonsterEdgeKey(0, from.y + 1, from.x));
        if (dy == -1) return !m_blocked.count(MonsterEdgeKey(0, from.y, from.x));
        return true;
    }

    AZ::Vector3 HearthMonsterComponent::Ahead(const AZ::Transform& self) const
    {
        // Where its face points: the model's +Y, less the turn some models are built with.
        const AZ::Vector3 y = AZ::Quaternion::CreateRotationZ(-m_turn).TransformVector(self.GetBasisY());
        AZ::Vector3 flat(y.GetX(), y.GetY(), 0.0f);
        return flat.GetLengthSq() > 1.0e-6f ? flat.GetNormalized() : AZ::Vector3::CreateAxisY();
    }

    bool HearthMonsterComponent::LineOfSight(const AZ::Vector3& a, const AZ::Vector3& b)
    {
        const float length = (b - a).GetLength();
        CellPos cell = Cell(a);
        const int steps = AZStd::max(1, static_cast<int>(length / 0.2f));
        for (int i = 1; i <= steps; ++i)
        {
            const CellPos next = Cell(a + (b - a) * (static_cast<float>(i) / steps));
            if (next == cell)
            {
                continue;
            }
            const int dx = next.x - cell.x;
            const int dy = next.y - cell.y;
            if (std::abs(dx) + std::abs(dy) == 1)
            {
                if (!Open(cell, dx, dy))
                {
                    return false;
                }
            }
            else if (!(Open(cell, dx, 0) && Open({ cell.x + dx, cell.y }, 0, dy)) && !(Open(cell, 0, dy) && Open({ cell.x, cell.y + dy }, dx, 0)))
            {
                return false;
            }
            cell = next;
        }
        return true;
    }

    AZStd::vector<HearthMonsterComponent::CellPos> HearthMonsterComponent::FindPath(const CellPos& from, const CellPos& to)
    {
        struct Node
        {
            int f;
            AZ::u64 key;
            bool operator>(const Node& o) const { return f > o.f; }
        };
        AZStd::priority_queue<Node, AZStd::vector<Node>, AZStd::greater<Node>> open;
        AZStd::unordered_map<AZ::u64, CellPos> cells;
        AZStd::unordered_map<AZ::u64, AZ::u64> cameFrom;
        AZStd::unordered_map<AZ::u64, int> cost;
        const AZ::u64 start = MonsterCellKey(from.x, from.y);
        cells[start] = from;
        cost[start] = 0;
        open.push({ 0, start });
        int visited = 0;
        while (!open.empty() && visited++ < 4000)
        {
            const AZ::u64 key = open.top().key;
            open.pop();
            const CellPos current = cells[key];
            if (current == to)
            {
                AZStd::vector<CellPos> path;
                AZ::u64 at = key;
                while (at != start)
                {
                    path.insert(path.begin(), cells[at]);
                    at = cameFrom[at];
                }
                return path;
            }
            const int directions[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            for (const auto& d : directions)
            {
                if (!Open(current, d[0], d[1]))
                {
                    continue;
                }
                const CellPos next{ current.x + d[0], current.y + d[1] };
                if (InSafe(next) && !InSafe(current))
                {
                    continue; // never into the spawn room (one already inside can still walk out)
                }
                const AZ::u64 nextKey = MonsterCellKey(next.x, next.y);
                const int newCost = cost[key] + 1;
                auto known = cost.find(nextKey);
                if (known == cost.end() || newCost < known->second)
                {
                    cost[nextKey] = newCost;
                    cells[nextKey] = next;
                    cameFrom[nextKey] = key;
                    open.push({ newCost + std::abs(to.x - next.x) + std::abs(to.y - next.y), nextKey });
                }
            }
        }
        return {};
    }

    void HearthMonsterComponent::Report(const AZ::Vector3& at, const AZStd::string& state)
    {
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            const AZStd::string base = "/NoclipProtocol/Monsters/" + m_name;
            registry->Set(base + "/X", static_cast<double>(at.GetX()));
            registry->Set(base + "/Y", static_cast<double>(at.GetY()));
            registry->Set(base + "/State", state);
        }
        if (state != m_lastState)
        {
            // Every change of mind goes in the log (what it was doing when a fight went wrong is the first question).
            m_lastState = state;
            AZ_Printf("Noclip", "monster: %s -> %s (path %d)", m_name.c_str(), state.c_str(), static_cast<int>(m_path.size()));
        }
    }

    void HearthMonsterComponent::Play(const char* motion, bool loop, float speed)
    {
        using EMotionFX::Integration::SimpleMotionComponentRequestBus;
        if (m_motions.empty())
        {
            return;
        }
        if (m_playing != motion)
        {
            AZ::Data::AssetId id;
            const AZStd::string path = m_motions + "_" + motion + ".motion";
            AZ::Data::AssetCatalogRequestBus::BroadcastResult(id, &AZ::Data::AssetCatalogRequests::GetAssetIdByPath, path.c_str(),
                                                              AZ::Data::s_invalidAssetType, false);
            if (!id.IsValid())
            {
                // A four-legged rig only has a walk: it stands for every other motion (slowed for idle, quickened
                // for a run or a strike).
                if (AZStd::string(motion) != "walk")
                {
                    const float scaled = AZStd::string(motion) == "idle" ? 0.2f : speed * 1.4f;
                    Play("walk", true, scaled);
                }
                return;
            }
            m_playing = motion;
            SimpleMotionComponentRequestBus::Event(GetEntityId(), &SimpleMotionComponentRequestBus::Events::LoopMotion, loop);
            SimpleMotionComponentRequestBus::Event(GetEntityId(), &SimpleMotionComponentRequestBus::Events::BlendInTime, 0.25f);
            SimpleMotionComponentRequestBus::Event(GetEntityId(), &SimpleMotionComponentRequestBus::Events::BlendOutTime, 0.25f);
            SimpleMotionComponentRequestBus::Event(GetEntityId(), &SimpleMotionComponentRequestBus::Events::Motion, id);
        }
        SimpleMotionComponentRequestBus::Event(GetEntityId(), &SimpleMotionComponentRequestBus::Events::SetPlaySpeed, speed);
    }

    void HearthMonsterComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        m_dt = deltaTime;
        if (!m_preloadDone && !m_motions.empty())
        {
            m_preloadDone = true;
            for (const char* motion : { "idle", "walk", "run", "slash" })
            {
                AZ::Data::AssetId id;
                const AZStd::string path = m_motions + "_" + motion + ".motion";
                AZ::Data::AssetCatalogRequestBus::BroadcastResult(id, &AZ::Data::AssetCatalogRequests::GetAssetIdByPath, path.c_str(),
                                                                  AZ::Data::s_invalidAssetType, false);
                if (!id.IsValid())
                {
                    continue;
                }
                AZ::Data::AssetInfo info;
                AZ::Data::AssetCatalogRequestBus::BroadcastResult(info, &AZ::Data::AssetCatalogRequests::GetAssetInfoById, id);
                m_preloaded.push_back(AZ::Data::AssetManager::Instance().GetAsset(id, info.m_assetType, AZ::Data::AssetLoadBehavior::PreLoad));
            }
        }
        auto* registry = AZ::SettingsRegistry::Get();
        bool enabled = true;
        if (registry)
        {
            registry->Get(enabled, "/NoclipProtocol/Monsters/Enabled");
        }
        AZ::Vector3 at = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(at, GetEntityId(), &AZ::TransformBus::Events::GetWorldTranslation);
        // Shot (also while frozen for testing): Weapons/Hit names the entity a bullet struck.
        {
            AZ::Vector3 shooterAt = AZ::Vector3::CreateZero();
            AZ::TransformBus::EventResult(shooterAt, MonsterPlayer(), &AZ::TransformBus::Events::GetWorldTranslation);
            AZ::s64 hits = 0;
            if (registry)
            {
                registry->Get(hits, "/NoclipProtocol/Weapons/Hit/Serial");
                if (m_hitsSeen >= 0 && hits > m_hitsSeen)
                {
                    AZ::u64 hitEntity = 0;
                    double damage = 0.0;
                    registry->Get(hitEntity, "/NoclipProtocol/Weapons/Hit/Entity");
                    registry->Get(damage, "/NoclipProtocol/Weapons/Hit/Damage");
                    if (hitEntity == static_cast<AZ::u64>(GetEntityId()) && m_health > 0.0f)
                    {
                        if (m_kind == "Scp096")
                        {
                            damage = 0.0;            // can't be killed: bullets only slow it a moment, and wake it
                            m_slowed = 0.8f;
                            if (m_mood == 0)
                            {
                                m_mood = 1;
                                m_moodTime = 6.0f;
                            }
                        }
                        else if (m_kind == "Scp106")
                        {
                            damage = 0.0;            // can't be killed: shot, it sinks back into the floor
                            if (m_sink < 0.0f && m_awayFor <= 0.0f && m_rise <= 0.0f)
                            {
                                m_sink = 0.0f;
                            }
                        }
                        else if (m_kind == "Scp173")
                        {
                            damage = 0.0;            // concrete: bullets do nothing
                        }
                        else if (m_kind == "Faceling")
                        {
                            m_provoked = 12.0f;
                        }
                        m_health -= static_cast<float>(damage);
                        if (m_health <= 0.0f)
                        {
                            Loop(false);
                            Sound(MonsterSounds::Die, true);
                        }
                        else if (damage > 0.0)
                        {
                            Sound(MonsterSounds::Hurt, true);
                            m_flinch = 0.28f;
                        }
                        m_heardAt = shooterAt;   // it knows where that came from
                        m_sinceHeard = 0.0f;
                        AZ_Printf("Noclip", "monster: %s hit for %.0f (%.0f left)%s", m_name.c_str(), damage, AZStd::max(0.0f, m_health),
                                  m_health <= 0.0f ? ", dies" : "");
                        m_fleeing = AZStd::max(0.0f, m_fleeing - 1.0f);
                    }
                }
            }
            m_hitsSeen = hits;
        }
        if (!enabled || !Settings())
        {
            Report(at, enabled ? "Waiting for the level" : "Off");
            return;
        }
        const AZ::EntityId player = MonsterPlayer();
        AZ::Vector3 playerAt = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(playerAt, player, &AZ::TransformBus::Events::GetWorldTranslation);
        if (registry)
        {
            registry->Set("/NoclipProtocol/Player/X", static_cast<double>(playerAt.GetX()));
            registry->Set("/NoclipProtocol/Player/Y", static_cast<double>(playerAt.GetY()));
        }
        AZStd::string playerName = "you";
        AZ::ComponentApplicationBus::BroadcastResult(playerName, &AZ::ComponentApplicationRequests::GetEntityName, player);

        // Hearing: a new footstep within the hearing distance.
        m_sinceHeard += deltaTime;
        AZ::s64 steps = 0;
        if (registry)
        {
            registry->Get(steps, "/NoclipProtocol/Debug/Steps");
        }
        const AZ::Vector3 flat(playerAt.GetX() - at.GetX(), playerAt.GetY() - at.GetY(), 0.0f);
        const float distance = flat.GetLength();
        if (m_stepsSeen >= 0 && steps > m_stepsSeen && distance <= m_hearing)
        {
            m_heardAt = playerAt;
            m_sinceHeard = 0.0f;
            if (registry)
            {
                AZ::s64 heard = 0;
                registry->Get(heard, "/NoclipProtocol/Debug/StepsHeard");
                registry->Set("/NoclipProtocol/Debug/StepsHeard", heard + (steps - m_stepsSeen));
            }
        }
        m_stepsSeen = steps;

        // Sounds: now and then while it's near; its loop (SCP-173's scrape while it moves, SCP-096's crying).
        {
            m_soundCooldown = AZStd::max(0.0f, m_soundCooldown - deltaTime);
            m_alertCooldown = AZStd::max(0.0f, m_alertCooldown - deltaTime);
            const float moved = deltaTime > 0.0f ? (at - m_lastAt).GetLength() / deltaTime : 0.0f;
            m_lastAt = at;
            if (m_kind == "Scp173" || m_kind == "Mannequin")
            {
                Loop(moved > 0.2f && moved < 30.0f && distance < 40.0f);
            }
            else if (m_kind == "Scp096")
            {
                Loop(m_mood == 0 && distance < 40.0f);
            }
            else if ((m_idleSoundIn -= deltaTime) <= 0.0f)
            {
                m_idleSoundIn = 6.0f + static_cast<float>(std::rand() % 900) / 100.0f;
                if (distance < 30.0f && m_health > 0.0f)
                {
                    Sound(MonsterSounds::Idle);
                }
            }
        }
        // A gunshot carries much further than a step: heard out to four times the hearing distance (a knife is quiet).
        AZ::s64 shots = 0;
        if (registry)
        {
            registry->Get(shots, "/NoclipProtocol/Player/Shots");
        }
        if (m_shotsSeen >= 0 && shots > m_shotsSeen && distance <= m_hearing * 4.0f)
        {
            m_heardAt = playerAt;
            m_sinceHeard = 0.0f;
        }
        m_shotsSeen = shots;

        // Sight: within the distance, in front (60° each way), nothing in between.
        AZ::Transform self = AZ::Transform::CreateIdentity();
        AZ::TransformBus::EventResult(self, GetEntityId(), &AZ::TransformBus::Events::GetWorldTM);
        const bool sees = player.IsValid() && distance <= m_sight && distance > 0.01f &&
            Ahead(self).Dot(flat / distance) >= 0.5f && LineOfSight(at, playerAt);

        if (m_health <= 0.0f)
        {
            // Dead: it falls back, hard, lies there a moment, then sinks into the dark (the spawner removes it after).
            if (m_sinceDeath == 0.0f)
            {
                // Where it faces (models that aren't built facing +Y carry m_turn on top).
                m_deathTurn = m_hasYaw ? AZ::Quaternion::CreateRotationZ(m_yaw)
                                       : self.GetRotation() * AZ::Quaternion::CreateRotationZ(-m_turn);
                m_deathAt = at;
                m_deathScale = self.GetUniformScale();
            }
            if (m_sinceDeath == 0.0f)
            {
                // Freeze the pose it was in when it died (switching to a stopped idle showed the bare rest pose, a
                // T-pose, while it fell: owner, Nov 28).
                if (m_playing.empty())
                {
                    Play("idle", true, 0.0f);
                }
                EMotionFX::Integration::SimpleMotionComponentRequestBus::Event(
                    GetEntityId(), &EMotionFX::Integration::SimpleMotionComponentRequestBus::Events::SetPlaySpeed, 0.0f);
            }
            m_sinceDeath += deltaTime;
            const float fall = AZStd::min(1.0f, m_sinceDeath / 0.55f);
            const float eased = fall * fall;                                   // gravity: slow, then fast
            const float bounce = fall >= 1.0f ? std::exp(-9.0f * (m_sinceDeath - 0.55f)) * std::sin((m_sinceDeath - 0.55f) * 30.0f) * 0.06f : 0.0f;
            const float tilt = AZ::DegToRad(84.0f) * eased - bounce;
            const bool fourLegs = m_kind == "Hound" || m_kind == "Scp939" || m_kind == "Crawler"; // these drop onto their side
            AZ::Vector3 lie = m_deathAt;
            lie.SetZ(m_deathAt.GetZ() - 0.12f * eased - (m_sinceDeath > 2.2f ? (m_sinceDeath - 2.2f) * 0.9f : 0.0f));
            AZ::Transform tm = AZ::Transform::CreateFromQuaternionAndTranslation(
                m_deathTurn * (fourLegs ? AZ::Quaternion::CreateRotationY(tilt * 0.95f) : AZ::Quaternion::CreateRotationX(tilt)) *
                AZ::Quaternion::CreateRotationZ(m_turn), lie);
            tm.SetUniformScale(m_deathScale);
            AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldTM, tm);
            Report(at, "Dead");
            return;
        }

        // Light: a Smiler can't stand being lit. The flashlight on it for a moment, or a burning flare near the
        // player, sends it back into the dark for a few seconds.
        bool flashlight = false;
        double flare = 0.0;
        if (registry)
        {
            registry->Get(flashlight, "/NoclipProtocol/Player/FlashlightOn");
            registry->Get(flare, "/NoclipProtocol/Player/FlareLight");
        }
        AZ::Transform playerTm = AZ::Transform::CreateIdentity();
        AZ::TransformBus::EventResult(playerTm, player, &AZ::TransformBus::Events::GetWorldTM);
        const AZ::Vector3 playerAhead = playerTm.GetBasisY().GetNormalized();
        const bool inBeam = flashlight && distance < 11.0f && distance > 0.01f &&
            playerAhead.Dot(-flat / distance) >= 0.9f && LineOfSight(playerAt, at);
        m_lit = inBeam ? m_lit + deltaTime : AZStd::max(0.0f, m_lit - deltaTime * 0.5f);
        if (m_kind == "Smiler" && (m_lit > 1.2f || (flare > 0.3 && distance < 9.0f)))
        {
            m_fleeing = 4.0f;
            m_lit = 0.0f;
            m_attack = -1.0f;
        }
        m_fleeing = AZStd::max(0.0f, m_fleeing - deltaTime);
        m_attackCooldown = AZStd::max(0.0f, m_attackCooldown - deltaTime);
        m_slowed = AZStd::max(0.0f, m_slowed - deltaTime);
        if (!m_hasFloor)
        {
            m_floorZ = at.GetZ();
            m_hasFloor = true;
        }
        auto drainSanity = [registry](double amount)
        {
            if (registry)
            {
                double add = 0.0;
                registry->Get(add, "/NoclipProtocol/Player/SanityAdd");
                registry->Set("/NoclipProtocol/Player/SanityAdd", add - amount);
            }
        };
        auto facePlayer = [this, &flat, distance]()
        {
            if (distance > 0.01f)
            {
                const AZ::Vector3 face = flat / distance;
                Face(face, 8.0f);
            }
        };
        // The player has it on screen (near the middle of the view, nothing in between).
        const bool lookedAt = distance > 0.01f && playerAhead.Dot(-flat / distance) >= 0.8f && LineOfSight(playerAt, at);

        // ------------------------------------------------------------ Kinds
        if (m_kind == "Faceling")
        {
            // Unsettling, not deadly: it follows you at a distance and stares; it freezes when you look at it. Hurt,
            // it charges once, strikes, and runs off into the dark.
            if (m_provoked > 0.0f)
            {
                m_provoked -= deltaTime;
                if (m_attack < 0.0f && distance < 1.7f && m_attackCooldown <= 0.0f)
                {
                    m_attack = 0.0f;
                    Sound(MonsterSounds::Attack, true);
                    m_struck = false;
                }
                if (m_attack >= 0.0f)
                {
                    const bool hadStruck = m_struck;
                    Strike(deltaTime, distance, flat, 0.10f, 0.03f, playerName, at);
                    if (!hadStruck && m_struck)
                    {
                        m_provoked = 0.0f;
                        m_fleeing = 8.0f;
                    }
                    return;
                }
                at = Walk(at, Cell(playerAt), m_speed * 1.6f, deltaTime, true, playerAt);
                Report(at, "Charging " + playerName);
                return;
            }
            if (m_fleeing > 0.0f)
            {
                const AZ::Vector3 away = distance > 0.01f ? -flat / distance : AZ::Vector3::CreateAxisY();
                at = Walk(at, Cell(at + away * 14.0f), m_speed * 1.4f, deltaTime, false, playerAt);
                Report(at, "Running off");
                return;
            }
            if (sees || (m_sinceHeard < 6.0f && distance < m_sight))
            {
                drainSanity(0.006 * deltaTime);
                if (lookedAt || distance <= 5.0f)
                {
                    Walk(at, Cell(at), 0.0f, deltaTime, false, playerAt);
                    facePlayer();
                    Report(at, lookedAt ? "Frozen, staring" : "Staring at " + playerName);
                    return;
                }
                at = Walk(at, Cell(playerAt), m_speed * 0.5f, deltaTime, false, playerAt);
                Report(at, "Following " + playerName);
                return;
            }
            at = Walk(at, WanderGoal(at), m_speed * 0.4f, deltaTime, false, playerAt);
            Report(at, "Drifting");
            return;
        }

        if (m_kind == "Scp096")
        {
            // Harmless while it cries, facing away. See its face and it screams, shakes for six seconds (your head
            // start), then runs you down through the doorways; one hit kills. Break its line of sight long enough and it
            // loses you: it wanders a while, then sits down and cries again. Bullets only slow it a moment.
            const bool faceShown = distance > 0.01f && Ahead(self).Dot(flat / distance) > 0.2f;
            const bool seesYou = player.IsValid() && distance < 40.0f && LineOfSight(at, playerAt);
            const float rageSpeed = 6.5f * (m_slowed > 0.0f ? 0.3f : 1.0f);
            switch (m_mood)
            {
            case 0:
                Walk(at, Cell(at), 0.0f, deltaTime, false, playerAt);
                Play("idle", true, 0.5f);
                if (lookedAt && faceShown && distance < 25.0f)
                {
                    m_mood = 1;
                    m_moodTime = 6.0f;
                    drainSanity(0.10);
                    if (registry)
                    {
                        AZ::s64 screams = 0;
                        registry->Get(screams, "/NoclipProtocol/Monsters/Scp096/Screams");
                        registry->Set("/NoclipProtocol/Monsters/Scp096/Screams", screams + 1);
                    }
                    AZ_Printf("Noclip", "monster: %s saw your face looking at it: panic", m_name.c_str());
                }
                Report(at, "Crying");
                return;
            case 1:
                m_moodTime -= deltaTime;
                facePlayer();
                Play("idle", true, 3.0f);
                drainSanity(0.01 * deltaTime);
                if (m_moodTime <= 0.0f)
                {
                    m_mood = 2;
                    Sound(MonsterSounds::Alert, true);
                    m_lostFor = 0.0f;
                    m_heardAt = playerAt;
                }
                Report(at, "Screaming");
                return;
            case 2:
            {
                if (seesYou)
                {
                    m_heardAt = playerAt;
                    m_lostFor = 0.0f;
                }
                else
                {
                    m_lostFor += deltaTime;
                }
                if (m_attack < 0.0f && seesYou && distance < 1.8f)
                {
                    m_attack = 0.0f;
                    Sound(MonsterSounds::Attack, true);
                    m_struck = false;
                }
                if (m_attack >= 0.0f)
                {
                    const bool hadStruck = m_struck;
                    Strike(deltaTime, distance, flat, 1.0f, 0.2f, playerName, at);
                    if (!hadStruck && m_struck)
                    {
                        m_mood = 3;
                        m_moodTime = 25.0f;
                    }
                    return;
                }
                at = Walk(at, Cell(m_heardAt), rageSpeed, deltaTime, seesYou, playerAt);
                if (m_lostFor > 4.0f && (Cell(at) == Cell(m_heardAt) || m_lostFor > 12.0f))
                {
                    m_mood = 3;
                    m_moodTime = 25.0f;
                    AZ_Printf("Noclip", "monster: %s lost you", m_name.c_str());
                }
                Report(at, m_lostFor > 0.0f ? "Raging, lost sight" : "Raging at " + playerName);
                return;
            }
            default:
                if (seesYou && distance < m_sight && Ahead(self).Dot(flat / AZStd::max(distance, 0.01f)) >= 0.5f)
                {
                    m_mood = 2;
                    Sound(MonsterSounds::Alert, true);
                    m_lostFor = 0.0f;
                    m_heardAt = playerAt;
                    return;
                }
                m_moodTime -= deltaTime;
                at = Walk(at, WanderGoal(at), 1.6f, deltaTime, false, playerAt);
                if (m_moodTime <= 0.0f)
                {
                    m_mood = 0;
                    m_playing.clear();
                }
                Report(at, "Wandering, searching");
                return;
            }
        }

        if (m_kind == "Scp106")
        {
            // Rises out of the floor, walks after you slowly through the doorways and never gives up. Shoot it and it
            // sinks back into the floor (your chance to get away) and rises somewhere else a couple of minutes later.
            // Flares slow it. If it reaches you it drags you down: you come up somewhere else in the level, hurt and
            // shaken (its Pocket Dimension comes later).
            if (m_awayFor > 0.0f)
            {
                m_awayFor -= deltaTime;
                at.SetZ(m_floorZ - 4.0f);
                if (m_awayFor <= 0.0f)
                {
                    const float angle = static_cast<float>(std::rand() % 6283) / 1000.0f;
                    const float radius = 18.0f + static_cast<float>(std::rand() % 800) / 100.0f;
                    const CellPos c = Cell(playerAt + AZ::Vector3(std::cos(angle), std::sin(angle), 0.0f) * radius);
                    if (InSafe(c))
                    {
                        m_awayFor = 8.0f; // not in the spawn room: try again in a bit
                        AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldTranslation, at);
                        return;
                    }
                    at = Centre(c, m_floorZ - 2.6f);
                    m_rise = 2.0f;
                    m_path.clear();
                    AZ_Printf("Noclip", "monster: %s rises again at %.1f %.1f", m_name.c_str(), at.GetX(), at.GetY());
                }
                AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldTranslation, at);
                Report(at, "Under the floor");
                return;
            }
            if (m_sink >= 0.0f)
            {
                m_sink += deltaTime;
                at.SetZ(m_floorZ - m_sink * 1.4f);
                AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldTranslation, at);
                Play("idle", true, 0.5f);
                if (m_sink >= 2.0f)
                {
                    m_sink = -1.0f;
                    m_awayFor = 120.0f + static_cast<float>(std::rand() % 60);
                }
                Report(at, "Sinking into the floor");
                return;
            }
            if (m_rise > 0.0f)
            {
                m_rise = AZStd::max(0.0f, m_rise - deltaTime);
                at.SetZ(m_floorZ - m_rise * 1.3f);
                AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldTranslation, at);
                facePlayer();
                Play("idle", true, 0.7f);
                Report(at, "Rising out of the floor");
                return;
            }
            if (sees)
            {
                drainSanity(0.008 * deltaTime);
            }
            if (sees && distance < 1.3f && registry)
            {
                // The grab: dragged down, and up again somewhere else in the level.
                double damage = 0.0;
                registry->Get(damage, "/NoclipProtocol/Player/Damage");
                registry->Set("/NoclipProtocol/Player/Damage", damage + 0.25);
                registry->Set("/NoclipProtocol/Player/LastHitBy", m_kind);
                drainSanity(0.35);
                AZ::Entity* playerEntity = nullptr;
                AZ::ComponentApplicationBus::BroadcastResult(playerEntity, &AZ::ComponentApplicationRequests::FindEntity, player);
                if (auto* controller = playerEntity ? playerEntity->FindComponent<FirstPersonControllerComponent>() : nullptr)
                {
                    const float angle = static_cast<float>(std::rand() % 6283) / 1000.0f;
                    const float radius = 40.0f + static_cast<float>(std::rand() % 2000) / 100.0f;
                    const CellPos c = Cell(playerAt + AZ::Vector3(std::cos(angle), std::sin(angle), 0.0f) * radius);
                    controller->Teleport(Centre(c, playerAt.GetZ()), 0.0f, 0.0f, false, false);
                }
                AZ_Printf("Noclip", "monster: %s dragged you down", m_name.c_str());
                m_sink = 0.0f;
                Report(at, "Dragged " + playerName + " down");
                return;
            }
            const float speed = m_speed * (flare > 0.3 && distance < 9.0f ? 0.5f : 1.0f) * (m_slowed > 0.0f ? 0.5f : 1.0f);
            at = Walk(at, Cell(playerAt), speed, deltaTime, sees, playerAt);
            Report(at, sees ? "Coming for " + playerName : "Following your trail");
            return;
        }

        if (m_kind == "Scp173")
        {
            // Owner, Nov 27: slow, and it can't move at all while you're looking at it. "Looking" is anywhere on the
            // screen, edges included (a wide cone, wider still up close), not just the middle; it kills on reaching you
            // and can't be hurt.
            const float facing = distance > 0.01f ? playerAhead.Dot(-flat / distance) : 1.0f;
            const bool watched = (facing >= 0.25f || (distance < 3.0f && facing > -0.1f)) && LineOfSight(playerAt, at);
            if (watched)
            {
                Walk(at, Cell(at), 0.0f, deltaTime, false, playerAt);
                Report(at, "Watched (still)");
                return;
            }
            if (distance < 1.3f && registry)
            {
                double damage = 0.0;
                registry->Get(damage, "/NoclipProtocol/Player/Damage");
                registry->Set("/NoclipProtocol/Player/Damage", damage + 1.0);
                registry->Set("/NoclipProtocol/Player/LastHitBy", m_kind);
                drainSanity(0.3);
                AZ_Printf("Noclip", "monster: %s reached you", m_name.c_str());
                Sound(MonsterSounds::Attack, true);
                Report(at, "Reached " + playerName);
                return;
            }
            at = Walk(at, Cell(playerAt), 1.9f, deltaTime, true, playerAt);
            Report(at, "Moving (unwatched)");
            return;
        }

        if (m_kind == "Doll" || m_kind == "Mannequin" || m_kind == "Plush" || m_kind == "Watcher")
        {
            // Statues of a kind: they move only while nobody looks at them (the Watcher never moves: looking at it
            // wears on you instead). Reaching you hurts and shakes you; they can't be hurt.
            const float facing = distance > 0.01f ? playerAhead.Dot(-flat / distance) : 1.0f;
            const bool watched = (facing >= 0.25f || (distance < 3.0f && facing > -0.1f)) && LineOfSight(playerAt, at);
            if (m_kind == "Watcher")
            {
                Walk(at, Cell(at), 0.0f, deltaTime, distance < 25.0f, playerAt);
                if (watched && distance < 25.0f)
                {
                    drainSanity(0.03 * deltaTime);
                }
                Report(at, watched ? "Staring back" : "Watching");
                return;
            }
            if (watched)
            {
                Walk(at, Cell(at), 0.0f, deltaTime, false, playerAt);
                Report(at, "Watched (still)");
                return;
            }
            if (distance < 1.3f && m_attackCooldown <= 0.0f && registry)
            {
                double damage = 0.0;
                registry->Get(damage, "/NoclipProtocol/Player/Damage");
                registry->Set("/NoclipProtocol/Player/Damage", damage + (m_kind == "Plush" ? 0.15 : 0.25));
                registry->Set("/NoclipProtocol/Player/LastHitBy", m_kind);
                drainSanity(0.12);
                m_attackCooldown = 2.0f;
                Sound(MonsterSounds::Attack, true);
                Report(at, "Reached " + playerName);
                return;
            }
            at = Walk(at, Cell(playerAt), m_speed, deltaTime, true, playerAt);
            Report(at, "Moving (unwatched)");
            return;
        }

        if (m_kind == "Scarecrow" && !sees && m_sinceHeard >= 6.0f)
        {
            // Stands stock still out in the open until you come close.
            Walk(at, Cell(at), 0.0f, deltaTime, false, playerAt);
            Report(at, "Standing in the field");
            return;
        }

        if (sees && (m_kind == "Frostwraith" || m_kind == "Partygoer" || m_kind == "Clown" || m_kind == "Dancer"))
        {
            drainSanity(0.008 * deltaTime);
        }

        if (m_kind == "Deathmoth")
        {
            // Flutters about at head height and is drawn to light: a lit flashlight within 20 m and it comes for
            // you, biting (a little health, more sanity). Easy to kill.
            const float hover = m_floorZ + 2.1f + 0.25f * std::sin(m_moodTime * 3.7f);
            m_moodTime += deltaTime;
            at.SetZ(hover);
            const bool drawn = (flashlight && distance < 20.0f) || (sees && distance < 6.0f);
            if (drawn && distance < 1.4f && m_attackCooldown <= 0.0f && registry)
            {
                double damage = 0.0;
                registry->Get(damage, "/NoclipProtocol/Player/Damage");
                registry->Set("/NoclipProtocol/Player/Damage", damage + 0.04);
                registry->Set("/NoclipProtocol/Player/LastHitBy", m_kind);
                drainSanity(0.03);
                m_attackCooldown = 1.2f;
            }
            const AZ::Vector3 swirl(std::cos(m_moodTime * 2.3f) * 0.8f, std::sin(m_moodTime * 1.9f) * 0.8f, 0.0f);
            at = Walk(at, drawn ? Cell(playerAt) : WanderGoal(at), drawn ? 4.0f : 1.4f, deltaTime, drawn, playerAt + swirl);
            Report(at, drawn ? "Drawn to your light" : "Fluttering");
            return;
        }

        if (m_kind == "Clump")
        {
            // A slow rolling mass that smells you out to 25 m and never stops coming; a heavy hit up close.
            m_moodTime += deltaTime;
            AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetLocalUniformScale,
                1.0f + 0.05f * std::sin(m_moodTime * 2.6f));
            if (distance < 2.0f && m_attackCooldown <= 0.0f && registry)
            {
                double damage = 0.0;
                registry->Get(damage, "/NoclipProtocol/Player/Damage");
                registry->Set("/NoclipProtocol/Player/Damage", damage + 0.3);
                registry->Set("/NoclipProtocol/Player/LastHitBy", m_kind);
                drainSanity(0.05);
                m_attackCooldown = 2.0f;
            }
            if (distance < 25.0f)
            {
                drainSanity(0.004 * deltaTime);
                at = Walk(at, Cell(playerAt), 1.0f, deltaTime, true, playerAt);
                Report(at, "Rolling after " + playerName);
            }
            else
            {
                at = Walk(at, WanderGoal(at), 0.5f, deltaTime, false, playerAt);
                Report(at, "Rolling");
            }
            return;
        }

        if (m_kind == "Scp939")
        {
            // Blind: it hunts by sound alone (footsteps, gunshots) and only knows you're there by touch up close.
            const bool touches = distance < 2.2f;
            if (m_attack < 0.0f && touches && m_attackCooldown <= 0.0f)
            {
                m_attack = 0.0f;
                Sound(MonsterSounds::Attack, true);
                m_struck = false;
            }
            if (m_attack >= 0.0f)
            {
                Strike(deltaTime, distance, flat, 0.35f, 0.05f, playerName, at);
                return;
            }
            if (m_sinceHeard < 8.0f)
            {
                at = Walk(at, Cell(m_heardAt), 4.5f, deltaTime, touches, playerAt);
                Report(at, AZStd::string::format("Heard you · %d m", static_cast<int>((m_heardAt - at).GetLength() + 0.5f)));
            }
            else
            {
                at = Walk(at, WanderGoal(at), 1.2f, deltaTime, false, playerAt);
                Report(at, "Listening");
            }
            return;
        }

        // ------------------------------------------------------------ Smiler (and the default)
        // Its grin in the dark wears on you.
        if (sees && m_kind == "Smiler")
        {
            drainSanity(0.012 * deltaTime);
        }
        // Up close it strikes: the blow lands partway into the attack, if you're still in reach.
        if (m_attack < 0.0f && m_fleeing <= 0.0f && sees && distance < 1.7f && m_attackCooldown <= 0.0f &&
            Ahead(self).Dot(flat / AZStd::max(distance, 0.01f)) < 0.9f)
        {
            // Too close to swing sideways at you: it squares up first (a quick turn on the spot, no sliding).
            Face(flat, 9.0f);
            Play("idle", true, 1.0f);
            Report(at, "Squaring up to " + playerName);
            return;
        }
        if (m_attack < 0.0f && m_fleeing <= 0.0f && sees && distance < 1.7f && m_attackCooldown <= 0.0f)
        {
            m_attack = 0.0f;
            m_struck = false;
            Sound(MonsterSounds::Attack, true);
        }
        if (m_attack >= 0.0f)
        {
            float damage = m_kind == "Skinstealer" ? 0.25f : m_kind == "Hound" ? 0.12f : m_kind == "Wretch" ? 0.10f : 0.15f;
            if (m_kind == "Scp049") { damage = 0.5f; }   // its touch: "the cure"
            else if (m_kind == "Diver" || m_kind == "Nurse") { damage = 0.25f; }
            else if (m_kind == "Crawler" || m_kind == "Patient") { damage = 0.12f; }
            else if (m_kind == "Miner" || m_kind == "Farmthing" || m_kind == "Scarecrow") { damage = 0.2f; }
            Strike(deltaTime, distance, flat, damage, 0.03f, playerName, at);
            return;
        }

        AZStd::string state;
        CellPos goal = Cell(at);
        float speed = m_speed;
        if (m_fleeing > 0.0f)
        {
            state = "Backing away from the light";
            speed = m_speed * 1.3f;
            const AZ::Vector3 away = distance > 0.01f ? -flat / distance : AZ::Vector3::CreateAxisY();
            goal = Cell(at + away * 12.0f);
        }
        else if (sees)
        {
            state = "Hunting " + playerName;
            goal = Cell(playerAt);
            if (m_kind == "Skinstealer" && distance > 6.0f)
            {
                // Wearing someone's skin it walks up slowly, like a lost agent, until it's close: then it lunges.
                state = "Walking up to " + playerName;
                speed = 1.2f;
            }
        }
        else if (m_sinceHeard < 6.0f)
        {
            state = AZStd::string::format("Hears footsteps · %d m", static_cast<int>((m_heardAt - at).GetLength() + 0.5f));
            goal = Cell(m_heardAt);
        }
        else
        {
            state = "Wandering";
            speed = m_speed * 0.4f;
            goal = WanderGoal(at);
        }
        at = Walk(at, goal, speed, deltaTime, sees && m_fleeing <= 0.0f, playerAt);
        Report(at, state);
    }

    HearthMonsterComponent::CellPos HearthMonsterComponent::WanderGoal(const AZ::Vector3& at)
    {
        if (!m_hasWander || Cell(at) == m_wander)
        {
            const CellPos here = Cell(at);
            m_wander = { here.x + (std::rand() % 9) - 4, here.y + (std::rand() % 9) - 4 };
            m_hasWander = true;
        }
        return m_wander;
    }

    bool HearthMonsterComponent::Strike(float deltaTime, float distance, const AZ::Vector3& flat, float damage, float sanity,
                                        const AZStd::string& playerName, const AZ::Vector3& at)
    {
        auto* registry = AZ::SettingsRegistry::Get();
        m_attack += deltaTime;
        Play("slash", true, 1.6f); // looped: a strike that ran out left the bare rest pose (a T-pose) until the next move
        // It steps into the blow: a short lunge during the wind-up, never closer than arm's length.
        if (m_attack > 0.12f && m_attack < 0.45f && distance > 1.05f)
        {
            AZ::Vector3 here = at;
            here += flat / distance * AZStd::min(distance - 1.05f, 2.4f * deltaTime);
            if (!InSafe(Cell(here)))
            {
                AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldTranslation, here);
            }
        }
        if (!m_struck && m_attack >= 0.45f)
        {
            m_struck = true;
            if (distance < 2.3f && registry && !InSafe(Cell(at + flat)))
            {
                bool shield = false;
                registry->Get(shield, "/NoclipProtocol/Player/Shield");
                double hurt = 0.0;
                registry->Get(hurt, "/NoclipProtocol/Player/Damage");
                // The shield takes most of a blow, but not a killing one.
                registry->Set("/NoclipProtocol/Player/Damage", hurt + (shield && damage < 0.5f ? damage * 0.27f : damage));
                registry->Set("/NoclipProtocol/Player/LastHitBy", m_kind);
                double add = 0.0;
                registry->Get(add, "/NoclipProtocol/Player/SanityAdd");
                registry->Set("/NoclipProtocol/Player/SanityAdd", add - sanity);
            }
        }
        if (distance > 0.01f)
        {
            const AZ::Vector3 face = flat / distance;
            Face(face, 12.0f); // squared up the whole swing
        }
        if (m_attack >= 1.1f)
        {
            m_attack = -1.0f;
            m_attackCooldown = 1.2f;
            m_playing.clear();
        }
        Report(at, "Attacking " + playerName);
        return true;
    }

    void HearthMonsterComponent::LoadSounds()
    {
        for (auto& takes : m_sounds)
        {
            takes.clear();
        }
        for (const MonsterSounds::Kind& kind : MonsterSounds::Kinds)
        {
            if (m_kind != kind.m_kind)
            {
                continue;
            }
            for (int e = 0; e < MonsterSounds::EventCount; ++e)
            {
                for (int n = 1; n <= kind.m_takes[e]; ++n)
                {
                    const AZStd::string product = AZStd::string::format("%s/%s_%d.wav.%s", kind.m_folder, MonsterSounds::EventNames[e], n,
                                                                        MiniAudio::SoundAsset::FileExtension);
                    AZ::Data::AssetId id;
                    AZ::Data::AssetCatalogRequestBus::BroadcastResult(id, &AZ::Data::AssetCatalogRequests::GetAssetIdByPath, product.c_str(),
                                                                      azrtti_typeid<MiniAudio::SoundAsset>(), false);
                    if (id.IsValid())
                    {
                        m_sounds[e].push_back(AZ::Data::AssetManager::Instance().GetAsset(
                            id, azrtti_typeid<MiniAudio::SoundAsset>(), AZ::Data::AssetLoadBehavior::PreLoad));
                    }
                }
            }
        }
    }

    void HearthMonsterComponent::Sound(int event, bool force)
    {
        if (event < 0 || event >= MonsterSounds::EventCount || m_sounds[event].empty() || (!force && m_soundCooldown > 0.0f))
        {
            return;
        }
        // A take that's loaded, never the same one twice running if there's a choice.
        const int count = static_cast<int>(m_sounds[event].size());
        int pick = std::rand() % count;
        for (int tries = 0; tries < count && !m_sounds[event][pick].IsReady(); ++tries)
        {
            pick = (pick + 1) % count;
        }
        if (!m_sounds[event][pick].IsReady())
        {
            return;
        }
        m_looping = false;
        const AZ::EntityId self = GetEntityId();
        MiniAudio::MiniAudioPlaybackRequestBus::Event(self, &MiniAudio::MiniAudioPlaybackRequests::Stop);
        MiniAudio::MiniAudioPlaybackRequestBus::Event(self, &MiniAudio::MiniAudioPlaybackRequests::SetLooping, false);
        MiniAudio::MiniAudioPlaybackRequestBus::Event(self, &MiniAudio::MiniAudioPlaybackRequests::SetSoundAsset,
                                                      AZ::Data::Asset<MiniAudio::SoundAsset>(m_sounds[event][pick]));
        MiniAudio::MiniAudioPlaybackRequestBus::Event(self, &MiniAudio::MiniAudioPlaybackRequests::SetVolumePercentage,
                                                      100.0f * GameSettings::Gain("vol_monsters"));
        MiniAudio::MiniAudioPlaybackRequestBus::Event(self, &MiniAudio::MiniAudioPlaybackRequests::Play);
        m_soundCooldown = event == MonsterSounds::Attack ? 0.8f : 2.0f;
    }

    void HearthMonsterComponent::Loop(bool on)
    {
        auto& takes = m_sounds[MonsterSounds::Loop];
        if (on == m_looping || takes.empty() || (on && !takes[0].IsReady()))
        {
            return;
        }
        // A one-off sound (an attack) finishes before the loop comes back.
        if (on && m_soundCooldown > 0.0f)
        {
            return;
        }
        m_looping = on;
        const AZ::EntityId self = GetEntityId();
        MiniAudio::MiniAudioPlaybackRequestBus::Event(self, &MiniAudio::MiniAudioPlaybackRequests::Stop);
        if (on)
        {
            MiniAudio::MiniAudioPlaybackRequestBus::Event(self, &MiniAudio::MiniAudioPlaybackRequests::SetSoundAsset,
                                                          AZ::Data::Asset<MiniAudio::SoundAsset>(takes[0]));
            MiniAudio::MiniAudioPlaybackRequestBus::Event(self, &MiniAudio::MiniAudioPlaybackRequests::SetLooping, true);
            MiniAudio::MiniAudioPlaybackRequestBus::Event(self, &MiniAudio::MiniAudioPlaybackRequests::SetVolumePercentage,
                                                          100.0f * GameSettings::Gain("vol_monsters"));
            MiniAudio::MiniAudioPlaybackRequestBus::Event(self, &MiniAudio::MiniAudioPlaybackRequests::Play);
        }
    }

    bool HearthMonsterComponent::InSafe(const CellPos& cell)
    {
        if (!m_safeKnown)
        {
            const Level0::Settings* settings = Settings();
            if (!settings)
            {
                return false;
            }
            m_safeKnown = true;
            const Level0::Point spawn = Level0::SpawnPoint(*settings);
            m_safeCentre = Cell(AZ::Vector3(spawn.m_x, spawn.m_y, 0.0f));
            // The reality anchor's field: the room and one cell round it (none in a level without a spawn room).
            m_safeHalf = settings->m_spawnRoomRadius > 0 && settings->m_style != 3 ? settings->m_spawnRoomRadius + 1 : -1;
        }
        return std::abs(cell.x - m_safeCentre.x) <= m_safeHalf && std::abs(cell.y - m_safeCentre.y) <= m_safeHalf;
    }

    HearthMonsterComponent::CellPos HearthMonsterComponent::LeaveGoal(const AZ::Vector3& at)
    {
        // Away from the spawn room, out past where it is now, a little to one side.
        const AZ::Vector3 centre = Centre(m_safeCentre, at.GetZ());
        AZ::Vector3 out(at.GetX() - centre.GetX(), at.GetY() - centre.GetY(), 0.0f);
        out = out.GetLength() > 0.1f ? out.GetNormalized() : AZ::Vector3(1.0f, 0.0f, 0.0f);
        const float side = (static_cast<float>(std::rand() % 1000) / 1000.0f - 0.5f) * 1.4f;
        const AZ::Vector3 dir = AZ::Quaternion::CreateRotationZ(side).TransformVector(out);
        const float far = (m_safeHalf + 7 + std::rand() % 5) * m_cell;
        return Cell(centre + dir * far);
    }

    void HearthMonsterComponent::FaceYaw(float target, float rate)
    {
        if (!m_hasYaw)
        {
            m_hasYaw = true;
            AZ::Quaternion q = AZ::Quaternion::CreateIdentity();
            AZ::TransformBus::EventResult(q, GetEntityId(), &AZ::TransformBus::Events::GetWorldRotationQuaternion);
            m_yaw = q.GetEulerRadians().GetZ() - m_turn;
            m_seed = static_cast<float>(std::rand() % 6283) / 1000.0f;
            m_gait = 0.9f + static_cast<float>(std::rand() % 200) / 1000.0f;
            m_pauseIn = 4.0f + static_cast<float>(std::rand() % 900) / 100.0f;
        }
        float diff = std::remainder(target - m_yaw, AZ::Constants::TwoPi);
        // Eases into the turn and out of it, no faster than a person turns.
        const float step = AZStd::clamp(diff * AZStd::min(1.0f, m_dt * 7.0f), -rate * m_dt, rate * m_dt);
        m_yaw = std::remainder(m_yaw + step, AZ::Constants::TwoPi);
        m_flinch = AZStd::max(0.0f, m_flinch - m_dt);
        const float lean = m_flinch > 0.0f ? -0.22f * std::sin(m_flinch / 0.28f * AZ::Constants::Pi) : 0.0f; // rocked back
        AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldRotationQuaternion,
            AZ::Quaternion::CreateRotationZ(m_yaw) * AZ::Quaternion::CreateRotationX(lean) * AZ::Quaternion::CreateRotationZ(m_turn + m_sway));
    }

    void HearthMonsterComponent::Face(const AZ::Vector3& direction, float rate)
    {
        if (direction.GetX() * direction.GetX() + direction.GetY() * direction.GetY() > 1.0e-6f)
        {
            FaceYaw(std::atan2(-direction.GetX(), direction.GetY()), rate);
        }
    }

    AZ::Vector3 HearthMonsterComponent::Walk(AZ::Vector3 at, const CellPos& wanted, float speed, float deltaTime, bool closeIn,
                                             const AZ::Vector3& target)
    {
        m_dt = deltaTime;
        m_clock += deltaTime;
        if (!m_hasYaw)
        {
            FaceYaw(m_yaw, 0.0f); // reads where it faces now and picks its own pace
        }
        // The spawn room: never in, and no waiting at its door.
        CellPos goal = wanted;
        m_leaveFor = AZStd::max(0.0f, m_leaveFor - deltaTime);
        if (InSafe(goal) && m_leaveFor <= 0.0f)
        {
            m_leaveFor = 25.0f + static_cast<float>(std::rand() % 1500) / 100.0f;
            m_leave = LeaveGoal(at);
            m_repath = 0.0f;
            AZ_Printf("Noclip", "monster: %s turns away from the reality anchor's field (goal cell %d %d, field %d %d +-%d, cell %.1f m)",
                      m_name.c_str(), goal.x, goal.y, m_safeCentre.x, m_safeCentre.y, m_safeHalf, m_cell);
        }
        if (closeIn && !m_wasChasing && m_alertCooldown <= 0.0f && m_kind != "Scp096")
        {
            m_alertCooldown = 12.0f;
            Sound(MonsterSounds::Alert, true);
        }
        m_wasChasing = closeIn;
        const bool leaving = m_leaveFor > 0.0f;
        if (leaving)
        {
            goal = m_leave;
            closeIn = false;
            speed = AZStd::min(speed, m_speed * 0.5f); // walks off, it doesn't flee
        }
        m_repath -= deltaTime;
        if (m_repath <= 0.0f)
        {
            m_repath = 0.4f;
            m_path = FindPath(Cell(at), goal);
        }

        // Its pace: its own, never quite even; on a wander it stops now and then and looks round.
        float pace = speed * m_gait * (1.0f + 0.06f * std::sin(m_clock * 0.8f + m_seed) + 0.03f * std::sin(m_clock * 2.3f + m_seed * 1.7f));
        const bool wandering = !closeIn && speed <= m_speed * 0.55f;
        if (wandering)
        {
            if (m_pausing > 0.0f)
            {
                m_pausing -= deltaTime;
                pace = 0.0f;
            }
            else if ((m_pauseIn -= deltaTime) <= 0.0f)
            {
                m_pausing = 1.2f + static_cast<float>(std::rand() % 250) / 100.0f;
                m_pauseIn = 7.0f + static_cast<float>(std::rand() % 900) / 100.0f;
                m_glance = (std::rand() % 2 ? 1.0f : -1.0f) * (0.5f + static_cast<float>(std::rand() % 60) / 100.0f);
            }
        }
        else
        {
            m_pausing = 0.0f;
        }

        // Where it's heading: along the path, cutting the corners a little instead of touching every cell's middle.
        AZ::Vector3 heading = AZ::Vector3::CreateZero();
        while (!m_path.empty())
        {
            const AZ::Vector3 next = Centre(m_path.front(), at.GetZ());
            const AZ::Vector3 to = next - at;
            const float length = to.GetLength();
            if (length < 0.05f || (m_path.size() >= 2 && length < m_cell * 0.35f))
            {
                m_path.erase(m_path.begin());
                continue;
            }
            heading = to / length;
            break;
        }
        const AZ::Vector3 flat(target.GetX() - at.GetX(), target.GetY() - at.GetY(), 0.0f);
        const float distance = flat.GetLength();
        const bool straight = closeIn && m_path.empty() && distance > 1.0f;
        if (straight)
        {
            heading = flat / distance;
        }

        // Close in on you, it faces you, not the next cell of its path.
        if (closeIn && distance < 4.0f && distance > 0.01f && !m_path.empty() && m_path.size() <= 2 && LineOfSight(at, target))
        {
            heading = flat / distance;
        }
        // Shot, it reels: a moment's stop, rocked back.
        if (m_flinch > 0.0f)
        {
            pace = 0.0f;
            m_curSpeed *= 0.5f;
        }
        // Turns into the way it's going first: it slows for a sharp turn rather than sliding sideways or backwards.
        float turnSlow = 1.0f;
        if (!heading.IsZero())
        {
            const float want = std::atan2(-heading.GetX(), heading.GetY());
            const float off = std::abs(std::remainder(want - m_yaw, AZ::Constants::TwoPi));
            turnSlow = off > 1.2f ? 0.05f : AZStd::clamp(std::cos(off) * 1.15f - 0.15f, 0.15f, 1.0f);
        }
        else
        {
            pace = 0.0f;
        }
        // Statues (SCP-173, dolls, mannequins...) stop dead the instant they're seen: no easing, no turning, no sway.
        const bool statue = m_kind == "Scp173" || m_kind == "Doll" || m_kind == "Mannequin" || m_kind == "Plush" || m_kind == "Watcher";
        if (statue)
        {
            m_pausing = 0.0f;
            turnSlow = 1.0f;
            if (pace <= 0.0f)
            {
                m_curSpeed = 0.0f;
            }
        }
        // Speeds up and slows down like a body with weight; a hunter gets going quicker.
        const float target_ = pace * turnSlow;
        const float accel = target_ > m_curSpeed ? (closeIn ? 6.0f : 2.8f) : 5.0f;
        m_curSpeed += AZStd::clamp(target_ - m_curSpeed, -accel * deltaTime, accel * deltaTime);

        const AZ::Vector3 startAt = at;
        float left = m_curSpeed * deltaTime;
        while (left > 0.0f && !m_path.empty())
        {
            const AZ::Vector3 next = Centre(m_path.front(), at.GetZ());
            const AZ::Vector3 to = next - at;
            const float length = to.GetLength();
            if (length < 0.05f)
            {
                m_path.erase(m_path.begin());
                continue;
            }
            const float move = AZStd::min(left, length);
            at += to / length * move;
            left -= move;
        }
        if (straight && left > 0.0f)
        {
            at += heading * AZStd::min(left, distance - 1.0f);
        }
        if (InSafe(Cell(at)) && !InSafe(Cell(startAt)))
        {
            at = startAt; // the doorway of the spawn room is as far as it gets
            m_curSpeed = 0.0f;
        }
        AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldTranslation, at);

        // Animation follows how fast it actually moved this frame.
        const float moved = deltaTime > 0.0f ? (at - startAt).GetLength() / deltaTime : 0.0f;
        if (moved < 0.15f)
        {
            Play("idle", true, 1.0f);
        }
        else if (moved < 2.6f)
        {
            Play("walk", true, AZ::GetClamp(moved / 1.6f, 0.5f, 1.6f));
        }
        else
        {
            Play("run", true, AZ::GetClamp(moved / 4.5f, 0.7f, 1.5f));
        }
        // A small sway from side to side with each stride.
        m_stride += moved * deltaTime / (moved < 2.6f ? 0.75f : 1.2f);
        m_sway = statue ? 0.0f : 0.035f * std::sin(m_stride * AZ::Constants::Pi) * AZStd::min(1.0f, moved / 1.2f);
        if (m_pausing > 0.0f)
        {
            FaceYaw(m_yaw + m_glance * AZStd::min(1.0f, deltaTime * 2.0f), 1.6f); // looking round
            m_glance *= 1.0f - AZStd::min(1.0f, deltaTime * 2.0f);
        }
        else if (!heading.IsZero() && !(statue && moved < 0.05f))
        {
            Face(heading, statue ? 30.0f : (moved < 2.6f ? 3.5f : 5.0f));
        }
        return at;
    }
} // namespace NoclipProtocol
