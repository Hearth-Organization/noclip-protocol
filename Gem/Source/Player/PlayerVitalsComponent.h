// Player vitals: health, stamina and sanity, shown on the HUD (HEALTH / STAMINA / SANITY bars).
//  - Stamina drains while sprinting and comes back while walking or standing; at zero you can't sprint until it's
//    back to a quarter ("exhausted").
//  - Sanity drains in the dark and comes back slowly in the light; the flashlight holds it off but doesn't restore it.
//    (What low sanity does comes later: whispers, things at the edge of the screen.)
//  - Health: full until something can hurt you; other systems lower it with /NoclipProtocol/Player/Damage.
// Values go to /NoclipProtocol/Hud/player.health|stamina|sanity (0..1).
#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>

namespace NoclipProtocol
{
    class PlayerVitalsComponent
        : public AZ::Component
        , public AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(PlayerVitalsComponent, "{6C1E4A2B-93D7-4F5A-B8E1-2D7C9A0F3B61}");
        static void Reflect(AZ::ReflectContext* context);

        void Activate() override;
        void Deactivate() override;
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

    private:
        float m_health = 1.0f;
        float m_stamina = 1.0f;
        float m_sanity = 1.0f;
        float m_grainSet = -1.0f; // film grain intensity last sent (follows sanity)
        bool m_exhausted = false;

        float m_staminaDrain = 0.12f;   // per second while sprinting (about 8 s from full)
        float m_staminaRegen = 0.08f;   // per second otherwise
        float m_sanityDarkDrain = 0.006f; // per second in pitch dark (about 3 minutes from full)
        float m_sanityLightRegen = 0.004f; // per second in a lit area
    };
} // namespace NoclipProtocol
