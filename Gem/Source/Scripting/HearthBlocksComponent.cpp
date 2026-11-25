#include "HearthBlocksComponent.h"
#include "../Player/FirstPersonControllerComponent.h"

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Console/IConsole.h>
#include <AzCore/JSON/document.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/std/algorithm.h>
#include <AzFramework/Physics/Collision/CollisionEvents.h>
#include <AzFramework/Physics/Common/PhysicsSimulatedBody.h>
#include <AzFramework/Physics/Common/PhysicsTypes.h>
#include <AzFramework/Physics/Components/SimulatedBodyComponentBus.h>
#include <AzFramework/Physics/PhysicsScene.h>
#include <AzFramework/Physics/PhysicsSystem.h>
#include <MiniAudio/MiniAudioPlaybackBus.h>

namespace NoclipProtocol
{
    namespace
    {
        AZStd::string BlocksItemKey(const AZStd::string& item)
        {
            AZStd::string key = "/NoclipProtocol/Items/";
            for (char c : item)
            {
                key += (c == '/' || c == ' ') ? '_' : c;
            }
            return key;
        }

        void NoclipGive(const AZ::ConsoleCommandContainer& arguments)
        {
            AZStd::string item;
            for (const auto& word : arguments)
            {
                item += item.empty() ? "" : " ";
                item += AZStd::string(word);
            }
            if (auto* registry = AZ::SettingsRegistry::Get(); registry && !item.empty())
            {
                registry->Set(BlocksItemKey(item), true);
                AZ_Printf("NoclipProtocol", "Gave the player: %s", item.c_str());
            }
        }
        AZ_CONSOLEFREEFUNC("noclip_give", NoclipGive, AZ::ConsoleFunctorFlags::Null, "Give the player an item, for example noclip_give Level 2 keycard");
    } // namespace

    void HearthBlocksComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<HearthBlocksComponent, AZ::Component>()
                ->Version(1)
                ->Field("Name", &HearthBlocksComponent::m_name)
                ->Field("Blocks", &HearthBlocksComponent::m_graph);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<HearthBlocksComponent>("Hearth Blocks", "A plain-language script made in Hearth Engine's Script Canvas window")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Noclip Protocol")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HearthBlocksComponent::m_name, "Name", "What this script is for")
                    ->DataElement(AZ::Edit::UIHandlers::MultiLineEdit, &HearthBlocksComponent::m_graph, "Blocks",
                                  "Written by the Script Canvas window (Tools > Script Canvas)");
            }
        }
    }

    void HearthBlocksComponent::Parse()
    {
        m_blocks.clear();
        rapidjson::Document document;
        document.Parse(m_graph.c_str());
        if (document.HasParseError() || !document.IsObject() || !document.HasMember("blocks") || !document["blocks"].IsArray())
        {
            return;
        }
        auto text = [](const rapidjson::Value& v, const char* key) -> AZStd::string
        {
            return v.HasMember(key) && v[key].IsString() ? AZStd::string(v[key].GetString()) : AZStd::string();
        };
        auto number = [](const rapidjson::Value& v, const char* key, float fallback)
        {
            return v.HasMember(key) && v[key].IsNumber() ? static_cast<float>(v[key].GetDouble()) : fallback;
        };
        auto ids = [](const rapidjson::Value& v, const char* key)
        {
            AZStd::vector<int> out;
            if (v.HasMember(key) && v[key].IsArray())
            {
                for (const auto& n : v[key].GetArray())
                {
                    if (n.IsInt())
                    {
                        out.push_back(n.GetInt());
                    }
                }
            }
            return out;
        };
        for (const auto& value : document["blocks"].GetArray())
        {
            if (!value.IsObject())
            {
                continue;
            }
            Block block;
            block.m_id = value.HasMember("id") && value["id"].IsInt() ? value["id"].GetInt() : 0;
            block.m_kind = text(value, "kind");
            block.m_target = text(value, "target");
            block.m_sound = text(value, "sound");
            block.m_mode = text(value, "mode").empty() ? AZStd::string("open") : text(value, "mode");
            block.m_seconds = number(value, "seconds", 1.5f);
            block.m_distance = number(value, "distance", 12.0f);
            block.m_offset = AZ::Vector3(number(value, "x", 0.0f), number(value, "y", 0.0f), number(value, "z", 3.0f));
            block.m_playerOnly = text(value, "who") != "anyone";
            block.m_next = ids(value, "next");
            block.m_no = ids(value, "no");
            m_blocks.push_back(block);
        }
    }

    const HearthBlocksComponent::Block* HearthBlocksComponent::Find(int id) const
    {
        for (const Block& block : m_blocks)
        {
            if (block.m_id == id)
            {
                return &block;
            }
        }
        return nullptr;
    }

    AZ::EntityId HearthBlocksComponent::Named(const AZStd::string& name) const
    {
        if (name.empty())
        {
            return GetEntityId();
        }
        AZ::EntityId found;
        AZ::ComponentApplicationBus::Broadcast(&AZ::ComponentApplicationRequests::EnumerateEntities, [&found, &name](AZ::Entity* entity)
        {
            if (!found.IsValid() && entity->GetName() == name)
            {
                found = entity->GetId();
            }
        });
        return found;
    }

    AZ::EntityId HearthBlocksComponent::Player() const
    {
        AZ::EntityId player;
        AZ::ComponentApplicationBus::Broadcast(&AZ::ComponentApplicationRequests::EnumerateEntities, [&player](AZ::Entity* entity)
        {
            if (!player.IsValid() && entity->FindComponent<FirstPersonControllerComponent>())
            {
                player = entity->GetId();
            }
        });
        return player;
    }

    void HearthBlocksComponent::Activate()
    {
        Parse();
        m_lightWasOn.assign(m_blocks.size(), true);
        m_wasSeen.assign(m_blocks.size(), false);
        m_hooked = false;
        AZ::TickBus::Handler::BusConnect();
    }

    void HearthBlocksComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
        for (auto& handler : m_enterHandlers)
        {
            handler.Disconnect();
        }
        m_enterHandlers.clear();
        m_moves.clear();
        m_openedNames.clear();
    }

    void HearthBlocksComponent::Run(int id, int depth)
    {
        const Block* block = Find(id);
        if (!block || depth > 32)
        {
            return;
        }
        AZ_Printf("HearthBlocks", "ran %s %d", m_name.c_str(), block->m_id);
        const AZStd::vector<int>* then = &block->m_next;
        if (block->m_kind == "check.item")
        {
            bool has = false;
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Get(has, BlocksItemKey(block->m_target));
            }
            then = has ? &block->m_next : &block->m_no;
        }
        else if (block->m_kind == "do.play")
        {
            const AZ::EntityId sound = Named(block->m_target);
            MiniAudio::MiniAudioPlaybackRequestBus::Event(sound, &MiniAudio::MiniAudioPlaybackRequests::Stop);
            MiniAudio::MiniAudioPlaybackRequestBus::Event(sound, &MiniAudio::MiniAudioPlaybackRequests::Play);
        }
        else if (block->m_kind == "do.move")
        {
            // open: moves by the offset once; close: moves back if it was opened; move: moves by the offset every time.
            const AZ::EntityId thing = Named(block->m_target);
            const AZStd::string key = block->m_target;
            const auto opened = AZStd::find(m_openedNames.begin(), m_openedNames.end(), key);
            const bool isOpen = opened != m_openedNames.end();
            const bool go = thing.IsValid() && (block->m_mode == "move" || (block->m_mode == "close" ? isOpen : !isOpen));
            if (go)
            {
                AZ::Vector3 from = AZ::Vector3::CreateZero();
                AZ::TransformBus::EventResult(from, thing, &AZ::TransformBus::Events::GetWorldTranslation);
                const AZ::Vector3 by = block->m_mode == "close" ? -block->m_offset : block->m_offset;
                m_moves.push_back({ thing, from, from + by, AZStd::max(0.05f, block->m_seconds), 0.0f });
                if (block->m_mode == "open")
                {
                    m_openedNames.push_back(key);
                }
                else if (block->m_mode == "close")
                {
                    m_openedNames.erase(opened);
                }
                if (!block->m_sound.empty())
                {
                    const AZ::EntityId sound = Named(block->m_sound);
                    MiniAudio::MiniAudioPlaybackRequestBus::Event(sound, &MiniAudio::MiniAudioPlaybackRequests::Play);
                }
            }
        }
        else if (block->m_kind == "do.level")
        {
            if (auto* console = AZ::Interface<AZ::IConsole>::Get(); console && !block->m_target.empty())
            {
                console->PerformCommand(AZStd::string::format("LoadLevel %s", block->m_target.c_str()).c_str());
            }
        }
        else if (block->m_kind == "do.shift")
        {
            // The endless level rebuilds every chunk the player isn't standing in, with new rooms.
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                AZ::s64 shifts = 0;
                registry->Get(shifts, "/NoclipProtocol/Level0/Shifts");
                registry->Set("/NoclipProtocol/Level0/Shifts", shifts + 1);
            }
        }
        for (int next : *then)
        {
            Run(next, depth + 1);
        }
    }

    void HearthBlocksComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        // Hook the area triggers once everything is active (physics bodies exist after the first frame).
        if (!m_hooked)
        {
            m_hooked = true;
            auto* scenes = AZ::Interface<AzPhysics::SceneInterface>::Get();
            const AzPhysics::SceneHandle scene = scenes ? scenes->GetSceneHandle(AzPhysics::DefaultPhysicsSceneName) : AzPhysics::InvalidSceneHandle;
            m_enterHandlers.reserve(m_blocks.size()); // handlers are registered by address: never let the vector move them
            for (const Block& block : m_blocks)
            {
                if (block.m_kind != "when.enter")
                {
                    continue;
                }
                const AZ::EntityId area = Named(block.m_target);
                AzPhysics::SimulatedBodyHandle body = AzPhysics::InvalidSimulatedBodyHandle;
                AzPhysics::SimulatedBodyComponentRequestsBus::EventResult(body, area, &AzPhysics::SimulatedBodyComponentRequests::GetSimulatedBodyHandle);
                if (body == AzPhysics::InvalidSimulatedBodyHandle || scene == AzPhysics::InvalidSceneHandle)
                {
                    AZ_Warning("HearthBlocks", false, "%s: '%s' has no trigger collider", m_name.c_str(), block.m_target.c_str());
                    continue;
                }
                const int id = block.m_id;
                const bool playerOnly = block.m_playerOnly;
                m_enterHandlers.emplace_back([this, id, playerOnly](AzPhysics::SimulatedBodyHandle, const AzPhysics::TriggerEvent& event)
                {
                    const AZ::EntityId who = event.m_otherBody ? event.m_otherBody->GetEntityId() : AZ::EntityId();
                    if (!playerOnly || who == Player())
                    {
                        Run(id);
                    }
                });
                AzPhysics::SimulatedBodyEvents::RegisterOnTriggerEnterHandler(scene, body, m_enterHandlers.back());
            }
        }

        // A light dies: its entity goes from active to not active (that's how Flicker Light switches it).
        // An entity sees the player: within its distance and 60° of where it faces.
        const AZ::EntityId player = Player();
        AZ::Vector3 playerAt = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(playerAt, player, &AZ::TransformBus::Events::GetWorldTranslation);
        for (size_t i = 0; i < m_blocks.size(); ++i)
        {
            const Block& block = m_blocks[i];
            if (block.m_kind == "when.lightDies")
            {
                AZ::Entity* light = nullptr;
                AZ::ComponentApplicationBus::BroadcastResult(light, &AZ::ComponentApplicationRequests::FindEntity, Named(block.m_target));
                const bool on = light && light->GetState() == AZ::Entity::State::Active;
                if (m_lightWasOn[i] && !on && light)
                {
                    Run(block.m_id);
                }
                m_lightWasOn[i] = on;
            }
            else if (block.m_kind == "when.seen" && player.IsValid())
            {
                AZ::Transform watcher = AZ::Transform::CreateIdentity();
                AZ::TransformBus::EventResult(watcher, Named(block.m_target), &AZ::TransformBus::Events::GetWorldTM);
                const AZ::Vector3 toPlayer = playerAt - watcher.GetTranslation();
                const float distance = toPlayer.GetLength();
                const bool seen = distance <= block.m_distance && distance > 0.01f &&
                                  watcher.GetBasisY().GetNormalized().Dot(toPlayer / distance) >= 0.5f;
                if (seen && !m_wasSeen[i])
                {
                    Run(block.m_id);
                }
                m_wasSeen[i] = seen;
            }
        }

        // Things being opened or moved.
        for (auto it = m_moves.begin(); it != m_moves.end();)
        {
            it->m_time += deltaTime;
            const float t = AZStd::min(1.0f, it->m_time / it->m_seconds);
            const float eased = t * t * (3.0f - 2.0f * t);
            AZ::TransformBus::Event(it->m_entity, &AZ::TransformBus::Events::SetWorldTranslation, it->m_from.Lerp(it->m_to, eased));
            it = t >= 1.0f ? m_moves.erase(it) : it + 1;
        }
    }
} // namespace NoclipProtocol
