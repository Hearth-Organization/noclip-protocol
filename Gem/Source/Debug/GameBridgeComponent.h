// A small file bridge into the running game (the game launcher), so Claude can test the game the way the editor's
// ClaudeBridge lets it test in the editor: requests in <project>/user/ClaudeBridge/game_inbox/*.json, answers in
// game_outbox/. Each request carries the token from game_session.json (new every run). Ops:
//   {"op":"console","command":"..."}        run a console command (hud_key, noclip_tp, LoadLevel, ...)
//   {"op":"screenshot","path":"/abs/x.png"} capture the screen
//   {"op":"registry","path":"/NoclipProtocol/..."} read a value (string, number or bool)
//   {"op":"record","dir":"/abs/dir","frames":N,"fps":30,"script":[[frame,"command"],...]}
//       film the game: a screenshot every frame (dir/f00000.png ...) and each frame's game time (dir/times.txt), the
//       script's console commands on their frames (the trailer); "scale" slows the game (t_simulationTickScale) for
//       more frames per game second. The answer comes when it's done. (t_simulationTickDeltaOverride stopped time.)
// Only in builds with the AZ_RELEASE_BUILD off (development and profile).
#pragma once

#include <AzCore/Component/Component.h>
#include <AzCore/Component/TickBus.h>
#include <AzCore/std/containers/vector.h>
#include <AzCore/std/utils.h>
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
        void Record(float deltaTime);
        struct Recording
        {
            AZStd::string m_id, m_dir;
            int m_frames = 0, m_frame = 0, m_failed = 0;
            AZStd::vector<AZStd::pair<int, AZStd::string>> m_script;
            AZStd::vector<float> m_times; // game time of each frame (the frames are resampled to an even rate later)
            float m_time = 0.0f;
            bool m_scaled = false;
        };
        Recording m_recording;
        // level_tour: a picture of every level in turn (the owner, Nov 28: "pictures from every level").
        // Its place is kept in the settings registry, since a level load replaces this component.
        void Tour(float deltaTime);
        float m_tourClock = 0.0f;
        bool m_tourShot = false;
        AZ::s64 m_tourSerial = -1;
    };
} // namespace NoclipProtocol
