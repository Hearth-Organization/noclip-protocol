// A small file bridge into the running game (the game launcher), so Claude can test the game the way the editor's
// ClaudeBridge lets it test in the editor: requests in <project>/user/ClaudeBridge/game_inbox/*.json, answers in
// game_outbox/. Each request carries the token from game_session.json (new every run). Ops:
//   {"op":"console","command":"..."}        run a console command (hud_key, noclip_tp, LoadLevel, ...)
//   {"op":"screenshot","path":"/abs/x.png"} capture the screen
//   {"op":"registry","path":"/NoclipProtocol/..."} read a value (string, number or bool)
// Only in builds with the AZ_RELEASE_BUILD off (development and profile).
#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/std/string/string.h>

namespace NoclipProtocol
{
    class GameBridgeComponent
        : public AZ::Component
        , public AZ::TickBus::Handler
    {
    public:
        AZ_COMPONENT(GameBridgeComponent, "{6E3B2D7A-41C9-4C58-9F0E-2B7D51A3C8E4}");
        static void Reflect(AZ::ReflectContext* context);

    protected:
        void Activate() override;
        void Deactivate() override;
        void OnTick(float deltaTime, AZ::ScriptTimePoint time) override;

    private:
        void Handle(const AZStd::string& file);
        AZStd::string m_folder; // <project>/user/ClaudeBridge
        AZStd::string m_token;
        float m_wait = 0.0f;
    };
} // namespace NoclipProtocol
