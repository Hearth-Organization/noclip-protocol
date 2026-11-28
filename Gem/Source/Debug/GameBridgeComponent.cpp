#include "GameBridgeComponent.h"

#include "FrameCaptureBus.h"
#include <AzCore/Console/IConsole.h>
#include <AzCore/IO/SystemFile.h>
#include <AzCore/JSON/document.h>
#include <AzCore/JSON/stringbuffer.h>
#include <AzCore/JSON/writer.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/Settings/SettingsRegistryMergeUtils.h>

#include <cstdio>
#include <random>

namespace NoclipProtocol
{
    namespace
    {
        constexpr const char* TourKey = "/NoclipProtocol/Tour";

        void GoToLevel(int number)
        {
            // Next frame, never inside the bridge's own tick (a load tears the bridge down).
            AZ::TickBus::QueueFunction([number]()
            {
                if (auto* console = AZ::Interface<AZ::IConsole>::Get())
                {
                    console->PerformCommand(AZStd::string::format("level_go %d", number).c_str());
                }
            });
        }

        // level_tour <first> <last> <folder> [seconds]: visits catalog levels first..last, and in each, after it has
        // settled for the given seconds (default 14), saves <folder>/levelNN.png with the HUD hidden and monsters frozen.
        void LevelTour(const AZ::ConsoleCommandContainer& arguments)
        {
            auto* registry = AZ::SettingsRegistry::Get();
            if (arguments.size() < 3 || !registry)
            {
                AZ_Printf("NoclipProtocol", "level_tour <first> <last> <folder> [seconds]");
                return;
            }
            const int first = AZStd::stoi(AZStd::string(arguments[0]));
            const int last = AZStd::stoi(AZStd::string(arguments[1]));
            const double wait = arguments.size() >= 4 ? AZStd::stod(AZStd::string(arguments[3])) : 14.0;
            registry->Set(AZStd::string(TourKey) + "/Current", static_cast<AZ::s64>(first));
            registry->Set(AZStd::string(TourKey) + "/Last", static_cast<AZ::s64>(last));
            registry->Set(AZStd::string(TourKey) + "/Dir", AZStd::string(arguments[2]));
            registry->Set(AZStd::string(TourKey) + "/Wait", wait);
            registry->Set(AZStd::string(TourKey) + "/Active", true);
            AZ::s64 serial = 0;
            registry->Get(serial, AZStd::string(TourKey) + "/Serial");
            registry->Set(AZStd::string(TourKey) + "/Serial", serial + 1); // a new tour: every bridge starts its clock again
            AZ_Printf("NoclipProtocol", "level_tour: levels %d to %d, %.0f s each", first, last, wait);
            GoToLevel(first);
        }
        AZ_CONSOLEFREEFUNC("level_tour", LevelTour, AZ::ConsoleFunctorFlags::Null,
            "level_tour <first> <last> <folder> [seconds]: a screenshot of each catalog level in turn");
    }

    void GameBridgeComponent::Tour(float deltaTime)
    {
        auto* registry = AZ::SettingsRegistry::Get();
        bool active = false;
        if (!registry || !registry->Get(active, AZStd::string(TourKey) + "/Active") || !active)
        {
            return;
        }
        AZ::s64 current = 0, last = 0;
        double wait = 14.0;
        AZStd::string dir;
        registry->Get(current, AZStd::string(TourKey) + "/Current");
        registry->Get(last, AZStd::string(TourKey) + "/Last");
        registry->Get(wait, AZStd::string(TourKey) + "/Wait");
        registry->Get(dir, AZStd::string(TourKey) + "/Dir");
        AZ::s64 serial = 0;
        registry->Get(serial, AZStd::string(TourKey) + "/Serial");
        if (serial != m_tourSerial)
        {
            m_tourSerial = serial;
            m_tourClock = 0.0f;
            m_tourShot = false;
        }
        if (m_tourClock == 0.0f)
        {
            if (auto* console = AZ::Interface<AZ::IConsole>::Get())
            {
                console->PerformCommand("hud_show 0");
                console->PerformCommand("player_god 1");
                console->PerformCommand("monster_debug freeze 1");
            }
        }
        m_tourClock += deltaTime;
        if (!m_tourShot && m_tourClock >= static_cast<float>(wait))
        {
            m_tourShot = true;
            AZ::IO::SystemFile::CreateDir(dir.c_str());
            const AZStd::string shot = AZStd::string::format("%s/level%02d.png", dir.c_str(), static_cast<int>(current));
            AZ::Render::FrameCaptureOutcome outcome;
            AZ::Render::FrameCaptureRequestBus::BroadcastResult(outcome, &AZ::Render::FrameCaptureRequestBus::Events::CaptureScreenshot, shot);
            AZ_Printf("NoclipProtocol", "level_tour: level %d -> %s", static_cast<int>(current), shot.c_str());
        }
        if (m_tourShot && m_tourClock >= static_cast<float>(wait) + 1.5f)
        {
            if (current >= last)
            {
                registry->Set(AZStd::string(TourKey) + "/Active", false);
                AZ_Printf("NoclipProtocol", "level_tour: done");
                return;
            }
            registry->Set(AZStd::string(TourKey) + "/Current", current + 1);
            m_tourShot = false;
            m_tourClock = -20.0f; // this bridge goes with the level (if a load never comes, it carries on after 20 s)
            GoToLevel(static_cast<int>(current + 1));
        }
    }
    namespace
    {
        bool WriteText(const AZStd::string& path, const AZStd::string& text)
        {
            const AZStd::string tmp = path + ".tmp";
            FILE* f = std::fopen(tmp.c_str(), "wb");
            if (!f)
            {
                return false;
            }
            std::fwrite(text.data(), 1, text.size(), f);
            std::fclose(f);
            return std::rename(tmp.c_str(), path.c_str()) == 0;
        }

        AZStd::string ReadText(const AZStd::string& path)
        {
            AZStd::string text;
            FILE* f = std::fopen(path.c_str(), "rb");
            if (!f)
            {
                return text;
            }
            char buffer[4096];
            size_t n;
            while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0)
            {
                text.append(buffer, n);
            }
            std::fclose(f);
            return text;
        }

        AZStd::string Escape(const AZStd::string& value)
        {
            rapidjson::StringBuffer buffer;
            rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
            writer.String(value.c_str(), static_cast<rapidjson::SizeType>(value.size()));
            return buffer.GetString();
        }
    } // namespace

    void GameBridgeComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<GameBridgeComponent, AZ::Component>()->Version(1);
        }
    }

    void GameBridgeComponent::Activate()
    {
#if !defined(AZ_RELEASE_BUILD)
        AZ::IO::FixedMaxPath project;
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Get(project.Native(), AZ::SettingsRegistryMergeUtils::FilePathKey_ProjectPath);
        }
        if (project.empty())
        {
            return;
        }
        m_folder = AZStd::string(project.c_str()) + "/user/ClaudeBridge";
        AZ::IO::SystemFile::CreateDir((m_folder + "/game_inbox").c_str());
        AZ::IO::SystemFile::CreateDir((m_folder + "/game_outbox").c_str());
        std::random_device random;
        m_token = AZStd::string::format("%08x%08x%08x", random(), random(), random());
        WriteText(m_folder + "/game_session.json", AZStd::string::format("{\"token\": \"%s\"}\n", m_token.c_str()));
        AZ::TickBus::Handler::BusConnect();
#endif
    }

    void GameBridgeComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
    }

    void GameBridgeComponent::Record(float deltaTime)
    {
        Recording& r = m_recording;
        auto* console = AZ::Interface<AZ::IConsole>::Get();
        if (r.m_frame > 0)
        {
            r.m_time += deltaTime;
        }
        r.m_times.push_back(r.m_time);
        for (const auto& [frame, command] : r.m_script)
        {
            if (frame == r.m_frame && console)
            {
                console->PerformCommand(command.c_str());
            }
        }
        AZ::Render::FrameCaptureOutcome outcome;
        const AZStd::string shot = AZStd::string::format("%s/f%05d.png", r.m_dir.c_str(), r.m_frame);
        AZ::Render::FrameCaptureRequestBus::BroadcastResult(outcome, &AZ::Render::FrameCaptureRequestBus::Events::CaptureScreenshot, shot);
        if (!outcome.IsSuccess())
        {
            ++r.m_failed;
        }
        if (++r.m_frame >= r.m_frames)
        {
            if (console && r.m_scaled)
            {
                console->PerformCommand("t_simulationTickScale 1");
            }
            AZStd::string times;
            for (float t : r.m_times)
            {
                times += AZStd::string::format("%.5f\n", t);
            }
            WriteText(r.m_dir + "/times.txt", times);
            WriteText(m_folder + "/game_outbox/" + r.m_id + ".json",
                AZStd::string::format("{\"ok\": true, \"frames\": %d, \"failed\": %d, \"dir\": %s}\n", r.m_frames, r.m_failed, Escape(r.m_dir).c_str()));
            r = Recording();
        }
    }

    void GameBridgeComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        if (m_recording.m_frames > 0)
        {
            Record(deltaTime);
        }
        Tour(deltaTime);
        m_wait -= deltaTime;
        if (m_wait > 0.0f)
        {
            return;
        }
        m_wait = 0.25f;
        AZStd::vector<AZStd::string> files;
        AZ::IO::SystemFile::FindFiles((m_folder + "/game_inbox/*.json").c_str(), [&files](const char* name, bool isFile)
        {
            if (isFile)
            {
                files.push_back(name);
            }
            return true;
        });
        AZStd::sort(files.begin(), files.end());
        for (const AZStd::string& name : files)
        {
            Handle(name);
        }
    }

    void GameBridgeComponent::Handle(const AZStd::string& name)
    {
        const AZStd::string path = m_folder + "/game_inbox/" + name;
        const AZStd::string text = ReadText(path);
        AZ::IO::SystemFile::Delete(path.c_str());
        AZStd::string id = name.substr(0, name.size() - 5);
        rapidjson::Document request;
        request.Parse(text.c_str());
        AZStd::string answer;
        auto str = [&request](const char* key) -> AZStd::string
        {
            return request.IsObject() && request.HasMember(key) && request[key].IsString() ? AZStd::string(request[key].GetString()) : AZStd::string();
        };
        if (!request.IsObject() || str("token") != m_token)
        {
            answer = "{\"ok\": false, \"error\": \"bad request or token\"}";
        }
        else if (str("op") == "console")
        {
            const AZStd::string command = str("command");
            // A command that loads a level tears down this level's entities, this bridge included, so it can't run while the
            // bridge is still answering (that crashed the game): such commands run next frame, after the answer is written.
            const bool loadsLevel = command.find("LoadLevel") == 0 || command.find("loadlevel") == 0 || command.find("UnloadLevel") == 0;
            bool ran = false;
            if (loadsLevel)
            {
                AZ::TickBus::QueueFunction([command]()
                {
                    if (auto* console = AZ::Interface<AZ::IConsole>::Get())
                    {
                        console->PerformCommand(command.c_str());
                    }
                });
                ran = true;
            }
            else if (auto* console = AZ::Interface<AZ::IConsole>::Get())
            {
                ran = static_cast<bool>(console->PerformCommand(command.c_str()));
            }
            answer = AZStd::string::format("{\"ok\": %s, \"command\": %s%s}", ran ? "true" : "false", Escape(command).c_str(),
                loadsLevel ? ", \"queued\": true" : "");
        }
        else if (str("op") == "screenshot")
        {
            const AZStd::string shot = str("path");
            AZ::Render::FrameCaptureOutcome outcome;
            AZ::Render::FrameCaptureRequestBus::BroadcastResult(outcome, &AZ::Render::FrameCaptureRequestBus::Events::CaptureScreenshot, shot);
            answer = outcome.IsSuccess() ? AZStd::string::format("{\"ok\": true, \"path\": %s}", Escape(shot).c_str())
                                         : AZStd::string::format("{\"ok\": false, \"error\": %s}", Escape(outcome.GetError().m_errorMessage).c_str());
        }
        else if (str("op") == "registry")
        {
            const AZStd::string key = str("path");
            auto* registry = AZ::SettingsRegistry::Get();
            AZStd::string s;
            double d = 0.0;
            bool b = false;
            AZ::s64 i = 0;
            if (registry && registry->Get(s, key))
            {
                answer = AZStd::string::format("{\"ok\": true, \"value\": %s}", Escape(s).c_str());
            }
            else if (registry && registry->Get(d, key))
            {
                answer = AZStd::string::format("{\"ok\": true, \"value\": %g}", d);
            }
            else if (registry && registry->Get(i, key))
            {
                answer = AZStd::string::format("{\"ok\": true, \"value\": %lld}", static_cast<long long>(i));
            }
            else if (registry && registry->Get(b, key))
            {
                answer = AZStd::string::format("{\"ok\": true, \"value\": %s}", b ? "true" : "false");
            }
            else
            {
                answer = "{\"ok\": false, \"error\": \"no such value\"}";
            }
        }
        else if (str("op") == "record")
        {
            Recording r;
            r.m_id = id;
            r.m_dir = str("dir");
            r.m_frames = request.HasMember("frames") && request["frames"].IsInt() ? request["frames"].GetInt() : 0;
            const int fps = request.HasMember("fps") && request["fps"].IsInt() ? request["fps"].GetInt() : 30;
            if (request.HasMember("script") && request["script"].IsArray())
            {
                for (const auto& step : request["script"].GetArray())
                {
                    if (step.IsArray() && step.Size() == 2 && step[0].IsInt() && step[1].IsString())
                    {
                        r.m_script.push_back({ step[0].GetInt(), AZStd::string(step[1].GetString()) });
                    }
                }
            }
            if (r.m_dir.empty() || r.m_frames <= 0 || fps <= 0)
            {
                answer = "{\"ok\": false, \"error\": \"record needs dir and frames\"}";
            }
            else
            {
                AZ::IO::SystemFile::CreateDir(r.m_dir.c_str());
                const double scale = request.HasMember("scale") && request["scale"].IsNumber() ? request["scale"].GetDouble() : 1.0;
                if (auto* console = AZ::Interface<AZ::IConsole>::Get(); console && scale != 1.0)
                {
                    console->PerformCommand(AZStd::string::format("t_simulationTickScale %g", scale).c_str());
                    r.m_scaled = true;
                }
                m_recording = AZStd::move(r);
                return; // answered when the last frame is taken
            }
        }
        else
        {
            answer = "{\"ok\": false, \"error\": \"unknown op\"}";
        }
        WriteText(m_folder + "/game_outbox/" + id + ".json", answer + "\n");
    }
} // namespace NoclipProtocol
