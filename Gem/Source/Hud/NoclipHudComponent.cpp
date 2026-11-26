#include "NoclipHudComponent.h"

#include <Atom/RPI.Public/AuxGeom/AuxGeomDraw.h>
#include <Atom/RPI.Public/AuxGeom/AuxGeomFeatureProcessorInterface.h>
#include <Atom/RPI.Public/ViewportContext.h>
#include <Atom/RPI.Public/ViewportContextBus.h>
#include <AzCore/Console/IConsole.h>
#include <AzCore/IO/FileIO.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/std/algorithm.h>
#include <AzFramework/Input/Devices/Keyboard/InputDeviceKeyboard.h>
#include <AzFramework/Input/Devices/Mouse/InputDeviceMouse.h>
#include <CrySystemBus.h>
#include <IFont.h>
#include <ISystem.h>

#include <cmath>

AZ_CVAR(bool, hud_mockup, false, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Fill every HUD element with the approved mockup's values (to compare the HUD against the design)");
AZ_CVAR(bool, hud_show, true, nullptr, AZ::ConsoleFunctorFlags::Null, "Show the in-game HUD");
AZ_CVAR(float, hud_text_scale, 0.7f, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Glyph size against the mockup's CSS font size (32 px glyph slots, no supersampling; supersampled glyphs need twice this)");
AZ_CVAR(int, hud_font_smooth, 0, nullptr, AZ::ConsoleFunctorFlags::Null,
    "HUD glyph smoothing (reloads the fonts): 0 plain antialiasing, 1 blur, 2 supersample (the engine misplaces supersampled glyphs)");
AZ_CVAR(int, hud_font_amount, 1, nullptr, AZ::ConsoleFunctorFlags::Null, "HUD glyph smoothing amount: 0 none, 1 2x, 2 4x");
namespace NoclipProtocol
{
    namespace
    {
        AZStd::string s_pendingKey; // hud_key: a key press to handle on the next frame
        void HudKey(const AZ::ConsoleCommandContainer& arguments)
        {
            if (!arguments.empty())
            {
                s_pendingKey = AZStd::string(arguments[0]);
            }
        }
        AZ_CONSOLEFREEFUNC("hud_key", HudKey, AZ::ConsoleFunctorFlags::Null,
            "Press a HUD key without a keyboard (for testing): hud_key E | Y | N | Esc");
    } // namespace
} // namespace NoclipProtocol

AZ_CVAR(float, hud_term_alpha, 0.5f, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Terminal page: alpha of each layer of its shapes (text over shapes of alpha much above 0.5 gets white boxes)");
AZ_CVAR(int, hud_term_layers, 7, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Terminal page: how many times its shapes are laid down (7 at 0.5 alpha covers 99% of the game behind)");
AZ_CVAR(int, hud_page, -1, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Open a menu page without a keyboard (for testing): 0 none, 1 pause, 2 settings, 3 terminal");

namespace NoclipProtocol
{
    namespace
    {
        constexpr float DesignW = 1280.0f;
        constexpr float DesignH = 720.0f;

        AZ::Color Hex(uint32_t rgb, float alpha = 1.0f)
        {
            return AZ::Color(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, alpha);
        }
        // The mockup's palette.
        const AZ::Color Cream = Hex(0xf3ead0);
        const AZ::Color Bright = Hex(0xfff3c0);
        const AZ::Color Muted = Hex(0xe3d9b4);
        const AZ::Color Gold = Hex(0xf0cf5a);
        const AZ::Color Alarm = Hex(0xff8a78);
        const AZ::Color SanityText = Hex(0xffb3a6);
        const AZ::Color SanityBar = Hex(0xe0624f);
        const AZ::Color Track(10 / 255.0f, 9 / 255.0f, 6 / 255.0f, 0.6f);
        const AZ::Color Panel(10 / 255.0f, 9 / 255.0f, 6 / 255.0f, 0.62f);
        const AZ::Color Line(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, 0.5f);
        const AZ::Color SegmentEdge(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, 0.4f);
        const AZ::Color Shadow(0.0f, 0.0f, 0.0f, 0.9f);

        enum Action { ActResume = 1, ActSettings, ActQuit, ActBack, ActAccept, ActReview, ActFile, ActRayTracing = ActFile + 16, ActSensDown,
                      ActSensUp, ActVolDown, ActVolUp };

        // ---- the intro's briefing terminal (Terminal.dc.html) ----
        // The mockup's palette for the terminal.
        const AZ::Color TermBg = Hex(0x0b0a07);
        const AZ::Color TermText = Hex(0xe8dfc0);
        const AZ::Color TermHover = Hex(0xfff4c4);
        const AZ::Color TermGold = Hex(0xd8c35a);
        const AZ::Color TermMuted = Hex(0xa59b78);
        const AZ::Color TermLocked = Hex(0x7d7560);
        const AZ::Color TermSelected = Hex(0xfff1b0);
        const AZ::Color TermTitle = Hex(0xf1e6b8);
        const AZ::Color TermThreat = Hex(0xff9b8a);
        const AZ::Color TermThreatValue = Hex(0xffb3a6);

        // A run of document text: 0 plain, 1 highlighted, 2 redacted (a solid bar, one character per byte of text).
        // A run that starts without a space joins the word before it.
        struct Run
        {
            const char* m_text;
            int m_kind;
        };
        struct TermFile
        {
            const char* m_name;     // in the side list
            int m_section;          // 0 BRIEFINGS, 1 PERSONNEL, 2 CONTAINMENT
            bool m_locked;
            const char* m_label;    // "DOCUMENT 4-~ · EYES ONLY" (~ is drawn as an omega)
            const char* m_title;
            AZStd::vector<AZStd::vector<Run>> m_paragraphs;
        };
        const AZStd::vector<TermFile>& TermFiles()
        {
            static const AZStd::vector<TermFile> files = {
                { "Operation Noclip Protocol", 0, false, "DOCUMENT 4-~ \xc2\xb7 EYES ONLY", "OPERATION NOCLIP PROTOCOL",
                  { { { "At 03:12 local time, a section of Sublevel 4 corridor ceased to exist. Personnel present at the time were not "
                        "recovered. Audio from their radios continued for", 0 }, { " xxxxxx", 2 },
                      { " hours and contained a persistent fluorescent hum.", 0 } },
                    { { "Survey drones confirm the breach opens onto an endless, empty office space designated", 0 }, { " Level 0", 1 },
                      { ". Lighting is unreliable. Layout does not remain consistent when unobserved.", 0 } },
                    { { "Your squad will enter through Breach 4-A, secure the first stable room and deploy a Foundation beacon. Do not "
                        "follow voices. Do not", 0 }, { " xxxxxxxxxxxxxx", 2 }, { ".", 0 } } } },
                { "Incident log, Sector 4", 0, false, "DOCUMENT 4-~-1 \xc2\xb7 INCIDENT LOG", "INCIDENT LOG, SECTOR 4",
                  { { { "03:11 Corridor 4-C lighting drops to emergency level. A low hum is reported on every radio channel.", 0 } },
                    { { "03:12 Twenty metres of corridor, its walls and the three people in it are replaced by an opening with yellow "
                        "light behind it.", 0 } },
                    { { "03:40 The opening is framed and designated Breach 4-A. The hum has not stopped.", 0 } } } },
                { "Recovered footage", 0, false, "DOCUMENT 4-~-2 \xc2\xb7 DRONE 2", "RECOVERED FOOTAGE",
                  { { { "Drone 2, the last 41 seconds before its signal was lost: yellow wallpaper, damp carpet, a ceiling light that "
                        "will not stop flickering.", 0 } },
                    { { "At 0:33 something moves at the edge of the frame. Enhancement shows", 0 }, { " nothing", 1 },
                      { " there.", 0 } } } },
                { "MTF squad roster", 1, false, "PERSONNEL \xc2\xb7 MTF EPSILON-11", "MTF SQUAD ROSTER",
                  { { { "Detachment 4: four operators and you. Callsigns are assigned when you deploy.", 0 } },
                    { { "Stay inside radio range. Past about forty metres the walls of Level 0 eat the signal.", 0 } } } },
                { "Missing personnel (3)", 1, false, "PERSONNEL \xc2\xb7 MISSING", "MISSING PERSONNEL (3)",
                  { { { "Dr.", 0 }, { " xxxxxxxx", 2 }, { ", Researcher M. Okafor, Agent", 0 }, { " xxxxxx", 2 },
                      { ". Last seen in Corridor 4-C at 03:11.", 0 } },
                    { { "Their voices have been heard on Level 0. If you hear one, do not answer it and do not follow it.", 0 } } } },
                { "Level 0 survey", 2, false, "CONTAINMENT \xc2\xb7 SURVEY", "LEVEL 0 SURVEY",
                  { { { "Rooms repeat with small differences. No exits have been found.", 0 } },
                    { { "Some walls show tears: thin dark gaps that drones fall through and do not return from. Mark them. Do not go "
                        "through one alone.", 0 } } } },
                { "Level 1 survey \xc2\xb7 LOCKED", 2, true, "CONTAINMENT \xc2\xb7 SURVEY", "ACCESS DENIED",
                  { { { "This file needs Clearance Level 4.", 0 } } } },
            };
            return files;
        }

        AZStd::string RegString(const char* path, const char* fallback = "")
        {
            AZStd::string value;
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Get(value, path);
            }
            return value.empty() ? AZStd::string(fallback) : value;
        }

        double RegNumber(const char* path, double fallback)
        {
            double value = fallback;
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Get(value, path);
            }
            return value;
        }

        // Real advance widths (em), from the font files: the engine spaces every glyph a full em apart, so the HUD
        // lays text out itself. IBM Plex Mono is 0.6 em for every character; Oswald Bold per character (ASCII 32..126).
        static const float OswaldAdvance[95] = {0.256f, 0.257f, 0.36f, 0.534f, 0.485f, 1.006f, 0.57f, 0.17f, 0.338f, 0.328f, 0.423f, 0.449f, 0.238f, 0.324f, 0.244f, 0.422f, 0.55f, 0.385f, 0.514f, 0.514f, 0.524f, 0.509f, 0.538f, 0.439f, 0.521f, 0.538f, 0.278f, 0.285f, 0.398f, 0.442f, 0.398f, 0.483f, 0.95f, 0.551f, 0.588f, 0.563f, 0.586f, 0.447f, 0.434f, 0.582f, 0.61f, 0.301f, 0.349f, 0.567f, 0.443f, 0.704f, 0.561f, 0.586f, 0.571f, 0.586f, 0.6f, 0.514f, 0.445f, 0.588f, 0.526f, 0.697f, 0.515f, 0.493f, 0.433f, 0.332f, 0.422f, 0.324f, 0.476f, 0.368f, 0.307f, 0.46f, 0.506f, 0.468f, 0.503f, 0.47f, 0.32f, 0.502f, 0.509f, 0.265f, 0.269f, 0.51f, 0.274f, 0.753f, 0.507f, 0.483f, 0.505f, 0.504f, 0.383f, 0.424f, 0.351f, 0.504f, 0.421f, 0.597f, 0.442f, 0.448f, 0.392f, 0.374f, 0.262f, 0.379f, 0.481f};

        float AdvanceEm(bool oswald, const AZStd::string& ch)
        {
            if (!oswald)
            {
                return 0.6f;
            }
            const unsigned char c = static_cast<unsigned char>(ch[0]);
            if (ch.size() == 1 && c >= 32 && c <= 126)
            {
                return OswaldAdvance[c - 32];
            }
            return ch == "\xc2\xb7" ? 0.226f : 0.5f;
        }

        // Splits UTF-8 text into characters (for letter spacing).
        AZStd::vector<AZStd::string> Characters(const AZStd::string& text)
        {
            AZStd::vector<AZStd::string> out;
            for (size_t i = 0; i < text.size();)
            {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                const size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : 4;
                out.push_back(text.substr(i, n));
                i += n;
            }
            return out;
        }
    } // namespace

    void NoclipHudComponent::Reflect(AZ::ReflectContext* context)
    {
        if (auto* serialize = azrtti_cast<AZ::SerializeContext*>(context))
        {
            serialize->Class<NoclipHudComponent, AZ::Component>()->Version(1)->Field("Show", &NoclipHudComponent::m_show);
            if (auto* edit = serialize->GetEditContext())
            {
                edit->Class<NoclipHudComponent>("Noclip HUD", "The in-game HUD from the approved mockup, and the pause menu")
                    ->ClassElement(AZ::Edit::ClassElements::EditorData, "")
                    ->Attribute(AZ::Edit::Attributes::Category, "Noclip Protocol")
                    ->Attribute(AZ::Edit::Attributes::AppearsInAddComponentMenu, AZ_CRC_CE("Game"))
                    ->DataElement(AZ::Edit::UIHandlers::Default, &NoclipHudComponent::m_show, "Show", "Draw the HUD");
            }
        }
    }

    void NoclipHudComponent::Activate()
    {
        m_time = 0.0f;
        m_page = Page::None;
        LoadSettings();
        ApplySettings();
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Set("/NoclipProtocol/Paused", false);
        }
        AZ::TickBus::Handler::BusConnect();
        AzFramework::InputChannelEventListener::Connect();
    }

    void NoclipHudComponent::Deactivate()
    {
        AzFramework::InputChannelEventListener::Disconnect();
        AZ::TickBus::Handler::BusDisconnect();
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Set("/NoclipProtocol/Paused", false);
        }
    }

    void NoclipHudComponent::LoadFonts()
    {
        m_fontsLoaded = true;
        ISystem* system = nullptr;
        CrySystemRequestBus::BroadcastResult(system, &CrySystemRequests::GetCrySystem);
        SSystemGlobalEnvironment* env = system ? system->GetGlobalEnvironment() : nullptr;
        auto* query = AZ::Interface<AzFramework::FontQueryInterface>::Get();
        if (!env || !env->pCryFont || !query)
        {
            AZ_Warning("NoclipHud", false, "Fonts: no font system; using the default font");
            return;
        }
        struct Spec { const char* name; const char* file; unsigned int texture; unsigned int slots; };
        const Spec specs[5] = {
            { "noclip-plexmono", "@products@/fonts/noclip/ibmplexmono-regular.ttf", 1024, 32 },        // 32 px glyph slots
            { "noclip-plexmono-medium", "@products@/fonts/noclip/ibmplexmono-medium.ttf", 1024, 32 },
            { "noclip-plexmono-semibold", "@products@/fonts/noclip/ibmplexmono-semibold.ttf", 1024, 32 },
            { "noclip-oswald-bold", "@products@/fonts/noclip/oswald-bold.ttf", 1024, 32 },
            { "noclip-oswald-bold-large", "@products@/fonts/noclip/oswald-bold.ttf", 1024, 32 },      // same slots: the ammo count
        };
        const int smooth = AZStd::clamp(static_cast<int>(hud_font_smooth), 0, 2);
        const int amount = AZStd::clamp(static_cast<int>(hud_font_amount), 0, 2);
        unsigned int flags = 0;
        if (smooth == 1)
        {
            flags |= TTFFLAG_SMOOTH_BLUR;
        }
        else if (smooth == 2)
        {
            flags |= TTFFLAG_SMOOTH_SUPERSAMPLE;
        }
        if (smooth != 0 && amount == 1)
        {
            flags |= TTFLAG_SMOOTH_AMOUNT_2X;
        }
        else if (smooth != 0 && amount == 2)
        {
            flags |= TTFLAG_SMOOTH_AMOUNT_4X;
        }
        for (int i = 0; i < 5; ++i)
        {
            const AZStd::string name = AZStd::string::format("%s-s%d", specs[i].name, m_fontKey);
            IFFont* font = env->pCryFont->GetFont(name.c_str());
            if (!font)
            {
                font = env->pCryFont->NewFont(name.c_str());
                // No supersampling: the engine's supersampled glyphs keep their offsets and advance at the 2x render size
                // (AtomFont GlyphCache::CacheGlyph halves only width and height), so every letter sat at the wrong height
                // ("-" drew like "_", tall letters rode high) and spacing came out doubled. Plain FreeType antialiasing.
                if (font && !font->Load(specs[i].file, specs[i].texture, specs[i].texture, specs[i].slots, specs[i].slots,
                        flags, IFFontConstants::defaultSizeRatio))
                {
                    AZ_Warning("NoclipHud", false, "Fonts: couldn't load %s; using the default font", specs[i].file);
                    continue;
                }
            }
            m_fonts[i] = query->GetFontDrawInterface(query->GetFontId(name.c_str()));
            AZ_Printf("NoclipHud", "Font %s %s", specs[i].name, m_fonts[i] ? "loaded" : "missing");
        }
    }

    AzFramework::FontDrawInterface* NoclipHudComponent::Font(Face face)
    {
        const int key = static_cast<int>(hud_font_smooth) * 10 + static_cast<int>(hud_font_amount);
        if (key != m_fontKey)
        {
            m_fontKey = key; // smoothing changed: load the faces again under names that include it
            m_fontsLoaded = false;
            for (auto*& f : m_fonts)
            {
                f = nullptr;
            }
        }
        if (!m_fontsLoaded)
        {
            LoadFonts();
        }
        AzFramework::FontDrawInterface* font = m_fonts[static_cast<int>(face)];
        if (!font)
        {
            auto* query = AZ::Interface<AzFramework::FontQueryInterface>::Get();
            font = query ? query->GetDefaultFontDrawInterface() : nullptr;
        }
        return font;
    }

    // ------------------------------------------------------------------ settings

    void NoclipHudComponent::LoadSettings()
    {
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Get(m_rayTracing, "/NoclipProtocol/Graphics/RayTracing");
            double sensitivity = m_mouseSensitivity;
            double volume = m_volumeDb;
            registry->Get(sensitivity, "/NoclipProtocol/Player/MouseSensitivity");
            registry->Get(volume, "/Hearth/Audio/Buses/Master");
            m_mouseSensitivity = static_cast<float>(sensitivity);
            m_volumeDb = static_cast<float>(volume);
        }
    }

    void NoclipHudComponent::ApplySettings()
    {
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Set("/NoclipProtocol/Graphics/RayTracing", m_rayTracing);
            registry->Set("/NoclipProtocol/Player/MouseSensitivity", static_cast<double>(m_mouseSensitivity));
            registry->Set("/Hearth/Audio/Buses/Master", static_cast<double>(m_volumeDb));
        }
        if (auto* console = AZ::Interface<AZ::IConsole>::Get())
        {
            console->PerformCommand("hearth_audioRefresh");
        }
    }

    void NoclipHudComponent::SaveSettings()
    {
        // The project's user/Registry is merged at every start, so these stick between runs.
        AZ::IO::FileIOBase* io = AZ::IO::FileIOBase::GetInstance();
        if (!io)
        {
            return;
        }
        const AZStd::string text = AZStd::string::format(
            "{\n    \"NoclipProtocol\": {\n        \"Graphics\": { \"RayTracing\": %s },\n"
            "        \"Player\": { \"MouseSensitivity\": %.2f }\n    },\n"
            "    \"Hearth\": { \"Audio\": { \"Buses\": { \"Master\": %.1f } } }\n}\n",
            m_rayTracing ? "true" : "false", m_mouseSensitivity, m_volumeDb);
        AZ::IO::HandleType handle = AZ::IO::InvalidHandle;
        if (io->Open("@projectroot@/user/Registry/noclip_settings.setreg", AZ::IO::OpenMode::ModeWrite | AZ::IO::OpenMode::ModeText, handle))
        {
            io->Write(handle, text.data(), text.size());
            io->Close(handle);
        }
    }

    void NoclipHudComponent::DoAction(int action)
    {
        auto* registry = AZ::SettingsRegistry::Get();
        switch (action)
        {
        case ActResume:
            m_page = Page::None;
            break;
        case ActSettings:
            m_page = Page::Settings;
            break;
        case ActBack:
            m_page = Page::Pause;
            break;
        case ActQuit:
            if (auto* console = AZ::Interface<AZ::IConsole>::Get())
            {
                console->PerformCommand("quit");
            }
            break;
        case ActAccept:
            m_page = Page::None;
            if (registry)
            {
                registry->Set("/NoclipProtocol/Intro/Accepted", true);
                registry->Set("/NoclipProtocol/Hud/toast", AZStd::string("DEPLOYMENT ACCEPTED \xc2\xb7 BREACH 4-A UNSEALED"));
                registry->Set("/NoclipProtocol/Hud/toast.until", static_cast<double>(m_time) + 4.0);
            }
            break;
        case ActReview:
            do
            {
                m_terminalFile = (m_terminalFile + 1) % static_cast<int>(TermFiles().size());
            } while (TermFiles()[m_terminalFile].m_locked && m_terminalFile != 0);
            break;
        case ActRayTracing: m_rayTracing = !m_rayTracing; break;
        case ActSensDown: m_mouseSensitivity = AZStd::max(0.2f, m_mouseSensitivity - 0.1f); break;
        case ActSensUp: m_mouseSensitivity = AZStd::min(3.0f, m_mouseSensitivity + 0.1f); break;
        case ActVolDown: m_volumeDb = AZStd::max(-40.0f, m_volumeDb - 3.0f); break;
        case ActVolUp: m_volumeDb = AZStd::min(6.0f, m_volumeDb + 3.0f); break;
        default:
            if (action >= ActFile && action < ActFile + static_cast<int>(TermFiles().size()))
            {
                m_terminalFile = action - ActFile;
            }
            break;
        }
        if (action >= ActRayTracing)
        {
            ApplySettings();
            SaveSettings();
        }
        if (registry)
        {
            registry->Set("/NoclipProtocol/Paused", m_page != Page::None);
        }
    }

    bool NoclipHudComponent::OnInputChannelEventFiltered(const AzFramework::InputChannel& inputChannel)
    {
        using namespace AzFramework;
        const InputChannelId& id = inputChannel.GetInputChannelId();
        if (id == InputDeviceMouse::SystemCursorPosition)
        {
            if (const auto* position = inputChannel.GetCustomData<InputChannel::PositionData2D>())
            {
                m_cursor = position->m_normalizedPosition; // converted to design pixels when drawing
            }
            return false;
        }
        if (!inputChannel.IsStateBegan())
        {
            return false;
        }
        return HandleKey(id);
    }

    bool NoclipHudComponent::HandleKey(const AzFramework::InputChannelId& id)
    {
        using namespace AzFramework;
        // The briefing terminal: E at the terminal opens it; Y accepts, N goes through the files, Esc / P closes it.
        if (m_page == Page::None && id == InputDeviceKeyboard::Key::AlphanumericE &&
            RegString("/NoclipProtocol/Hud/interact.target", "") == "terminal")
        {
            m_page = Page::Terminal;
            m_terminalFile = 0;
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Set("/NoclipProtocol/Paused", true);
            }
            return true;
        }
        if (m_page == Page::Terminal)
        {
            if (id == InputDeviceKeyboard::Key::AlphanumericY)
            {
                DoAction(ActAccept);
                return true;
            }
            if (id == InputDeviceKeyboard::Key::AlphanumericN)
            {
                DoAction(ActReview);
                return true;
            }
            if (id == InputDeviceKeyboard::Key::Escape || id == InputDeviceKeyboard::Key::AlphanumericP)
            {
                DoAction(ActResume);
                return true;
            }
        }
        // Esc in the game; P too, because Esc leaves Play in the editor.
        if (id == InputDeviceKeyboard::Key::Escape || id == InputDeviceKeyboard::Key::AlphanumericP)
        {
            m_page = m_page == Page::None ? Page::Pause : (m_page == Page::Settings ? Page::Pause : Page::None);
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Set("/NoclipProtocol/Paused", m_page != Page::None);
            }
            return m_page != Page::None || id == InputDeviceKeyboard::Key::AlphanumericP;
        }
        if (m_page != Page::None && id == InputDeviceMouse::Button::Left)
        {
            m_click = true;
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------------ drawing

    void NoclipHudComponent::OnTick(float deltaTime, [[maybe_unused]] AZ::ScriptTimePoint time)
    {
        m_time += deltaTime;
        if (!s_pendingKey.empty())
        {
            using namespace AzFramework;
            const AZStd::string key = s_pendingKey;
            s_pendingKey.clear();
            if (key == "E" || key == "e") { HandleKey(InputDeviceKeyboard::Key::AlphanumericE); }
            else if (key == "Y" || key == "y") { HandleKey(InputDeviceKeyboard::Key::AlphanumericY); }
            else if (key == "N" || key == "n") { HandleKey(InputDeviceKeyboard::Key::AlphanumericN); }
            else if (key == "Esc" || key == "esc") { HandleKey(InputDeviceKeyboard::Key::Escape); }
        }
        auto* registry = AZ::SettingsRegistry::Get();
        if (static_cast<int>(hud_page) >= 0)
        {
            const int page = static_cast<int>(hud_page);
            m_page = page == 1 ? Page::Pause : (page == 2 ? Page::Settings : (page == 3 ? Page::Terminal : Page::None));
            hud_page = -1;
            if (registry)
            {
                registry->Set("/NoclipProtocol/Paused", m_page != Page::None);
            }
        }
        if (registry)
        {
            registry->Set("/NoclipProtocol/Hud/time", static_cast<double>(m_time));
        }
        if (!m_show || !static_cast<bool>(hud_show))
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
        if (!draw)
        {
            return;
        }
        m_viewportId = viewport->GetId();
        const AzFramework::WindowSize size = viewport->GetViewportSize();
        const float W = static_cast<float>(size.m_width);
        const float H = static_cast<float>(size.m_height);
        if (W < 16.0f || H < 16.0f)
        {
            return;
        }
        // The mockup is 1280 x 720; scale to the window's height and keep each block anchored to its own edge.
        const float s = H / DesignH;
        const int32_t overlay = draw->GetOrAdd2DViewProjOverride();
        const bool mock = static_cast<bool>(hud_mockup);

        // Positions: L = from the left edge, R = from the right edge, C = from the centre (all in design pixels).
        auto L = [&](float x) { return x * s; };
        auto R = [&](float x) { return W - (DesignW - x) * s; };
        auto C = [&](float x) { return W * 0.5f + (x - DesignW * 0.5f) * s; };
        auto Y = [&](float y) { return y * s; };

        auto triangles = [&](const AZStd::vector<AZ::Vector3>& points, const AZStd::vector<AZ::Color>& colours)
        {
            if (points.empty())
            {
                return;
            }
            AZ::RPI::AuxGeomDraw::AuxGeomDynamicDrawArguments args;
            args.m_verts = points.data();
            args.m_vertCount = static_cast<uint32_t>(points.size());
            args.m_colors = colours.data();
            args.m_colorCount = static_cast<uint32_t>(colours.size());
            args.m_opacityType = AZ::RPI::AuxGeomDraw::OpacityType::Translucent;
            args.m_depthTest = AZ::RPI::AuxGeomDraw::DepthTest::Off;
            args.m_depthWrite = AZ::RPI::AuxGeomDraw::DepthWrite::Off;
            args.m_viewProjectionOverrideIndex = overlay;
            draw->DrawTriangles(args);
        };
        auto P = [&](float px, float py) { return AZ::Vector3(px / W, py / H, 0.0f); };
        // A filled rectangle in screen pixels.
        auto rect = [&](float x, float y, float w, float h, const AZ::Color& c)
        {
            triangles({ P(x, y), P(x + w, y), P(x + w, y + h), P(x, y), P(x + w, y + h), P(x, y + h) }, { c });
        };
        auto outline = [&](float x, float y, float w, float h, float t, const AZ::Color& c)
        {
            rect(x, y, w, t, c);
            rect(x, y + h - t, w, t, c);
            rect(x, y, t, h, c);
            rect(x + w - t, y, t, h, c);
        };
        // A line as a thin quad (for icon outlines).
        auto line = [&](float x0, float y0, float x1, float y1, float t, const AZ::Color& c)
        {
            const float dx = x1 - x0;
            const float dy = y1 - y0;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len <= 0.0f)
            {
                return;
            }
            const float nx = -dy / len * t * 0.5f;
            const float ny = dx / len * t * 0.5f;
            triangles({ P(x0 + nx, y0 + ny), P(x1 + nx, y1 + ny), P(x1 - nx, y1 - ny),
                        P(x0 + nx, y0 + ny), P(x1 - nx, y1 - ny), P(x0 - nx, y0 - ny) }, { c });
        };
        auto diamond = [&](float cx, float cy, float r, const AZ::Color& c, bool filled, float t)
        {
            if (filled)
            {
                triangles({ P(cx, cy - r), P(cx + r, cy), P(cx, cy + r), P(cx, cy - r), P(cx, cy + r), P(cx - r, cy) }, { c });
            }
            else
            {
                line(cx, cy - r, cx + r, cy, t, c);
                line(cx + r, cy, cx, cy + r, t, c);
                line(cx, cy + r, cx - r, cy, t, c);
                line(cx - r, cy, cx, cy - r, t, c);
            }
        };

        // Text. size = CSS font size in design px; spacing = CSS letter-spacing; x/y = top-left (screen px);
        // align 0 left, 1 centre, 2 right (x is then the centre or right edge). Returns the width drawn.
        auto pick = [&](Face face, float size) { return face == Face::Oswald && size >= 30.0f ? Face::OswaldLarge : face; };
        auto isOswald = [](Face face) { return face == Face::Oswald || face == Face::OswaldLarge; };
        // Width in screen px of text at a CSS font size with CSS letter-spacing.
        auto measure = [&](Face face, const AZStd::string& text, float size, float spacing) -> float
        {
            float width = 0.0f;
            int count = 0;
            for (const AZStd::string& ch : Characters(text))
            {
                width += AdvanceEm(isOswald(face), ch) * size * s;
                ++count;
            }
            return width + AZStd::max(0, count - 1) * spacing * s;
        };
        // Draws text; x/y = top-left of the CSS line box (screen px); align 0 left, 1 centre, 2 right. Returns the width.
        auto text = [&](Face face, const AZStd::string& value, float x, float y, float size, const AZ::Color& colour,
                        int align = 0, float spacing = 0.0f, bool shadow = true) -> float
        {
            const float width = measure(face, value, size, spacing);
            const bool oswald = isOswald(face);
            face = pick(face, size);
            AzFramework::FontDrawInterface* font = Font(face);
            if (!font || value.empty())
            {
                return 0.0f;
            }
            const float left = align == 1 ? x - width * 0.5f : (align == 2 ? x - width : x);
            AzFramework::TextDrawParameters params;
            params.m_drawViewportId = viewport->GetId();
            // With 32 px slots and no supersampling the engine draws glyphs at about 1.4x the asked size, so ask for 0.7x.
            params.m_textSizeFactor = size * s * static_cast<float>(hud_text_scale) * (static_cast<int>(hud_font_smooth) == 2 ? 2.0f : 1.0f);
            params.m_hAlign = AzFramework::TextHorizontalAlignment::Left;
            params.m_vAlign = AzFramework::TextVerticalAlignment::Top;
            const float top = y + size * 0.1f * s; // CSS centres the glyphs in a 1.2 line box
            auto run = [&](float px, float py, const AZ::Color& c)
            {
                params.m_color = c;
                float cursor = px;
                for (const AZStd::string& ch : Characters(value))
                {
                    if (ch != " ")
                    {
                        params.m_position = AZ::Vector3(cursor, py, 1.0f);
                        font->DrawScreenAlignedText2d(params, ch);
                    }
                    cursor += AdvanceEm(oswald, ch) * size * s + spacing * s;
                }
            };
            if (shadow)
            {
                run(left, top + 1.0f * s, Shadow); // text-shadow: 0 1px 2px rgba(0,0,0,0.9)
            }
            run(left, top, colour);
            return width;
        };
        // Wraps text to a width (screen px); returns the lines.
        auto wrap = [&](Face face, const AZStd::string& value, float size, float width) -> AZStd::vector<AZStd::string>
        {
            AZStd::vector<AZStd::string> lines;
            AZStd::string current;
            size_t start = 0;
            while (start <= value.size())
            {
                size_t end = value.find(' ', start);
                if (end == AZStd::string::npos)
                {
                    end = value.size();
                }
                const AZStd::string word = value.substr(start, end - start);
                const AZStd::string attempt = current.empty() ? word : current + " " + word;
                if (!current.empty() && measure(face, attempt, size, 0.0f) > width)
                {
                    lines.push_back(current);
                    current = word;
                }
                else
                {
                    current = attempt;
                }
                start = end + 1;
            }
            if (!current.empty())
            {
                lines.push_back(current);
            }
            return lines;
        };

        // The HUD itself; the terminal covers the whole screen (and text always draws over shapes), so not under it.
        if (m_page != Page::Terminal)
        {
            // ---- vignette: radial-gradient(ellipse at center, transparent 55%, rgba(8,7,4,0.55) 100%) ----
            {
                const AZ::Color edge(8 / 255.0f, 7 / 255.0f, 4 / 255.0f, 0.55f);
                const AZ::Color clear(8 / 255.0f, 7 / 255.0f, 4 / 255.0f, 0.0f);
                const float cx = W * 0.5f;
                const float cy = H * 0.5f;
                // "farthest-corner" ellipse: 100% reaches the corners.
                const float rx = cx * 1.41421356f;
                const float ry = cy * 1.41421356f;
                const int segments = 64;
                const int rings = 6;
                AZStd::vector<AZ::Vector3> points;
                AZStd::vector<AZ::Color> colours;
                for (int ring = 0; ring < rings; ++ring)
                {
                    const float t0 = 0.55f + 0.45f * ring / rings;
                    const float t1 = 0.55f + 0.45f * (ring + 1) / rings;
                    const AZ::Color c0 = clear.Lerp(edge, static_cast<float>(ring) / rings);
                    const AZ::Color c1 = clear.Lerp(edge, static_cast<float>(ring + 1) / rings);
                    for (int i = 0; i < segments; ++i)
                    {
                        const float a0 = AZ::Constants::TwoPi * i / segments;
                        const float a1 = AZ::Constants::TwoPi * (i + 1) / segments;
                        auto at = [&](float t, float a) { return P(cx + std::cos(a) * rx * t, cy + std::sin(a) * ry * t); };
                        points.insert(points.end(), { at(t0, a0), at(t1, a0), at(t1, a1), at(t0, a0), at(t1, a1), at(t0, a1) });
                        colours.insert(colours.end(), { c0, c1, c1, c0, c1, c0 });
                    }
                }
                triangles(points, colours);
            }

            // ---- top left: level badge and forward base ----
            {
                const AZStd::string title = mock ? "LEVEL 0" : RegString("/NoclipProtocol/Hud/level.title", "LEVEL 0");
                const AZStd::string subtitle = mock ? "THE LOBBY · UNSTABLE" : RegString("/NoclipProtocol/Hud/level.subtitle", "THE LOBBY · UNSTABLE");
                const double baseDistance = mock ? 34.0 : RegNumber("/NoclipProtocol/Hud/base.distance", -1.0);
                text(Face::Oswald, title, L(40), Y(32), 22.0f, Bright, 0, 3.0f);
                text(Face::Mono, subtitle, L(40), Y(62), 12.0f, Muted, 0, 2.0f);
                if (baseDistance >= 0.0)
                {
                    diamond(L(47), Y(97), 6.0f * s, Bright, false, 1.5f * s);
                    text(Face::Mono, AZStd::string::format("FORWARD BASE · %.0f m", baseDistance), L(62), Y(89), 13.0f, Bright);
                }
            }

            // ---- top centre: compass (W .. N .. E across 500 px, the base marked with a gold diamond) ----
            {
                const float left = C(390);
                const float width = 500.0f * s;
                rect(left, Y(61), width, AZStd::max(1.0f, 1.0f * s), Line); // border-bottom
                float yaw = 0.0f; // degrees clockwise from north (+Y)
                AZ::Vector3 eye = AZ::Vector3::CreateZero();
                const AZ::Transform camera = viewport->GetCameraTransform();
                const AZ::Vector3 forward = camera.GetBasisY();
                eye = camera.GetTranslation();
                yaw = AZ::RadToDeg(std::atan2(forward.GetX(), forward.GetY()));
                if (mock)
                {
                    yaw = 8.0f;
                }
                const float inner = width - 32.0f * s; // padding 0 16px
                const float span = 180.0f;             // the strip shows W (-90) to E (+90) around the heading
                auto toX = [&](float bearing) -> float
                {
                    float d = std::fmod(bearing - yaw + 540.0f, 360.0f) - 180.0f;
                    return d < -span * 0.5f || d > span * 0.5f ? -1.0f : left + 16.0f * s + (d / span + 0.5f) * inner;
                };
                static const char* const names[] = { "N", "·", "·", "E", "·", "·", "S", "·", "·", "W", "·", "·" };
                for (int i = 0; i < 12; ++i)
                {
                    const float x = toX(i * 30.0f);
                    if (x < 0.0f)
                    {
                        continue;
                    }
                    const bool north = i == 0;
                    text(north ? Face::MonoSemiBold : Face::Mono, names[i], x, Y(37), 12.0f, north ? Bright : Muted, 1);
                }
                double bx = 0.0;
                double by = 0.0;
                if (mock || (RegNumber("/NoclipProtocol/Hud/base.distance", -1.0) >= 0.0))
                {
                    bx = RegNumber("/NoclipProtocol/Hud/base.x", 0.0);
                    by = RegNumber("/NoclipProtocol/Hud/base.y", 0.0);
                    const float bearing = mock ? 50.0f
                        : AZ::RadToDeg(std::atan2(static_cast<float>(bx) - eye.GetX(), static_cast<float>(by) - eye.GetY()));
                    const float x = toX(bearing);
                    if (x >= 0.0f)
                    {
                        diamond(x, Y(45), 6.0f * s, Gold, true, 0.0f);
                    }
                }
            }

            // ---- top right: objective panel ----
            {
                const AZStd::string objective = mock ? "Place the Foundation beacon in the spawn room"
                    : RegString("/NoclipProtocol/Hud/game.objective", "");
                const AZStd::string progress = mock ? "Supplies recovered 2 / 5" : RegString("/NoclipProtocol/Hud/game.progress", "");
                if (!objective.empty())
                {
                    const float x = R(940);
                    const float w = 300.0f * s;
                    const AZStd::vector<AZStd::string> lines = wrap(Face::Mono, objective, 14.0f, (300.0f - 3.0f - 28.0f) * s);
                    const float lineH = 21.0f; // 14px * 1.5
                    const float h = 12.0f + 13.2f + 6.0f + lineH * lines.size() + (progress.empty() ? 0.0f : 8.0f + 14.4f) + 12.0f;
                    rect(x, Y(32), w, h * s, Panel);
                    rect(x, Y(32), 3.0f * s, h * s, Gold);
                    const float tx = x + 17.0f * s;
                    text(Face::Mono, "OBJECTIVE", tx, Y(44), 11.0f, Muted, 0, 2.0f);
                    float y = 44.0f + 13.2f + 6.0f;
                    for (const AZStd::string& l : lines)
                    {
                        text(Face::Mono, l, tx, Y(y + 3.5f), 14.0f, Bright);
                        y += lineH;
                    }
                    if (!progress.empty())
                    {
                        text(Face::Mono, progress, tx, Y(y + 8.0f), 12.0f, Muted);
                    }
                }
            }

            // ---- right: squad ("Name|12 m" lines; NO SIGNAL rows in red) ----
            {
                AZStd::string squad = mock ? "Agent Hollis|12 m\nDr. ████|41 m\nAgent Reyes|NO SIGNAL" : RegString("/NoclipProtocol/Hud/squad", "");
                float y = 176.0f;
                size_t start = 0;
                while (!squad.empty() && start < squad.size())
                {
                    size_t end = squad.find('\n', start);
                    if (end == AZStd::string::npos)
                    {
                        end = squad.size();
                    }
                    const AZStd::string row = squad.substr(start, end - start);
                    const size_t bar = row.find('|');
                    const AZStd::string name = row.substr(0, bar);
                    const AZStd::string value = bar == AZStd::string::npos ? AZStd::string() : row.substr(bar + 1);
                    const bool lost = value == "NO SIGNAL";
                    text(Face::Mono, name, R(1012), Y(y), 12.0f, lost ? Alarm : Cream);
                    text(Face::Mono, value, R(1240), Y(y), 12.0f, lost ? Alarm : Muted, 2);
                    y += 14.4f + 8.0f;
                    start = end + 1;
                }
            }

            // ---- centre: dot and interaction prompt ----
            rect(C(638), Y(358), 4.0f * s, 4.0f * s, Bright);
            {
                const AZStd::string verb = mock ? "Pick up" : RegString("/NoclipProtocol/Hud/interact.verb", "");
                const AZStd::string item = mock ? "Almond Water" : RegString("/NoclipProtocol/Hud/interact.item", "");
                const AZStd::string key = mock ? "E" : RegString("/NoclipProtocol/Hud/interact.key", "E");
                if (!verb.empty())
                {
                    const float verbW = measure(Face::Mono, verb, 13.0f, 0.0f);
                    const float rowW = 24.0f * s + 8.0f * s + verbW;
                    const float x0 = C(640) - rowW * 0.5f;
                    outline(x0, Y(400), 24.0f * s, 24.0f * s, 1.5f * s, Bright);
                    text(Face::MonoSemiBold, key, x0 + 12.0f * s, Y(405), 13.0f, Cream, 1);
                    text(Face::Mono, verb, x0 + 32.0f * s, Y(405), 13.0f, Cream);
                    if (!item.empty())
                    {
                        text(Face::Mono, item, C(640), Y(430), 12.0f, Muted, 1);
                    }
                }
            }

            // ---- bottom left: HEALTH / STAMINA / SANITY ----
            {
                struct Bar { const char* label; const char* bind; float mockValue; AZ::Color labelColour; AZ::Color fill; };
                const Bar bars[3] = {
                    { "HEALTH", "/NoclipProtocol/Hud/player.health", 0.82f, Cream, Cream },
                    { "STAMINA", "/NoclipProtocol/Hud/player.stamina", 0.45f, Cream, Gold },
                    { "SANITY", "/NoclipProtocol/Hud/player.sanity", 0.61f, SanityText, SanityBar },
                };
                float y = DesignH - 36.0f - (26.4f * 3 + 24.0f);
                for (const Bar& bar : bars)
                {
                    const float value = mock ? bar.mockValue : static_cast<float>(AZ::GetClamp(RegNumber(bar.bind, 1.0), 0.0, 1.0));
                    text(Face::Mono, bar.label, L(40), Y(y), 11.0f, bar.labelColour, 0, 2.0f);
                    text(Face::Mono, AZStd::string::format("%d", static_cast<int>(std::round(value * 100.0f))), L(340), Y(y), 11.0f,
                        bar.labelColour, 2, 2.0f);
                    rect(L(40), Y(y + 13.2f + 5.0f), 300.0f * s, 6.0f * s, Track);
                    rect(L(40), Y(y + 13.2f + 5.0f), 300.0f * s * value, 6.0f * s, bar.fill);
                    y += 26.4f + 12.0f;
                }
            }

            // ---- bottom right: flashlight and weapon ----
            {
                const AZStd::string weapon = mock ? "MTF CARBINE" : RegString("/NoclipProtocol/Hud/weapon.name", "");
                float right = 1240.0f; // design x of the block's right edge
                if (!weapon.empty())
                {
                    const AZStd::string ammo = mock ? "24" : RegString("/NoclipProtocol/Hud/weapon.ammo", "0");
                    const AZStd::string reserve = mock ? " / 90" : AZStd::string(" / ") + RegString("/NoclipProtocol/Hud/weapon.reserve", "0");
                    const float reserveW = measure(Face::Oswald, reserve, 22.0f, 0.0f);
                    const float ammoW = measure(Face::Oswald, ammo, 44.0f, 0.0f);
                    const float blockW = AZStd::max(reserveW + ammoW, measure(Face::Mono, weapon, 11.0f, 2.0f)) / s;
                    const float bottom = DesignH - 36.0f;
                    text(Face::Mono, weapon, R(right), Y(bottom - 44.0f - 13.2f), 11.0f, Muted, 2, 2.0f);
                    text(Face::Oswald, reserve, R(right), Y(bottom - 22.0f * 1.15f), 22.0f, Muted, 2);
                    text(Face::Oswald, ammo, R(right) - reserveW, Y(bottom - 44.0f * 1.05f), 44.0f, Bright, 2);
                    right -= blockW + 28.0f;
                }
                const double fraction = mock ? 0.6 : AZ::GetClamp(RegNumber("/NoclipProtocol/Hud/player.flashlight", 1.0), 0.0, 1.0);
                const double seconds = mock ? 108.0 : RegNumber("/NoclipProtocol/Hud/player.flashlight.seconds", 180.0 * fraction);
                const float bottom = DesignH - 36.0f;
                // "1:48 left"
                const int secs = static_cast<int>(std::round(seconds));
                text(Face::Mono, AZStd::string::format("%d:%02d left", secs / 60, secs % 60), R(right), Y(bottom - 14.4f), 11.0f, Muted, 2);
                // five segments, 16 x 10, gap 3
                const int lit = static_cast<int>(std::ceil(fraction * 5.0 - 0.001));
                for (int i = 0; i < 5; ++i)
                {
                    const float x = R(right - (5 - i) * 16.0f - (4 - i) * 3.0f);
                    const float y = Y(bottom - 14.4f - 6.0f - 10.0f);
                    if (i < lit)
                    {
                        rect(x, y, 16.0f * s, 10.0f * s, Gold);
                    }
                    else
                    {
                        rect(x, y, 16.0f * s, 10.0f * s, Track);
                        outline(x, y, 16.0f * s, 10.0f * s, AZStd::max(1.0f, s), SegmentEdge);
                    }
                }
                // icon + "FLASHLIGHT [F]"
                const float labelY = bottom - 14.4f - 6.0f - 10.0f - 6.0f - 16.0f;
                const float labelW = text(Face::Mono, "FLASHLIGHT [F]", R(right), Y(labelY + 1.5f), 11.0f, Cream, 2, 2.0f);
                const float ix = R(right) - labelW - 8.0f * s - 16.0f * s;
                const float iy = Y(labelY);
                const float u = s; // icon path is in a 16 x 16 box: M2 6 H9 L14 3 V13 L9 10 H2 Z
                const float px[6] = { 2, 9, 14, 14, 9, 2 };
                const float py[6] = { 6, 6, 3, 13, 10, 10 };
                for (int i = 0; i < 6; ++i)
                {
                    const int j = (i + 1) % 6;
                    line(ix + px[i] * u, iy + py[i] * u, ix + px[j] * u, iy + py[j] * u, 1.4f * s, Cream);
                }
            }

            // ---- toast (noclip, arrivals) ----
            {
                const double until = RegNumber("/NoclipProtocol/Hud/toast.until", 0.0);
                const AZStd::string toast = RegString("/NoclipProtocol/Hud/toast", "");
                if (!toast.empty() && until > m_time)
                {
                    const float fade = AZStd::min(1.0f, static_cast<float>(until - m_time));
                    AZ::Color c = Bright;
                    c.SetA(fade);
                    text(Face::Mono, toast, C(640), Y(470), 14.0f, c, 1, 2.0f);
                }
            }

        }

        // ---- the intro's briefing terminal: Terminal.dc.html, the whole screen ----
        if (m_page == Page::Terminal)
        {
            m_buttons.clear();
            // Every shape of the page goes into one mesh, drawn in order (later on top): separate see-through batches can
            // come out in any order. Colours with alpha are mixed with the page background here, so all are solid.
            AZStd::vector<AZ::Vector3> tv;
            AZStd::vector<AZ::Color> tc;
            auto solid = [&](const AZ::Color& c)
            {
                const float a = c.GetA();
                return AZ::Color(TermBg.GetR() + (c.GetR() - TermBg.GetR()) * a, TermBg.GetG() + (c.GetG() - TermBg.GetG()) * a,
                                 TermBg.GetB() + (c.GetB() - TermBg.GetB()) * a, static_cast<float>(hud_term_alpha));
            };
            auto tquad = [&](const AZ::Vector3& p0, const AZ::Vector3& p1, const AZ::Vector3& p2, const AZ::Vector3& p3, const AZ::Color& c)
            {
                for (const AZ::Vector3& v : { p0, p1, p2, p0, p2, p3 })
                {
                    tv.push_back(v);
                    tc.push_back(c);
                }
            };
            auto trect = [&](float x, float y, float w, float h, const AZ::Color& c)
            { tquad(P(x, y), P(x + w, y), P(x + w, y + h), P(x, y + h), solid(c)); };
            auto toutline = [&](float x, float y, float w, float h, float t, const AZ::Color& c)
            {
                trect(x, y, w, t, c);
                trect(x, y + h - t, w, t, c);
                trect(x, y, t, h, c);
                trect(x + w - t, y, t, h, c);
            };
            auto tline = [&](float x0, float y0, float x1, float y1, float t, const AZ::Color& c)
            {
                const float dx = x1 - x0;
                const float dy = y1 - y0;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len <= 0.0f)
                {
                    return;
                }
                const float nx = -dy / len * t * 0.5f;
                const float ny = dx / len * t * 0.5f;
                tquad(P(x0 + nx, y0 + ny), P(x1 + nx, y1 + ny), P(x1 - nx, y1 - ny), P(x0 - nx, y0 - ny), solid(c));
            };
            const AZ::Vector2 cursor(m_cursor.GetX() * W, m_cursor.GetY() * H);
            auto over = [&](float x, float y, float w, float h)
            { return cursor.GetX() >= x && cursor.GetX() <= x + w && cursor.GetY() >= y && cursor.GetY() <= y + h; };
            auto clicked = [&](float x, float y, float w, float h)
            {
                if (m_click && over(x, y, w, h))
                {
                    m_click = false;
                    return true;
                }
                return false;
            };
            // Text in a CSS line box: top = the box's top (design px), lineHeight = the box's height as a multiple of the size
            // (Plex Mono "normal" is 1.3, Oswald's 1.482). text() itself centres the glyphs in a 1.2 box. The shadow pass
            // stays on (black on the near-black page, so it doesn't show): without it every glyph came out in a white box.
            auto textIn = [&](Face face, const AZStd::string& value, float x, float top, float size, float lineHeight,
                              const AZ::Color& c, int align = 0, float spacing = 0.0f)
            { return text(face, value, x, Y(top + (lineHeight - 1.2f) * size * 0.5f), size, c, align, spacing, true); };
            // An omega (Plex Mono has none; the browser falls back to another font): a ring open at the bottom, two feet.
            auto omega = [&](float x, float top, float size, const AZ::Color& c)
            {
                const float w = 0.6f * size;
                const float cx = C(x + w * 0.5f);
                const float cy = Y(top + size * 0.68f);
                const float r = 0.3f * size * s;
                const float t = AZStd::max(1.0f, 0.09f * size * s);
                const float pi = 3.14159265f;
                float px = 0.0f, py = 0.0f;
                for (int k = 0; k <= 12; ++k)
                {
                    const float a = pi * 0.5f + pi * 0.3f + (2.0f * pi - pi * 0.6f) * k / 12.0f;
                    const float qx = cx + std::cos(a) * r;
                    const float qy = cy + std::sin(a) * r;
                    if (k > 0)
                    {
                        tline(px, py, qx, qy, t, c);
                    }
                    else
                    {
                        tline(qx, qy, qx - 0.12f * size * s, qy, t, c); // left foot
                    }
                    px = qx;
                    py = qy;
                }
                tline(px, py, px + 0.12f * size * s, py, t, c); // right foot
                return w;
            };
            // Mono text with "~" drawn as an omega.
            auto labelText = [&](const AZStd::string& value, float x, float top, float size, const AZ::Color& c, float spacing)
            {
                float cursorX = x;
                size_t start = 0;
                while (start <= value.size())
                {
                    size_t end = value.find('~', start);
                    const AZStd::string part = value.substr(start, end == AZStd::string::npos ? AZStd::string::npos : end - start);
                    if (!part.empty())
                    {
                        textIn(Face::Mono, part, C(cursorX), top, size, 1.3f, c, 0, spacing);
                        cursorX += (measure(Face::Mono, part, size, spacing) / s) + spacing;
                    }
                    if (end == AZStd::string::npos)
                    {
                        break;
                    }
                    cursorX += omega(cursorX, top, size, c) + spacing;
                    start = end + 1;
                }
            };
            auto textX = [&](float x) { return C(x); };

            // Background and scanlines: a 1 px line every 3 px, rgba(255,240,180,0.035) over #0b0a07.
            {
                const AZ::Color scan(1.0f, 240 / 255.0f, 180 / 255.0f, 0.035f);
                for (float y = 0.0f; y < DesignH; y += 3.0f)
                {
                    trect(0.0f, Y(y), W, Y(y + 1.0f) - Y(y), scan);
                    trect(0.0f, Y(y + 1.0f), W, AZStd::min(H, Y(y + 3.0f)) - Y(y + 1.0f), TermBg);
                }
            }

            // Header: 44 px, a gold-ish rule under it.
            {
                const float stroke = 1.227f;
                toutline(textX(28.614f), Y(13.614f), 16.77f * s, 16.77f * s, AZStd::max(1.0f, stroke * s), TermGold);
                trect(textX(28.0f + 5.727f), Y(13.0f + 5.727f), 6.545f * s, 6.545f * s, TermGold);
                textIn(Face::Mono, "SECURE TERMINAL", textX(62.0f), 14.2f, 12.0f, 1.3f, TermGold, 0, 2.0f);
                textIn(Face::Mono, "SITE-[REDACTED] \xc2\xb7 SUBLEVEL 4", textX(216.0f), 14.2f, 12.0f, 1.3f, TermMuted, 0, 2.0f);
                const AZStd::string who = "CLEARANCE LEVEL 3 \xc2\xb7 DR. " + RegString("/NoclipProtocol/Player/Name", "[PLAYER NAME]");
                textIn(Face::Mono, who, textX(1250.0f), 14.2f, 12.0f, 1.3f, TermMuted, 2, 2.0f);
                trect(0.0f, Y(44.0f), W, AZStd::max(1.0f, s), AZ::Color(216 / 255.0f, 195 / 255.0f, 90 / 255.0f, 0.35f));
            }

            // Files list on the left.
            const auto& files = TermFiles();
            {
                trect(textX(259.0f), Y(45.0f), AZStd::max(1.0f, s), H - Y(45.0f), AZ::Color(216 / 255.0f, 195 / 255.0f, 90 / 255.0f, 0.2f));
                static const char* const sections[] = { "BRIEFINGS", "PERSONNEL", "CONTAINMENT" };
                float y = 67.0f;
                int section = -1;
                for (int i = 0; i < static_cast<int>(files.size()); ++i)
                {
                    const TermFile& file = files[i];
                    if (file.m_section != section)
                    {
                        if (section >= 0)
                        {
                            y += 16.0f; // margin-top of the heading
                        }
                        section = file.m_section;
                        textIn(Face::Mono, sections[section], textX(20.0f), y, 11.0f, 1.3f, TermMuted, 0, 2.0f);
                        y += 11.0f * 1.3f + 6.0f + 6.0f; // heading, its margin-bottom, the gap
                    }
                    const float h = 13.0f * 1.3f + 14.0f;
                    const bool selected = i == m_terminalFile;
                    const bool hover = over(textX(20.0f), Y(y), 219.0f * s, h * s);
                    if (selected)
                    {
                        trect(textX(20.0f), Y(y), 219.0f * s, h * s, AZ::Color(216 / 255.0f, 195 / 255.0f, 90 / 255.0f, 0.14f));
                        trect(textX(20.0f), Y(y), 2.0f * s, h * s, TermGold);
                    }
                    const AZ::Color c = hover ? TermHover : (selected ? TermSelected : (file.m_locked ? TermLocked : TermText));
                    textIn(Face::Mono, file.m_name, textX(32.0f), y + 7.0f, 13.0f, 1.3f, c);
                    if (clicked(textX(20.0f), Y(y), 219.0f * s, h * s))
                    {
                        DoAction(ActFile + i);
                    }
                    y += h + 6.0f;
                }
            }

            // The open document.
            {
                const TermFile& file = files[AZStd::clamp(m_terminalFile, 0, static_cast<int>(files.size()) - 1)];
                labelText(file.m_label, 304.0f, 75.0f, 11.0f, TermMuted, 2.0f);
                textIn(Face::Oswald, file.m_title, textX(304.0f), 99.3f, 40.0f, 1.482f, file.m_locked ? TermThreatValue : TermTitle, 0, 2.0f);
                float y = 158.6f;
                if (m_terminalFile == 0)
                {
                    // OBJECTIVE / ENTRY POINT / THREAT boxes.
                    y += 18.0f;
                    const float bw = (932.0f - 24.0f) / 3.0f;
                    const float bh = 1.0f + 10.0f + 11.0f * 1.3f + 4.0f + 12.0f * 1.3f + 10.0f + 1.0f;
                    struct Cell { const char* label; const char* value; bool threat; };
                    static const Cell cells[] = { { "OBJECTIVE", "Establish forward base", false },
                                                  { "ENTRY POINT", "Breach 4-A, Level 0", false },
                                                  { "THREAT", "UNKNOWN", true } };
                    for (int k = 0; k < 3; ++k)
                    {
                        const float x = 304.0f + k * (bw + 12.0f);
                        toutline(textX(x), Y(y), bw * s, bh * s, AZStd::max(1.0f, s),
                            cells[k].threat ? AZ::Color(224 / 255.0f, 98 / 255.0f, 79 / 255.0f, 0.6f)
                                            : AZ::Color(216 / 255.0f, 195 / 255.0f, 90 / 255.0f, 0.3f));
                        textIn(Face::Mono, cells[k].label, textX(x + 13.0f), y + 11.0f, 11.0f, 1.3f, cells[k].threat ? TermThreat : TermMuted, 0, 2.0f);
                        textIn(Face::Mono, cells[k].value, textX(x + 13.0f), y + 11.0f + 11.0f * 1.3f + 4.0f, 12.0f, 1.3f,
                            cells[k].threat ? TermThreatValue : TermText);
                    }
                    y += bh;
                }
                // Paragraphs: 14 px, line height 1.75, at most 840 px wide, 12 px apart.
                y += 22.0f;
                const float size = 14.0f;
                const float lh = size * 1.75f;
                const float charW = 0.6f * size;
                for (const auto& paragraph : file.m_paragraphs)
                {
                    // Words: (text, kind, joined to the word before).
                    struct Word { AZStd::string text; int kind; bool joined; };
                    AZStd::vector<Word> words;
                    for (const Run& run : paragraph)
                    {
                        const AZStd::string value(run.m_text);
                        size_t start = 0;
                        bool first = true;
                        while (start < value.size())
                        {
                            size_t end = value.find(' ', start);
                            if (end == AZStd::string::npos)
                            {
                                end = value.size();
                            }
                            if (end > start)
                            {
                                words.push_back(Word{ value.substr(start, end - start), run.m_kind, first && start == 0 && !words.empty() });
                            }
                            first = false;
                            start = end + 1;
                        }
                    }
                    float x = 0.0f;
                    for (size_t w = 0; w < words.size(); ++w)
                    {
                        // A word and everything joined to it break together.
                        float width = static_cast<float>(Characters(words[w].text).size()) * charW;
                        for (size_t k = w + 1; k < words.size() && words[k].joined; ++k)
                        {
                            width += static_cast<float>(Characters(words[k].text).size()) * charW;
                        }
                        const float gap = (x > 0.0f && !words[w].joined) ? charW : 0.0f;
                        if (!words[w].joined && x > 0.0f && x + gap + width > 840.0f)
                        {
                            x = 0.0f;
                            y += lh;
                        }
                        else
                        {
                            x += gap;
                        }
                        const float wordW = static_cast<float>(Characters(words[w].text).size()) * charW;
                        if (words[w].kind == 2)
                        {
                            // class="rd": a cream bar over the content area (1.3 em, centred in the line).
                            trect(textX(304.0f + x), Y(y + (lh - size * 1.3f) * 0.5f), wordW * s, size * 1.3f * s, TermText);
                        }
                        else
                        {
                            textIn(Face::Mono, words[w].text, textX(304.0f + x), y, size, 1.75f, words[w].kind == 1 ? TermSelected : TermText);
                        }
                        x += wordW;
                    }
                    y += lh + 12.0f;
                }
            }

            // Bottom: "> ACCEPT DEPLOYMENT?", ACCEPT [Y], REVIEW FILES [N].
            {
                const float barTop = 637.1f;
                trect(textX(304.0f), Y(barTop), 932.0f * s, AZStd::max(1.0f, s), AZ::Color(216 / 255.0f, 195 / 255.0f, 90 / 255.0f, 0.2f));
                const float mid = 654.1f + 36.9f * 0.5f;
                textIn(Face::Mono, "> ACCEPT DEPLOYMENT?", textX(304.0f), mid - 15.0f * 1.3f * 0.5f, 15.0f, 1.3f, TermSelected);
                const float acceptW = 22.0f + 10.0f * (7.8f + 2.0f) + 22.0f;
                const float acceptH = 13.0f * 1.3f + 18.0f;
                trect(textX(504.0f), Y(mid - acceptH * 0.5f), acceptW * s, acceptH * s, TermGold);
                textIn(Face::MonoSemiBold, "ACCEPT [Y]", textX(526.0f), mid - 13.0f * 1.3f * 0.5f, 13.0f, 1.3f, TermBg, 0, 2.0f);
                if (clicked(textX(504.0f), Y(mid - acceptH * 0.5f), acceptW * s, acceptH * s))
                {
                    DoAction(ActAccept);
                }
                const float reviewX = 504.0f + acceptW + 20.0f;
                const float reviewW = 1.0f + 22.0f + 16.0f * (7.8f + 2.0f) + 22.0f + 1.0f;
                const float reviewH = acceptH + 2.0f;
                const bool hover = over(textX(reviewX), Y(mid - reviewH * 0.5f), reviewW * s, reviewH * s);
                toutline(textX(reviewX), Y(mid - reviewH * 0.5f), reviewW * s, reviewH * s, AZStd::max(1.0f, s),
                    AZ::Color(216 / 255.0f, 195 / 255.0f, 90 / 255.0f, 0.6f));
                textIn(Face::Mono, "REVIEW FILES [N]", textX(reviewX + 23.0f), mid - 13.0f * 1.3f * 0.5f, 13.0f, 1.3f,
                    hover ? TermHover : TermText, 0, 2.0f);
                if (clicked(textX(reviewX), Y(mid - reviewH * 0.5f), reviewW * s, reviewH * s))
                {
                    DoAction(ActReview);
                }
            }
            // Laid down several times at half alpha: text drawn over shapes whose alpha is near 1 comes out in white boxes
            // (the engine's text blending goes by what's already there), so the page is built up from half-alpha layers.
            for (int layer = 0; layer < AZStd::clamp(static_cast<int>(hud_term_layers), 1, 8); ++layer)
            {
                AZ::RPI::AuxGeomDraw::AuxGeomDynamicDrawArguments args;
                args.m_verts = tv.data();
                args.m_vertCount = static_cast<uint32_t>(tv.size());
                args.m_colors = tc.data();
                args.m_colorCount = static_cast<uint32_t>(tc.size());
                // See-through type (with solid colours): text over opaque aux shapes comes out in white boxes.
                args.m_opacityType = AZ::RPI::AuxGeomDraw::OpacityType::Translucent;
                args.m_depthTest = AZ::RPI::AuxGeomDraw::DepthTest::Off;
                args.m_depthWrite = AZ::RPI::AuxGeomDraw::DepthWrite::Off;
                args.m_viewProjectionOverrideIndex = overlay;
                draw->DrawTriangles(args);
            }
            m_click = false;
        }
        // ---- pause menu and settings ----
        else if (m_page != Page::None)
        {
            rect(0.0f, 0.0f, W, H, AZ::Color(8 / 255.0f, 7 / 255.0f, 4 / 255.0f, 0.78f));
            m_buttons.clear();
            const AZ::Vector2 cursor(m_cursor.GetX() * W, m_cursor.GetY() * H);
            auto button = [&](const AZStd::string& label, float y, int action, bool primary = false)
            {
                const float x = C(490);
                const float w = 300.0f * s;
                const float h = 44.0f * s;
                const bool hover = cursor.GetX() >= x && cursor.GetX() <= x + w && cursor.GetY() >= Y(y) && cursor.GetY() <= Y(y) + h;
                rect(x, Y(y), w, h, hover ? AZ::Color(Gold.GetR(), Gold.GetG(), Gold.GetB(), 0.25f) : Panel);
                rect(x, Y(y), 3.0f * s, h, primary || hover ? Gold : Line);
                text(Face::MonoMedium, label, x + 20.0f * s, Y(y + 13.0f), 14.0f, hover ? Bright : Cream, 0, 2.0f);
                if (hover && m_click)
                {
                    m_click = false;
                    DoAction(action);
                }
            };
            auto setting = [&](const AZStd::string& label, const AZStd::string& value, float y, int down, int up)
            {
                text(Face::Mono, label, C(490), Y(y + 4.0f), 12.0f, Muted, 0, 2.0f);
                const float bw = 32.0f * s;
                const float bx0 = C(790) - bw * 2.0f - 90.0f * s;
                for (int k = 0; k < (up ? 2 : 1); ++k)
                {
                    const float bx = k == 0 ? (up ? bx0 : C(790) - 90.0f * s) : C(790) - bw;
                    const bool hover = cursor.GetX() >= bx && cursor.GetX() <= bx + bw && cursor.GetY() >= Y(y) && cursor.GetY() <= Y(y) + 24.0f * s;
                    outline(bx, Y(y), up ? bw : 90.0f * s, 24.0f * s, AZStd::max(1.0f, 1.5f * s), hover ? Gold : Line);
                    text(Face::MonoSemiBold, up ? (k == 0 ? "-" : "+") : value, bx + (up ? bw : 90.0f * s) * 0.5f, Y(y + 5.0f), 12.0f, Bright, 1);
                    if (hover && m_click)
                    {
                        m_click = false;
                        DoAction(k == 0 ? down : up);
                    }
                }
                if (up)
                {
                    text(Face::Mono, value, bx0 + bw + 45.0f * s, Y(y + 5.0f), 12.0f, Bright, 1);
                }
            };
            if (m_page == Page::Pause)
            {
                text(Face::Oswald, "PAUSED", C(640), Y(200), 44.0f, Bright, 1, 3.0f);
                text(Face::Mono, RegString("/NoclipProtocol/Hud/level.title", "LEVEL 0") + " · " + RegString("/NoclipProtocol/Hud/level.subtitle", ""),
                    C(640), Y(262), 12.0f, Muted, 1, 2.0f);
                button("RESUME", 310, ActResume, true);
                button("SETTINGS", 366, ActSettings);
                button("QUIT TO DESKTOP", 422, ActQuit);
                text(Face::Mono, "ESC / P to resume", C(640), Y(492), 11.0f, Muted, 1, 2.0f);
            }
            else
            {
                text(Face::Oswald, "SETTINGS", C(640), Y(170), 44.0f, Bright, 1, 3.0f);
                bool supported = true;
                if (auto* reg = AZ::SettingsRegistry::Get())
                {
                    reg->Get(supported, "/NoclipProtocol/Graphics/RayTracingSupported");
                }
                setting("RAY-TRACED LIGHT", supported ? (m_rayTracing ? "ON" : "OFF") : "NOT ON THIS PC", 250, ActRayTracing, 0);
                setting("MOUSE SENSITIVITY", AZStd::string::format("%.1fx", m_mouseSensitivity), 300, ActSensDown, ActSensUp);
                setting("VOLUME", AZStd::string::format("%+.0f dB", m_volumeDb), 350, ActVolDown, ActVolUp);
                button("BACK", 420, ActBack, true);
            }
            m_click = false;
        }
    }
} // namespace NoclipProtocol
