// "Hearth HUD": draws the HUD made in Hearth Engine's Game UI editor (UI/HUD.uicanvas, Hearth JSON) while the game
// runs. Bars, meters, text, rows, buttons, crosshair; each element can be bound to a value the game sets in the
// settings registry under /NoclipProtocol/Hud/<bind> (numbers 0..1 for bars and meters, text for text):
//   player.health, player.stamina, player.sanity, player.flashlight, weapon.ammo, interact.prompt, game.objective,
//   squad (text, one "Name · 100" per line), toast (text; shown while /NoclipProtocol/Hud/toast.until > game time).
// Values nothing sets yet show full (bars) or the element's own text.
#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/Math/Color.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/string/string.h>

namespace NoclipProtocol
{
    class HearthHudComponent
        : public AZ::Component
        , public AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(HearthHudComponent, "{2B6A3D5E-8C1F-4E7A-A9D2-5F0B3C7E1D44}");
        static void Reflect(AZ::ReflectContext* context);

        void Activate() override;
        void Deactivate() override;
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;
        int GetTickOrder() override { return AZ::TICK_UI; }

    private:
        struct Element
        {
            AZStd::string m_id;
            AZStd::string m_parent;
            AZStd::string m_type;
            AZStd::string m_anchor = "top-left";
            AZStd::string m_bind;
            AZStd::string m_text;
            AZ::Color m_fill = AZ::Color(0.925f, 0.902f, 0.871f, 1.0f);
            float m_x = 0.0f, m_y = 0.0f, m_w = 100.0f, m_h = 24.0f;
            bool m_hasSize = true;
            float m_pulseBelow = -1.0f;
        };
        struct Box
        {
            float x, y, w, h;
        };

        bool Load();
        Box Rect(const Element& element, int depth = 0) const;
        Box ParentRect(const Element& element, int depth) const;
        double Number(const AZStd::string& bind, double fallback) const;
        AZStd::string Text(const Element& element) const;

        AZStd::string m_canvasPath = "UI/HUD.uicanvas";
        bool m_show = true;
        AZStd::vector<Element> m_elements;
        float m_designW = 1920.0f;
        float m_designH = 1080.0f;
        float m_time = 0.0f;
    };
} // namespace NoclipProtocol
