// Noclip Protocol's in-game HUD, built from the approved mockup (hearth-workspace
// noclip-protocol/mockups/ui-and-hearth-engine/HUD.dc.html: 1280 x 720 design, IBM Plex Mono and Oswald), plus the
// pause menu (Esc) with settings, and the intro's briefing terminal (mockups/ui-and-hearth-engine/Terminal.dc.html).
//
// Every element of the mockup is drawn, with live values:
//   level badge (level.title / level.subtitle), forward base distance (base.distance), compass with the base marker,
//   objective panel (game.objective, game.progress), squad list (squad: "Name|12 m" per line, "NO SIGNAL" in red),
//   centre dot, interaction prompt (interact.key / interact.verb / interact.item), HEALTH / STAMINA / SANITY bars
//   (player.health / stamina / sanity, 0..1), flashlight segments and time left (player.flashlight,
//   player.flashlight.seconds), weapon and ammo (weapon.name / weapon.ammo / weapon.reserve), edge vignette, toast.
// Values live in the settings registry under /NoclipProtocol/Hud/. Parts with nothing to show (no squad in single
// player, no weapon yet, nothing to pick up) stay hidden until there is. Console: hud_mockup 1 fills every part with
// the mockup's own values, to compare against the design; hud_show 0 hides the HUD.
#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Color.h>
#include <AzCore/Math/Vector2.h>
#include <AzCore/std/containers/unordered_map.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>
#include <AzFramework/Font/FontInterface.h>
#include <AzFramework/Input/Events/InputChannelEventListener.h>

namespace NoclipProtocol
{
    class NoclipHudComponent
        : public AZ::Component
        , public AZ::TickBus::Handler
        , public AzFramework::InputChannelEventListener
    {
    public:
        AZ_COMPONENT(NoclipHudComponent, "{8F2D6B1A-4C7E-4A93-B5D0-1E9A7C3F6B28}");
        static void Reflect(AZ::ReflectContext* context);

        void Activate() override;
        void Deactivate() override;
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;
        int GetTickOrder() override { return AZ::TICK_UI; }
        bool OnInputChannelEventFiltered(const AzFramework::InputChannel& inputChannel) override;

    private:
        enum class Face { Mono, MonoMedium, MonoSemiBold, Oswald, OswaldLarge };
        enum class Page { None, Pause, Settings, Terminal };

        struct Button
        {
            float x, y, w, h; // design pixels
            int action;
        };

        AzFramework::FontDrawInterface* Font(Face face);
        bool HandleKey(const AzFramework::InputChannelId& id); // a key press (from the keyboard or hud_key)
        void LoadFonts();
        void LoadSettings();
        void SaveSettings();
        void ApplySettings();
        void DoAction(int action);

        bool m_show = true;
        int m_terminalFile = 0; // the intro terminal's open file (0 = Operation Noclip Protocol)
        float m_time = 0.0f;
        Page m_page = Page::None;
        AZStd::vector<Button> m_buttons;
        AZ::Vector2 m_cursor = AZ::Vector2::CreateZero(); // design pixels
        bool m_click = false;
        bool m_fontsLoaded = false;
        int m_fontKey = -1; // hud_font_smooth * 10 + hud_font_amount the faces were loaded with
        AzFramework::ViewportId m_viewportId = AzFramework::InvalidViewportId; // the viewport the HUD draws into (fonts measure against it)
        AzFramework::FontDrawInterface* m_fonts[5] = { nullptr, nullptr, nullptr, nullptr, nullptr };


        // Settings (saved to the project's user/Registry/noclip_settings.setreg)
        bool m_rayTracing = true;
        float m_mouseSensitivity = 1.0f; // multiplier
        float m_volumeDb = 0.0f;
    };
} // namespace NoclipProtocol
