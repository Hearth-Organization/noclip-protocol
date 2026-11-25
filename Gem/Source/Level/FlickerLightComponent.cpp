#include "FlickerLightComponent.h"

#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>

namespace NoclipProtocol
{
    void FlickerLightComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<FlickerLightComponent, AZ::Component>()
                ->Version(1)
                ->Field("LightEntity", &FlickerLightComponent::m_lightEntity)
                ->Field("PanelEntity", &FlickerLightComponent::m_panelEntity)
                ->Field("MinSteady", &FlickerLightComponent::m_minSteadySeconds)
                ->Field("MaxSteady", &FlickerLightComponent::m_maxSteadySeconds)
                ->Field("BlackoutChance", &FlickerLightComponent::m_blackoutChance);

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
                        ->Attribute(AZ::Edit::Attributes::Max, 1.0f);
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

    void FlickerLightComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        m_timer -= deltaTime;
        if (m_timer > 0.0f)
        {
            return;
        }

        if (m_burstToggles > 0)
        {
            // In a stutter: rapid on/off.
            --m_burstToggles;
            SetLit(!m_lit);
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
            m_burstToggles = 2 + static_cast<int>(m_random.GetRandomFloat() * 4.0f);
            m_timer = Range(0.05f, 0.15f);
            return;
        }

        // Start a stutter.
        m_burstToggles = 3 + static_cast<int>(m_random.GetRandomFloat() * 9.0f);
        m_timer = 0.0f;
    }
} // namespace NoclipProtocol
