#include "HearthMonsterComponent.h"
#include "../Level/EndlessLevelComponent.h"
#include "../Level/Level0Layout.h"
#include "../Player/FirstPersonControllerComponent.h"
#include "SimpleMotionComponentBus.h"

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
                        m_health -= static_cast<float>(damage);
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
            self.GetBasisY().GetNormalized().Dot(flat / distance) >= 0.5f && LineOfSight(at, playerAt);

        if (m_health <= 0.0f)
        {
            // Dead: it sinks back into the dark (the spawner removes it shortly after).
            m_sinceDeath += deltaTime;
            Play("idle", false, 0.0f);
            at.SetZ(at.GetZ() - deltaTime * 1.6f);
            AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldTranslation, at);
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
        const bool inBeam = flashlight && distance < 11.0f && distance > 0.01f &&
            playerTm.GetBasisY().GetNormalized().Dot(-flat / distance) >= 0.9f && LineOfSight(playerAt, at);
        m_lit = inBeam ? m_lit + deltaTime : AZStd::max(0.0f, m_lit - deltaTime * 0.5f);
        if (m_kind == "Smiler" && (m_lit > 1.2f || (flare > 0.3 && distance < 9.0f)))
        {
            m_fleeing = 4.0f;
            m_lit = 0.0f;
            m_attack = -1.0f;
        }
        m_fleeing = AZStd::max(0.0f, m_fleeing - deltaTime);
        m_attackCooldown = AZStd::max(0.0f, m_attackCooldown - deltaTime);

        // Its grin in the dark wears on you.
        if (sees && registry && m_kind == "Smiler")
        {
            double add = 0.0;
            registry->Get(add, "/NoclipProtocol/Player/SanityAdd");
            registry->Set("/NoclipProtocol/Player/SanityAdd", add - 0.012 * deltaTime);
        }

        // Up close it strikes: the blow lands partway into the attack, if you're still in reach.
        if (m_attack < 0.0f && m_fleeing <= 0.0f && sees && distance < 1.7f && m_attackCooldown <= 0.0f)
        {
            m_attack = 0.0f;
            m_struck = false;
        }
        if (m_attack >= 0.0f)
        {
            m_attack += deltaTime;
            Play("slash", false, 1.6f);
            if (!m_struck && m_attack >= 0.45f)
            {
                m_struck = true;
                if (distance < 2.3f && registry)
                {
                    bool shield = false;
                    registry->Get(shield, "/NoclipProtocol/Player/Shield");
                    double damage = 0.0;
                    registry->Get(damage, "/NoclipProtocol/Player/Damage");
                    registry->Set("/NoclipProtocol/Player/Damage", damage + (shield ? 0.04 : 0.15));
                    double add = 0.0;
                    registry->Get(add, "/NoclipProtocol/Player/SanityAdd");
                    registry->Set("/NoclipProtocol/Player/SanityAdd", add - 0.03);
                }
            }
            if (distance > 0.01f)
            {
                const AZ::Vector3 face = flat / distance;
                AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldRotationQuaternion,
                    AZ::Quaternion::CreateRotationZ(std::atan2(-face.GetX(), face.GetY())));
            }
            if (m_attack >= 1.1f)
            {
                m_attack = -1.0f;
                m_attackCooldown = 1.2f;
                m_playing.clear();
            }
            Report(at, "Attacking " + playerName);
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
            if (!m_hasWander || Cell(at) == m_wander)
            {
                const CellPos here = Cell(at);
                m_wander = { here.x + (std::rand() % 9) - 4, here.y + (std::rand() % 9) - 4 };
                m_hasWander = true;
            }
            goal = m_wander;
        }
        m_repath -= deltaTime;
        if (m_repath <= 0.0f)
        {
            m_repath = 0.4f;
            m_path = FindPath(Cell(at), goal);
        }
        // Walk the path; face where it's going.
        float left = speed * deltaTime;
        AZ::Vector3 heading = AZ::Vector3::CreateZero();
        const AZ::Vector3 startAt = at;
        while (left > 0.0f && !m_path.empty())
        {
            const AZ::Vector3 target = Centre(m_path.front(), at.GetZ());
            const AZ::Vector3 to = target - at;
            const float length = to.GetLength();
            if (length < 0.05f)
            {
                m_path.erase(m_path.begin());
                continue;
            }
            const float move = AZStd::min(left, length);
            heading = to / length;
            at += heading * move;
            left -= move;
        }
        if (sees && m_path.empty() && distance > 1.0f)
        {
            heading = flat / distance;
            at += heading * AZStd::min(left, distance - 1.0f);
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
        if (!heading.IsZero())
        {
            AZ::TransformBus::Event(GetEntityId(), &AZ::TransformBus::Events::SetWorldRotationQuaternion,
                AZ::Quaternion::CreateRotationZ(std::atan2(-heading.GetX(), heading.GetY())));
        }
        Report(at, state);
    }
} // namespace NoclipProtocol
