#include "HearthDebugOverlayComponent.h"

#include <Atom/RPI.Public/AuxGeom/AuxGeomDraw.h>
#include <Atom/RPI.Public/AuxGeom/AuxGeomFeatureProcessorInterface.h>
#include <Atom/RPI.Public/Pass/ParentPass.h>
#include <Atom/RPI.Public/Pass/PassSystemInterface.h>
#include <Atom/RPI.Public/ViewportContext.h>
#include <Atom/RPI.Public/ViewportContextBus.h>
#include <AzCore/Component/ComponentApplicationBus.h>
#include <AzCore/Component/Entity.h>
#include <AzCore/Component/TransformBus.h>
#include <AzCore/IO/FileIO.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/std/time.h>
#include <AzFramework/Components/CameraBus.h>
#include <AzFramework/Font/FontInterface.h>
#include <AzFramework/Input/Buses/Requests/InputSystemCursorRequestBus.h>
#include <AzFramework/Input/Devices/Keyboard/InputDeviceKeyboard.h>
#include <AzFramework/Input/Devices/Mouse/InputDeviceMouse.h>

#include <cstdio>
#include <ctime>

namespace NoclipProtocol
{
    namespace
    {
        const char* const DebugSwitches[3][2] = { { "Show chunk borders", "/Hearth/Viewport/ShowChunks" },
                                                  { "Fly (noclip)", "/NoclipProtocol/Player/Fly" },
                                                  { "All lights on", "/NoclipProtocol/Debug/AllLightsOn" } };

        AZStd::string DebugText(const char* key, const char* fallback = "")
        {
            AZStd::string value;
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Get(value, key);
            }
            return value.empty() ? AZStd::string(fallback) : value;
        }

        bool DebugIsLight(const AZ::Entity* entity)
        {
            for (const AZ::Component* component : entity->GetComponents())
            {
                const AZStd::string type = component->RTTI_GetTypeName();
                if (type.find("Light") != AZStd::string::npos && type.find("Flicker") == AZStd::string::npos)
                {
                    return true;
                }
            }
            return false;
        }
    } // namespace

    void HearthDebugOverlayComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<HearthDebugOverlayComponent, AZ::Component>()->Version(1);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<HearthDebugOverlayComponent>("Hearth Debug Overlay", "F3 debug overlay: frame rate, chunks, seed, fly, bug report for Claude")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Noclip Protocol")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"));
            }
        }
    }

    void HearthDebugOverlayComponent::Activate()
    {
        AZ::TickBus::Handler::BusConnect();
        AzFramework::InputChannelEventListener::Connect();
    }

    void HearthDebugOverlayComponent::Deactivate()
    {
        AzFramework::InputChannelEventListener::Disconnect();
        AZ::TickBus::Handler::BusDisconnect();
        if (auto* passes = AZ::RPI::PassSystemInterface::Get(); passes && passes->GetRootPass())
        {
            passes->GetRootPass()->SetTimestampQueryEnabled(false);
        }
    }

    bool HearthDebugOverlayComponent::Flag(const char* key) const
    {
        bool on = false;
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Get(on, key);
        }
        return on;
    }

    void HearthDebugOverlayComponent::Toggle(const char* key)
    {
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Set(key, !Flag(key));
        }
    }

    bool HearthDebugOverlayComponent::OnInputChannelEventFiltered(const AzFramework::InputChannel& inputChannel)
    {
        using AzFramework::InputDeviceKeyboard;
        using AzFramework::InputDeviceMouse;
        if (!inputChannel.IsStateBegan())
        {
            return false;
        }
        const AzFramework::InputChannelId& id = inputChannel.GetInputChannelId();
        if (id == InputDeviceKeyboard::Key::Function03)
        {
            m_visible = !m_visible;
            if (auto* passes = AZ::RPI::PassSystemInterface::Get(); passes && passes->GetRootPass())
            {
                passes->GetRootPass()->SetTimestampQueryEnabled(m_visible);
            }
            return true;
        }
        if (id == InputDeviceKeyboard::Key::Function04)
        {
            Toggle("/NoclipProtocol/Player/Fly");
            m_toast = Flag("/NoclipProtocol/Player/Fly") ? "Flying: walls don't stop you" : "Walking again";
            m_toastTime = 2.0f;
            return true;
        }
        if (id == InputDeviceKeyboard::Key::Function08)
        {
            SaveBugReport();
            return true;
        }
        if (m_visible && id == InputDeviceMouse::Button::Left)
        {
            // Clicks on the switches and the button (works with the mouse free: Esc).
            AZ::Vector2 cursor = AZ::Vector2::CreateZero();
            AzFramework::InputSystemCursorRequestBus::EventResult(cursor, InputDeviceMouse::Id,
                &AzFramework::InputSystemCursorRequests::GetSystemCursorPositionNormalized);
            auto* viewports = AZ::Interface<AZ::RPI::ViewportContextRequestsInterface>::Get();
            AZ::RPI::ViewportContextPtr viewport = viewports ? viewports->GetDefaultViewportContext() : nullptr;
            if (!viewport)
            {
                return false;
            }
            const float x = cursor.GetX() * viewport->GetViewportSize().m_width;
            const float y = cursor.GetY() * viewport->GetViewportSize().m_height;
            if (x < m_panelLeft || x > m_panelLeft + m_panelWidth || m_rowHeight <= 0.0f)
            {
                return false;
            }
            const int row = static_cast<int>((y - m_rowsTop) / m_rowHeight);
            if (y >= m_rowsTop && row >= 0 && row < 3)
            {
                Toggle(DebugSwitches[row][1]);
                return true;
            }
            if (y >= m_rowsTop && row == 3)
            {
                SaveBugReport();
                return true;
            }
        }
        return false;
    }

    void HearthDebugOverlayComponent::SaveBugReport()
    {
        // <project>/user/BugReports/<time>.txt: what the overlay shows, where the player is, and the log's tail.
        AZ::IO::FileIOBase* io = AZ::IO::FileIOBase::GetInstance();
        if (!io)
        {
            return;
        }
        char stamp[32] = {};
        const std::time_t now = std::time(nullptr);
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d-%H%M%S", std::localtime(&now));
        io->CreatePath("@user@/BugReports");
        const AZStd::string path = AZStd::string::format("@user@/BugReports/%s.txt", stamp);
        AZ::EntityId camera;
        Camera::CameraSystemRequestBus::BroadcastResult(camera, &Camera::CameraSystemRequests::GetActiveCamera);
        AZ::Vector3 at = AZ::Vector3::CreateZero();
        AZ::TransformBus::EventResult(at, camera, &AZ::TransformBus::Events::GetWorldTranslation);
        AZStd::string report = AZStd::string::format(
            "Noclip Protocol bug report %s\n"
            "Frame rate %.0f fps, frame %.1f ms, worst %.1f ms\n"
            "Level 0 seed %s, chunk %s, %s\n"
            "Camera at %.2f %.2f %.2f\n"
            "Fly %s, chunk borders %s, all lights on %s\n"
            "What happened: (tell Claude, or write it here)\n",
            stamp, m_fps, m_frameMs, m_worst, DebugText("/NoclipProtocol/Level0/Seed", "?").c_str(),
            DebugText("/NoclipProtocol/Level0/PlayerChunk", "?").c_str(), DebugText("/Hearth/Viewport/Stats", "no chunks").c_str(),
            static_cast<float>(at.GetX()), static_cast<float>(at.GetY()), static_cast<float>(at.GetZ()),
            Flag("/NoclipProtocol/Player/Fly") ? "on" : "off", Flag("/Hearth/Viewport/ShowChunks") ? "on" : "off",
            Flag("/NoclipProtocol/Debug/AllLightsOn") ? "on" : "off");
        // The last part of the game's log.
        AZ::IO::HandleType log = AZ::IO::InvalidHandle;
        for (const char* logPath : { "@log@/Game.log", "@log@/Editor.log" })
        {
            if (io->Open(logPath, AZ::IO::OpenMode::ModeRead | AZ::IO::OpenMode::ModeBinary, log))
            {
                AZ::u64 size = 0;
                io->Size(log, size);
                const AZ::u64 keep = AZStd::min<AZ::u64>(size, 16384);
                io->Seek(log, static_cast<AZ::s64>(size - keep), AZ::IO::SeekType::SeekFromStart);
                AZStd::string tail;
                tail.resize(keep);
                io->Read(log, tail.data(), keep);
                io->Close(log);
                report += AZStd::string::format("\n--- last of %s ---\n", logPath) + tail;
                break;
            }
        }
        AZ::IO::HandleType out = AZ::IO::InvalidHandle;
        if (io->Open(path.c_str(), AZ::IO::OpenMode::ModeWrite | AZ::IO::OpenMode::ModeBinary, out))
        {
            io->Write(out, report.data(), report.size());
            io->Close(out);
            char resolved[1024] = {};
            io->ResolvePath(path.c_str(), resolved, sizeof(resolved));
            AZ_Printf("NoclipProtocol", "Bug report saved for Claude: %s", resolved);
            m_toast = "Bug report saved for Claude";
        }
        else
        {
            m_toast = "Couldn't save the bug report";
        }
        m_toastTime = 2.5f;
    }

    void HearthDebugOverlayComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        m_sampleTime += deltaTime;
        m_sampleSum += deltaTime;
        ++m_sampleFrames;
        m_worst = AZStd::max(m_worst, deltaTime * 1000.0f);
        if (m_sampleTime >= 0.5f)
        {
            m_frameMs = m_sampleSum / m_sampleFrames * 1000.0f;
            m_fps = m_sampleFrames / m_sampleSum;
            m_sampleTime = 0.0f;
            m_sampleSum = 0.0f;
            m_sampleFrames = 0;
        }
        m_toastTime -= deltaTime;
        if (!m_visible && m_toastTime <= 0.0f)
        {
            return;
        }
        auto* viewports = AZ::Interface<AZ::RPI::ViewportContextRequestsInterface>::Get();
        AZ::RPI::ViewportContextPtr viewport = viewports ? viewports->GetDefaultViewportContext() : nullptr;
        if (!viewport || !viewport->GetRenderScene())
        {
            return;
        }
        AZ::RPI::AuxGeomDrawPtr draw = AZ::RPI::AuxGeomFeatureProcessorInterface::GetDrawQueueForScene(viewport->GetRenderScene());
        auto* fonts = AZ::Interface<AzFramework::FontQueryInterface>::Get();
        AzFramework::FontDrawInterface* font = fonts ? fonts->GetDefaultFontDrawInterface() : nullptr;
        if (!draw || !font)
        {
            return;
        }
        const float w = static_cast<float>(viewport->GetViewportSize().m_width);
        const float h = static_cast<float>(viewport->GetViewportSize().m_height);
        const int32_t overlay = draw->GetOrAdd2DViewProjOverride();
        auto rect = [&](float x, float y, float rw, float rh, const AZ::Color& colour)
        {
            const AZ::Vector3 p[6] = { AZ::Vector3(x / w, y / h, 0.0f), AZ::Vector3((x + rw) / w, y / h, 0.0f), AZ::Vector3((x + rw) / w, (y + rh) / h, 0.0f),
                                       AZ::Vector3(x / w, y / h, 0.0f), AZ::Vector3((x + rw) / w, (y + rh) / h, 0.0f), AZ::Vector3(x / w, (y + rh) / h, 0.0f) };
            AZ::RPI::AuxGeomDraw::AuxGeomDynamicDrawArguments args;
            args.m_verts = p;
            args.m_vertCount = 6;
            args.m_colors = &colour;
            args.m_colorCount = 1;
            args.m_opacityType = AZ::RPI::AuxGeomDraw::OpacityType::Translucent;
            args.m_depthTest = AZ::RPI::AuxGeomDraw::DepthTest::Off;
            args.m_depthWrite = AZ::RPI::AuxGeomDraw::DepthWrite::Off;
            args.m_viewProjectionOverrideIndex = overlay;
            draw->DrawTriangles(args);
        };
        auto text = [&](float x, float y, const AZStd::string& value, float size, const AZ::Color& colour, bool right = false)
        {
            AzFramework::TextDrawParameters params;
            params.m_drawViewportId = viewport->GetId();
            params.m_position = AZ::Vector3(x, y, 1.0f);
            params.m_color = colour;
            params.m_textSizeFactor = size;
            params.m_hAlign = right ? AzFramework::TextHorizontalAlignment::Right : AzFramework::TextHorizontalAlignment::Left;
            params.m_vAlign = AzFramework::TextVerticalAlignment::Center;
            font->DrawScreenAlignedText2d(params, value);
        };
        const AZ::Color textColour(0.925f, 0.902f, 0.871f, 1.0f);
        const AZ::Color dim(0.553f, 0.522f, 0.482f, 1.0f);
        const AZ::Color ember(0.910f, 0.455f, 0.231f, 1.0f);
        const AZ::Color good(0.651f, 0.867f, 0.592f, 1.0f);
        const AZ::Color warn(0.941f, 0.820f, 0.478f, 1.0f);
        const AZ::Color panel(0.051f, 0.047f, 0.043f, 0.86f);

        if (m_toastTime > 0.0f && !m_toast.empty())
        {
            rect(w / 2.0f - 170.0f, h - 90.0f, 340.0f, 36.0f, panel);
            text(w / 2.0f - 156.0f, h - 72.0f, m_toast, 15.0f, textColour);
        }
        if (!m_visible)
        {
            return;
        }

        // Lights on screen: lit lights in front of the camera, within 40 m (counted twice a second).
        m_lightTimer -= deltaTime;
        if (m_lightTimer <= 0.0f)
        {
            m_lightTimer = 0.5f;
            AZ::EntityId camera;
            Camera::CameraSystemRequestBus::BroadcastResult(camera, &Camera::CameraSystemRequests::GetActiveCamera);
            AZ::Transform view = AZ::Transform::CreateIdentity();
            AZ::TransformBus::EventResult(view, camera, &AZ::TransformBus::Events::GetWorldTM);
            const AZ::Vector3 eye = view.GetTranslation();
            const AZ::Vector3 forward = view.GetBasisY().GetNormalized();
            int count = 0;
            AZ::ComponentApplicationBus::Broadcast(&AZ::ComponentApplicationRequests::EnumerateEntities, [&](AZ::Entity* entity)
            {
                if (entity->GetState() != AZ::Entity::State::Active || !DebugIsLight(entity))
                {
                    return;
                }
                AZ::Vector3 at = AZ::Vector3::CreateZero();
                AZ::TransformBus::EventResult(at, entity->GetId(), &AZ::TransformBus::Events::GetWorldTranslation);
                const AZ::Vector3 to = at - eye;
                const float distance = to.GetLength();
                if (distance < 40.0f && (distance < 2.0f || forward.Dot(to / distance) > 0.5f))
                {
                    ++count;
                }
            });
            m_lights = count;
        }
        float graphicsMs = 0.0f;
        if (auto* passes = AZ::RPI::PassSystemInterface::Get(); passes && passes->GetRootPass())
        {
            graphicsMs = static_cast<float>(passes->GetRootPass()->GetLatestTimestampResult().GetDurationInNanoseconds() / 1.0e6);
        }
        long residentMb = 0;
        if (FILE* statm = fopen("/proc/self/statm", "r"))
        {
            long pages = 0;
            long resident = 0;
            if (fscanf(statm, "%ld %ld", &pages, &resident) == 2)
            {
                residentMb = resident * 4096 / (1024 * 1024);
            }
            fclose(statm);
        }
        AZStd::string chunks = DebugText("/Hearth/Viewport/Stats", "0 chunks");
        chunks = chunks.substr(0, chunks.find(' '));
        AZ::s64 steps = 0;
        AZ::s64 heard = 0;
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Get(steps, "/NoclipProtocol/Debug/Steps");
            registry->Get(heard, "/NoclipProtocol/Debug/StepsHeard");
        }

        // The panel, top left.
        const float left = 24.0f;
        const float top = 24.0f;
        const float width = 330.0f;
        const float row = 26.0f;
        const float height = 92.0f + row * 5 + 34.0f + row * 3 + 18.0f + row * 3 + 52.0f + 40.0f;
        rect(left, top, width, height, panel);
        rect(left, top, 3.0f, height, ember);
        float y = top + 36.0f;
        const bool smooth = m_fps >= 30.0f;
        text(left + 18.0f, y, AZStd::string::format("%.0f", m_fps), 40.0f, smooth ? good : warn);
        text(left + 90.0f, y + 4.0f, smooth ? "fps · smooth" : "fps · slow", 15.0f, dim);
        y += 48.0f;
        const AZStd::string rows[5][2] = { { "Frame", AZStd::string::format("%.1f ms", m_frameMs) },
                                           { "Graphics", graphicsMs > 0.0f ? AZStd::string::format("%.1f ms", graphicsMs) : AZStd::string("…") },
                                           { "Memory", AZStd::string::format("%ld MB", residentMb) },
                                           { "Lights on screen", AZStd::string::format("%d", m_lights) },
                                           { "Chunks loaded", chunks } };
        for (const auto& line : rows)
        {
            text(left + 18.0f, y, line[0], 15.0f, dim);
            text(left + width - 18.0f, y, line[1], 15.0f, textColour, true);
            y += row;
        }
        y += 10.0f;
        text(left + 18.0f, y, "Level 0", 16.0f, ember);
        y += row;
        const AZStd::string level[3][2] = { { "Seed", DebugText("/NoclipProtocol/Level0/Seed", "not running") },
                                            { "Chunk", DebugText("/NoclipProtocol/Level0/PlayerChunk", "-") },
                                            { "Steps heard", AZStd::string::format("%lld of %lld", static_cast<long long>(heard), static_cast<long long>(steps)) } };
        for (const auto& line : level)
        {
            text(left + 18.0f, y, line[0], 15.0f, dim);
            text(left + width - 18.0f, y, line[1], 14.0f, textColour, true);
            y += row;
        }
        y += 18.0f;
        // Switches (click them with the mouse free).
        m_panelLeft = left;
        m_panelWidth = width;
        m_rowsTop = y - row / 2.0f;
        m_rowHeight = row;
        for (const auto& item : DebugSwitches)
        {
            const bool on = Flag(item[1]);
            text(left + 18.0f, y, item[0], 15.0f, textColour);
            rect(left + width - 58.0f, y - 9.0f, 38.0f, 18.0f, on ? ember : AZ::Color(1.0f, 1.0f, 1.0f, 0.15f));
            rect(on ? left + width - 38.0f : left + width - 56.0f, y - 7.0f, 16.0f, 14.0f, textColour);
            y += row;
        }
        rect(left + 18.0f, y - 12.0f, width - 36.0f, 30.0f, AZ::Color(0.910f, 0.455f, 0.231f, 0.25f));
        text(left + 30.0f, y + 3.0f, "Save a bug report for Claude", 15.0f, textColour);
        y += 44.0f;
        text(left + 18.0f, y, "F3  Hide     F4  Fly     F8  Bug report", 13.0f, dim);
    }
} // namespace NoclipProtocol
