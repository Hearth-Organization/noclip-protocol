#include "HearthHudComponent.h"

#include <Atom/RPI.Public/AuxGeom/AuxGeomDraw.h>
#include <Atom/RPI.Public/AuxGeom/AuxGeomFeatureProcessorInterface.h>
#include <Atom/RPI.Public/RPISystemInterface.h>
#include <Atom/RPI.Public/Scene.h>
#include <Atom/RPI.Public/ViewportContext.h>
#include <Atom/RPI.Public/ViewportContextBus.h>
#include <AzCore/IO/FileIO.h>
#include <AzCore/JSON/document.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzFramework/Font/FontInterface.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace NoclipProtocol
{
    namespace
    {
        AZ::Color HudColour(const char* hex, const AZ::Color& fallback)
        {
            if (!hex || hex[0] != '#' || (strlen(hex) != 7 && strlen(hex) != 9))
            {
                return fallback;
            }
            const unsigned long value = strtoul(hex + 1, nullptr, 16);
            if (strlen(hex) == 9)
            {
                return AZ::Color(((value >> 24) & 0xFF) / 255.0f, ((value >> 16) & 0xFF) / 255.0f, ((value >> 8) & 0xFF) / 255.0f, (value & 0xFF) / 255.0f);
            }
            return AZ::Color(((value >> 16) & 0xFF) / 255.0f, ((value >> 8) & 0xFF) / 255.0f, (value & 0xFF) / 255.0f, 1.0f);
        }

        int HudColumn(const AZStd::string& anchor)
        {
            if (anchor.ends_with("left")) return 0;
            if (anchor.ends_with("right")) return 2;
            return 1;
        }

        int HudRow(const AZStd::string& anchor)
        {
            if (anchor.starts_with("top")) return 0;
            if (anchor.starts_with("bottom")) return 2;
            return 1;
        }
    } // namespace

    void HearthHudComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<HearthHudComponent, AZ::Component>()
                ->Version(1)
                ->Field("Canvas", &HearthHudComponent::m_canvasPath)
                ->Field("Show", &HearthHudComponent::m_show);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<HearthHudComponent>("Hearth HUD", "Draws the HUD made in Hearth Engine's Game UI editor while the game runs")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Noclip Protocol")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HearthHudComponent::m_canvasPath, "Canvas", "The HUD file, from the project folder")
                    ->DataElement(AZ::Edit::UIHandlers::Default, &HearthHudComponent::m_show, "Show", "Draw the HUD");
            }
        }
    }

    bool HearthHudComponent::Load()
    {
        m_elements.clear();
        AZ::IO::FileIOBase* io = AZ::IO::FileIOBase::GetInstance();
        if (!io)
        {
            return false;
        }
        // The project's file while developing; the packaged game has a copy next to its assets.
        AZStd::string text;
        for (const AZStd::string& path : { AZStd::string("@projectroot@/") + m_canvasPath, AZStd::string("@products@/") + [this]() { AZStd::string lower = m_canvasPath; for (char& c : lower) { c = static_cast<char>(tolower(static_cast<unsigned char>(c))); } return lower; }() })
        {
            AZ::IO::HandleType handle = AZ::IO::InvalidHandle;
            if (io->Open(path.c_str(), AZ::IO::OpenMode::ModeRead | AZ::IO::OpenMode::ModeBinary, handle))
            {
                AZ::u64 size = 0;
                io->Size(handle, size);
                text.resize(size);
                io->Read(handle, text.data(), size);
                io->Close(handle);
                break;
            }
        }
        rapidjson::Document document;
        document.Parse(text.c_str());
        if (document.HasParseError() || !document.IsObject() || !document.HasMember("elements") || !document["elements"].IsArray())
        {
            AZ_Warning("HearthHud", false, "No HUD in %s (make one in Hearth Engine: Tools > Game UI editor)", m_canvasPath.c_str());
            return false;
        }
        if (document.HasMember("designSize") && document["designSize"].IsArray() && document["designSize"].Size() == 2)
        {
            m_designW = static_cast<float>(document["designSize"][0].GetDouble());
            m_designH = static_cast<float>(document["designSize"][1].GetDouble());
        }
        auto str = [](const rapidjson::Value& v, const char* key) -> AZStd::string
        {
            return v.HasMember(key) && v[key].IsString() ? AZStd::string(v[key].GetString()) : AZStd::string();
        };
        auto num = [](const rapidjson::Value& v, const char* key, float fallback)
        {
            return v.HasMember(key) && v[key].IsNumber() ? static_cast<float>(v[key].GetDouble()) : fallback;
        };
        for (const auto& v : document["elements"].GetArray())
        {
            if (!v.IsObject())
            {
                continue;
            }
            Element e;
            e.m_id = str(v, "id");
            e.m_parent = str(v, "parent");
            e.m_type = str(v, "type");
            e.m_anchor = str(v, "anchor").empty() ? AZStd::string("top-left") : str(v, "anchor");
            e.m_bind = str(v, "bind");
            e.m_text = str(v, "text");
            e.m_fill = HudColour(str(v, "fill").c_str(), e.m_fill);
            e.m_hasSize = v.HasMember("w");
            e.m_x = num(v, "x", 0.0f);
            e.m_y = num(v, "y", 0.0f);
            e.m_w = num(v, "w", 100.0f);
            e.m_h = num(v, "h", 24.0f);
            e.m_pulseBelow = num(v, "pulseBelow", -1.0f);
            m_elements.push_back(e);
        }
        return true;
    }

    HearthHudComponent::Box HearthHudComponent::ParentRect(const Element& element, int depth) const
    {
        if (!element.m_parent.empty() && depth < 16)
        {
            for (const Element& other : m_elements)
            {
                if (other.m_id == element.m_parent)
                {
                    return other.m_type == "group" && !other.m_hasSize ? ParentRect(other, depth + 1) : Rect(other, depth + 1);
                }
            }
        }
        return { 0.0f, 0.0f, m_designW, m_designH };
    }

    HearthHudComponent::Box HearthHudComponent::Rect(const Element& element, int depth) const
    {
        const Box parent = ParentRect(element, depth);
        if (element.m_type == "group" && !element.m_hasSize)
        {
            return parent;
        }
        float left = parent.x + element.m_x;
        float top = parent.y + element.m_y;
        switch (HudColumn(element.m_anchor))
        {
        case 1: left = parent.x + parent.w / 2.0f - element.m_w / 2.0f + element.m_x; break;
        case 2: left = parent.x + parent.w - element.m_w - element.m_x; break;
        default: break;
        }
        switch (HudRow(element.m_anchor))
        {
        case 1: top = parent.y + parent.h / 2.0f - element.m_h / 2.0f + element.m_y; break;
        case 2: top = parent.y + parent.h - element.m_h - element.m_y; break;
        default: break;
        }
        return { left, top, element.m_w, element.m_h };
    }

    double HearthHudComponent::Number(const AZStd::string& bind, double fallback) const
    {
        double value = fallback;
        if (auto* registry = AZ::SettingsRegistry::Get(); registry && !bind.empty())
        {
            registry->Get(value, AZStd::string::format("/NoclipProtocol/Hud/%s", bind.c_str()));
        }
        return AZ::GetClamp(value, 0.0, 1.0);
    }

    AZStd::string HearthHudComponent::Text(const Element& element) const
    {
        AZStd::string value;
        if (auto* registry = AZ::SettingsRegistry::Get(); registry && !element.m_bind.empty())
        {
            registry->Get(value, AZStd::string::format("/NoclipProtocol/Hud/%s", element.m_bind.c_str()));
        }
        return value.empty() ? element.m_text : value;
    }

    void HearthHudComponent::Activate()
    {
        if (Load())
        {
            AZ::TickBus::Handler::BusConnect();
        }
    }

    void HearthHudComponent::Deactivate()
    {
        AZ::TickBus::Handler::BusDisconnect();
    }

    void HearthHudComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        m_time += deltaTime;
        if (!m_show)
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
        if (!draw)
        {
            return;
        }
        const AzFramework::WindowSize size = viewport->GetViewportSize();
        const float screenW = static_cast<float>(size.m_width);
        const float screenH = static_cast<float>(size.m_height);
        // Fit the design size into the window (same as the editor's device view).
        const float scale = AZStd::min(screenW / m_designW, screenH / m_designH);
        const float offsetX = (screenW - m_designW * scale) / 2.0f;
        const float offsetY = (screenH - m_designH * scale) / 2.0f;
        const int32_t overlay = draw->GetOrAdd2DViewProjOverride();

        auto rect = [&](const Box& b, const AZ::Color& colour)
        {
            const float x0 = (offsetX + b.x * scale) / screenW;
            const float y0 = (offsetY + b.y * scale) / screenH;
            const float x1 = (offsetX + (b.x + b.w) * scale) / screenW;
            const float y1 = (offsetY + (b.y + b.h) * scale) / screenH;
            const AZ::Vector3 points[6] = { AZ::Vector3(x0, y0, 0.0f), AZ::Vector3(x1, y0, 0.0f), AZ::Vector3(x1, y1, 0.0f),
                                            AZ::Vector3(x0, y0, 0.0f), AZ::Vector3(x1, y1, 0.0f), AZ::Vector3(x0, y1, 0.0f) };
            AZ::RPI::AuxGeomDraw::AuxGeomDynamicDrawArguments args;
            args.m_verts = points;
            args.m_vertCount = 6;
            args.m_colors = &colour;
            args.m_colorCount = 1;
            args.m_opacityType = colour.GetA() < 1.0f ? AZ::RPI::AuxGeomDraw::OpacityType::Translucent : AZ::RPI::AuxGeomDraw::OpacityType::Opaque;
            args.m_depthTest = AZ::RPI::AuxGeomDraw::DepthTest::Off;
            args.m_depthWrite = AZ::RPI::AuxGeomDraw::DepthWrite::Off;
            args.m_viewProjectionOverrideIndex = overlay;
            draw->DrawTriangles(args);
        };
        auto ring = [&](const Box& b, float share, const AZ::Color& colour, float thickness)
        {
            const float cx = offsetX + (b.x + b.w / 2.0f) * scale;
            const float cy = offsetY + (b.y + b.h / 2.0f) * scale;
            const float outer = AZStd::min(b.w, b.h) * 0.46f * scale;
            const float inner = outer - thickness * scale;
            const int steps = AZStd::max(2, static_cast<int>(48 * share));
            AZStd::vector<AZ::Vector3> points;
            for (int i = 0; i < steps; ++i)
            {
                const float a0 = -AZ::Constants::HalfPi + AZ::Constants::TwoPi * share * i / steps;
                const float a1 = -AZ::Constants::HalfPi + AZ::Constants::TwoPi * share * (i + 1) / steps;
                auto at = [&](float r, float a) { return AZ::Vector3((cx + r * std::cos(a)) / screenW, (cy + r * std::sin(a)) / screenH, 0.0f); };
                points.push_back(at(outer, a0));
                points.push_back(at(outer, a1));
                points.push_back(at(inner, a1));
                points.push_back(at(outer, a0));
                points.push_back(at(inner, a1));
                points.push_back(at(inner, a0));
            }
            AZ::RPI::AuxGeomDraw::AuxGeomDynamicDrawArguments args;
            args.m_verts = points.data();
            args.m_vertCount = static_cast<uint32_t>(points.size());
            args.m_colors = &colour;
            args.m_colorCount = 1;
            args.m_opacityType = AZ::RPI::AuxGeomDraw::OpacityType::Translucent;
            args.m_depthTest = AZ::RPI::AuxGeomDraw::DepthTest::Off;
            args.m_depthWrite = AZ::RPI::AuxGeomDraw::DepthWrite::Off;
            args.m_viewProjectionOverrideIndex = overlay;
            draw->DrawTriangles(args);
        };
        auto write = [&](const Box& b, const AZStd::string& text, const AZ::Color& colour, int align, float sizeShare)
        {
            if (!font || text.empty())
            {
                return;
            }
            AzFramework::TextDrawParameters params;
            params.m_drawViewportId = viewport->GetId();
            const float x = align == 0 ? b.x : (align == 2 ? b.x + b.w : b.x + b.w / 2.0f);
            params.m_position = AZ::Vector3(offsetX + x * scale, offsetY + (b.y + b.h / 2.0f) * scale, 1.0f);
            params.m_color = colour;
            params.m_textSizeFactor = AZStd::max(8.0f, b.h * sizeShare * scale);
            params.m_hAlign = align == 0 ? AzFramework::TextHorizontalAlignment::Left
                            : (align == 2 ? AzFramework::TextHorizontalAlignment::Right : AzFramework::TextHorizontalAlignment::Center);
            params.m_vAlign = AzFramework::TextVerticalAlignment::Center;
            font->DrawScreenAlignedText2d(params, text);
        };

        const AZ::Color shadow(0.0f, 0.0f, 0.0f, 0.47f);
        const AZ::Color ink(0.051f, 0.047f, 0.043f, 1.0f);
        double toastUntil = 0.0;
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Get(toastUntil, "/NoclipProtocol/Hud/toast.until");
        }
        for (const Element& element : m_elements)
        {
            const Box b = Rect(element);
            AZ::Color fill = element.m_fill;
            const double value = Number(element.m_bind, 1.0);
            if (element.m_pulseBelow >= 0.0f && value < element.m_pulseBelow)
            {
                fill.SetA(fill.GetA() * (0.55f + 0.45f * std::sin(m_time * 6.0f)));
            }
            if (element.m_type == "bar")
            {
                rect(b, shadow);
                rect({ b.x, b.y, b.w * static_cast<float>(value), b.h }, fill);
            }
            else if (element.m_type == "meter")
            {
                ring(b, 1.0f, shadow, AZStd::min(b.w, b.h) * 0.1f);
                ring(b, static_cast<float>(value), fill, AZStd::min(b.w, b.h) * 0.1f);
            }
            else if (element.m_type == "crosshair")
            {
                const float t = AZStd::max(1.0f, b.w * 0.08f);
                const float cx = b.x + b.w / 2.0f;
                const float cy = b.y + b.h / 2.0f;
                rect({ b.x, cy - t / 2, b.w / 3.0f, t }, fill);
                rect({ b.x + b.w * 2.0f / 3.0f, cy - t / 2, b.w / 3.0f, t }, fill);
                rect({ cx - t / 2, b.y, t, b.h / 3.0f }, fill);
                rect({ cx - t / 2, b.y + b.h * 2.0f / 3.0f, t, b.h / 3.0f }, fill);
            }
            else if (element.m_type == "list")
            {
                rect(b, AZ::Color(0.0f, 0.0f, 0.0f, 0.35f));
            }
            else if (element.m_type == "button")
            {
                rect(b, fill);
                write(b, Text(element), ink, 1, 0.5f);
            }
            else if (element.m_type == "text" || element.m_type == "row")
            {
                if (element.m_bind == "toast" && toastUntil <= m_time)
                {
                    continue;
                }
                write(b, Text(element), fill, HudColumn(element.m_anchor) == 2 ? 2 : 0, element.m_type == "text" ? 0.62f : 0.5f);
            }
        }
    }
} // namespace NoclipProtocol

// Noclip HUD (the approved HUD mockup and the pause menu) is compiled with this file until the project is reconfigured
// with it in noclipprotocol_files.cmake (a reconfigure build waits for the owner's OK).
#include "NoclipHudComponent.cpp"
