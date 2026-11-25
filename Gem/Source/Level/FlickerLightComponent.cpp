#include "FlickerLightComponent.h"

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/std/algorithm.h>
#include <AzFramework/Components/CameraBus.h>
#include <MiniAudio/MiniAudioPlaybackBus.h>

namespace NoclipProtocol
{
    void FlickerLightComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<FlickerLightComponent, AZ::Component>()
                ->Version(2)
                ->Field("LightEntity", &FlickerLightComponent::m_lightEntity)
                ->Field("PanelEntity", &FlickerLightComponent::m_panelEntity)
                ->Field("MinSteady", &FlickerLightComponent::m_minSteadySeconds)
                ->Field("MaxSteady", &FlickerLightComponent::m_maxSteadySeconds)
                ->Field("BlackoutChance", &FlickerLightComponent::m_blackoutChance)
                ->Field("CrackleChance", &FlickerLightComponent::m_crackleChance)
                ->Field("DiesWhenNear", &FlickerLightComponent::m_diesWhenNear)
                ->Field("TriggerRadius", &FlickerLightComponent::m_triggerRadius);

            if (auto edit = serialize->GetEditContext())
            {
                edit->Class<FlickerLightComponent>("Flicker Light", "A failing fluorescent light that stutters and blacks out")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Noclip Protocol")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->Attribute(AZ::Edit::Attributes::AutoExpand, true)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FlickerLightComponent::m_lightEntity, "Light entity", "The light to switch")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FlickerLightComponent::m_panelEntity, "Panel entity",
                        "The glowing panel mesh to switch with it")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FlickerLightComponent::m_minSteadySeconds, "Min steady (s)",
                        "Shortest time it stays on between stutters")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FlickerLightComponent::m_maxSteadySeconds, "Max steady (s)",
                        "Longest time it stays on between stutters")
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &FlickerLightComponent::m_blackoutChance, "Blackout chance",
                        "Chance a stutter ends in a long blackout")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Slider, &FlickerLightComponent::m_crackleChance, "Crackle chance",
                        "Chance it crackles (MiniAudio Playback on this entity) each time it sputters back on")
                        ->Attribute(AZ::Edit::Attributes::Min, 0.0f)
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f)
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FlickerLightComponent::m_diesWhenNear, "Dies when near",
                        "Looks like a working light until the player gets close, then stutters and goes dark for good")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &FlickerLightComponent::m_triggerRadius, "Trigger radius (m)",
                        "How close (horizontally) the player must get to a dying light");
            }
        }
    }

    float FlickerLightComponent::Range(float low, float high)
    {
        return low + (high - low) * m_random.GetRandomFloat();
    }

    void FlickerLightComponent::Activate()
    {
        // Every light gets its own rhythm.
        m_random.SetSeed(static_cast<AZ::u64>(GetEntityId()) ^ 0xC0FFEEull);
        m_lit = true;
        m_dying = false;
        m_dead = false;
        m_burstToggles = 0;
        m_timer = Range(0.2f, m_maxSteadySeconds);
        AZ::TickBus::Handler::BusConnect();
    }

    void FlickerLightComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
    }

    void FlickerLightComponent::SetLit(bool lit)
    {
        m_lit = lit;
        for (const AZ::EntityId& id : { m_lightEntity, m_panelEntity })
        {
            if (!id.IsValid())
            {
                continue;
            }
            AZ::Entity* entity = nullptr;
            AZ::ComponentApplicationBus::BroadcastResult(entity, &AZ::ComponentApplicationRequests::FindEntity, id);
            if (!entity)
            {
                continue;
            }
            if (lit && entity->GetState() == AZ::Entity::State::Init)
            {
                entity->Activate();
            }
            else if (!lit && entity->GetState() == AZ::Entity::State::Active)
            {
                entity->Deactivate();
            }
        }
    }

    void FlickerLightComponent::Crackle()
    {
        // Report the first few crackles in the log, so it is easy to tell whether they fire at all.
        static int reported = 0;
        if (reported < 5)
        {
            ++reported;
            const bool hasSound = MiniAudio::MiniAudioPlaybackRequestBus::HasHandlers(GetEntityId());
            AZ_Printf("NoclipProtocol", "Flicker Light crackle (%s)", hasSound ? "playing" : "no MiniAudio Playback on this entity");
        }
        // Restart the one-shot from the top (Stop rewinds it).
        MiniAudio::MiniAudioPlaybackRequestBus::Event(GetEntityId(), &MiniAudio::MiniAudioPlaybackRequests::Stop);
        MiniAudio::MiniAudioPlaybackRequestBus::Event(GetEntityId(), &MiniAudio::MiniAudioPlaybackRequests::Play);
    }

    bool FlickerLightComponent::PlayerIsNear() const
    {
        AZ::Transform camera = AZ::Transform::CreateIdentity();
        Camera::ActiveCameraRequestBus::BroadcastResult(camera, &Camera::ActiveCameraRequests::GetActiveCameraTransform);
        AZ::Vector3 here = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(here, GetEntityId(), &AZ::TransformBus::Events::GetWorldTranslation);
        AZ::Vector3 offset = camera.GetTranslation() - here;
        offset.SetZ(0.0f);
        return offset.GetLengthSq() < m_triggerRadius * m_triggerRadius;
    }

    void FlickerLightComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        if (m_diesWhenNear)
        {
            if (!m_dying)
            {
                // Waiting like an ordinary light. Check a few times a second.
                m_timer -= deltaTime;
                if (m_timer > 0.0f)
                {
                    return;
                }
                m_timer = 0.15f;
                if (PlayerIsNear())
                {
                    m_dying = true;
                    m_burstToggles = 9 + static_cast<int>(m_random.GetRandomFloat() * 8.0f);
                    m_timer = 0.0f;
                }
                return;
            }
            m_timer -= deltaTime;
            if (m_timer <= 0.0f)
            {
                TickDying();
            }
            return;
        }

        m_timer -= deltaTime;
        if (m_timer <= 0.0f)
        {
            TickFlicker();
        }
    }

    void FlickerLightComponent::TickDying()
    {
        if (m_burstToggles > 0)
        {
            --m_burstToggles;
            SetLit(!m_lit);
            if (m_lit)
            {
                Crackle();
            }
            // The stutter slows down as it fails.
            const float slowing = 1.0f + 0.12f * static_cast<float>(12 - AZStd::min(m_burstToggles, 12));
            m_timer = (m_lit ? Range(0.03f, 0.12f) : Range(0.04f, 0.16f)) * slowing;
            return;
        }
        // Gone. It stays dark until this part of the level unloads.
        SetLit(false);
        m_dead = true;
        AZ::TickBus::Handler::BusDisconnect();
    }

    void FlickerLightComponent::TickFlicker()
    {
        if (m_burstToggles > 0)
        {
            // In a stutter: rapid on/off.
            --m_burstToggles;
            SetLit(!m_lit);
            if (m_lit && m_random.GetRandomFloat() < m_crackleChance)
            {
                Crackle();
            }
            if (m_burstToggles == 0)
            {
                if (m_random.GetRandomFloat() < m_blackoutChance)
                {
                    SetLit(false);
                    m_timer = Range(1.0f, 4.5f); // dead for a few seconds...
                }
                else
                {
                    SetLit(true);
                    m_timer = Range(m_minSteadySeconds, m_maxSteadySeconds);
                }
            }
            else
            {
                m_timer = m_lit ? Range(0.03f, 0.18f) : Range(0.02f, 0.12f);
            }
            return;
        }

        if (!m_lit)
        {
            // ...then it struggles back on.
            SetLit(true);
            Crackle();
            m_burstToggles = 2 + static_cast<int>(m_random.GetRandomFloat() * 4.0f);
            m_timer = Range(0.05f, 0.15f);
            return;
        }

        // Start a stutter.
        m_burstToggles = 3 + static_cast<int>(m_random.GetRandomFloat() * 9.0f);
        m_timer = 0.0f;
    }
} // namespace NoclipProtocol
