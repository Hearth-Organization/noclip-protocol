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

    void GameBridgeComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
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
        else
        {
            answer = "{\"ok\": false, \"error\": \"unknown op\"}";
        }
        WriteText(m_folder + "/game_outbox/" + id + ".json", answer + "\n");
    }
} // namespace NoclipProtocol
