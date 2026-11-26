#include "PlayerVitalsComponent.h"

#include <cmath>

#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/std/algorithm.h>
#include "../Render/FilmGrainBus.h"

#include <AzCore/Console/IConsole.h>

AZ_CVAR(float, player_sanity_set, -1.0f, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Set the player's sanity once (0..1, for testing; goes back to -1 after)");

namespace NoclipProtocol
{
    void PlayerVitalsComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<PlayerVitalsComponent, AZ::Component>()
                ->Version(1)
                ->Field("StaminaDrain", &PlayerVitalsComponent::m_staminaDrain)
                ->Field("StaminaRegen", &PlayerVitalsComponent::m_staminaRegen)
                ->Field("SanityDarkDrain", &PlayerVitalsComponent::m_sanityDarkDrain)
                ->Field("SanityLightRegen", &PlayerVitalsComponent::m_sanityLightRegen);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<PlayerVitalsComponent>("Player Vitals", "Health, stamina and sanity for the HUD")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Noclip Protocol")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->DataElement(AZ::Edit::UIHandlers::Default, &PlayerVitalsComponent::m_staminaDrain, "Stamina drain", "Per second while sprinting")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &PlayerVitalsComponent::m_staminaRegen, "Stamina regen", "Per second otherwise")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &PlayerVitalsComponent::m_sanityDarkDrain, "Sanity drain in the dark", "Per second")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &PlayerVitalsComponent::m_sanityLightRegen, "Sanity regen in the light", "Per second");
            }
        }
    }

    void PlayerVitalsComponent::Activate()
    {
        m_health = 1.0f;
        m_stamina = 1.0f;
        m_sanity = 1.0f;
        m_grainSet = -1.0f;
        m_exhausted = false;
        AZ::TickBus::Handler::BusConnect();
    }

    void PlayerVitalsComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
    }

    void PlayerVitalsComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        auto* registry = AZ::SettingsRegistry::Get();
        if (!registry || deltaTime <= 0.0f)
        {
            return;
        }
        if (static_cast<float>(player_sanity_set) >= 0.0f)
        {
            m_sanity = AZStd::clamp(static_cast<float>(player_sanity_set), 0.0f, 1.0f);
            player_sanity_set = -1.0f;
        }
        // (Also while paused: the main menu is paused.) Film grain only creeps in as sanity goes: none above 60%, up to 0.3 at none left.
        const float grain = 0.3f * AZStd::clamp((0.6f - m_sanity) / 0.6f, 0.0f, 1.0f);
        // Sent again every second too: the level's post effects can come up after the player does.
        m_grainResend -= deltaTime;
        if (std::fabs(grain - m_grainSet) > 0.005f || m_grainSet < 0.0f || m_grainResend <= 0.0f)
        {
            m_grainResend = 1.0f;
            m_grainSet = grain;
            AZ::Render::FilmGrainRequestBus::Broadcast(&AZ::Render::FilmGrainRequests::SetIntensity, grain);
        }
        bool paused = false;
        registry->Get(paused, "/NoclipProtocol/Paused");
        if (paused)
        {
            return;
        }
        bool sprinting = false;
        bool flashlight = false;
        double light = 1.0;
        double damage = 0.0;
        registry->Get(sprinting, "/NoclipProtocol/Player/Sprinting");
        registry->Get(flashlight, "/NoclipProtocol/Player/FlashlightOn");
        registry->Get(light, "/NoclipProtocol/Player/Light");
        if (registry->Get(damage, "/NoclipProtocol/Player/Damage") && damage != 0.0)
        {
            m_health = AZStd::clamp(m_health - static_cast<float>(damage), 0.0f, 1.0f);
            registry->Set("/NoclipProtocol/Player/Damage", 0.0);
        }

        // Stamina.
        if (sprinting && !m_exhausted)
        {
            m_stamina -= m_staminaDrain * deltaTime;
            if (m_stamina <= 0.0f)
            {
                m_stamina = 0.0f;
                m_exhausted = true;
            }
        }
        else
        {
            m_stamina = AZStd::min(1.0f, m_stamina + m_staminaRegen * deltaTime);
            if (m_exhausted && m_stamina >= 0.25f)
            {
                m_exhausted = false;
            }
        }
        registry->Set("/NoclipProtocol/Player/Exhausted", m_exhausted);

        // Sanity: the dark eats at you; light (not the flashlight) slowly gives it back.
        const float dark = 1.0f - static_cast<float>(AZStd::clamp(light, 0.0, 1.0));
        if (dark > 0.6f && !flashlight)
        {
            m_sanity -= m_sanityDarkDrain * (dark - 0.6f) / 0.4f * deltaTime;
        }
        else if (dark < 0.3f)
        {
            m_sanity += m_sanityLightRegen * deltaTime;
        }
        m_sanity = AZStd::clamp(m_sanity, 0.0f, 1.0f);

        registry->Set("/NoclipProtocol/Hud/player.health", static_cast<double>(m_health));
        registry->Set("/NoclipProtocol/Hud/player.stamina", static_cast<double>(m_stamina));
        registry->Set("/NoclipProtocol/Hud/player.sanity", static_cast<double>(m_sanity));

    }
} // namespace NoclipProtocol
