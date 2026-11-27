// The recorded lines (the owner, Nov 27 2026): Foundation Command (a woman, over the radio) when an agent dies, and the
// agent's own lines: random things while exploring ("Can't wait to get home"), "What the hell was that?" when something
// shows up, when hurt, on a new level, and a few jokes. The agent's lines come in a man's and a woman's voice, to match
// the agent in play (Agents.h). All made in Voice Studio on the Hearth server (Tools/Voice; the project's Voice folder).
//
// Header-only like the weapons: created on the player's runtime "Noclip UI" entity. Subtitles go to the HUD through
// /NoclipProtocol/Hud/voice.who|text|until.
#pragma once

#include "Agents.h"
#include "VoiceLines.h"
#include "../Hud/GameSettings.h"
#include "../Level/EndlessLevelComponent.h"
#include "../Monsters/HearthMonsterComponent.h"

#include <AzCore/Asset/AssetManager.h>
#include <AzCore/Asset/AssetManagerBus.h>
#include <AzCore/Component/Component.h>
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzFramework/Components/TransformComponent.h>
#include <MiniAudio/MiniAudioConstants.h>
#include <MiniAudio/MiniAudioPlaybackBus.h>
#include <MiniAudio/SoundAsset.h>

#include <random>

namespace NoclipProtocol
{
    class VoicesComponent
        : public AZ::Component
        , public AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(VoicesComponent, "{3F7A1C92-5E4B-4D08-A6C3-9B2E7D41F865}");

        static void Reflect(AZ::ReflectContext* context)
        {
            if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
            {
                serialize->Class<VoicesComponent, AZ::Component>()->Version(1);
            }
        }

        AZ::EntityId m_player;

        //! Foundation Command's line when an agent dies (a random one of the 45). Returns its words, for the subtitle.
        static AZStd::string SayCommand()
        {
            const auto& line = VoiceLines::Command[Roll(0, static_cast<int>(AZ_ARRAY_SIZE(VoiceLines::Command)) - 1)];
            Queue(AZStd::string(VoiceLines::CommandFolder) + "/" + line.m_id, line.m_text, "FOUNDATION", true);
            return line.m_text;
        }

        void Activate() override
        {
            m_speaker = aznew AZ::Entity("Noclip Voice");
            m_speaker->CreateComponent<AzFramework::TransformComponent>();
            m_speaker->CreateComponent(AZ::Uuid(MiniAudio::MiniAudioPlaybackComponentTypeId));
            m_speaker->Init();
            m_speaker->Activate();
            m_idleClock = static_cast<float>(Roll(40, 90));
            m_levelClock = 4.0f;
            m_lastHealth = -1.0;
            Pending().clear();
            AZ::TickBus::Handler::BusConnect();
        }

        void Deactivate() override
        {
            AZ::TickBus::Handler::BusDisconnect();
            if (m_speaker)
            {
                m_speaker->Deactivate();
                delete m_speaker;
                m_speaker = nullptr;
            }
            m_asset = {};
        }

    private:
        struct Request
        {
            AZStd::string m_path; // e.g. voice/dying/cmd07 (without .wav)
            AZStd::string m_text;
            AZStd::string m_who;
            bool m_command = false;
        };
        static AZStd::vector<Request>& Pending()
        {
            static AZStd::vector<Request> pending;
            return pending;
        }
        static void Queue(const AZStd::string& path, const char* text, const char* who, bool command)
        {
            auto& pending = Pending();
            if (command)
            {
                pending.clear(); // Command always gets through
            }
            if (pending.size() < 2)
            {
                pending.push_back({ path, text, who, command });
            }
        }
        static int Roll(int lo, int hi) { return Agents::Roll(lo, hi); }

        // The agent's line from a group, in their voice.
        void SayAgent(const VoiceLines::Line* lines, int count)
        {
            // Settings › Gameplay › Agent chatter: Off keeps the agent quiet except when hurt.
            if (m_talkingUntil > m_now || count <= 0 || (GameSettings::Index("chatter") == 2 && lines != VoiceLines::Hurt))
            {
                return;
            }
            int pick = Roll(0, count - 1);
            if (count > 1 && lines[pick].m_id == m_lastId)
            {
                pick = (pick + 1) % count;
            }
            m_lastId = lines[pick].m_id;
            const Agents::Agent agent = Agents::Load();
            Queue(AZStd::string(agent.m_female ? VoiceLines::WomanFolder : VoiceLines::ManFolder) + "/" + lines[pick].m_id, lines[pick].m_text,
                  "", false);
        }

        void Start(const Request& request)
        {
            AZ::Data::AssetId id;
            const AZStd::string product = request.m_path + ".wav." + MiniAudio::SoundAsset::FileExtension;
            AZ::Data::AssetCatalogRequestBus::BroadcastResult(id, &AZ::Data::AssetCatalogRequests::GetAssetIdByPath, product.c_str(),
                                                              azrtti_typeid<MiniAudio::SoundAsset>(), false);
            m_playing = request;
            if (!id.IsValid())
            {
                AZ_Printf("Noclip", "voice: no asset for %s", product.c_str());
                m_asset = {};
            }
            else
            {
                m_asset = AZ::Data::AssetManager::Instance().GetAsset<MiniAudio::SoundAsset>(id, AZ::Data::AssetLoadBehavior::PreLoad);
                m_asset.QueueLoad();
            }
            m_waiting = true;
            m_waitClock = 0.0f;
        }

        void Subtitle(const Request& request, float seconds)
        {
            if (auto* r = AZ::SettingsRegistry::Get())
            {
                AZStd::string who = request.m_who;
                if (who.empty())
                {
                    const Agents::Agent agent = Agents::Load();
                    who = AZStd::string::format("AGT. %d", static_cast<int>(agent.m_number));
                }
                r->Set("/NoclipProtocol/Hud/voice.who", who);
                r->Set("/NoclipProtocol/Hud/voice.text", request.m_text);
                r->Set("/NoclipProtocol/Hud/voice.command", request.m_command);
                r->Set("/NoclipProtocol/Hud/voice.until", m_now + seconds);
            }
            m_talkingUntil = m_now + seconds + 1.5;
        }

        void OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time) override
        {
            auto* r = AZ::SettingsRegistry::Get();
            if (!r || !m_speaker)
            {
                return;
            }
            r->Get(m_now, "/NoclipProtocol/Hud/time");
            // The speaker stays with the player (the voice is in their head, or on their radio).
            AZ::Vector3 at = AZ::Vector3::CreateZero();
            AZ::TransformBus::EventResult(at, m_player, &AZ::TransformBus::Events::GetWorldTranslation);
            AZ::TransformBus::Event(m_speaker->GetId(), &AZ::TransformBus::Events::SetWorldTranslation, at + AZ::Vector3(0.0f, 0.0f, 1.6f));

            // Playing what's next.
            if (m_waiting)
            {
                m_waitClock += deltaTime;
                if (m_asset.IsReady() || !m_asset.GetId().IsValid() || m_waitClock > 3.0f)
                {
                    m_waiting = false;
                    const float seconds = 1.2f + 0.068f * static_cast<float>(m_playing.m_text.size());
                    if (m_asset.IsReady())
                    {
                        const AZ::EntityId speaker = m_speaker->GetId();
                        MiniAudio::MiniAudioPlaybackRequestBus::Event(speaker, &MiniAudio::MiniAudioPlaybackRequests::Stop);
                        MiniAudio::MiniAudioPlaybackRequestBus::Event(speaker, &MiniAudio::MiniAudioPlaybackRequests::SetSoundAsset, m_asset);
                        MiniAudio::MiniAudioPlaybackRequestBus::Event(speaker, &MiniAudio::MiniAudioPlaybackRequests::SetVolumePercentage,
                                                                      100.0f * GameSettings::Gain("vol_voices"));
                        MiniAudio::MiniAudioPlaybackRequestBus::Event(speaker, &MiniAudio::MiniAudioPlaybackRequests::Play);
                    }
                    Subtitle(m_playing, seconds); // the words show even if the sound isn't there yet
                }
            }
            else if (!Pending().empty())
            {
                const Request next = Pending().front();
                Pending().erase(Pending().begin());
                Start(next);
            }

            bool dead = false, menu = false, paused = false;
            r->Get(dead, "/NoclipProtocol/Player/Dead");
            r->Get(menu, "/NoclipProtocol/Menu/Active");
            r->Get(paused, "/NoclipProtocol/Paused");
            if (dead || menu || paused)
            {
                m_lastHealth = -1.0;
                return;
            }
            const EndlessLevelComponent* level = Level();
            if (!level)
            {
                return; // the intro facility: the briefing does the talking
            }

            // Arriving somewhere new.
            if (m_levelClock > 0.0f)
            {
                m_levelClock -= deltaTime;
                if (m_levelClock <= 0.0f && level->GetLevelNumber() > 0)
                {
                    SayAgent(VoiceLines::Newlevel, AZ_ARRAY_SIZE(VoiceLines::Newlevel));
                }
            }

            // Hurt.
            double health = 1.0;
            r->Get(health, "/NoclipProtocol/Hud/player.health");
            if (m_lastHealth >= 0.0 && health < m_lastHealth - 0.05 && health > 0.0 && m_now > m_hurtAfter)
            {
                m_hurtAfter = m_now + 25.0;
                m_talkingUntil = AZStd::min(m_talkingUntil, m_now); // pain cuts in
                SayAgent(VoiceLines::Hurt, AZ_ARRAY_SIZE(VoiceLines::Hurt));
            }
            m_lastHealth = health;

            // Something shows up in front of them.
            m_lookClock -= deltaTime;
            if (m_lookClock <= 0.0f)
            {
                m_lookClock = 0.4f;
                if (m_now > m_spottedAfter && MonsterInView(at))
                {
                    m_spottedAfter = m_now + 50.0;
                    m_idleClock = AZStd::max(m_idleClock, 30.0f);
                    SayAgent(VoiceLines::Spotted, AZ_ARRAY_SIZE(VoiceLines::Spotted));
                }
            }

            // Every minute or two of quiet: something to themselves, now and then a joke.
            m_idleClock -= deltaTime;
            if (m_idleClock <= 0.0f)
            {
                m_idleClock = static_cast<float>(Roll(70, 160)) * (GameSettings::Index("chatter") == 1 ? 2.5f : 1.0f);
                if (Roll(0, 99) < 30)
                {
                    SayAgent(VoiceLines::Jokes, AZ_ARRAY_SIZE(VoiceLines::Jokes));
                }
                else
                {
                    SayAgent(VoiceLines::Idle, AZ_ARRAY_SIZE(VoiceLines::Idle));
                }
            }
        }

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

        // A monster within 14 m, roughly where the player is looking.
        bool MonsterInView(const AZ::Vector3& at) const
        {
            AZ::Transform body = AZ::Transform::CreateIdentity();
            AZ::TransformBus::EventResult(body, m_player, &AZ::TransformBus::Events::GetWorldTM);
            AZ::Vector3 forward = body.GetBasisY();
            forward.SetZ(0.0f);
            forward.NormalizeSafe();
            bool seen = false;
            AZ::ComponentApplicationBus::Broadcast(&AZ::ComponentApplicationRequests::EnumerateEntities, [&](AZ::Entity* entity)
            {
                if (seen || entity->GetState() != AZ::Entity::State::Active || !entity->FindComponent<HearthMonsterComponent>())
                {
                    return;
                }
                AZ::Vector3 p = AZ::Vector3::CreateZero();
                AZ::TransformBus::EventResult(p, entity->GetId(), &AZ::TransformBus::Events::GetWorldTranslation);
                AZ::Vector3 to = p - at;
                to.SetZ(0.0f);
                const float distance = to.GetLength();
                if (distance > 1.0f && distance < 14.0f && forward.Dot(to / distance) > 0.6f)
                {
                    seen = true;
                }
            });
            return seen;
        }

        AZ::Entity* m_speaker = nullptr;
        AZ::Data::Asset<MiniAudio::SoundAsset> m_asset;
        Request m_playing;
        bool m_waiting = false;
        float m_waitClock = 0.0f;
        double m_now = 0.0;
        double m_talkingUntil = 0.0;
        double m_hurtAfter = 0.0;
        double m_spottedAfter = 20.0;
        double m_lastHealth = -1.0;
        float m_idleClock = 60.0f;
        float m_levelClock = 4.0f;
        float m_lookClock = 0.0f;
        AZStd::string m_lastId;
    };
} // namespace NoclipProtocol
