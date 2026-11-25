// The in-game debug overlay (approved mockup "Debug"): F3 shows/hides it, F4 flies (noclip), F8 saves a bug report
// for Claude. Frame rate, frame and graphics time, memory, lights on screen, chunks loaded, Level 0's seed and the
// chunk you're in, steps heard; switches for chunk borders, fly and all lights on (click them with the mouse free:
// Esc frees it). Put it on any entity in the level (the player is a good place).
#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/std/string/string.h>
#include <AzFramework/Input/Events/InputChannelEventListener.h>

namespace NoclipProtocol
{
    class HearthDebugOverlayComponent
        : public AZ::Component
        , public AZ::TickBus::Handler
        , public AzFramework::InputChannelEventListener
    {
    public:
        AZ_COMPONENT(HearthDebugOverlayComponent, "{C4E1A9B7-5D2F-4B8E-9E3A-7A6F1D0C2B58}");
        static void Reflect(AZ::ReflectContext* context);

        void Activate() override;
        void Deactivate() override;
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;
        int GetTickOrder() override { return AZ::TICK_UI; }
        bool OnInputChannelEventFiltered(const AzFramework::InputChannel& inputChannel) override;

    private:
        void SaveBugReport();
        void Toggle(const char* key);
        bool Flag(const char* key) const;

        bool m_visible = false;
        float m_frameMs = 0.0f;
        float m_fps = 0.0f;
        float m_sampleTime = 0.0f;
        int m_sampleFrames = 0;
        float m_sampleSum = 0.0f;
        float m_worst = 0.0f;
        int m_lights = 0;
        float m_lightTimer = 0.0f;
        AZStd::string m_toast;
        float m_toastTime = 0.0f;
        // Where the switches and the button were drawn (pixels), for clicks.
        float m_rowsTop = 0.0f;
        float m_rowHeight = 0.0f;
        float m_panelLeft = 0.0f;
        float m_panelWidth = 0.0f;
    };
} // namespace NoclipProtocol
