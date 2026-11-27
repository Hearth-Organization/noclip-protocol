#include "NoclipHudComponent.h"

#include <Atom/RPI.Public/AuxGeom/AuxGeomDraw.h>
#include <Atom/RPI.Public/AuxGeom/AuxGeomFeatureProcessorInterface.h>
#include <Atom/RPI.Public/ViewportContext.h>
#include <Atom/RPI.Public/ViewportContextBus.h>
#include <Atom/RPI.Public/View.h>
#include "../Base/BaseBuilding.h"
#include "BuildIcons.h"
#include "HudIcons.h"
#include "../Player/Weapons.h"
#include <AzCore/Console/IConsole.h>
#include <AzCore/IO/FileIO.h>
#include <AzCore/Math/MathUtils.h>
#include <AzCore/Serialization/EditContext.h>
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Settings/SettingsRegistry.h>
#include <AzCore/std/algorithm.h>
#include <AzCore/std/sort.h>
#include <AzFramework/Input/Devices/Keyboard/InputDeviceKeyboard.h>
#include <AzFramework/Input/Devices/Mouse/InputDeviceMouse.h>
#include <CrySystemBus.h>
#include <IFont.h>
#include <ISystem.h>

#include <cmath>
#include <cstdlib>
#include <ctime>

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
            "Press a HUD key without a keyboard (for testing): hud_key E | Y | N | Esc | Up | Down | Enter | Tab | R | X | Click | 1..6");
    } // namespace
} // namespace NoclipProtocol

AZ_CVAR(float, hud_term_alpha, 0.5f, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Terminal page: alpha of each layer of its shapes (text over shapes of alpha much above 0.5 gets white boxes)");
AZ_CVAR(int, hud_orders_layers, 4, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Foundation requisitions page: half-alpha layers over its dark backdrop");
AZ_CVAR(int, hud_term_layers, 7, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Terminal page: how many times its shapes are laid down (7 at 0.5 alpha covers 99% of the game behind)");
AZ_CVAR(int, hud_wheel, -1, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Show the weapon wheel with slice 0-5 picked (testing; -1 off)");
AZ_CVAR(int, hud_page, -1, nullptr, AZ::ConsoleFunctorFlags::Null,
    "Open a menu page without a keyboard (for testing): 0 none, 1 pause, 2 settings, 3 terminal, 4 main menu");

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
        const AZ::Color Panel(10 / 255.0f, 9 / 255.0f, 6 / 255.0f, 0.62f);
        const AZ::Color Line(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, 0.5f);
        const AZ::Color Shadow(0.0f, 0.0f, 0.0f, 0.9f);

        enum Action { ActResume = 1, ActSettings, ActQuit, ActBack, ActAccept, ActReview, ActContinue, ActNew, ActCoop, ActArchive,
                      ActFile, ActRayTracing = ActFile + 16, ActSensDown,
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

        // Loads a level on the next frame (loading tears down the HUD's own entity).
        void LoadLevelLater(const AZStd::string& level)
        {
            AZ::TickBus::QueueFunction([level]()
            {
                if (auto* console = AZ::Interface<AZ::IConsole>::Get())
                {
                    console->PerformCommand(AZStd::string::format("LoadLevel %s", level.c_str()).c_str());
                }
            });
        }

        // "Level0" -> "Level 0", "Intro" -> "Sublevel 4"
        AZStd::string LevelLabel(const AZStd::string& level)
        {
            if (level.size() == 6 && level.substr(0, 5) == "Level")
            {
                return AZStd::string("Level ") + level.substr(5);
            }
            return level == "Intro" ? AZStd::string("Sublevel 4") : level;
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

        // Oswald Medium (weight 500, the main menu's items), same way.
        static const float OswaldMediumAdvance[95] = {0.243f, 0.233f, 0.315f, 0.511f, 0.488f, 0.95f, 0.587f, 0.149f, 0.319f, 0.295f, 0.411f, 0.433f, 0.213f, 0.314f, 0.217f, 0.396f, 0.534f, 0.382f, 0.497f, 0.496f, 0.504f, 0.493f, 0.521f, 0.413f, 0.51f, 0.521f, 0.239f, 0.251f, 0.386f, 0.429f, 0.386f, 0.483f, 0.928f, 0.522f, 0.557f, 0.54f, 0.557f, 0.428f, 0.413f, 0.559f, 0.586f, 0.275f, 0.326f, 0.532f, 0.421f, 0.683f, 0.545f, 0.563f, 0.527f, 0.564f, 0.561f, 0.494f, 0.43f, 0.566f, 0.516f, 0.712f, 0.499f, 0.487f, 0.425f, 0.336f, 0.396f, 0.317f, 0.46f, 0.356f, 0.293f, 0.435f, 0.477f, 0.44f, 0.473f, 0.446f, 0.304f, 0.472f, 0.478f, 0.248f, 0.25f, 0.471f, 0.252f, 0.718f, 0.474f, 0.455f, 0.476f, 0.474f, 0.353f, 0.4f, 0.33f, 0.473f, 0.404f, 0.593f, 0.416f, 0.421f, 0.37f, 0.338f, 0.251f, 0.351f, 0.465f};

        // kind: 0 IBM Plex Mono, 1 Oswald Bold, 2 Oswald Medium
        float AdvanceEm(int kind, const AZStd::string& ch)
        {
            if (kind == 0)
            {
                return 0.6f;
            }
            const unsigned char c = static_cast<unsigned char>(ch[0]);
            if (ch.size() == 1 && c >= 32 && c <= 126)
            {
                return kind == 2 ? OswaldMediumAdvance[c - 32] : OswaldAdvance[c - 32];
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
        m_savedThisLevel = false;
        LoadSettings();
        ApplySettings();
        if (auto* registry = AZ::SettingsRegistry::Get())
        {
            registry->Set("/NoclipProtocol/Paused", false);
            // The HUD's clock starts again with each level: a toast from the last level ("Stepping through...")
            // would otherwise stay up for as long as that level ran.
            registry->Set("/NoclipProtocol/Hud/toast.until", 0.0);
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
        const Spec specs[6] = {
            { "noclip-plexmono", "@products@/fonts/noclip/ibmplexmono-regular.ttf", 1024, 32 },        // 32 px glyph slots
            { "noclip-plexmono-medium", "@products@/fonts/noclip/ibmplexmono-medium.ttf", 1024, 32 },
            { "noclip-plexmono-semibold", "@products@/fonts/noclip/ibmplexmono-semibold.ttf", 1024, 32 },
            { "noclip-oswald-bold", "@products@/fonts/noclip/oswald-bold.ttf", 1024, 32 },
            { "noclip-oswald-bold-large", "@products@/fonts/noclip/oswald-bold.ttf", 1024, 8 },       // 128 px slots: titles, the ammo count
            { "noclip-oswald-medium", "@products@/fonts/noclip/oswald-medium.ttf", 1024, 32 },        // the main menu's items
        };
        const int smooth = AZ::GetClamp(static_cast<int>(hud_font_smooth), 0, 2);
        const int amount = AZ::GetClamp(static_cast<int>(hud_font_amount), 0, 2);
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
        for (int i = 0; i < 6; ++i)
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
            registry->Get(m_lastLevel, "/NoclipProtocol/Save/Level");
            registry->Get(m_lastWhen, "/NoclipProtocol/Save/When");
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
            "        \"Player\": { \"MouseSensitivity\": %.2f },\n"
            "        \"Save\": { \"Level\": \"%s\", \"When\": \"%s\" }\n    },\n"
            "    \"Hearth\": { \"Audio\": { \"Buses\": { \"Master\": %.1f } } }\n}\n",
            m_rayTracing ? "true" : "false", m_mouseSensitivity, m_lastLevel.c_str(), m_lastWhen.c_str(), m_volumeDb);
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
            m_page = m_inMenu ? Page::MainMenu : Page::None;
            break;
        case ActContinue:
            LoadLevelLater(m_lastLevel.empty() ? AZStd::string("Intro") : m_lastLevel);
            break;
        case ActNew:
            Base::Get().NewExpedition(); // a new expedition: starting stockpile, no base
            LoadLevelLater("Intro");
            break;
        case ActCoop:
            m_menuNotice = "co-op isn't in yet";
            m_menuNoticeUntil = m_time + 3.0f;
            break;
        case ActArchive:
            m_page = Page::Terminal;
            m_terminalFile = 0;
            break;
        case ActSettings:
            m_page = Page::Settings;
            break;
        case ActBack:
            m_page = m_inMenu ? Page::MainMenu : Page::Pause;
            break;
        case ActQuit:
            if (auto* console = AZ::Interface<AZ::IConsole>::Get())
            {
                console->PerformCommand("quit");
            }
            break;
        case ActAccept:
            if (m_inMenu)
            {
                Base::Get().NewExpedition(); // the archive from the main menu: accepting starts a new expedition
                LoadLevelLater("Intro");
                break;
            }
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
            registry->Set("/NoclipProtocol/Paused", Pausing());
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
        // Supplies: E at one picks it up (the level generator does it); Q drinks an Almond Water (sanity +35%).
        if (m_page == Page::None && id == InputDeviceKeyboard::Key::AlphanumericE &&
            RegString("/NoclipProtocol/Hud/interact.target", "") == "pickup")
        {
            Base::Get().m_pickRequested = true;
            return true;
        }
        // In the base: E at a piece uses it (rest, make, study...) or opens a delivered crate; at the field radio it
        // opens the Foundation's requisitions.
        if (m_page == Page::None && id == InputDeviceKeyboard::Key::AlphanumericE)
        {
            const AZStd::string target = RegString("/NoclipProtocol/Hud/interact.target", "");
            if (target == "use")
            {
                Base::Get().m_useRequested = true;
                return true;
            }
            if (target == "radio")
            {
                m_page = Page::Orders;
                m_orderRequest = false;
                if (auto* registry = AZ::SettingsRegistry::Get())
                {
                    registry->Set("/NoclipProtocol/Paused", true);
                }
                return true;
            }
        }
        // The Foundation's requisitions: TAB through the sections, arrows / WASD between the cards, E requests, Esc closes.
        if (m_page == Page::Orders)
        {
            auto move = [&](int by)
            {
                m_orderItem = AZStd::max(0, m_orderItem + by);
            };
            if (id == InputDeviceKeyboard::Key::EditTab) { m_orderSection = (m_orderSection + 1) % 8; m_orderItem = 0; return true; }
            if (id == InputDeviceKeyboard::Key::AlphanumericE || id == InputDeviceKeyboard::Key::EditEnter) { m_orderRequest = true; return true; }
            if (id == InputDeviceKeyboard::Key::NavigationArrowRight || id == InputDeviceKeyboard::Key::AlphanumericD) { move(1); return true; }
            if (id == InputDeviceKeyboard::Key::NavigationArrowLeft || id == InputDeviceKeyboard::Key::AlphanumericA) { move(-1); return true; }
            if (id == InputDeviceKeyboard::Key::NavigationArrowDown || id == InputDeviceKeyboard::Key::AlphanumericS) { move(3); return true; }
            if (id == InputDeviceKeyboard::Key::NavigationArrowUp || id == InputDeviceKeyboard::Key::AlphanumericW) { move(-3); return true; }
            if (id == InputDeviceKeyboard::Key::Escape || id == InputDeviceKeyboard::Key::AlphanumericP)
            {
                m_page = Page::None;
                if (auto* registry = AZ::SettingsRegistry::Get())
                {
                    registry->Set("/NoclipProtocol/Paused", false);
                }
                return true;
            }
        }
        // B: a fresh flashlight battery (from the workbench or the Foundation).
        if (m_page == Page::None && id == InputDeviceKeyboard::Key::AlphanumericB && !m_inMenu)
        {
            Base::State& base = Base::Get();
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                const char* text = "NO SPARE BATTERIES \xc2\xb7 MAKE ONE AT THE WORKBENCH";
                if (base.m_batteries > 0)
                {
                    base.m_batteries -= 1;
                    registry->Set("/NoclipProtocol/Player/FlashlightRefill", true);
                    base.Save();
                    text = "FRESH BATTERY IN";
                }
                registry->Set("/NoclipProtocol/Hud/toast", AZStd::string(text));
                registry->Set("/NoclipProtocol/Hud/toast.until", static_cast<double>(m_time) + 2.5);
            }
            return true;
        }
        if (m_page == Page::None && id == InputDeviceKeyboard::Key::AlphanumericQ && !m_inMenu)
        {
            Base::State& base = Base::Get();
            auto* registry = AZ::SettingsRegistry::Get();
            if (registry)
            {
                double sanity = 1.0;
                registry->Get(sanity, "/NoclipProtocol/Hud/player.sanity");
                const char* text = "NO ALMOND WATER LEFT";
                if (base.m_almondWater > 0 && sanity < 0.995)
                {
                    base.m_almondWater -= 1;
                    double add = 0.0;
                    registry->Get(add, "/NoclipProtocol/Player/SanityAdd");
                    registry->Set("/NoclipProtocol/Player/SanityAdd", add + 0.35);
                    base.Save();
                    text = "ALMOND WATER \xc2\xb7 SANITY +35%";
                }
                else if (base.m_almondWater > 0)
                {
                    text = "YOU FEEL FINE \xc2\xb7 SAVE IT FOR LATER";
                }
                registry->Set("/NoclipProtocol/Hud/toast", AZStd::string(text));
                registry->Set("/NoclipProtocol/Hud/toast.until", static_cast<double>(m_time) + 2.5);
            }
            return true;
        }
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
        // Base building: TAB opens build mode in the forward base (Level 0's spawn room) and closes it.
        if (id == InputDeviceKeyboard::Key::EditTab && (m_page == Page::None || m_page == Page::Build))
        {
            if (m_page == Page::Build)
            {
                m_page = Page::None;
            }
            else if (Base::Get().m_active && m_inBase)
            {
                m_page = Page::Build;
            }
            else if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Set("/NoclipProtocol/Hud/toast", AZStd::string(Base::Get().m_active ? "BUILD IN THE FORWARD BASE \xc2\xb7 THE SPAWN ROOM"
                                                                                                 : "NOTHING TO BUILD HERE"));
                registry->Set("/NoclipProtocol/Hud/toast.until", static_cast<double>(m_time) + 2.5);
            }
            return true;
        }
        if (m_page == Page::Build)
        {
            if (id == InputDeviceKeyboard::Key::AlphanumericR)
            {
                m_buildYaw = std::fmod(m_buildYaw + 1.5707963f, 6.2831853f);
                return true;
            }
            if (id == InputDeviceKeyboard::Key::AlphanumericX)
            {
                m_demolish = true;
                return true;
            }
            static const InputChannelId digits[6] = { InputDeviceKeyboard::Key::Alphanumeric1, InputDeviceKeyboard::Key::Alphanumeric2,
                                                      InputDeviceKeyboard::Key::Alphanumeric3, InputDeviceKeyboard::Key::Alphanumeric4,
                                                      InputDeviceKeyboard::Key::Alphanumeric5, InputDeviceKeyboard::Key::Alphanumeric6 };
            for (int i = 0; i < 6; ++i)
            {
                if (id == digits[i])
                {
                    m_buildTile = i;
                    return true;
                }
            }
            // Q / E: the tab before / after.
            if (id == InputDeviceKeyboard::Key::AlphanumericQ || id == InputDeviceKeyboard::Key::AlphanumericE)
            {
                m_buildTab = (m_buildTab + (id == InputDeviceKeyboard::Key::AlphanumericE ? 1 : Base::TabCount - 1)) % Base::TabCount;
                m_buildTile = 0;
                return true;
            }
            if (id == InputDeviceKeyboard::Key::Escape || id == InputDeviceKeyboard::Key::AlphanumericP)
            {
                m_page = Page::None;
                return id == InputDeviceKeyboard::Key::Escape;
            }
        }
        // The main menu: up / down to move, Enter to choose.
        if (m_page == Page::MainMenu)
        {
            if (id == InputDeviceKeyboard::Key::NavigationArrowUp || id == InputDeviceKeyboard::Key::AlphanumericW)
            {
                m_menuSelected = (m_menuSelected + 5) % 6;
                return true;
            }
            if (id == InputDeviceKeyboard::Key::NavigationArrowDown || id == InputDeviceKeyboard::Key::AlphanumericS)
            {
                m_menuSelected = (m_menuSelected + 1) % 6;
                return true;
            }
            if (id == InputDeviceKeyboard::Key::EditEnter || id == InputDeviceKeyboard::Key::NumPadEnter)
            {
                static const int actions[6] = { ActContinue, ActNew, ActCoop, ActArchive, ActSettings, ActQuit };
                DoAction(actions[AZ::GetClamp(m_menuSelected, 0, 5)]);
                return true;
            }
            if (id == InputDeviceKeyboard::Key::Escape || id == InputDeviceKeyboard::Key::AlphanumericP)
            {
                return true; // nothing to go back to
            }
        }
        if (m_inMenu && m_page == Page::Settings && (id == InputDeviceKeyboard::Key::Escape || id == InputDeviceKeyboard::Key::AlphanumericP))
        {
            DoAction(ActBack);
            return true;
        }
        // Esc in the game; P too, because Esc leaves Play in the editor.
        if (id == InputDeviceKeyboard::Key::Escape || id == InputDeviceKeyboard::Key::AlphanumericP)
        {
            m_page = m_page == Page::None ? Page::Pause : (m_page == Page::Settings ? Page::Pause : Page::None);
            if (auto* registry = AZ::SettingsRegistry::Get())
            {
                registry->Set("/NoclipProtocol/Paused", Pausing());
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
            else if (key == "Tab") { HandleKey(InputDeviceKeyboard::Key::EditTab); }
            else if (key == "Q") { HandleKey(InputDeviceKeyboard::Key::AlphanumericQ); }
            else if (key == "R") { HandleKey(InputDeviceKeyboard::Key::AlphanumericR); }
            else if (key == "X") { HandleKey(InputDeviceKeyboard::Key::AlphanumericX); }
            else if (key == "Click") { HandleKey(InputDeviceMouse::Button::Left); }
            else if (key.size() == 1 && key[0] >= '1' && key[0] <= '6') { m_buildTile = key[0] - '1'; }
            else if (key.size() == 2 && key[0] == 'T' && key[1] >= '1' && key[1] <= '6') { m_buildTab = key[1] - '1'; m_buildTile = 0; }
            else if (key == "B") { HandleKey(InputDeviceKeyboard::Key::AlphanumericB); }
            else if (key == "Left") { HandleKey(InputDeviceKeyboard::Key::NavigationArrowLeft); }
            else if (key == "Right") { HandleKey(InputDeviceKeyboard::Key::NavigationArrowRight); }
            else if (key == "Orders") { m_page = Page::Orders; }
            else if (key == "Up") { HandleKey(InputDeviceKeyboard::Key::NavigationArrowUp); }
            else if (key == "Down") { HandleKey(InputDeviceKeyboard::Key::NavigationArrowDown); }
            else if (key == "Enter") { HandleKey(InputDeviceKeyboard::Key::EditEnter); }
        }
        auto* registry = AZ::SettingsRegistry::Get();
        if (static_cast<int>(hud_page) >= 0)
        {
            const int page = static_cast<int>(hud_page);
            m_page = page == 1 ? Page::Pause : (page == 2 ? Page::Settings : (page == 3 ? Page::Terminal : (page == 4 ? Page::MainMenu : Page::None)));
            hud_page = -1;
            if (registry)
            {
                registry->Set("/NoclipProtocol/Paused", Pausing());
            }
        }
        if (registry)
        {
            registry->Set("/NoclipProtocol/Hud/time", static_cast<double>(m_time));
            registry->Set("/NoclipProtocol/Hud/pageOpen", m_page != Page::None || m_inMenu); // the weapons stay still
            bool menu = false;
            registry->Get(menu, "/NoclipProtocol/Menu/Active");
            if (menu != m_inMenu)
            {
                m_inMenu = menu;
                m_page = menu ? Page::MainMenu : Page::None;
                registry->Set("/NoclipProtocol/Paused", Pausing());
            }
            // The level being played, for CONTINUE: saved with the settings whenever it changes.
            AZStd::string level;
            registry->Get(level, "/NoclipProtocol/Save/Level");
            if (!level.empty() && (level != m_lastLevel || !m_savedThisLevel))
            {
                m_savedThisLevel = true; // the time it was last played: once per level load
                m_lastLevel = level;
                char when[32] = {};
                const std::time_t now = std::time(nullptr);
                if (const std::tm* local = std::localtime(&now))
                {
                    std::strftime(when, sizeof(when), "%b %d %H:%M", local);
                }
                m_lastWhen = when;
                registry->Set("/NoclipProtocol/Save/When", m_lastWhen);
                SaveSettings();
            }
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
        AZ::RPI::ViewPtr cameraView = viewport->GetDefaultView();
        const AZ::Transform camera = cameraView ? cameraView->GetCameraTransform() : AZ::Transform::CreateIdentity();
        {
            const Base::State& base = Base::Get();
            const AZ::Vector3 at = camera.GetTranslation();
            m_inBase = base.m_active && at.GetX() > base.m_minX && at.GetX() < base.m_maxX && at.GetY() > base.m_minY && at.GetY() < base.m_maxY;
            if (m_page == Page::Build && !m_inBase)
            {
                m_page = Page::None; // walked out of the base
            }
        }
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

        // Every HUD shape goes into one draw for the frame. AuxGeom sorts each see-through draw by the camera depth of its
        // centre (an unstable sort), so separate draws at the same depth come out in any order and a panel can land on top
        // of its own bars; inside one draw the triangles keep the order they were made in.
        AZStd::vector<AZ::Vector3> frameVerts;
        AZStd::vector<AZ::Color> frameColours;
        frameVerts.reserve(32768);
        frameColours.reserve(32768);
        struct FlushAtEnd
        {
            AZ::RPI::AuxGeomDrawPtr& m_draw;
            AZStd::vector<AZ::Vector3>& m_verts;
            AZStd::vector<AZ::Color>& m_colours;
            int32_t m_overlay;
            ~FlushAtEnd()
            {
                if (m_verts.empty() || !m_draw)
                {
                    return;
                }
                AZ::RPI::AuxGeomDraw::AuxGeomDynamicDrawArguments args;
                args.m_verts = m_verts.data();
                args.m_vertCount = static_cast<uint32_t>(m_verts.size());
                args.m_colors = m_colours.data();
                args.m_colorCount = static_cast<uint32_t>(m_colours.size());
                args.m_opacityType = AZ::RPI::AuxGeomDraw::OpacityType::Translucent;
                args.m_depthTest = AZ::RPI::AuxGeomDraw::DepthTest::Off;
                args.m_depthWrite = AZ::RPI::AuxGeomDraw::DepthWrite::Off;
                args.m_viewProjectionOverrideIndex = m_overlay;
                m_draw->DrawTriangles(args);
            }
        } flushAtEnd{ draw, frameVerts, frameColours, overlay };
        auto triangles = [&](const AZStd::vector<AZ::Vector3>& points, const AZStd::vector<AZ::Color>& colours)
        {
            if (points.empty() || colours.empty())
            {
                return;
            }
            frameVerts.insert(frameVerts.end(), points.begin(), points.end());
            if (colours.size() == points.size())
            {
                frameColours.insert(frameColours.end(), colours.begin(), colours.end());
            }
            else
            {
                frameColours.insert(frameColours.end(), points.size(), colours[0]);
            }
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
        auto advanceKind = [](Face face) { return face == Face::OswaldMedium ? 2 : ((face == Face::Oswald || face == Face::OswaldLarge) ? 1 : 0); };
        // Width in screen px of text at a CSS font size with CSS letter-spacing.
        auto measure = [&](Face face, const AZStd::string& text, float size, float spacing) -> float
        {
            float width = 0.0f;
            int count = 0;
            for (const AZStd::string& ch : Characters(text))
            {
                width += AdvanceEm(advanceKind(face), ch) * size * s;
                ++count;
            }
            return width + AZStd::max(0, count - 1) * spacing * s;
        };
        // Draws text; x/y = top-left of the CSS line box (screen px); align 0 left, 1 centre, 2 right. Returns the width.
        auto text = [&](Face face, const AZStd::string& value, float x, float y, float size, const AZ::Color& colour,
                        int align = 0, float spacing = 0.0f, bool shadow = true) -> float
        {
            const float width = measure(face, value, size, spacing);
            const int kind = advanceKind(face);
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
            // Glyphs come out in proportion to the slot size: the large face's 128 px slots draw 4x bigger than 32 px ones.
            const float slotScale = face == Face::OswaldLarge ? 0.25f : 1.0f;
            params.m_textSizeFactor = size * s * static_cast<float>(hud_text_scale) * slotScale *
                (static_cast<int>(hud_font_smooth) == 2 ? 2.0f : 1.0f);
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
                    cursor += AdvanceEm(kind, ch) * size * s + spacing * s;
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
        if (m_page != Page::Terminal && !m_inMenu && m_page != Page::Build && m_page != Page::Orders)
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

            // ==== The player HUD: PlayerHUD.dc.html (approved Nov 26; every block at 85%, shrunk toward its own corner) ====
            const float K = 0.85f;
            struct Anchor { float ox, oy; int hx; }; // origin (design px) and which edge x follows: 0 left, 1 centre, 2 right
            const Anchor TL{ 0.0f, 0.0f, 0 }, TC{ 640.0f, 0.0f, 1 }, TR{ 1280.0f, 0.0f, 2 }, BL{ 0.0f, 720.0f, 0 }, BR{ 1280.0f, 720.0f, 2 },
                MID{ 640.0f, 360.0f, 1 };
            auto AX = [&](const Anchor& a, float x) { const float d = a.ox + (x - a.ox) * K; return a.hx == 0 ? L(d) : (a.hx == 2 ? R(d) : C(d)); };
            auto AY = [&](const Anchor& a, float y) { return Y(a.oy + (y - a.oy) * K); };
            auto AS = [&](float v) { return v * s * K; }; // a size in design px
            const AZ::Color Olive = Hex(0x9aa36a);
            const AZ::Color Green = Hex(0x7fe0a0);
            const AZ::Color Violet = Hex(0xc9a0ff);
            auto atext = [&](const Anchor& a, Face face, const AZStd::string& value, float x, float top, float size, const AZ::Color& c,
                             int align = 0, float spacing = 0.0f)
            { return text(face, value, AX(a, x), AY(a, top), size * K, c, align, spacing * K); };
            // A 24 x 24 icon (HudIcons) at screen (x, y), drawn size px.
            auto hudIcon = [&](int index, float x, float y, float size, const AZ::Color& c)
            {
                if (index < 0)
                {
                    return;
                }
                const float u = size / 24.0f;
                const HudIcons::Icon& ic = HudIcons::Icons[index];
                for (int k = 0; k < ic.m_strokes; ++k)
                {
                    const HudIcons::Stroke& st = HudIcons::Strokes[ic.m_firstStroke + k];
                    for (int p = 0; p + 1 < st.m_count; ++p)
                    {
                        const float* a = HudIcons::Points[st.m_first + p];
                        const float* b = HudIcons::Points[st.m_first + p + 1];
                        line(x + a[0] * u, y + a[1] * u, x + b[0] * u, y + b[1] * u, AZStd::max(1.0f, st.m_width * u), c);
                    }
                }
                for (int k = 0; k < ic.m_fills; ++k)
                {
                    const HudIcons::Fill& f = HudIcons::Fills[ic.m_firstFill + k];
                    AZStd::vector<AZ::Vector3> tri;
                    for (int q = 0; q < 12; ++q)
                    {
                        const float a0 = AZ::Constants::TwoPi * q / 12, a1 = AZ::Constants::TwoPi * (q + 1) / 12;
                        tri.push_back(P(x + f.m_x * u, y + f.m_y * u));
                        tri.push_back(P(x + (f.m_x + std::cos(a0) * f.m_r) * u, y + (f.m_y + std::sin(a0) * f.m_r) * u));
                        tri.push_back(P(x + (f.m_x + std::cos(a1) * f.m_r) * u, y + (f.m_y + std::sin(a1) * f.m_r) * u));
                    }
                    triangles(tri, { c });
                }
            };
            const Base::State& kit = Base::Get();
            const float health = mock ? 0.82f : static_cast<float>(AZ::GetClamp(RegNumber("/NoclipProtocol/Hud/player.health", 1.0), 0.0, 1.0));
            const float stamina = mock ? 0.64f : static_cast<float>(AZ::GetClamp(RegNumber("/NoclipProtocol/Hud/player.stamina", 1.0), 0.0, 1.0));
            const float sanity = mock ? 0.71f : static_cast<float>(AZ::GetClamp(RegNumber("/NoclipProtocol/Hud/player.sanity", 1.0), 0.0, 1.0));

            // ---- the visor's corners: angled brackets, gold ticks (full size: they frame the screen) ----
            {
                const AZ::Color c(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, 0.35f);
                const float t = AZStd::max(1.0f, 1.5f * s);
                // top left, top right, bottom left, bottom right: x0 = the edge, then the bracket
                line(L(22), Y(120), L(22), Y(40), t, c); line(L(22), Y(40), L(42), Y(20), t, c); line(L(42), Y(20), L(170), Y(20), t, c);
                line(R(1258), Y(120), R(1258), Y(40), t, c); line(R(1258), Y(40), R(1238), Y(20), t, c); line(R(1238), Y(20), R(1110), Y(20), t, c);
                line(L(22), Y(600), L(22), Y(680), t, c); line(L(22), Y(680), L(42), Y(700), t, c); line(L(42), Y(700), L(170), Y(700), t, c);
                line(R(1258), Y(600), R(1258), Y(680), t, c); line(R(1258), Y(680), R(1238), Y(700), t, c); line(R(1238), Y(700), R(1110), Y(700), t, c);
                const float g = 2.0f * s;
                line(L(32), Y(40), L(40), Y(32), g, Gold); line(R(1248), Y(40), R(1240), Y(32), g, Gold);
                line(L(32), Y(680), L(40), Y(688), g, Gold); line(R(1248), Y(680), R(1240), Y(688), g, Gold);
            }

            // Where the camera looks, and where the base is.
            const AZ::Transform view = camera;
            const AZ::Vector3 eye = view.GetTranslation();
            const AZ::Vector3 ahead = view.GetBasisY();
            float yaw = AZ::RadToDeg(std::atan2(ahead.GetX(), ahead.GetY())); // degrees clockwise from north (+Y)
            const double baseDistance = mock ? 34.0 : RegNumber("/NoclipProtocol/Hud/base.distance", -1.0);
            float baseBearing = 0.0f;
            if (baseDistance >= 0.0)
            {
                const float bx = static_cast<float>(RegNumber("/NoclipProtocol/Hud/base.x", 0.0));
                const float by = static_cast<float>(RegNumber("/NoclipProtocol/Hud/base.y", 0.0));
                baseBearing = AZ::RadToDeg(std::atan2(bx - eye.GetX(), by - eye.GetY()));
            }
            // A room shifting out of sight makes the compass drift for a moment (PlayerEffects).
            const double drift = RegNumber("/NoclipProtocol/Hud/fx.shiftUntil", 0.0) - m_time;
            if (drift > 0.0)
            {
                yaw += 8.0f * std::sin(m_time * 5.0f) * static_cast<float>(AZStd::min(1.0, drift / 3.0));
            }
            if (sanity < 0.35f)
            {
                yaw += (0.35f - sanity) * 40.0f * std::sin(m_time * 0.7f); // the compass lies
            }
            if (mock)
            {
                yaw = 354.0f;
                baseBearing = 336.0f;
            }
            auto compassWord = [](float bearing)
            {
                static const char* const words[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
                const int i = static_cast<int>(std::floor(std::fmod(bearing + 360.0f + 22.5f, 360.0f) / 45.0f)) % 8;
                return words[i];
            };

            // ---- top left: Foundation mark, LEVEL 0, THE LOBBY · UNSTABLE · DEPTH 1, FORWARD BASE · 34 m · NW; the pickup feed ----
            {
                const AZStd::string title = mock ? "LEVEL 0" : RegString("/NoclipProtocol/Hud/level.title", "LEVEL 0");
                const AZStd::string subtitle = mock ? "THE LOBBY · UNSTABLE" : RegString("/NoclipProtocol/Hud/level.subtitle", "THE LOBBY · UNSTABLE");
                const int depth = mock ? 1 : static_cast<int>(RegNumber("/NoclipProtocol/Hud/level.depth", 1.0));
                hudIcon(HudIcons::Icon_foundation, AX(TL, 40), AY(TL, 33), AS(22), Bright);
                atext(TL, Face::Oswald, title, 72, 32, 22.0f, Bright, 0, 3.0f);
                atext(TL, Face::Mono, depth > 0 ? subtitle + AZStd::string::format(" \xc2\xb7 DEPTH %d", depth) : subtitle, 40, 64, 12.0f, Muted, 0, 2.0f);
                if (baseDistance >= 0.0 && (mock || kit.m_beaconUp))
                {
                    diamond(AX(TL, 47), AY(TL, 96), AS(6.0f), Gold, false, AS(1.5f));
                    atext(TL, Face::Mono, AZStd::string::format("FORWARD BASE \xc2\xb7 %.0f m \xc2\xb7 %s", baseDistance, compassWord(baseBearing)), 62, 88, 13.0f, Bright);
                }
                // The feed: what was just picked up (the newest on top, the older one fading).
                const AZStd::string toast = RegString("/NoclipProtocol/Hud/toast", "");
                if (!toast.empty() && toast != m_feedLast)
                {
                    m_feedLast = toast;
                    if (toast[0] == '+' || toast.find("DOCUMENT") != AZStd::string::npos || toast.find("SAMPLE") != AZStd::string::npos)
                    {
                        m_feed.insert(m_feed.begin(), AZStd::pair<AZStd::string, float>(toast, m_time));
                        if (m_feed.size() > 2)
                        {
                            m_feed.pop_back();
                        }
                    }
                }
                if (mock)
                {
                    m_feed.clear();
                    m_feed.push_back({ "+3 SCRAP \xc2\xb7 BACKPACK 14 / 20", m_time });
                    m_feed.push_back({ "FOUNDATION DOCUMENT \xc2\xb7 1 / 3", m_time });
                    m_feedLast.clear();
                }
                float y = 150.0f;
                for (size_t i = 0; i < m_feed.size(); ++i)
                {
                    const float age = m_time - m_feed[i].second;
                    if (age > 8.0f)
                    {
                        continue;
                    }
                    const float alpha = (i == 0 ? 1.0f : 0.7f) * AZStd::min(1.0f, (8.0f - age) / 1.5f);
                    const float w = measure(Face::Mono, m_feed[i].first, 12.0f * K, 0.0f) + AS(20);
                    rect(AX(TL, 40), AY(TL, y), w, AS(24), Hex(0x0a0906, (i == 0 ? 0.55f : 0.4f) * alpha));
                    rect(AX(TL, 40), AY(TL, y), AS(2), AS(24), i == 0 ? Green : Gold);
                    AZ::Color c = Cream;
                    c.SetA(alpha);
                    atext(TL, Face::Mono, m_feed[i].first, 50, y + 5.0f, 12.0f, c);
                    y += 30.0f;
                }
            }

            // ---- top centre: the heading tape (120 degrees across 500 px), the base and squad marks, 354° under it ----
            {
                const float left = 390.0f, width = 500.0f, top = 26.0f;
                const float px = width / 120.0f;
                const int first = static_cast<int>(std::ceil((yaw - 60.0f) / 5.0f)) * 5;
                for (int mark = first; mark <= yaw + 60.0f; mark += 5)
                {
                    const float d = mark - yaw;
                    const int deg = ((mark % 360) + 360) % 360;
                    const bool major = deg % 45 == 0;
                    const float h = major ? 12.0f : (deg % 15 == 0 ? 7.0f : 4.0f);
                    const float x = left + width * 0.5f + d * px;
                    rect(AX(TC, x), AY(TC, top), AZStd::max(1.0f, AS(1)), AS(h), AZ::Color(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, major ? 0.85f : 0.45f));
                    if (major)
                    {
                        const char* name = compassWord(static_cast<float>(deg));
                        const bool north = deg == 0;
                        atext(TC, north ? Face::MonoSemiBold : Face::Mono, name, x, top + 14.0f, 12.0f, north ? Bright : Muted, 1, 1.0f);
                    }
                }
                auto mark = [&](float bearing) -> float
                {
                    float d = std::fmod(bearing - yaw + 540.0f, 360.0f) - 180.0f;
                    return std::fabs(d) > 58.0f ? -1.0f : left + width * 0.5f + d * px;
                };
                if (baseDistance >= 0.0 && (mock || kit.m_beaconUp))
                {
                    const float x = mark(baseBearing);
                    if (x >= 0.0f)
                    {
                        diamond(AX(TC, x), AY(TC, top + 36.0f), AS(6.0f), Gold, true, 0.0f);
                    }
                }
                {
                    const AZStd::string bearings = mock ? "18" : RegString("/NoclipProtocol/Hud/squad.bearings", "");
                    size_t at = 0;
                    while (!bearings.empty() && at < bearings.size())
                    {
                        size_t end = bearings.find(';', at);
                        if (end == AZStd::string::npos)
                        {
                            end = bearings.size();
                        }
                        const float b0 = static_cast<float>(std::atof(bearings.substr(at, end - at).c_str()));
                        const float x = mark(mock ? yaw + b0 : b0);
                        if (x >= 0.0f)
                        {
                            const float tx = AX(TC, x), ty = AY(TC, top + 31.0f), r = AS(5.0f);
                            triangles({ P(tx, ty - r), P(tx + r, ty + r), P(tx - r, ty + r) }, { Green });
                        }
                        at = end + 1;
                    }
                }
                rect(AX(TC, left + width * 0.5f - 0.5f), AY(TC, top), AZStd::max(1.0f, AS(1)), AS(44), Gold);
                const int heading = (static_cast<int>(std::round(yaw)) % 360 + 360) % 360;
                rect(AX(TC, 610), AY(TC, top + 46.0f), AS(60), AS(17), Hex(0x0a0906, 0.55f));
                atext(TC, Face::MonoSemiBold, AZStd::string::format("%03d\xc2\xb0", heading), 640, top + 47.0f, 12.0f, Bright, 1);
            }

            // ---- top right: the order card, the radio, the squad ----
            {
                float y = 32.0f;
                const float x0 = 920.0f, w = 320.0f;
                // The order: before the beacon goes up it's the objective; after, the first open Foundation order.
                AZStd::string head, reward, title, progressText;
                float frac = -1.0f;
                const AZStd::string objective = mock ? "" : RegString("/NoclipProtocol/Hud/game.objective", "");
                int open[3];
                const int openCount = kit.OpenOrders(open);
                if (mock)
                {
                    head = "FOUNDATION ORDER"; reward = "+60 RP"; title = "Recover 3 Foundation documents"; progressText = "1 / 3"; frac = 0.33f;
                }
                else if (!kit.m_beaconUp && !objective.empty())
                {
                    head = "OBJECTIVE";
                    title = objective;
                    progressText = RegString("/NoclipProtocol/Hud/game.progress", "");
                }
                else if (openCount > 0)
                {
                    head = "FOUNDATION ORDER";
                    reward = AZStd::string::format("+%d RP", Base::Order(open[0]).m_rp);
                    title = Base::Order(open[0]).m_title;
                    progressText = kit.OrderProgressText(open[0]);
                    frac = kit.OrderProgress(open[0]);
                }
                if (!title.empty())
                {
                    const AZStd::vector<AZStd::string> lines = wrap(Face::Mono, title, 14.0f * K, AS(w - 31.0f));
                    const AZStd::vector<AZStd::string> notes = (frac < 0.0f && !progressText.empty())
                        ? wrap(Face::Mono, progressText, 12.0f * K, AS(w - 31.0f)) : AZStd::vector<AZStd::string>{};
                    const float h = 10.0f + 13.0f + 6.0f + 20.3f * lines.size() +
                        (progressText.empty() ? 0.0f : 8.0f + (frac >= 0.0f ? 15.6f : 17.0f * notes.size())) + 10.0f;
                    rect(AX(TR, x0), AY(TR, y), AS(w), AS(h), Hex(0x0a0906, 0.62f));
                    rect(AX(TR, x0), AY(TR, y), AS(3), AS(h), Gold);
                    atext(TR, Face::Mono, head, x0 + 17.0f, y + 10.0f, 10.0f, Muted, 0, 2.0f);
                    if (!reward.empty())
                    {
                        atext(TR, Face::Mono, reward, x0 + w - 14.0f, y + 10.0f, 10.0f, Muted, 2, 2.0f);
                    }
                    float ty = y + 10.0f + 13.0f + 6.0f;
                    for (const AZStd::string& l : lines)
                    {
                        atext(TR, Face::Mono, l, x0 + 17.0f, ty, 14.0f, Bright);
                        ty += 20.3f;
                    }
                    if (!progressText.empty())
                    {
                        ty += 8.0f;
                        if (frac >= 0.0f)
                        {
                            const float tw = measure(Face::Mono, progressText, 11.0f * K, 0.0f) / (s * K);
                            const float barW = w - 31.0f - tw - 8.0f;
                            rect(AX(TR, x0 + 17.0f), AY(TR, ty + 6.0f), AS(barW), AS(4), AZ::Color(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, 0.15f));
                            rect(AX(TR, x0 + 17.0f), AY(TR, ty + 6.0f), AS(barW * AZ::GetClamp(frac, 0.0f, 1.0f)), AS(4), Gold);
                            atext(TR, Face::Mono, progressText, x0 + w - 14.0f, ty, 11.0f, Muted, 2);
                        }
                        else
                        {
                            for (const AZStd::string& l : notes)
                            {
                                atext(TR, Face::Mono, l, x0 + 17.0f, ty, 12.0f, Muted);
                                ty += 17.0f;
                            }
                            ty -= 15.6f;
                        }
                        ty += 15.6f;
                    }
                    y = ty + 10.0f + 8.0f;
                }
                // The radio: CH 3 · EPSILON-6, the last message (for a minute and a half).
                const float age = mock ? 4.0f : kit.m_clock - kit.m_radioAt;
                const AZStd::string who = mock ? "HOLLIS" : kit.m_radioWho;
                const AZStd::string said = mock ? "Lights are dying past the east hall. Stay together." : kit.m_radioText;
                if (!said.empty() && age < 90.0f)
                {
                    const AZStd::string lead = who + ": ";
                    const AZStd::vector<AZStd::string> lines = wrap(Face::Mono, lead + said, 12.0f * K, AS(w - 28.0f));
                    const float h = 9.0f + 13.0f + 4.0f + 18.0f * lines.size() + 9.0f;
                    rect(AX(TR, x0), AY(TR, y), AS(w), AS(h), Hex(0x0a0906, 0.5f));
                    hudIcon(HudIcons::Icon_radio, AX(TR, x0 + 14.0f), AY(TR, y + 9.0f), AS(12), Olive);
                    atext(TR, Face::Mono, "CH 3 \xc2\xb7 EPSILON-6", x0 + 30.0f, y + 9.0f, 10.0f, Olive, 0, 2.0f);
                    const int secs = static_cast<int>(age);
                    atext(TR, Face::Mono, AZStd::string::format("%d:%02d AGO", secs / 60, secs % 60), x0 + w - 14.0f, y + 9.0f, 10.0f, Olive, 2, 2.0f);
                    float ty = y + 9.0f + 13.0f + 4.0f;
                    for (size_t i = 0; i < lines.size(); ++i)
                    {
                        AZStd::string l = lines[i];
                        float lx = x0 + 14.0f;
                        if (i == 0 && l.size() >= lead.size() && l.substr(0, lead.size()) == lead)
                        {
                            lx += atext(TR, Face::Mono, who + ":", lx, ty, 12.0f, Green) / (s * K) + 7.2f;
                            l = l.substr(lead.size());
                        }
                        atext(TR, Face::Mono, l, lx, ty, 12.0f, Cream);
                        ty += 18.0f;
                    }
                    y = ty + 9.0f + 8.0f;
                }
                // The squad: "Name|12 m · 94" lines (co-op), NO SIGNAL in red.
                const AZStd::string squad = mock ? "Agt. Hollis|12 m \xc2\xb7 94\nDr. \xe2\x96\x88\xe2\x96\x88\xe2\x96\x88\xe2\x96\x88|41 m \xc2\xb7 60\nAgt. Reyes|NO SIGNAL"
                                                 : RegString("/NoclipProtocol/Hud/squad", "");
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
                    const bool lost = row.find("NO SIGNAL") != AZStd::string::npos;
                    {
                        const float tx = AX(TR, x0 + 8.0f), ty = AY(TR, y + 9.0f), r = AS(4.0f);
                        triangles({ P(tx, ty - r), P(tx + r, ty + r * 0.8f), P(tx - r, ty + r * 0.8f) }, { lost ? Alarm : Green });
                    }
                    // A name with redacted letters (the block character): the font has no block, so each is a cream bar.
                    {
                        const AZStd::string name = row.substr(0, bar);
                        const AZStd::string block = "\xe2\x96\x88";
                        float nx = x0 + 4.0f + 7.2f + 7.2f;
                        size_t p = 0;
                        while (p < name.size())
                        {
                            const size_t hit = name.find(block, p);
                            const AZStd::string part = name.substr(p, hit == AZStd::string::npos ? AZStd::string::npos : hit - p);
                            if (!part.empty())
                            {
                                nx += atext(TR, Face::Mono, part, nx, y, 12.0f, lost ? Alarm : Cream) / (s * K);
                            }
                            if (hit == AZStd::string::npos)
                            {
                                break;
                            }
                            rect(AX(TR, nx), AY(TR, y + 2.0f), AS(7.2f), AS(13.0f), lost ? Alarm : Cream);
                            nx += 7.2f;
                            p = hit + block.size();
                        }
                    }
                    if (bar != AZStd::string::npos)
                    {
                        atext(TR, Face::Mono, row.substr(bar + 1), x0 + w - 4.0f, y, 12.0f, lost ? Alarm : Muted, 2);
                    }
                    y += 19.0f;
                    start = end + 1;
                }
            }

            // ---- centre: the crosshair (a dot and four short arms) and the interaction prompt ----
            {
                const AZ::Color armC(1.0f, 243 / 255.0f, 192 / 255.0f, 0.7f);
                const float cx = C(640), cy = Y(360);
                const float u = s;
                AZStd::vector<AZ::Vector3> dot;
                for (int q = 0; q < 12; ++q)
                {
                    const float a0 = AZ::Constants::TwoPi * q / 12, a1 = AZ::Constants::TwoPi * (q + 1) / 12;
                    dot.push_back(P(cx, cy));
                    dot.push_back(P(cx + std::cos(a0) * 1.6f * u, cy + std::sin(a0) * 1.6f * u));
                    dot.push_back(P(cx + std::cos(a1) * 1.6f * u, cy + std::sin(a1) * 1.6f * u));
                }
                triangles(dot, { Bright });
                const float t = AZStd::max(1.0f, 1.4f * u);
                line(cx, cy - 11.0f * u, cx, cy - 6.0f * u, t, armC); line(cx, cy + 6.0f * u, cx, cy + 11.0f * u, t, armC);
                line(cx - 11.0f * u, cy, cx - 6.0f * u, cy, t, armC); line(cx + 6.0f * u, cy, cx + 11.0f * u, cy, t, armC);

                const AZStd::string verb = mock ? "Pick up" : RegString("/NoclipProtocol/Hud/interact.verb", "");
                AZStd::string item = mock ? "Almond Water \xc2\xb7 backpack 14 / 20" : RegString("/NoclipProtocol/Hud/interact.item", "");
                const AZStd::string key = mock ? "E" : RegString("/NoclipProtocol/Hud/interact.key", "E");
                if (!verb.empty())
                {
                    if (RegString("/NoclipProtocol/Hud/interact.target", "") == "pickup" && kit.Carried() > 0)
                    {
                        item += AZStd::string::format(" \xc2\xb7 backpack %d / %d", kit.Carried(), Base::State::PackMax);
                    }
                    const float verbW = measure(Face::Mono, verb, 13.0f * K, 0.0f) / (s * K);
                    const float rowW = 24.0f + 10.0f + verbW;
                    const float x0 = 640.0f - rowW * 0.5f;
                    outline(AX(MID, x0), AY(MID, 396), AS(24), AS(24), AS(1.5f), Cream);
                    atext(MID, Face::MonoSemiBold, key, x0 + 12.0f, 400.0f, 11.0f, Cream, 1);
                    atext(MID, Face::Mono, verb, x0 + 34.0f, 400.0f, 13.0f, Cream);
                    // Hold to use: a ring around the key fills up (interact.hold 0..1).
                    const float hold = static_cast<float>(RegNumber("/NoclipProtocol/Hud/interact.hold", -1.0));
                    if (hold >= 0.0f)
                    {
                        const float rcx = AX(MID, x0 + 12.0f), rcy = AY(MID, 408.0f), rr = AS(15.0f);
                        for (int q = 0; q < 32; ++q)
                        {
                            const float a0 = -AZ::Constants::HalfPi + AZ::Constants::TwoPi * q / 32, a1 = -AZ::Constants::HalfPi + AZ::Constants::TwoPi * (q + 1) / 32;
                            const bool on = q < hold * 32.0f;
                            line(rcx + std::cos(a0) * rr, rcy + std::sin(a0) * rr, rcx + std::cos(a1) * rr, rcy + std::sin(a1) * rr, AS(2.0f),
                                on ? Gold : AZ::Color(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, 0.2f));
                        }
                    }
                    if (!item.empty())
                    {
                        atext(MID, Face::Mono, item, 640.0f, 428.0f, 12.0f, Muted, 1);
                    }
                }
            }

            // ---- bottom left: the wrist unit (FIELD UNIT · AGT. 4417, the pulse, HEALTH, STAMINA, SANITY, backpack / light / rad) ----
            {
                const float x0 = 40.0f, w = 330.0f, h = 207.0f;
                const float y0 = 720.0f - 36.0f - h;
                // background: linear-gradient(180deg, rgba(28,30,20,.86), rgba(14,15,10,.86)); 1px olive border; gold corner ticks
                triangles({ P(AX(BL, x0), AY(BL, y0)), P(AX(BL, x0 + w), AY(BL, y0)), P(AX(BL, x0 + w), AY(BL, y0 + h)),
                            P(AX(BL, x0), AY(BL, y0)), P(AX(BL, x0 + w), AY(BL, y0 + h)), P(AX(BL, x0), AY(BL, y0 + h)) },
                    { Hex(0x1c1e14, 0.86f), Hex(0x1c1e14, 0.86f), Hex(0x0e0f0a, 0.86f), Hex(0x1c1e14, 0.86f), Hex(0x0e0f0a, 0.86f), Hex(0x0e0f0a, 0.86f) });
                outline(AX(BL, x0), AY(BL, y0), AS(w), AS(h), AZStd::max(1.0f, AS(1)), AZ::Color(154 / 255.0f, 163 / 255.0f, 106 / 255.0f, 0.55f));
                rect(AX(BL, x0 - 1), AY(BL, y0 - 1), AS(14), AS(2), Gold); rect(AX(BL, x0 - 1), AY(BL, y0 - 1), AS(2), AS(14), Gold);
                rect(AX(BL, x0 + w - 13), AY(BL, y0 + h - 1), AS(14), AS(2), Gold); rect(AX(BL, x0 + w - 1), AY(BL, y0 + h - 13), AS(2), AS(14), Gold);
                const float px0 = x0 + 14.0f;
                float y = y0 + 12.0f;
                hudIcon(HudIcons::Icon_foundation, AX(BL, px0), AY(BL, y), AS(14), Olive);
                atext(BL, Face::Mono, "FIELD UNIT \xc2\xb7 AGT. 4417", px0 + 20.0f, y + 1.0f, 10.0f, Olive, 0, 2.0f);
                {
                    char clock[16] = {};
                    const std::time_t now = std::time(nullptr);
                    if (const std::tm* local = std::localtime(&now))
                    {
                        std::strftime(clock, sizeof(clock), "%H:%M", local);
                    }
                    atext(BL, Face::Mono, mock ? "21:42" : clock, x0 + w - 14.0f, y + 1.0f, 10.0f, Olive, 2, 2.0f);
                }
                y += 13.0f + 10.0f;
                // The pulse: an ECG trace that scrolls with the heartbeat (faster when hurt, tired or frightened).
                const float bpm = mock ? 88.0f : 66.0f + (1.0f - stamina) * 40.0f + (1.0f - sanity) * 35.0f + (1.0f - health) * 45.0f;
                const AZ::Color pulse = health < 0.3f ? Alarm : Green;
                {
                    const float traceW = 236.0f, traceH = 32.0f;
                    const float period = 70.0f; // design px between beats
                    const float shift = std::fmod(m_time * bpm / 60.0f * period, period);
                    auto beat = [&](float u) -> float // height 0..32 at u px into a period (the mockup's shape)
                    {
                        if (u < 6.0f) return 18.0f;
                        if (u < 10.0f) return 18.0f - (u - 6.0f) * 3.0f;      // up to 6
                        if (u < 14.0f) return 6.0f + (u - 10.0f) * 5.5f;      // down to 28
                        if (u < 18.0f) return 28.0f - (u - 14.0f) * 3.5f;     // up to 14
                        if (u < 22.0f) return 14.0f + (u - 18.0f);            // back to 18
                        return 18.0f;
                    };
                    float lastX = px0, lastY = y + beat(std::fmod(shift, period));
                    for (float u = 1.0f; u <= traceW; u += 1.0f)
                    {
                        const float v = beat(std::fmod(u + shift, period));
                        const float nx = px0 + u, ny = y + v;
                        line(AX(BL, lastX), AY(BL, lastY), AX(BL, nx), AY(BL, ny), AS(1.6f), pulse);
                        lastX = nx;
                        lastY = ny;
                    }
                    atext(BL, Face::MonoSemiBold, AZStd::string::format("%d", static_cast<int>(bpm)), x0 + w - 14.0f, y, 18.0f, pulse, 2);
                    atext(BL, Face::Mono, "BPM", x0 + w - 14.0f, y + 20.0f, 9.0f, Muted, 2, 2.0f);
                    y += traceH + 10.0f;
                }
                // HEALTH: ten segments
                atext(BL, Face::Mono, "HEALTH", px0, y, 11.0f, Cream, 0, 2.0f);
                atext(BL, Face::MonoSemiBold, AZStd::string::format("%d", static_cast<int>(std::round(health * 100))), x0 + w - 14.0f, y, 11.0f, Cream, 2);
                y += 14.0f + 5.0f;
                {
                    const float segW = (w - 28.0f - 27.0f) / 10.0f;
                    const int lit = static_cast<int>(std::round(health * 10.0f));
                    for (int k = 0; k < 10; ++k)
                    {
                        rect(AX(BL, px0 + k * (segW + 3.0f)), AY(BL, y), AS(segW), AS(10),
                            k < lit ? (health < 0.3f ? Alarm : Cream) : AZ::Color(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, 0.15f));
                    }
                }
                y += 10.0f + 10.0f;
                atext(BL, Face::Mono, "STAMINA", px0, y, 11.0f, Cream, 0, 2.0f);
                atext(BL, Face::MonoSemiBold, AZStd::string::format("%d", static_cast<int>(std::round(stamina * 100))), x0 + w - 14.0f, y, 11.0f, Cream, 2);
                y += 14.0f + 5.0f;
                rect(AX(BL, px0), AY(BL, y), AS(w - 28.0f), AS(5), AZ::Color(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, 0.15f));
                rect(AX(BL, px0), AY(BL, y), AS((w - 28.0f) * stamina), AS(5), Gold);
                y += 5.0f + 10.0f;
                // SANITY: STEADY · 71 (UNEASY, SLIPPING, BREAKING), a tick at the halfway mark (below it the rooms start to shift)
                const char* word = sanity >= 0.6f ? "STEADY" : (sanity >= 0.4f ? "UNEASY" : (sanity >= 0.2f ? "SLIPPING" : "BREAKING"));
                const AZ::Color sc = sanity >= 0.6f ? Green : (sanity >= 0.4f ? Gold : (sanity >= 0.2f ? Violet : Alarm));
                atext(BL, Face::Mono, "SANITY", px0, y, 11.0f, Cream, 0, 2.0f);
                atext(BL, Face::MonoSemiBold, AZStd::string::format("%s \xc2\xb7 %d", word, static_cast<int>(std::round(sanity * 100))), x0 + w - 14.0f, y, 11.0f, sc, 2, 2.0f);
                y += 14.0f + 5.0f;
                rect(AX(BL, px0), AY(BL, y), AS(w - 28.0f), AS(5), AZ::Color(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, 0.15f));
                rect(AX(BL, px0), AY(BL, y), AS((w - 28.0f) * sanity), AS(5), sc);
                rect(AX(BL, px0 + (w - 28.0f) * 0.5f), AY(BL, y - 3.0f), AZStd::max(1.0f, AS(1)), AS(11), AZ::Color(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, 0.6f));
                y += 5.0f + 10.0f;
                // BACKPACK 14 / 20 · LIGHT 3:00 · RAD 0.1
                const double lightSecs = mock ? 180.0 : RegNumber("/NoclipProtocol/Hud/player.flashlight.seconds", 180.0);
                const int ls = static_cast<int>(std::round(lightSecs));
                const int style = static_cast<int>(RegNumber("/NoclipProtocol/Hud/level.depth", 1.0)) - 1;
                const float rad = style <= 0 ? 0.1f : (style == 1 ? 0.2f : 0.6f);
                atext(BL, Face::Mono, AZStd::string::format("BACKPACK %d / %d", mock ? 14 : kit.Carried(), Base::State::PackMax), px0, y, 10.0f, Muted, 0, 1.0f);
                atext(BL, Face::Mono, AZStd::string::format("LIGHT %d:%02d", ls / 60, ls % 60), x0 + w * 0.5f + 12.0f, y, 10.0f, ls < 30 ? Alarm : Muted, 1, 1.0f);
                atext(BL, Face::Mono, AZStd::string::format("RAD %.1f", rad), x0 + w - 14.0f, y, 10.0f, Muted, 2, 1.0f);
            }

            // ---- bottom right: the weapon block (above) and the quick slots Q / B / G / H ----
            {
                const AZStd::string weapon = mock ? "SERVICE PISTOL" : RegString("/NoclipProtocol/Hud/weapon.name", "");
                if (!weapon.empty())
                {
                    // right 40, bottom 104, 300 wide, padding 12 14: mode + icon, the name, mag / reserve, the magazine's pips, the state line
                    // (PlayerWeapons: a melee weapon or the shield has no rounds, just its keys under the name).
                    const bool melee = !mock && RegString("/NoclipProtocol/Hud/weapon.kind", "gun") == "melee";
                    const int mag = mock ? 9 : static_cast<int>(RegNumber("/NoclipProtocol/Hud/weapon.mag", 0.0));
                    const int cap = mock ? 12 : AZ::GetClamp(static_cast<int>(RegNumber("/NoclipProtocol/Hud/weapon.cap", 1.0)), 1, 40);
                    const int reserve = mock ? 36 : static_cast<int>(RegNumber("/NoclipProtocol/Hud/weapon.reserve", 0.0));
                    const AZStd::string mode = mock ? "SEMI" : RegString("/NoclipProtocol/Hud/weapon.mode", "SEMI");
                    const AZStd::string iconName = mock ? "pistol" : RegString("/NoclipProtocol/Hud/weapon.icon", "pistol");
                    const AZStd::string roundsName = mock ? "rounds" : RegString("/NoclipProtocol/Hud/weapon.rounds", "rounds");
                    const AZStd::string state = mock ? "" : RegString("/NoclipProtocol/Hud/weapon.state", "");
                    const AZStd::string extra = mock ? "" : RegString("/NoclipProtocol/Hud/weapon.note", "");
                    const float reload = mock ? -1.0f : static_cast<float>(RegNumber("/NoclipProtocol/Hud/weapon.reload", -1.0));
                    const bool low = !mock && RegNumber("/NoclipProtocol/Hud/weapon.low", 0.0) > 0.5;
                    const float right = 1240.0f, w = 300.0f;
                    const float h = melee ? 12.0f + 30.0f + 21.0f + 4.0f + 16.0f + 12.0f
                                          : 12.0f + 30.0f + 2.0f + 21.0f + 4.0f + 40.0f + 6.0f + 16.0f + (state.empty() ? 0.0f : 6.0f + 15.0f) +
                                                (extra.empty() ? 0.0f : 6.0f + 15.0f) + 12.0f;
                    const float bottom = 720.0f - 104.0f, top = bottom - h, x0 = right - w;
                    // linear-gradient(270deg, .72 at the right to .2 at the left); border-right 3px gold
                    const AZ::Color dark = Hex(0x0a0906, 0.72f), light = Hex(0x0a0906, 0.2f);
                    triangles({ P(AX(BR, x0), AY(BR, top)), P(AX(BR, right), AY(BR, top)), P(AX(BR, right), AY(BR, bottom)),
                                P(AX(BR, x0), AY(BR, top)), P(AX(BR, right), AY(BR, bottom)), P(AX(BR, x0), AY(BR, bottom)) },
                        { light, dark, dark, light, dark, light });
                    rect(AX(BR, right - 3.0f), AY(BR, top), AS(3), AS(h), Gold);
                    const float px = right - 3.0f - 14.0f;
                    float y = top + 12.0f;
                    hudIcon(HudIcons::Find(iconName.c_str()), AX(BR, px - 30.0f), AY(BR, y), AS(30), Cream);
                    atext(BR, Face::Mono, mode, px - 40.0f, y + 8.0f, 11.0f, Muted, 2, 2.0f);
                    y += 30.0f + (melee ? 0.0f : 2.0f);
                    atext(BR, Face::Oswald, weapon, px, y, 16.0f, Cream, 2, 3.0f);
                    y += 21.0f + 4.0f;
                    if (melee)
                    {
                        atext(BR, Face::Mono, extra, px, y, 12.0f, state.empty() ? Muted : Gold, 2);
                    }
                    else
                    {
                        hudIcon(HudIcons::Find(roundsName.c_str()), AX(BR, px - 18.0f), AY(BR, y + 20.0f), AS(18), Muted);
                        const float resW = atext(BR, Face::Mono, AZStd::string::format("/ %d", reserve), px - 18.0f - 8.0f, y + 19.0f, 16.0f, Muted, 2) / (s * K);
                        atext(BR, Face::Oswald, AZStd::string::format("%d", mag), px - 18.0f - 8.0f - resW - 8.0f, y - 4.0f, 40.0f, low ? Alarm : Bright, 2);
                        y += 40.0f + 6.0f;
                        for (int k = 0; k < cap; ++k)
                        {
                            const float x = px - (cap - k) * 6.0f - (cap - 1 - k) * 3.0f;
                            rect(AX(BR, x), AY(BR, y), AS(6), AS(16), k < mag ? (low ? Alarm : Cream) : AZ::Color(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, 0.15f));
                        }
                        y += 16.0f;
                        if (!state.empty())
                        {
                            // RELOADING then five bars filling up (the mockup's ▮▮▮▯▯; the font has no such glyph, so they're drawn).
                            float sx = px;
                            if (reload >= 0.0f)
                            {
                                const int filled = AZ::GetClamp(static_cast<int>(reload * 5.0f + 0.5f), 0, 5);
                                for (int k = 4; k >= 0; --k)
                                {
                                    sx -= 7.0f;
                                    rect(AX(BR, sx), AY(BR, y + 7.0f), AS(5), AS(11), k < filled ? Gold : AZ::Color(240 / 255.0f, 207 / 255.0f, 90 / 255.0f, 0.25f));
                                    sx -= 1.5f;
                                }
                                sx -= 6.0f;
                            }
                            atext(BR, Face::Mono, state, sx, y + 6.0f, 11.0f, Gold, 2, 2.0f);
                            y += 21.0f;
                        }
                        if (!extra.empty())
                        {
                            atext(BR, Face::Mono, extra, px, y + 6.0f, 11.0f, Muted, 2);
                        }
                    }
                }
                struct Slot { int icon; int count; const char* key; };
                const Slot slots[4] = { { HudIcons::Icon_water, mock ? 5 : kit.m_almondWater, "Q" },
                                        { HudIcons::Icon_battery, mock ? 2 : kit.m_batteries, "B" },
                                        { HudIcons::Icon_flare, mock ? 3 : kit.GearCount("Flares"), "G" },
                                        { HudIcons::Icon_medkit, mock ? 1 : kit.GearCount("Medkit"), "H" } };
                for (int k = 0; k < 4; ++k)
                {
                    const float x = 1240.0f - 52.0f - (3 - k) * 58.0f;
                    const float y = 720.0f - 36.0f - 52.0f;
                    rect(AX(BR, x), AY(BR, y), AS(52), AS(52), Hex(0x0a0906, 0.62f));
                    outline(AX(BR, x), AY(BR, y), AS(52), AS(52), AZStd::max(1.0f, AS(1)), AZ::Color(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, 0.25f));
                    hudIcon(slots[k].icon, AX(BR, x + 14.0f), AY(BR, y + 14.0f), AS(24), slots[k].count > 0 ? Cream : Muted);
                    atext(BR, Face::MonoSemiBold, AZStd::string::format("%d", slots[k].count), x + 48.0f, y + 36.0f, 11.0f, slots[k].count > 0 ? Cream : Muted, 2);
                    const float kw = measure(Face::MonoSemiBold, slots[k].key, 10.0f * K, 0.0f) + AS(8);
                    for (int layer = 0; layer < 4; ++layer)
                    {
                        rect(AX(BR, x - 1.0f), AY(BR, y - 1.0f), kw, AS(14), Hex(0xf0cf5a, 0.5f));
                    }
                    atext(BR, Face::MonoSemiBold, slots[k].key, x + 3.0f, y, 10.0f, Hex(0x0b0a07));
                }
            }

            // ---- the weapon wheel (PlayerWeapons): hold [C]; six slices clockwise from the top, the one you point at in gold ----
            if (Weapons::Get().m_wheelOpen || static_cast<int>(hud_wheel) >= 0)
            {
                const Weapons::State& arms = Weapons::Get();
                const int pick = static_cast<int>(hud_wheel) >= 0 ? Weapons::WheelOrder[static_cast<int>(hud_wheel) % Weapons::Count] : arms.m_wheelPick;
                const float cx = 640.0f, cy = 360.0f; // the svg's 210, 210 at left 430, top 150
                auto disc = [&](float r0, float r1, float a0, float a1, const AZ::Color& c, int steps)
                {
                    AZStd::vector<AZ::Vector3> tri;
                    for (int q = 0; q < steps; ++q)
                    {
                        const float b0 = a0 + (a1 - a0) * q / steps, b1 = a0 + (a1 - a0) * (q + 1) / steps;
                        const AZ::Vector3 i0 = P(C(cx + std::cos(b0) * r0), Y(cy + std::sin(b0) * r0)), i1 = P(C(cx + std::cos(b1) * r0), Y(cy + std::sin(b1) * r0));
                        const AZ::Vector3 o0 = P(C(cx + std::cos(b0) * r1), Y(cy + std::sin(b0) * r1)), o1 = P(C(cx + std::cos(b1) * r1), Y(cy + std::sin(b1) * r1));
                        tri.insert(tri.end(), { i0, o0, o1, i0, o1, i1 });
                    }
                    triangles(tri, { c });
                };
                auto ring = [&](float r, const AZ::Color& c)
                {
                    for (int q = 0; q < 72; ++q)
                    {
                        const float b0 = AZ::Constants::TwoPi * q / 72, b1 = AZ::Constants::TwoPi * (q + 1) / 72;
                        line(C(cx + std::cos(b0) * r), Y(cy + std::sin(b0) * r), C(cx + std::cos(b1) * r), Y(cy + std::sin(b1) * r), AZStd::max(1.0f, s), c);
                    }
                };
                const AZ::Color edge(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, 0.3f);
                disc(70.0f, 190.0f, 0.0f, AZ::Constants::TwoPi, Hex(0x0a0906, 0.72f), 72);
                disc(0.0f, 70.0f, 0.0f, AZ::Constants::TwoPi, Hex(0x0a0906, 0.85f), 48);
                ring(190.0f, edge);
                ring(70.0f, edge);
                const AZ::Color dim = Hex(0x8a8168);
                for (int k = 0; k < Weapons::Count; ++k)
                {
                    const int id = Weapons::WheelOrder[k];
                    const float a0 = AZ::DegToRad(-90.0f - 30.0f + k * 60.0f), a1 = a0 + AZ::DegToRad(60.0f);
                    const bool sel = id == pick;
                    if (sel)
                    {
                        disc(72.0f, 188.0f, a0, a1, AZ::Color(240 / 255.0f, 207 / 255.0f, 90 / 255.0f, 0.22f), 16);
                    }
                    const AZ::Color sep(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, 0.2f);
                    line(C(cx + std::cos(a0) * 72.0f), Y(cy + std::sin(a0) * 72.0f), C(cx + std::cos(a0) * 188.0f), Y(cy + std::sin(a0) * 188.0f), AZStd::max(1.0f, s), sep);
                    const bool locked = Weapons::State::Locked(id);
                    const bool owned = Weapons::State::Owned(id) || (mock && id != Weapons::Carbine);
                    const Weapons::Def& d = Weapons::Info(id);
                    const float am = (a0 + a1) * 0.5f;
                    const float ix = cx + 130.0f * std::cos(am), iy = cy + 130.0f * std::sin(am);
                    const AZ::Color col = sel ? Gold : ((locked || !owned) ? dim : Cream);
                    hudIcon(HudIcons::Find(locked ? "lock" : d.m_icon), C(ix - 16.0f), Y(iy - 26.0f), 32.0f * s, col);
                    text(Face::Mono, d.m_wheel, C(ix), Y(iy + 22.0f - 11.0f), 11.0f, col, 1, 2.0f);
                    AZStd::string sub;
                    if (locked) { sub = "Weapons I"; }
                    else if (!owned) { sub = "not issued"; }
                    else if (Weapons::State::UsesAmmo(id))
                    {
                        sub = mock ? (id == Weapons::Shotgun ? "2 / 18" : (id == Weapons::Pistol ? "9 / 36" : "1 / 3"))
                                   : AZStd::string::format("%d / %d", Weapons::State::Mag(id), Weapons::State::Reserve(id));
                    }
                    if (!sub.empty())
                    {
                        text(Face::Mono, sub, C(ix), Y(iy + 37.0f - 10.0f), 10.0f, Muted, 1);
                    }
                }
                const AZStd::string name = pick >= 0 ? AZStd::string(Weapons::Info(pick).m_wheel) : AZStd::string("HOLSTER");
                text(Face::Oswald, name, C(cx), Y(cy - 5.0f - 17.0f), 18.0f, Bright, 1, 3.0f);
                text(Face::Mono, "release to equip", C(cx), Y(cy + 16.0f - 11.0f), 11.0f, Muted, 1);
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

        // Menu pages' shapes: collected into one mesh and laid down in several lighter layers. Text drawn over shapes
        // whose alpha is much above 0.5 comes out in white boxes (the engine's text blending goes by what's already on
        // screen), and separate see-through batches can come out in any order, so each page is one mesh, drawn in order.
        AZStd::vector<AZ::Vector3> lv;
        AZStd::vector<AZ::Color> lc;
        auto lquad = [&](float x0, float y0, float x1, float y1, const AZ::Color& c0, const AZ::Color& c1)
        {
            // c0 on the left edge, c1 on the right
            const AZ::Vector3 a = P(x0, y0), b = P(x1, y0), c = P(x1, y1), d = P(x0, y1);
            lv.push_back(a); lc.push_back(c0);
            lv.push_back(b); lc.push_back(c1);
            lv.push_back(c); lc.push_back(c1);
            lv.push_back(a); lc.push_back(c0);
            lv.push_back(c); lc.push_back(c1);
            lv.push_back(d); lc.push_back(c0);
        };
        auto lrect = [&](float x, float y, float w, float h, const AZ::Color& c) { lquad(x, y, x + w, y + h, c, c); };
        auto loutline = [&](float x, float y, float w, float h, float t, const AZ::Color& c)
        {
            lrect(x, y, w, t, c);
            lrect(x, y + h - t, w, t, c);
            lrect(x, y, t, h, c);
            lrect(x + w - t, y, t, h, c);
        };
        auto lflush = [&](int layers)
        {
            if (lv.empty())
            {
                return;
            }
            for (AZ::Color& c : lc)
            {
                // At most half alpha a layer (solid shapes too: they come out 1 - 0.5^layers, nearly solid).
                c.SetA(AZStd::min(0.5f, 1.0f - std::pow(AZStd::max(0.0f, 1.0f - c.GetA()), 1.0f / static_cast<float>(layers))));
            }
            for (int layer = 0; layer < layers; ++layer)
            {
                AZ::RPI::AuxGeomDraw::AuxGeomDynamicDrawArguments args;
                args.m_verts = lv.data();
                args.m_vertCount = static_cast<uint32_t>(lv.size());
                args.m_colors = lc.data();
                args.m_colorCount = static_cast<uint32_t>(lc.size());
                args.m_opacityType = AZ::RPI::AuxGeomDraw::OpacityType::Translucent;
                args.m_depthTest = AZ::RPI::AuxGeomDraw::DepthTest::Off;
                args.m_depthWrite = AZ::RPI::AuxGeomDraw::DepthWrite::Off;
                args.m_viewProjectionOverrideIndex = overlay;
                draw->DrawTriangles(args);
            }
            lv.clear();
            lc.clear();
        };

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
                const TermFile& file = files[AZ::GetClamp(m_terminalFile, 0, static_cast<int>(files.size()) - 1)];
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
            for (int layer = 0; layer < AZ::GetClamp(static_cast<int>(hud_term_layers), 1, 8); ++layer)
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
        // ---- build mode: Build*.dc.html, six tabs (the game keeps running; aim at the floor to place) ----
        else if (m_page == Page::Build)
        {
            Base::State& base = Base::Get();
            m_buildTab = AZ::GetClamp(m_buildTab, 0, Base::TabCount - 1);
            m_buildTile = AZ::GetClamp(m_buildTile, 0, 5);
            const Base::Kind kind = Base::KindAt(m_buildTab, m_buildTile);
            const Base::ItemInfo& info = Base::Info(kind);
            const bool locked = base.Locked(kind);
            const AZ::Color cream = Hex(0xf3ead0);
            const AZ::Color soft = Hex(0xc9bf9a);
            const AZ::Color gold = Hex(0xf0cf5a);
            const AZ::Color ok = Hex(0x7fe0a0);
            const AZ::Color okText = Hex(0xb8f5cc);
            const AZ::Color bad = Hex(0xf08a6a);
            const AZ::Color badText = Hex(0xffc4b0);
            const AZ::Color grey = Hex(0x8a8168);
            auto textIn = [&](Face face, const AZStd::string& value, float x, float top, float size, float lineHeight,
                              const AZ::Color& c, int align = 0, float spacing = 0.0f)
            { return text(face, value, x, Y(top + (lineHeight - 1.2f) * size * 0.5f), size, c, align, spacing, true); };
            auto lline = [&](float x0, float y0, float x1, float y1, float t, const AZ::Color& c)
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
                const AZ::Vector3 a = P(x0 + nx, y0 + ny), b = P(x1 + nx, y1 + ny), c2 = P(x1 - nx, y1 - ny), d = P(x0 - nx, y0 - ny);
                for (const AZ::Vector3& v : { a, b, c2, a, c2, d })
                {
                    lv.push_back(v);
                    lc.push_back(c);
                }
            };
            auto say = [&](const AZStd::string& message)
            {
                if (auto* registry = AZ::SettingsRegistry::Get())
                {
                    registry->Set("/NoclipProtocol/Hud/toast", message);
                    registry->Set("/NoclipProtocol/Hud/toast.until", static_cast<double>(m_time) + 3.0);
                }
            };

            // Aim: where the view meets the floor, within 8 m, on a half-metre grid.
            const AZ::Vector3 eye = camera.GetTranslation();
            const AZ::Vector3 forward = camera.GetBasisY().GetNormalized();
            bool aimed = false;
            float ax = 0.0f, ay = 0.0f;
            if (forward.GetZ() < -0.05f)
            {
                const float t = -eye.GetZ() / forward.GetZ();
                if (t < 8.0f)
                {
                    ax = std::round((eye.GetX() + forward.GetX() * t) * 2.0f) * 0.5f;
                    ay = std::round((eye.GetY() + forward.GetY() * t) * 2.0f) * 0.5f;
                    aimed = true;
                }
            }
            const bool project = info.m_project;
            const char* blocked = project ? nullptr : (aimed ? base.Blocked(kind, ax, ay, m_buildYaw) : "aim at the floor");
            if (m_demolish)
            {
                m_demolish = false;
                if (aimed)
                {
                    base.m_requests.push_back(Base::Request{ false, kind, ax, ay, 0.0f });
                }
            }
            if (m_click)
            {
                m_click = false;
                if (locked)
                {
                    say(AZStd::string(info.m_upper) + " \xc2\xb7 LOCKED \xc2\xb7 " + info.m_lockNote);
                }
                else if (project)
                {
                    say(base.Study(kind == Base::Kind::StudyPowerII ? 0 : 1));
                }
                else if (!blocked)
                {
                    base.m_requests.push_back(Base::Request{ true, kind, ax, ay, m_buildYaw });
                }
            }

            // The piece itself, see-through (green when it fits, red when it doesn't), and its footprint on the floor.
            if (aimed && !project)
            {
                const AZ::Color edge = blocked ? bad : ok;
                const float c0 = std::cos(m_buildYaw), s0 = std::sin(m_buildYaw);
                const AZ::Quaternion turn = AZ::Quaternion::CreateRotationZ(m_buildYaw);
                const AZ::Vector3 origin(ax, ay, 0.0f);
                AZStd::vector<AZ::Vector3> faces;
                AZStd::vector<AZ::Vector3> edges;
                for (int k = 0; k < info.m_parts; ++k)
                {
                    const Base::Part& part = Base::Parts[info.m_firstPart + k];
                    const AZ::Quaternion rot = turn * AZ::Quaternion::CreateRotationZ(AZ::DegToRad(part.m_rz)) *
                        AZ::Quaternion::CreateRotationY(AZ::DegToRad(part.m_ry)) * AZ::Quaternion::CreateRotationX(AZ::DegToRad(part.m_rx));
                    const AZ::Vector3 centre = origin + turn.TransformVector(AZ::Vector3(part.m_x, part.m_y, part.m_z));
                    auto at = [&](float lx, float ly, float lz) { return centre + rot.TransformVector(AZ::Vector3(lx, ly, lz)); };
                    const float hx = part.m_sx * 0.5f, hy = part.m_sy * 0.5f, hz = part.m_sz * 0.5f;
                    if (!part.m_cylinder)
                    {
                        AZ::Vector3 v[8];
                        for (int c = 0; c < 8; ++c)
                        {
                            v[c] = at((c & 1) ? hx : -hx, (c & 2) ? hy : -hy, (c & 4) ? hz : -hz);
                        }
                        static const int quads[6][4] = { { 0, 1, 3, 2 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 3, 7, 6 }, { 0, 2, 6, 4 }, { 1, 3, 7, 5 } };
                        for (const auto& q : quads)
                        {
                            for (int idx : { q[0], q[1], q[2], q[0], q[2], q[3] })
                            {
                                faces.push_back(v[idx]);
                            }
                        }
                        static const int lines[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
                        for (const auto& l : lines)
                        {
                            edges.push_back(v[l[0]]);
                            edges.push_back(v[l[1]]);
                        }
                    }
                    else
                    {
                        const int n = 12;
                        for (int k2 = 0; k2 < n; ++k2)
                        {
                            const float a0 = 6.2831853f * k2 / n, a1 = 6.2831853f * (k2 + 1) / n;
                            const AZ::Vector3 b0 = at(std::cos(a0) * hx, std::sin(a0) * hy, -hz), b1 = at(std::cos(a1) * hx, std::sin(a1) * hy, -hz);
                            const AZ::Vector3 t0 = at(std::cos(a0) * hx, std::sin(a0) * hy, hz), t1 = at(std::cos(a1) * hx, std::sin(a1) * hy, hz);
                            const AZ::Vector3 bc = at(0.0f, 0.0f, -hz), tc2 = at(0.0f, 0.0f, hz);
                            for (const AZ::Vector3& p : { b0, b1, t1, b0, t1, t0, bc, b0, b1, tc2, t1, t0 })
                            {
                                faces.push_back(p);
                            }
                            edges.push_back(b0); edges.push_back(b1);
                            edges.push_back(t0); edges.push_back(t1);
                            if (k2 % 3 == 0)
                            {
                                edges.push_back(b0); edges.push_back(t0);
                            }
                        }
                    }
                }
                // The footprint (its size turned with the piece).
                const float hx = info.m_sizeX * 0.5f, hy = info.m_sizeY * 0.5f;
                auto corner = [&](float px, float py) { return AZ::Vector3(ax + px * c0 - py * s0, ay + px * s0 + py * c0, 0.02f); };
                const AZ::Vector3 k[4] = { corner(-hx, -hy), corner(hx, -hy), corner(hx, hy), corner(-hx, hy) };
                AZ::Color fill = edge;
                fill.SetA(0.22f);
                {
                    const AZ::Vector3 tri[6] = { k[0], k[1], k[2], k[0], k[2], k[3] };
                    AZ::RPI::AuxGeomDraw::AuxGeomDynamicDrawArguments args;
                    args.m_verts = tri;
                    args.m_vertCount = 6;
                    args.m_colors = &fill;
                    args.m_colorCount = 1;
                    args.m_opacityType = AZ::RPI::AuxGeomDraw::OpacityType::Translucent;
                    args.m_depthTest = AZ::RPI::AuxGeomDraw::DepthTest::On;
                    args.m_depthWrite = AZ::RPI::AuxGeomDraw::DepthWrite::Off;
                    draw->DrawTriangles(args);
                }
                if (!faces.empty())
                {
                    AZ::Color body = edge;
                    body.SetA(0.16f); // the mockup's fill-opacity .13 over a dark room
                    AZ::RPI::AuxGeomDraw::AuxGeomDynamicDrawArguments args;
                    args.m_verts = faces.data();
                    args.m_vertCount = static_cast<uint32_t>(faces.size());
                    args.m_colors = &body;
                    args.m_colorCount = 1;
                    args.m_opacityType = AZ::RPI::AuxGeomDraw::OpacityType::Translucent;
                    args.m_depthTest = AZ::RPI::AuxGeomDraw::DepthTest::On;
                    args.m_depthWrite = AZ::RPI::AuxGeomDraw::DepthWrite::Off;
                    draw->DrawTriangles(args);
                }
                AZStd::vector<AZ::Vector3> dashes;
                for (int e = 0; e < 4; ++e)
                {
                    const AZ::Vector3 a = k[e];
                    const AZ::Vector3 b = k[(e + 1) % 4];
                    const float len = (b - a).GetLength();
                    for (float d = 0.0f; d < len; d += 0.16f)
                    {
                        dashes.push_back(a + (b - a) * (d / len));
                        dashes.push_back(a + (b - a) * (AZStd::min(len, d + 0.09f) / len));
                    }
                }
                for (const AZStd::vector<AZ::Vector3>* list : { &dashes, &edges })
                {
                    if (list->empty())
                    {
                        continue;
                    }
                    AZ::Color lineColour = edge;
                    lineColour.SetA(list == &edges ? 0.85f : 1.0f);
                    AZ::RPI::AuxGeomDraw::AuxGeomDynamicDrawArguments lines;
                    lines.m_verts = list->data();
                    lines.m_vertCount = static_cast<uint32_t>(list->size());
                    lines.m_colors = &lineColour;
                    lines.m_colorCount = 1;
                    lines.m_size = static_cast<uint8_t>(AZStd::max(1.0f, std::round((list == &edges ? 1.2f : 1.5f) * s)));
                    lines.m_opacityType = list == &edges ? AZ::RPI::AuxGeomDraw::OpacityType::Translucent : AZ::RPI::AuxGeomDraw::OpacityType::Opaque;
                    lines.m_depthTest = list == &edges ? AZ::RPI::AuxGeomDraw::DepthTest::On : AZ::RPI::AuxGeomDraw::DepthTest::Off;
                    draw->DrawLines(lines);
                }

                // The label just under the footprint's near edge: a check mark (or a cross) and "BARRICADE · north doorway · covers it".
                if (cameraView)
                {
                    float sx[4], sy[4];
                    bool visible = true;
                    for (int c = 0; c < 4; ++c)
                    {
                        const AZ::Vector4 clip = cameraView->GetWorldToClipMatrix() * AZ::Vector4(k[c].GetX(), k[c].GetY(), 0.0f, 1.0f);
                        if (clip.GetW() <= 0.01f)
                        {
                            visible = false;
                            break;
                        }
                        sx[c] = (clip.GetX() / clip.GetW() * 0.5f + 0.5f) * W;
                        sy[c] = (1.0f - (clip.GetY() / clip.GetW() * 0.5f + 0.5f)) * H;
                    }
                    if (visible)
                    {
                        // The two nearest corners (lowest on screen): the label starts at the left one.
                        int n0 = 0;
                        for (int c = 1; c < 4; ++c) { n0 = sy[c] > sy[n0] ? c : n0; }
                        int n1 = n0 == 0 ? 1 : 0;
                        for (int c = 0; c < 4; ++c) { n1 = c != n0 && sy[c] > sy[n1] ? c : n1; }
                        const char* note = blocked ? nullptr : base.PlaceNote(kind, ax, ay, m_buildYaw);
                        const AZStd::string label = AZStd::string(info.m_upper) + " \xc2\xb7 " +
                            (blocked ? AZStd::string(blocked) : (note ? AZStd::string(note) : AZStd::string("placement OK \xc2\xb7 [R] rotate")));
                        const float labelW = 20.0f * s + measure(Face::Mono, label, 12.0f, 1.0f);
                        float lx = AZ::GetClamp(AZStd::min(sx[n0], sx[n1]), 20.0f * s, W - labelW - 20.0f * s);
                        const float ly = AZStd::min(AZStd::max(sy[n0], sy[n1]) + 14.0f * s, Y(486.0f));
                        // Keep clear of the tab's side panel.
                        static const float panelWidths[6] = { 0.0f, 300.0f, 300.0f, 300.0f, 420.0f, 320.0f };
                        if (panelWidths[m_buildTab] > 0.0f && ly < Y(600.0f))
                        {
                            const float panelLeft = R(1240.0f - panelWidths[m_buildTab]);
                            if (lx + labelW > panelLeft - 12.0f * s)
                            {
                                lx = AZStd::max(20.0f * s, panelLeft - 12.0f * s - labelW);
                            }
                        }
                        const AZ::Color tc = blocked ? badText : okText;
                        if (blocked)
                        {
                            lline(lx + 3.0f * s, ly + 3.0f * s, lx + 9.0f * s, ly + 9.0f * s, 2.0f * s, tc);
                            lline(lx + 9.0f * s, ly + 3.0f * s, lx + 3.0f * s, ly + 9.0f * s, 2.0f * s, tc);
                        }
                        else
                        {
                            lline(lx + 2.0f * s, ly + 6.0f * s, lx + 5.0f * s, ly + 9.0f * s, 2.0f * s, tc);
                            lline(lx + 5.0f * s, ly + 9.0f * s, lx + 10.0f * s, ly + 3.0f * s, 2.0f * s, tc);
                        }
                        text(Face::Mono, label, lx + 20.0f * s, ly - 1.2f * s, 12.0f, tc, 0, 1.0f);
                    }
                }
            }

            // linear-gradient(180deg, rgba(8,7,4,.35) 0%, 0 30%, 0 55%, .85 100%)
            {
                const float stops[4][2] = { { 0.0f, 0.35f }, { 0.30f, 0.0f }, { 0.55f, 0.0f }, { 1.0f, 0.85f } };
                for (int k2 = 0; k2 < 3; ++k2)
                {
                    const float y0 = H * stops[k2][0], y1 = H * stops[k2 + 1][0];
                    const AZ::Color a = Hex(0x080704, stops[k2][1]), b = Hex(0x080704, stops[k2 + 1][1]);
                    const AZ::Vector3 verts[6] = { P(0, y0), P(W, y0), P(W, y1), P(0, y0), P(W, y1), P(0, y1) };
                    const AZ::Color colours[6] = { a, a, b, a, b, b };
                    for (int v = 0; v < 6; ++v)
                    {
                        lv.push_back(verts[v]);
                        lc.push_back(colours[v]);
                    }
                }
            }

            // Top left: FORWARD BASE · TIER 1 / SPAWN ROOM · 28 × 28 m · STABLE · COMFORT 3 / 10
            textIn(Face::Oswald, AZStd::string::format("FORWARD BASE \xc2\xb7 TIER %d", base.m_tier), L(40.0f), 32.0f, 22.0f, 32.0f / 22.0f,
                Hex(0xfff3c0), 0, 3.0f);
            {
                const int size = static_cast<int>(std::round(base.m_maxX - base.m_minX));
                const char* state = base.m_blackout ? "POWER OUT" : (base.m_beaconUp ? "SECURED" : "STABLE");
                textIn(Face::Mono, AZStd::string::format("SPAWN ROOM \xc2\xb7 %d \xc3\x97 %d m \xc2\xb7 %s \xc2\xb7 COMFORT %d / 10", size, size, state, base.m_comfort),
                    L(40.0f), 64.0f, 12.0f, 1.25f, Hex(0xe3d9b4), 0, 2.0f);
            }

            // Top right: SCRAP / WIRE / FUEL / ALMOND WATER / POWER, right-aligned, 10 px apart.
            {
                struct Stock { const char* label; AZStd::string value; bool power; };
                const Stock stocks[5] = { { "SCRAP", AZStd::string::format("%d", base.m_scrap), false },
                                          { "WIRE", AZStd::string::format("%d", base.m_wire), false },
                                          { "FUEL", AZStd::string::format("%d", base.m_fuel), false },
                                          { "ALMOND WATER", AZStd::string::format("%d", base.m_almondWater), false },
                                          { "POWER", AZStd::string::format("%g / %g kW", base.m_demand, base.m_capacity), true } };
                float right = R(1240.0f);
                for (int i = 4; i >= 0; --i)
                {
                    const float labelW = measure(Face::Mono, stocks[i].label, 10.0f, 2.0f) + 2.0f * s; // CSS spacing after the last letter too
                    const float valueW = measure(Face::MonoSemiBold, stocks[i].value, 16.0f, 0.0f);
                    const float inner = AZStd::max(labelW, valueW);
                    const float w = inner + 24.0f * s + (stocks[i].power ? 2.0f * s : 0.0f);
                    const float x = right - w;
                    lrect(x, Y(32.0f), w, 49.0f * s, Hex(0x0a0906, 0.72f));
                    const float tx = x + (stocks[i].power ? 14.0f : 12.0f) * s;
                    if (stocks[i].power)
                    {
                        lrect(x, Y(32.0f), 2.0f * s, 49.0f * s, gold);
                    }
                    textIn(Face::Mono, stocks[i].label, tx, 40.0f, 10.0f, 1.3f, soft, 0, 2.0f);
                    textIn(Face::MonoSemiBold, stocks[i].value, tx, 53.0f, 16.0f, 1.25f, base.m_blackout && stocks[i].power ? bad : cream);
                    right = x - 10.0f * s;
                }
            }

            // The tab's side panel (right: 40 px, top: 118 px): BASE SAFETY, POWER, CARRYING, the research tree, CONTAINED.
            {
                struct Line { int type; AZStd::string label, value; float frac; AZ::Color colour; }; // 0 row, 1 meter, 2 note
                AZStd::vector<Line> lines;
                const char* title = nullptr;
                float width = 300.0f;
                auto row = [&](const AZStd::string& l, const AZStd::string& v, const AZ::Color& c) { lines.push_back({ 0, l, v, 0.0f, c }); };
                auto meter = [&](const AZStd::string& l, const AZStd::string& v, float f, const AZ::Color& c) { lines.push_back({ 1, l, v, f, c }); };
                auto note = [&](const AZStd::string& t) { lines.push_back({ 2, t, AZStd::string(), 0.0f, soft }); };
                auto plural = [](const char* name, int n)
                {
                    AZStd::string out(name);
                    if (n > 1)
                    {
                        out += (out.back() == 's' || out.back() == 'x') ? "es" : "s";
                        out += AZStd::string::format(" \xc3\x97%d", n);
                    }
                    return out;
                };
                switch (m_buildTab)
                {
                case 1:
                    title = "BASE SAFETY";
                    meter("Doorways covered", AZStd::string::format("%d / 2", base.m_covered), base.m_covered / 2.0f, gold);
                    meter("Light inside", AZStd::string::format("%d%%", static_cast<int>(std::round(base.m_lit * 100.0f))), base.m_lit, gold);
                    row("Dark corners", AZStd::string::format("%d", base.m_darkCorners), base.m_darkCorners > 0 ? gold : ok);
                    row("Last breach", "never", cream);
                    note("Lit, powered rooms keep monsters out. Block or shutter every doorway; a tripwire rings an alarm when something crosses it.");
                    break;
                case 2:
                {
                    title = "POWER";
                    meter("In use", AZStd::string::format("%g / %g kW", base.m_demand, base.m_capacity),
                        base.m_capacity > 0.0f ? base.m_demand / base.m_capacity : 0.0f, base.m_blackout ? bad : gold);
                    // What draws the most, kind by kind (up to four rows).
                    struct Use { int kind; int n; float kw; };
                    AZStd::vector<Use> uses;
                    for (const Base::Placed& p : base.m_placed)
                    {
                        const float kw = Base::Info(p.m_kind).m_power;
                        if (kw <= 0.0f)
                        {
                            continue;
                        }
                        auto it = AZStd::find_if(uses.begin(), uses.end(), [&](const Use& u) { return u.kind == static_cast<int>(p.m_kind); });
                        if (it == uses.end())
                        {
                            uses.push_back({ static_cast<int>(p.m_kind), 1, kw });
                        }
                        else
                        {
                            it->n += 1;
                            it->kw += kw;
                        }
                    }
                    AZStd::sort(uses.begin(), uses.end(), [](const Use& a, const Use& b) { return a.kw > b.kw; });
                    if (uses.empty())
                    {
                        row("Nothing drawing power", "0 kW", cream);
                    }
                    float rest = 0.0f;
                    for (size_t i = 0; i < uses.size(); ++i)
                    {
                        if (i < 3 || uses.size() == 4)
                        {
                            row(plural(Base::Info(static_cast<Base::Kind>(uses[i].kind)).m_name, uses[i].n), AZStd::string::format("%g kW", uses[i].kw), cream);
                        }
                        else
                        {
                            rest += uses[i].kw;
                        }
                    }
                    if (rest > 0.0f)
                    {
                        row(AZStd::string::format("%d more", static_cast<int>(uses.size()) - 3), AZStd::string::format("%g kW", rest), cream);
                    }
                    float burn = 0.0f;
                    for (const Base::Placed& p : base.m_placed)
                    {
                        const float m = Base::Info(p.m_kind).m_fuelMinutes;
                        burn = m > 0.0f ? (burn > 0.0f ? AZStd::min(burn, m) : m) : burn;
                    }
                    if (burn > 0.0f)
                    {
                        const int minutes = static_cast<int>(base.m_fuel * burn + base.m_canMinutes);
                        row("Generator fuel", AZStd::string::format("%d can%s \xc2\xb7 %d min", base.m_fuel, base.m_fuel == 1 ? "" : "s", minutes),
                            minutes > 0 ? gold : bad);
                    }
                    else
                    {
                        row("Generator fuel", "no generator", grey);
                    }
                    meter("Battery", AZStd::string::format("%d / %d kWh", static_cast<int>(base.m_batteryKwh), static_cast<int>(base.m_batteryMax)),
                        base.m_batteryMax > 0.0f ? base.m_batteryKwh / base.m_batteryMax : 0.0f, ok);
                    if (base.m_blackout)
                    {
                        row("Status", "POWER OUT", bad);
                    }
                    else if (base.m_off > 0)
                    {
                        row("Status", AZStd::string::format("%d off \xc2\xb7 breaker", base.m_off), bad);
                    }
                    note("When the fuel runs out the battery takes over; when that runs out the base goes dark.");
                    break;
                }
                case 3:
                {
                    title = "CARRYING";
                    meter("Backpack", AZStd::string::format("%d / %d", base.Carried(), Base::State::PackMax), base.Carried() / static_cast<float>(Base::State::PackMax), gold);
                    meter("Base storage", AZStd::string::format("%d / %d", base.Stored(), base.m_storageMax),
                        AZStd::min(1.0f, base.Stored() / static_cast<float>(AZStd::max(1, base.m_storageMax))), ok);
                    const int stills = base.Count(Base::Kind::Still), stillsOn = base.Count(Base::Kind::Still, true);
                    row("Still", stills == 0 ? AZStd::string("none built") : (stillsOn == 0 ? AZStd::string("off \xc2\xb7 no power")
                        : AZStd::string::format("%d Almond Water / %d min", stillsOn, 8)), stills == 0 ? grey : (stillsOn ? cream : bad));
                    row("Workbench queue", base.Count(Base::Kind::Workbench) == 0 ? AZStd::string("none built")
                        : (base.m_craftQueue > 0 ? AZStd::string::format("Flashlight battery \xc3\x97%d", base.m_craftQueue) : AZStd::string("empty")),
                        base.Count(Base::Kind::Workbench) == 0 ? grey : cream);
                    note("You only carry 20 per trip. Bring supplies back and drop them in a crate; the workbench makes batteries, flares and tools from them.");
                    break;
                }
                case 5:
                {
                    title = "CONTAINED";
                    width = 320.0f;
                    int number[3] = { 0, 0, 0 };
                    float used = 0.0f, needed = 0.0f;
                    for (const Base::Placed& p : base.m_placed)
                    {
                        const int which = p.m_kind == Base::Kind::ContainmentCell ? 0 : (p.m_kind == Base::Kind::LightCage ? 1 : (p.m_kind == Base::Kind::Pen999 ? 2 : -1));
                        if (which < 0)
                        {
                            if (p.m_kind == Base::Kind::Camera)
                            {
                                needed += Base::Info(p.m_kind).m_power;
                                used += p.m_powered ? Base::Info(p.m_kind).m_power : 0.0f;
                            }
                            continue;
                        }
                        ++number[which];
                        needed += Base::Info(p.m_kind).m_power;
                        used += p.m_powered ? Base::Info(p.m_kind).m_power : 0.0f;
                        static const char* const names[3] = { "Cell", "Light cage", "SCP-999 pen" };
                        row(AZStd::string::format("%s %d", names[which], number[which]), p.m_powered ? "empty" : "no power \xc2\xb7 OPEN", p.m_powered ? cream : bad);
                    }
                    if (number[0] + number[1] + number[2] == 0)
                    {
                        row("Nothing contained", "build a cell", grey);
                    }
                    meter("Research from them", "+0 samples / 10 min", 0.0f, ok);
                    meter("Cell power", AZStd::string::format("%g / %g kW", used, needed), needed > 0.0f ? used / needed : 0.0f, bad);
                    note("Lose power and the cells open. A contained monster slowly gives research; SCP-173 only stays put while a light or a camera watches it.");
                    break;
                }
                default:
                    break;
                }
                const float lineH = 12.0f * 1.3f;
                if (title)
                {
                    const float innerW = width - 32.0f;
                    // Heights first (the panel's background goes under its text).
                    float h = 14.0f + 10.0f * 1.3f;
                    AZStd::vector<AZStd::vector<AZStd::string>> wrapped(lines.size());
                    for (size_t i = 0; i < lines.size(); ++i)
                    {
                        h += 9.0f;
                        if (lines[i].type == 0) { h += lineH; }
                        else if (lines[i].type == 1) { h += lineH + 5.0f + 6.0f; }
                        else
                        {
                            wrapped[i] = wrap(Face::Mono, lines[i].label, 11.0f, innerW * s);
                            h += wrapped[i].size() * 11.0f * 1.5f;
                        }
                    }
                    h += 14.0f;
                    const float px = R(1240.0f - width);
                    lrect(px, Y(118.0f), width * s, h * s, Hex(0x0a0906, 0.8f));
                    float y = 118.0f + 14.0f;
                    textIn(Face::Mono, title, px + 16.0f * s, y, 10.0f, 1.3f, soft, 0, 2.0f);
                    y += 10.0f * 1.3f;
                    const float right = px + (width - 16.0f) * s;
                    for (size_t i = 0; i < lines.size(); ++i)
                    {
                        y += 9.0f;
                        const Line& line = lines[i];
                        if (line.type == 2)
                        {
                            for (const AZStd::string& w2 : wrapped[i])
                            {
                                textIn(Face::Mono, w2, px + 16.0f * s, y, 11.0f, 1.5f, soft);
                                y += 11.0f * 1.5f;
                            }
                            continue;
                        }
                        textIn(Face::Mono, line.label, px + 16.0f * s, y, 12.0f, 1.3f, soft);
                        textIn(Face::MonoSemiBold, line.value, right, y, 12.0f, 1.3f, line.type == 0 ? line.colour : cream, 2);
                        y += lineH;
                        if (line.type == 1)
                        {
                            y += 5.0f;
                            lrect(px + 16.0f * s, Y(y), innerW * s, 6.0f * s, AZ::Color(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, 0.15f));
                            lrect(px + 16.0f * s, Y(y), innerW * s * AZ::GetClamp(line.frac, 0.0f, 1.0f), 6.0f * s, line.colour);
                            y += 6.0f;
                        }
                    }
                }
                else if (m_buildTab == 4)
                {
                    // RESEARCH: the tree (420 px wide), DOCUMENTS / SAMPLES at the top right.
                    width = 420.0f;
                    const float px = R(1240.0f - width);
                    const float innerW = width - 32.0f;
                    const AZStd::vector<AZStd::string> noteLines = wrap(Face::Mono,
                        "Foundation documents and odd samples turn up in the levels and on fallen MTF soldiers. Studying them at the desk unlocks new pieces, gear and outposts.",
                        11.0f, innerW * s);
                    const float cellH = 7.0f + lineH + 3.0f + 10.0f * 1.3f + 7.0f;
                    const float tierH = 2.0f + 10.0f * 1.3f + 10.0f + cellH;
                    const float h = 14.0f + lineH + 3.0f * (10.0f + tierH) + 10.0f + noteLines.size() * 16.5f + 14.0f;
                    lrect(px, Y(118.0f), width * s, h * s, Hex(0x0a0906, 0.8f));
                    float y = 118.0f + 14.0f;
                    textIn(Face::Mono, "RESEARCH", px + 16.0f * s, y + 1.0f, 10.0f, 1.3f, soft, 0, 2.0f);
                    textIn(Face::Mono, AZStd::string::format("DOCUMENTS %d \xc2\xb7 SAMPLES %d", base.m_docs, base.m_samples), px + (width - 16.0f) * s, y,
                        11.0f, 1.3f, soft, 2);
                    y += lineH;
                    const bool p2 = base.ResearchDone(0), s2 = base.ResearchDone(1);
                    const auto& st = base.m_studied;
                    struct Cell { const char* name; AZStd::string status; AZ::Color colour; };
                    const Cell tiers[3][3] = {
                        { { "Lighting I", "done", ok }, { "Storage I", "done", ok }, { "Defenses I", "done", ok } },
                        { { "Power II", p2 ? AZStd::string("done") : AZStd::string::format("%d / 12", st[0][0]), p2 ? ok : gold },
                          { "Storage II", s2 ? AZStd::string("done") : (p2 ? AZStd::string::format("%d / 8 \xc2\xb7 %d / 2", st[1][0], st[1][1]) : AZStd::string("locked")),
                            s2 ? ok : (p2 ? gold : grey) },
                          { "Light tower", p2 ? AZStd::string("unlocked") : AZStd::string("after Power II"), p2 ? ok : grey } },
                        { { "Level 1 outpost", "locked", grey }, { "Containment", "locked", grey }, { "SCP-914", "locked", grey } },
                    };
                    static const char* const tierNames[3] = { "TIER I", "TIER II", "TIER III" };
                    const float cellW = (innerW - 12.0f) / 3.0f;
                    for (int t = 0; t < 3; ++t)
                    {
                        y += 10.0f + 2.0f;
                        textIn(Face::Mono, tierNames[t], px + 16.0f * s, y, 10.0f, 1.3f, Hex(0xa59b78), 0, 2.0f);
                        y += 10.0f * 1.3f + 10.0f;
                        for (int c = 0; c < 3; ++c)
                        {
                            const float cx = px + (16.0f + c * (cellW + 6.0f)) * s;
                            loutline(cx, Y(y), cellW * s, cellH * s, AZStd::max(1.0f, s), tiers[t][c].colour);
                            textIn(Face::MonoSemiBold, tiers[t][c].name, cx + 8.0f * s, y + 7.0f, 12.0f, 1.3f, cream);
                            textIn(Face::Mono, tiers[t][c].status, cx + 8.0f * s, y + 7.0f + lineH + 3.0f, 10.0f, 1.3f, tiers[t][c].colour);
                        }
                        y += cellH;
                    }
                    y += 10.0f;
                    for (const AZStd::string& w2 : noteLines)
                    {
                        textIn(Face::Mono, w2, px + 16.0f * s, y, 11.0f, 1.5f, soft);
                        y += 16.5f;
                    }
                }
                // A research project picked: what studying it takes (there's nothing to place).
                if (project && !locked)
                {
                    const int which = kind == Base::Kind::StudyPowerII ? 0 : 1;
                    const AZStd::string label = base.ResearchDone(which) ? AZStd::string(info.m_upper) + " \xc2\xb7 done"
                        : AZStd::string::format("%s \xc2\xb7 %d / %d documents%s \xc2\xb7 [LMB] study at the research desk", info.m_upper,
                              base.m_studied[which][0], info.m_docs,
                              info.m_samples > 0 ? AZStd::string::format(" \xc2\xb7 %d / %d samples", base.m_studied[which][1], info.m_samples).c_str() : "");
                    text(Face::Mono, label, C(640.0f), Y(470.0f), 12.0f, okText, 1, 1.0f);
                }
                else if (locked)
                {
                    text(Face::Mono, AZStd::string(info.m_upper) + " \xc2\xb7 locked \xc2\xb7 " + info.m_lockNote, C(640.0f), Y(470.0f), 12.0f, badText, 1, 1.0f);
                }
            }

            // Bottom: category tabs, the tile panel, the key hints. A tile's second line wraps like the mockup's (the
            // grid row grows, and the block, anchored to the bottom, grows upward).
            {
                const float panelX = L(40.0f);
                const float panelW = R(1240.0f) - panelX;
                const float tileW = (panelW - 28.0f * s - 50.0f * s) / 6.0f;
                AZStd::vector<AZStd::string> costLines[6];
                int maxLines = 1;
                for (int i = 0; i < 6; ++i)
                {
                    const Base::Kind tileKind = Base::KindAt(m_buildTab, i);
                    const Base::ItemInfo& tile = Base::Info(tileKind);
                    AZStd::string cost;
                    if (base.Locked(tileKind))
                    {
                        cost = tile.m_lockNote;
                    }
                    else if (tile.m_project && base.ResearchDone(tileKind == Base::Kind::StudyPowerII ? 0 : 1))
                    {
                        cost = "done";
                    }
                    else if (tile.m_costText)
                    {
                        cost = tile.m_costText;
                    }
                    else
                    {
                        auto add = [&](const char* name, int n)
                        {
                            if (n > 0)
                            {
                                cost += (cost.empty() ? "" : " \xc2\xb7 ") + AZStd::string::format("%s %d", name, n);
                            }
                        };
                        add("Scrap", tile.m_scrap);
                        add("Wire", tile.m_wire);
                        add("Fuel", tile.m_fuel);
                    }
                    costLines[i] = wrap(Face::Mono, cost, 11.0f, tileW - 26.0f * s);
                    maxLines = AZStd::max(maxLines, static_cast<int>(costLines[i].size()));
                }
                const float grow = 14.0f * (maxLines - 1); // design px
                float x = L(40.0f);
                for (int i = 0; i < Base::TabCount; ++i)
                {
                    const bool on = i == m_buildTab;
                    const float w = measure(Face::Mono, Base::TabNames[i], 12.0f, 2.0f) + 2.0f * s + 32.0f * s;
                    lrect(x, Y(510.0f - grow), w, 31.0f * s, on ? gold : Hex(0x0a0906, 0.75f));
                    textIn(on ? Face::MonoSemiBold : Face::Mono, Base::TabNames[i], x + 16.0f * s, 518.0f - grow, 12.0f, 1.25f, on ? Hex(0x0b0a07) : cream, 0, 2.0f);
                    x += w + 4.0f * s;
                }
                lrect(panelX, Y(545.0f - grow), panelW, (125.0f + grow) * s, Hex(0x0a0906, 0.82f));
                for (int i = 0; i < 6; ++i)
                {
                    const Base::Kind tileKind = Base::KindAt(m_buildTab, i);
                    const Base::ItemInfo& tile = Base::Info(tileKind);
                    const bool tileLocked = base.Locked(tileKind);
                    const float tx = panelX + 14.0f * s + i * (tileW + 10.0f * s);
                    const float ty = Y(559.0f - grow);
                    const float th = (97.0f + grow) * s;
                    const bool selected = i == m_buildTile && !tileLocked;
                    const AZ::Color stroke = tileLocked ? grey : (selected ? gold : cream);
                    const float bw = AZStd::max(1.0f, 1.5f * s);
                    if (selected)
                    {
                        lrect(tx, ty, tileW, th, AZ::Color(240 / 255.0f, 207 / 255.0f, 90 / 255.0f, 0.12f));
                        loutline(tx, ty, tileW, th, bw, gold);
                    }
                    else if (tileLocked)
                    {
                        // 1.5 px dashed border, rgba(243,234,208,.2); a picked locked tile gets a dashed gold one
                        const AZ::Color dash = i == m_buildTile ? AZ::Color(240 / 255.0f, 207 / 255.0f, 90 / 255.0f, 0.6f) : AZ::Color(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, 0.2f);
                        for (float d = 0.0f; d < tileW; d += 6.0f * s)
                        {
                            lrect(tx + d, ty, AZStd::min(3.0f * s, tileW - d), bw, dash);
                            lrect(tx + d, ty + th - bw, AZStd::min(3.0f * s, tileW - d), bw, dash);
                        }
                        for (float d = 0.0f; d < th; d += 6.0f * s)
                        {
                            lrect(tx, ty + d, bw, AZStd::min(3.0f * s, th - d), dash);
                            lrect(tx + tileW - bw, ty + d, bw, AZStd::min(3.0f * s, th - d), dash);
                        }
                    }
                    else
                    {
                        loutline(tx, ty, tileW, th, bw, AZ::Color(243 / 255.0f, 234 / 255.0f, 208 / 255.0f, 0.25f));
                    }
                    // The 28 x 28 icon (the mockups' SVGs, 1.6 px strokes), 13 px in from the tile's corner.
                    const float ix = tx + 13.0f * s;
                    const float iy = Y(572.0f - grow);
                    auto il = [&](float x0, float y0, float x1, float y1, float w = 1.6f) { lline(ix + x0 * s, iy + y0 * s, ix + x1 * s, iy + y1 * s, w * s, stroke); };
                    auto irect = [&](float x0, float y0, float w, float h) { il(x0, y0, x0 + w, y0); il(x0 + w, y0, x0 + w, y0 + h); il(x0 + w, y0 + h, x0, y0 + h); il(x0, y0 + h, x0, y0); };
                    auto icircle = [&](float cx, float cy, float r, float from = 0.0f, float to = 6.2831853f)
                    {
                        const int n = 20;
                        for (int k3 = 0; k3 < n; ++k3)
                        {
                            const float a0 = from + (to - from) * k3 / n, a1 = from + (to - from) * (k3 + 1) / n;
                            il(cx + std::cos(a0) * r, cy + std::sin(a0) * r, cx + std::cos(a1) * r, cy + std::sin(a1) * r);
                        }
                    };
                    auto traced = [&](int icon)
                    {
                        const BuildIcons::Icon& ic = BuildIcons::Icons[icon];
                        for (int k3 = 0; k3 < ic.m_strokes; ++k3)
                        {
                            const BuildIcons::Stroke& st = BuildIcons::Strokes[ic.m_firstStroke + k3];
                            for (int p = 0; p + 1 < st.m_count; ++p)
                            {
                                const float* a = BuildIcons::Points[st.m_first + p];
                                const float* b = BuildIcons::Points[st.m_first + p + 1];
                                if (!st.m_dashed)
                                {
                                    il(a[0], a[1], b[0], b[1], st.m_width);
                                    continue;
                                }
                                // stroke-dasharray="2 2"
                                const float len = std::sqrt((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]));
                                for (float d = 0.0f; d < len; d += 4.0f)
                                {
                                    const float e = AZStd::min(len, d + 2.0f);
                                    il(a[0] + (b[0] - a[0]) * d / len, a[1] + (b[1] - a[1]) * d / len, a[0] + (b[0] - a[0]) * e / len,
                                        a[1] + (b[1] - a[1]) * e / len, st.m_width);
                                }
                            }
                        }
                        for (int k3 = 0; k3 < ic.m_fills; ++k3)
                        {
                            const BuildIcons::Fill& f = BuildIcons::Fills[ic.m_firstFill + k3];
                            for (int q = 0; q < 12; ++q)
                            {
                                const float a0 = 6.2831853f * q / 12, a1 = 6.2831853f * (q + 1) / 12;
                                for (const AZ::Vector3& v : { P(ix + f.m_x * s, iy + f.m_y * s), P(ix + (f.m_x + std::cos(a0) * f.m_r) * s, iy + (f.m_y + std::sin(a0) * f.m_r) * s),
                                                              P(ix + (f.m_x + std::cos(a1) * f.m_r) * s, iy + (f.m_y + std::sin(a1) * f.m_r) * s) })
                                {
                                    lv.push_back(v);
                                    lc.push_back(stroke);
                                }
                            }
                        }
                    };
                    if (tileLocked)
                    {
                        traced(BuildIcons::Icon_lock);
                    }
                    else if (tile.m_icon >= 0)
                    {
                        traced(tile.m_icon);
                    }
                    else
                    {
                        switch (i)
                        {
                        case 0: irect(6, 4, 16, 10); il(14, 14, 14, 24); il(9, 24, 19, 24); break;
                        case 1: irect(5, 5, 18, 6); il(8, 11, 8, 23); il(20, 11, 20, 23); break;
                        case 2: icircle(14, 14, 5); il(14, 3, 14, 7); il(14, 21, 14, 25); il(3, 14, 7, 14); il(21, 14, 25, 14); break;
                        case 3:
                            il(14, 4, 22, 14); il(22, 14, 14, 24); il(14, 24, 6, 14); il(6, 14, 14, 4);
                            for (float r = 0.4f; r <= 2.5f; r += 0.7f) { icircle(14, 14, r); }
                            break;
                        case 4: il(5, 22, 23, 22); il(8, 22, 8, 12); il(8, 12, 20, 12); il(20, 12, 20, 22); il(11, 12, 11, 7); il(11, 7, 17, 7); il(17, 7, 17, 12); break;
                        default: irect(5, 22, 18, 1); il(9, 22, 14, 4); il(19, 22, 14, 4); il(10, 16, 18, 16); break; // the light tower, once unlocked
                        }
                    }
                    textIn(Face::MonoSemiBold, tile.m_name, tx + 13.0f * s, 606.0f - grow, 13.0f, 17.0f / 13.0f, tileLocked ? grey : cream);
                    for (size_t l = 0; l < costLines[i].size(); ++l)
                    {
                        textIn(Face::Mono, costLines[i][l], tx + 13.0f * s, 629.0f - grow + 14.0f * l, 11.0f, 14.0f / 11.0f, tileLocked ? grey : soft);
                    }
                }
                static const char* const hints[4] = { "[LMB] place", "[R] rotate", "[X] demolish", "[TAB] close" };
                float hx = L(40.0f);
                for (const char* hint : hints)
                {
                    textIn(Face::Mono, hint, hx, 678.0f, 11.0f, 14.0f / 11.0f, soft, 0, 1.0f);
                    hx += measure(Face::Mono, hint, 11.0f, 1.0f) + 1.0f * s + 22.0f * s;
                }
            }
            lflush(4);
            // The toast (the beacon going up, refunds...) shows in build mode too.
            {
                const AZStd::string toast = RegString("/NoclipProtocol/Hud/toast", "");
                const double until = RegNumber("/NoclipProtocol/Hud/toast.until", 0.0);
                if (!toast.empty() && until > m_time)
                {
                    AZ::Color c = Bright;
                    c.SetA(AZStd::min(1.0f, static_cast<float>(until - m_time)));
                    text(Face::Mono, toast, C(640), Y(440), 14.0f, c, 1, 2.0f);
                }
            }
        }
        // ---- the Foundation's requisitions: FoundationOrders.dc.html, the whole screen (the field radio's [E]) ----
        else if (m_page == Page::Orders)
        {
            Base::State& base = Base::Get();
            AZStd::vector<AZ::Vector3> tv;
            AZStd::vector<AZ::Color> tc;
            auto solid = [&](const AZ::Color& c)
            {
                const float a = c.GetA();
                return AZ::Color(TermBg.GetR() + (c.GetR() - TermBg.GetR()) * a, TermBg.GetG() + (c.GetG() - TermBg.GetG()) * a,
                                 TermBg.GetB() + (c.GetB() - TermBg.GetB()) * a, static_cast<float>(hud_term_alpha));
            };
            auto trect = [&](float x, float y, float w, float h, const AZ::Color& c)
            {
                const AZ::Color col = solid(c);
                for (const AZ::Vector3& v : { P(x, y), P(x + w, y), P(x + w, y + h), P(x, y), P(x + w, y + h), P(x, y + h) })
                {
                    tv.push_back(v);
                    tc.push_back(col);
                }
            };
            auto toutline = [&](float x, float y, float w, float h, float t, const AZ::Color& c)
            {
                trect(x, y, w, t, c);
                trect(x, y + h - t, w, t, c);
                trect(x, y, t, h, c);
                trect(x + w - t, y, t, h, c);
            };
            auto tdashed = [&](float x, float y, float w, float h, float t, const AZ::Color& c)
            {
                for (float d = 0.0f; d < w; d += 6.0f * s)
                {
                    trect(x + d, y, AZStd::min(3.0f * s, w - d), t, c);
                    trect(x + d, y + h - t, AZStd::min(3.0f * s, w - d), t, c);
                }
                for (float d = 0.0f; d < h; d += 6.0f * s)
                {
                    trect(x, y + d, t, AZStd::min(3.0f * s, h - d), c);
                    trect(x + w - t, y + d, t, AZStd::min(3.0f * s, h - d), c);
                }
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
            auto textIn = [&](Face face, const AZStd::string& value, float x, float top, float size, float lineHeight,
                              const AZ::Color& c, int align = 0, float spacing = 0.0f)
            { return text(face, value, x, Y(top + (lineHeight - 1.2f) * size * 0.5f), size, c, align, spacing, true); };
            auto X = [&](float x) { return C(x); };
            const AZ::Color goldLine(216 / 255.0f, 195 / 255.0f, 90 / 255.0f, 0.2f);
            const AZ::Color goldTrack(216 / 255.0f, 195 / 255.0f, 90 / 255.0f, 0.15f);
            auto say = [&](const AZStd::string& message)
            {
                m_orderNotice = message;
                m_orderNoticeUntil = m_time + 4.0f;
            };

            // Background and scanlines, like the briefing terminal.
            {
                const AZ::Color scan(1.0f, 240 / 255.0f, 180 / 255.0f, 0.035f);
                for (float y = 0.0f; y < DesignH; y += 3.0f)
                {
                    trect(0.0f, Y(y), W, Y(y + 1.0f) - Y(y), scan);
                    trect(0.0f, Y(y + 1.0f), W, AZStd::min(H, Y(y + 3.0f)) - Y(y + 1.0f), TermBg);
                }
            }
            // Header: the mark, FOUNDATION REQUISITIONS, the link, REQUISITION 120 RP.
            {
                const float stroke = 1.227f;
                toutline(X(28.614f), Y(13.614f), 16.77f * s, 16.77f * s, AZStd::max(1.0f, stroke * s), TermGold);
                trect(X(28.0f + 5.727f), Y(13.0f + 5.727f), 6.545f * s, 6.545f * s, TermGold);
                const float w1 = measure(Face::Mono, "FOUNDATION REQUISITIONS", 12.0f, 2.0f) / s + 2.0f;
                textIn(Face::Mono, "FOUNDATION REQUISITIONS", X(62.0f), 14.2f, 12.0f, 1.3f, TermGold, 0, 2.0f);
                textIn(Face::Mono, "FORWARD BASE \xc2\xb7 LEVEL 0 \xc2\xb7 LINK VIA BREACH 4-A", X(62.0f + w1 + 16.0f), 14.2f, 12.0f, 1.3f, TermMuted, 0, 2.0f);
                textIn(Face::Mono, AZStd::string::format("REQUISITION %d RP", base.m_rp), X(1252.0f), 14.2f, 12.0f, 1.3f, TermSelected, 2, 2.0f);
                trect(0.0f, Y(44.0f), W, AZStd::max(1.0f, s), AZ::Color(216 / 255.0f, 195 / 255.0f, 90 / 255.0f, 0.35f));
            }
            // The sections (left, 250 px).
            int open[3];
            const int openCount = base.OpenOrders(open);
            {
                trect(X(250.0f), Y(45.0f), AZStd::max(1.0f, s), H - Y(45.0f), goldLine);
                float y = 66.0f;
                auto heading = [&](const char* name, float marginTop)
                {
                    y += marginTop;
                    textIn(Face::Mono, name, X(20.0f), y, 11.0f, 1.3f, TermMuted, 0, 2.0f);
                    y += 11.0f * 1.3f + 6.0f + 6.0f;
                };
                auto item = [&](const AZStd::string& name, int section)
                {
                    const float h = 13.0f * 1.3f + 14.0f;
                    const bool cur = section == m_orderSection;
                    if (cur)
                    {
                        trect(X(20.0f), Y(y), 210.0f * s, h * s, AZ::Color(216 / 255.0f, 195 / 255.0f, 90 / 255.0f, 0.14f));
                        trect(X(20.0f), Y(y), 2.0f * s, h * s, TermGold);
                    }
                    const bool hover = over(X(20.0f), Y(y), 210.0f * s, h * s);
                    textIn(Face::Mono, name, X(cur ? 30.0f : 32.0f), y + 7.0f, 13.0f, 1.3f, cur ? TermSelected : (hover ? TermHover : TermText));
                    if (clicked(X(20.0f), Y(y), 210.0f * s, h * s))
                    {
                        m_orderSection = section;
                        m_orderItem = 0;
                    }
                    y += h + 6.0f;
                };
                heading("REQUEST", 0.0f);
                for (int i = 0; i < Base::SectionCount; ++i)
                {
                    item(Base::SectionNames[i], i);
                }
                heading("FOUNDATION ORDERS", 10.0f);
                item(AZStd::string::format("Open orders (%d)", openCount), 5);
                item(AZStd::string::format("Completed (%d)", base.OrdersDone()), 6);
                heading("DELIVERIES", 10.0f);
                item(AZStd::string::format("On the way (%d)", static_cast<int>(base.m_deliveries.size())), 7);
            }
            // The main column: the catalog, the button, the open orders.
            const float mainX = 276.0f, mainW = 1280.0f - 280.0f - 26.0f - mainX;
            float y = 66.0f;
            auto orderRows = [&](const int* which, int count, bool done)
            {
                for (int k = 0; k < count; ++k)
                {
                    const Base::OrderDef& order = Base::Order(which[k]);
                    const float frac = done ? 1.0f : base.OrderProgress(which[k]);
                    textIn(Face::Mono, order.m_title, X(mainX), y, 12.0f, 1.3f, TermText);
                    const float barX = mainX + mainW - 170.0f - 14.0f - 160.0f;
                    trect(X(barX), Y(y + 5.0f), 160.0f * s, 6.0f * s, goldTrack);
                    trect(X(barX), Y(y + 5.0f), 160.0f * s * AZ::GetClamp(frac, 0.0f, 1.0f), 6.0f * s, TermGold);
                    textIn(Face::Mono, done ? AZStd::string("done \xc2\xb7 ") + order.m_reward : AZStd::string(order.m_reward), X(mainX + mainW), y, 12.0f, 1.3f,
                        TermGold, 2);
                    y += 12.0f * 1.3f + 8.0f;
                }
            };
            if (m_orderSection < Base::SectionCount)
            {
                const int section = m_orderSection;
                const float titleW = measure(Face::Oswald, Base::SectionTitles[section], 24.0f, 3.0f) / s + 3.0f;
                textIn(Face::Oswald, Base::SectionTitles[section], X(mainX), y, 24.0f, 1.482f, TermSelected, 0, 3.0f);
                textIn(Face::Mono, "Delivered through Breach 4-A. It opens a crack, the crate comes through, it seals again.", X(mainX + titleW + 14.0f),
                    y + 13.0f, 12.0f, 1.3f, TermMuted);
                y += 24.0f * 1.482f + 14.0f;
                // The cards, three across.
                int items[8];
                int n = 0;
                for (int i = 0; i < Base::ReqCount && n < 8; ++i)
                {
                    if (Base::Req(i).m_section == section)
                    {
                        items[n++] = i;
                    }
                }
                m_orderItem = AZ::GetClamp(m_orderItem, 0, AZStd::max(0, n - 1));
                const float cardW = (mainW - 20.0f) / 3.0f;
                const float cardH = 14.0f + 14.0f * 1.3f + 6.0f + 11.0f * 1.3f + 6.0f + 12.0f * 1.3f + 14.0f;
                for (int k = 0; k < n; ++k)
                {
                    const Base::ReqItem& req = Base::Req(items[k]);
                    const bool itemLocked = !base.Unlocked(req.m_unlock);
                    const float cx = mainX + (k % 3) * (cardW + 10.0f);
                    const float cy = y + (k / 3) * (cardH + 10.0f);
                    const bool sel = k == m_orderItem;
                    if (itemLocked)
                    {
                        tdashed(X(cx), Y(cy), cardW * s, cardH * s, AZStd::max(1.0f, s), AZ::Color(216 / 255.0f, 195 / 255.0f, 90 / 255.0f, 0.25f));
                    }
                    else
                    {
                        if (sel)
                        {
                            trect(X(cx), Y(cy), cardW * s, cardH * s, AZ::Color(216 / 255.0f, 195 / 255.0f, 90 / 255.0f, 0.12f));
                        }
                        toutline(X(cx), Y(cy), cardW * s, cardH * s, AZStd::max(1.0f, s), sel ? TermGold : AZ::Color(216 / 255.0f, 195 / 255.0f, 90 / 255.0f, 0.3f));
                    }
                    textIn(Face::MonoSemiBold, req.m_name, X(cx + 14.0f), cy + 14.0f, 14.0f, 1.3f, itemLocked ? TermLocked : TermSelected);
                    textIn(Face::Mono, req.m_desc, X(cx + 14.0f), cy + 14.0f + 14.0f * 1.3f + 6.0f, 11.0f, 1.3f, itemLocked ? TermLocked : TermMuted);
                    textIn(Face::Mono, AZStd::string::format("%d RP", req.m_cost), X(cx + 14.0f), cy + 14.0f + 14.0f * 1.3f + 6.0f + 11.0f * 1.3f + 6.0f, 12.0f, 1.3f,
                        itemLocked ? TermLocked : TermGold);
                    if (clicked(X(cx), Y(cy), cardW * s, cardH * s))
                    {
                        m_orderItem = k;
                    }
                }
                y += ((n + 2) / 3) * (cardH + 10.0f) - 10.0f + 14.0f + 4.0f;
                // REQUEST PUMP SHOTGUN · 90 RP
                if (n > 0)
                {
                    const Base::ReqItem& req = Base::Req(items[m_orderItem]);
                    AZStd::string label = "REQUEST " + AZStd::string(req.m_name) + AZStd::string::format(" \xc2\xb7 %d RP", req.m_cost);
                    for (char& c : label)
                    {
                        c = static_cast<char>(c >= 'a' && c <= 'z' ? c - 32 : c);
                    }
                    const float bw = measure(Face::MonoSemiBold, label, 12.0f, 2.0f) / s + 2.0f + 36.0f;
                    const float bh = 12.0f * 1.3f + 20.0f;
                    const bool usable = base.Unlocked(req.m_unlock) && base.m_rp >= req.m_cost;
                    trect(X(mainX), Y(y), bw * s, bh * s, usable ? TermGold : AZ::Color(216 / 255.0f, 195 / 255.0f, 90 / 255.0f, 0.35f));
                    textIn(Face::MonoSemiBold, label, X(mainX + 18.0f), y + 10.0f, 12.0f, 1.3f, TermBg, 0, 2.0f);
                    const bool notice = !m_orderNotice.empty() && m_orderNoticeUntil > m_time;
                    const float after = base.m_deliveries.empty() ? 0.0f : base.m_deliveries.back().m_left;
                    const int eta = static_cast<int>(after + Base::DeliverySeconds);
                    textIn(Face::Mono, notice ? m_orderNotice : AZStd::string::format("Arrives in about %d:%02d \xc2\xb7 stand clear of the breach", eta / 60, eta % 60),
                        X(mainX + bw + 10.0f), y + 10.0f, 12.0f, 1.3f, notice ? TermSelected : TermMuted);
                    if (clicked(X(mainX), Y(y), bw * s, bh * s) || m_orderRequest)
                    {
                        say(base.Requisition(items[m_orderItem]));
                    }
                    y += bh;
                }
                m_orderRequest = false;
                // OPEN FOUNDATION ORDERS · EARN REQUISITION
                y += 6.0f;
                trect(X(mainX), Y(y), mainW * s, AZStd::max(1.0f, s), goldLine);
                y += 14.0f;
                textIn(Face::Mono, "OPEN FOUNDATION ORDERS \xc2\xb7 EARN REQUISITION", X(mainX), y, 11.0f, 1.3f, TermMuted, 0, 2.0f);
                y += 11.0f * 1.3f + 8.0f;
                orderRows(open, openCount, false);
            }
            else
            {
                static const char* const titles[3] = { "OPEN ORDERS", "COMPLETED", "ON THE WAY" };
                static const char* const subs[3] = { "Do what the Foundation asks and it pays in requisition.", "Orders you have carried out.",
                                                     "Crates waiting their turn at Breach 4-A." };
                const int page = m_orderSection - 5;
                const float titleW = measure(Face::Oswald, titles[page], 24.0f, 3.0f) / s + 3.0f;
                textIn(Face::Oswald, titles[page], X(mainX), y, 24.0f, 1.482f, TermSelected, 0, 3.0f);
                textIn(Face::Mono, subs[page], X(mainX + titleW + 14.0f), y + 13.0f, 12.0f, 1.3f, TermMuted);
                y += 24.0f * 1.482f + 14.0f;
                if (page == 0)
                {
                    orderRows(open, openCount, false);
                }
                else if (page == 1)
                {
                    int done[Base::OrderCount];
                    int n = 0;
                    for (int i = 0; i < Base::OrderCount; ++i)
                    {
                        if (base.m_ordersDone & (1 << i))
                        {
                            done[n++] = i;
                        }
                    }
                    orderRows(done, n, true);
                    if (n == 0)
                    {
                        textIn(Face::Mono, "None yet.", X(mainX), y, 12.0f, 1.3f, TermMuted);
                    }
                }
                else
                {
                    for (const Base::Delivery& d : base.m_deliveries)
                    {
                        const int left = AZStd::max(0, static_cast<int>(d.m_left));
                        textIn(Face::Mono, d.m_item < 0 ? "Foundation supply drop" : Base::Req(d.m_item).m_name, X(mainX), y, 12.0f, 1.3f, TermText);
                        textIn(Face::Mono, AZStd::string::format("%d:%02d", left / 60, left % 60), X(mainX + mainW), y, 12.0f, 1.3f, TermGold, 2);
                        y += 12.0f * 1.3f + 8.0f;
                    }
                    for (int item : base.m_waiting)
                    {
                        textIn(Face::Mono, item < 0 ? "Foundation supply drop" : Base::Req(item).m_name, X(mainX), y, 12.0f, 1.3f, TermText);
                        textIn(Face::Mono, "at the breach \xc2\xb7 [E] open it", X(mainX + mainW), y, 12.0f, 1.3f, TermGold, 2);
                        y += 12.0f * 1.3f + 8.0f;
                    }
                    if (base.m_deliveries.empty() && base.m_waiting.empty())
                    {
                        textIn(Face::Mono, "Nothing on the way.", X(mainX), y, 12.0f, 1.3f, TermMuted);
                    }
                }
                m_orderRequest = false;
            }
            // The delivery column (right, 280 px): Breach 4-A sealed with its seam, what's on the way.
            {
                const float ax = 1000.0f + 20.0f, aw = 240.0f;
                trect(X(1000.0f), Y(45.0f), AZStd::max(1.0f, s), H - Y(45.0f), goldLine);
                float ay = 66.0f;
                textIn(Face::Mono, "BREACH 4-A", X(ax), ay, 11.0f, 1.3f, TermMuted, 0, 2.0f);
                ay += 11.0f * 1.3f + 12.0f;
                trect(X(ax), Y(ay), aw * s, 150.0f * s, Hex(0x050403));
                toutline(X(ax), Y(ay), aw * s, 150.0f * s, AZStd::max(1.0f, s), AZ::Color(216 / 255.0f, 195 / 255.0f, 90 / 255.0f, 0.3f));
                // The seam: 4 px of violet light fading out at both ends (wider while it's open).
                const float seam = 4.0f + 36.0f * AZ::GetClamp(base.m_breachOpen, 0.0f, 1.0f);
                for (int k = 0; k < 20; ++k)
                {
                    const float t = (k + 0.5f) / 20.0f;
                    const float a = 0.9f * (1.0f - std::fabs(t - 0.5f) * 2.0f);
                    trect(X(ax + aw * 0.5f - seam * 0.5f), Y(ay + 14.0f + k * 6.1f), seam * s, 6.2f * s, AZ::Color(140 / 255.0f, 130 / 255.0f, 1.0f, a));
                }
                textIn(Face::Mono, base.m_breachOpen > 0.05f ? "OPEN A CRACK \xc2\xb7 CRATE COMING THROUGH" : "SEALED \xc2\xb7 SEAM GLOWING", X(ax + 10.0f),
                    ay + 150.0f - 8.0f - 13.0f, 10.0f, 1.3f, TermMuted, 0, 2.0f);
                ay += 150.0f + 12.0f + 4.0f;
                textIn(Face::Mono, "ON THE WAY", X(ax), ay, 11.0f, 1.3f, TermMuted, 0, 2.0f);
                ay += 11.0f * 1.3f + 12.0f;
                if (!base.m_deliveries.empty())
                {
                    const Base::Delivery& d = base.m_deliveries.front();
                    const int left = AZStd::max(0, static_cast<int>(d.m_left));
                    textIn(Face::Mono, d.m_item < 0 ? "Supply drop" : Base::Req(d.m_item).m_name, X(ax), ay, 12.0f, 1.3f, TermText);
                    textIn(Face::Mono, AZStd::string::format("%d:%02d", left / 60, left % 60), X(ax + aw), ay, 12.0f, 1.3f, TermGold, 2);
                    ay += 12.0f * 1.3f + 12.0f;
                    trect(X(ax), Y(ay), aw * s, 6.0f * s, goldTrack);
                    const float total = d.m_item < 0 ? 30.0f : Base::DeliverySeconds;
                    trect(X(ax), Y(ay), aw * s * AZ::GetClamp(1.0f - d.m_left / total, 0.0f, 1.0f), 6.0f * s, TermGold);
                    ay += 6.0f + 12.0f;
                }
                else if (!base.m_waiting.empty())
                {
                    textIn(Face::Mono, "A crate is waiting at the breach", X(ax), ay, 12.0f, 1.3f, TermGold);
                    ay += 12.0f * 1.3f + 12.0f;
                }
                else
                {
                    textIn(Face::Mono, "Nothing on the way", X(ax), ay, 12.0f, 1.3f, TermMuted);
                    ay += 12.0f * 1.3f + 12.0f;
                }
                for (const AZStd::string& w2 : wrap(Face::Mono, "The breach opens just wide enough for the crate. Anything near it may come through too.", 11.0f, aw * s))
                {
                    textIn(Face::Mono, w2, X(ax), ay, 11.0f, 1.5f, TermMuted);
                    ay += 16.5f;
                }
                {
                    const auto keys = wrap(Face::Mono, "[E] request \xc2\xb7 [TAB] sections \xc2\xb7 [ESC] close", 11.0f, aw * s);
                    float ky = 720.0f - 22.0f - keys.size() * 11.0f * 1.3f;
                    for (const AZStd::string& k : keys)
                    {
                        textIn(Face::Mono, k, X(ax), ky, 11.0f, 1.3f, TermMuted);
                        ky += 11.0f * 1.3f;
                    }
                }
            }
            for (int layer = 0; layer < AZ::GetClamp(static_cast<int>(hud_orders_layers), 1, 8); ++layer)
            {
                AZ::RPI::AuxGeomDraw::AuxGeomDynamicDrawArguments args;
                args.m_verts = tv.data();
                args.m_vertCount = static_cast<uint32_t>(tv.size());
                args.m_colors = tc.data();
                args.m_colorCount = static_cast<uint32_t>(tc.size());
                args.m_opacityType = AZ::RPI::AuxGeomDraw::OpacityType::Translucent;
                args.m_depthTest = AZ::RPI::AuxGeomDraw::DepthTest::Off;
                args.m_depthWrite = AZ::RPI::AuxGeomDraw::DepthWrite::Off;
                args.m_viewProjectionOverrideIndex = overlay;
                draw->DrawTriangles(args);
            }
            m_click = false;
        }
        // ---- main menu: Main.dc.html (the live Level 0 behind it instead of the mockup's still) ----
        else if (m_page == Page::MainMenu)
        {
            const AZ::Vector2 cursor(m_cursor.GetX() * W, m_cursor.GetY() * H);
            const AZ::Color dim = Hex(0xb8ad86);
            const AZ::Color menuText = Hex(0xe8dfc0);
            const AZ::Color menuBright = Hex(0xfff1b0);
            const AZ::Color menuGold = Hex(0xd8c35a);
            const AZ::Color menuMuted = Hex(0x7d7560);
            // Text in a CSS line box of lineHeight x size (see the terminal's textIn).
            auto textIn = [&](Face face, const AZStd::string& value, float x, float top, float size, float lineHeight,
                              const AZ::Color& c, int align = 0, float spacing = 0.0f)
            { return text(face, value, x, Y(top + (lineHeight - 1.2f) * size * 0.5f), size, c, align, spacing, true); };

            // linear-gradient(90deg, rgba(10,9,6,.94) 0%, .86 34%, .25 62%, .55 100%) across the window.
            {
                const float stops[4][2] = { { 0.0f, 0.94f }, { 0.34f, 0.86f }, { 0.62f, 0.25f }, { 1.0f, 0.55f } };
                for (int k = 0; k < 3; ++k)
                {
                    lquad(W * stops[k][0], 0.0f, W * stops[k + 1][0], H, Hex(0x0a0906, stops[k][1]), Hex(0x0a0906, stops[k + 1][1]));
                }
                // Scanlines: rgba(0,0,0,.12), 1 px every 3 px.
                for (float y = 0.0f; y < DesignH; y += 3.0f)
                {
                    lrect(0.0f, Y(y), W, AZStd::max(1.0f, s), AZ::Color(0.0f, 0.0f, 0.0f, 0.12f));
                }
            }

            // Left column: x 72, from 64 down to 664, 460 wide.
            // Header: the Foundation square and "FOUNDATION · MTF DEPLOYMENT TERMINAL".
            {
                const float k = 22.0f / 22.0f;
                loutline(L(72.0f + 1.5f * k - 0.75f), Y(64.0f + 1.5f * k - 0.75f), 19.5f * s, 19.5f * s, AZStd::max(1.0f, 1.5f * s), menuGold);
                lrect(L(72.0f + 7.0f), Y(64.0f + 7.0f), 8.0f * s, 8.0f * s, menuGold);
                textIn(Face::Mono, "FOUNDATION \xc2\xb7 MTF DEPLOYMENT TERMINAL", L(106.0f), 75.0f - 7.8f, 12.0f, 1.3f, dim, 0, 3.0f);
            }
            // NOCLIP / PROTOCOL: Oswald 700, 72 px, line-height 0.92, letter-spacing 2.
            textIn(Face::Oswald, "NOCLIP", L(72.0f), 110.0f, 72.0f, 0.92f, Hex(0xf1e6b8), 0, 2.0f);
            textIn(Face::Oswald, "PROTOCOL", L(72.0f), 110.0f + 66.24f, 72.0f, 0.92f, Hex(0xf1e6b8), 0, 2.0f);
            // The line under it: 14 px, line-height 1.6, at most 440 wide.
            float y = 110.0f + 2.0f * 66.24f + 16.0f;
            for (const AZStd::string& lineText : wrap(Face::Mono,
                     "Breach confirmed beneath Site-[REDACTED]. Mobile Task Force cleared to enter Level 0 and establish a forward base.",
                     14.0f, 440.0f * s))
            {
                textIn(Face::Mono, lineText, L(72.0f), y, 14.0f, 1.6f, dim);
                y += 14.0f * 1.6f;
            }

            // The menu: six items, 2 px apart, each 8 px padding around a 21 px Oswald line.
            y += 24.0f;
            struct Item { const char* label; const char* note; int action; bool muted; };
            const AZStd::string continueNote = m_lastLevel.empty() ? AZStd::string("no expedition yet")
                : (m_lastWhen.empty() ? AZStd::string("") : m_lastWhen + " \xc2\xb7 ") + LevelLabel(m_lastLevel);
            const Item items[6] = { { "CONTINUE", "", ActContinue, false }, { "NEW EXPEDITION", nullptr, ActNew, false },
                                    { "CO-OP", "host or join a squad", ActCoop, false }, { "FOUNDATION ARCHIVE", nullptr, ActArchive, false },
                                    { "SETTINGS", nullptr, ActSettings, false }, { "QUIT", nullptr, ActQuit, true } };
            const float itemH = 8.0f + 21.0f * 1.482f + 8.0f;
            for (int i = 0; i < 6; ++i)
            {
                const float top = y + i * (itemH + 2.0f);
                const bool hover = cursor.GetX() >= L(72.0f) && cursor.GetX() <= L(532.0f) && cursor.GetY() >= Y(top) && cursor.GetY() <= Y(top + itemH);
                if (hover)
                {
                    m_menuSelected = i;
                }
                const bool selected = i == m_menuSelected;
                if (selected)
                {
                    // The mockup's highlighted item: gold left border, rgba(232,210,110,.12).
                    lrect(L(72.0f), Y(top), 460.0f * s, itemH * s, AZ::Color(232 / 255.0f, 210 / 255.0f, 110 / 255.0f, 0.12f));
                    lrect(L(72.0f), Y(top), 3.0f * s, itemH * s, menuGold);
                }
                const AZ::Color c = selected ? menuBright : (hover ? Hex(0xfff4c4) : (items[i].muted ? Hex(0x9c9274) : menuText));
                textIn(Face::OswaldMedium, items[i].label, L(91.0f), top + 8.0f, 21.0f, 1.482f, c, 0, 2.0f);
                const bool notice = i == 2 && !m_menuNotice.empty() && m_time < m_menuNoticeUntil; // "co-op isn't in yet"
                const AZStd::string note = i == 0 ? continueNote : (notice ? m_menuNotice : AZStd::string(items[i].note ? items[i].note : ""));
                if (!note.empty())
                {
                    // Baseline-aligned with the label: Oswald's baseline sits 25 px down its 31 px line.
                    textIn(Face::Mono, note, L(516.0f), top + 8.0f + 25.05f - 12.3f, 12.0f, 1.3f, notice ? menuGold : dim, 2);
                }
                if (hover && m_click)
                {
                    m_click = false;
                    DoAction(items[i].action);
                }
            }

            // Footer, 11 px, line-height 1.7: at the column's foot (664), or right under the menu when that runs longer
            // (as in the mockup at 1280 x 720, where it starts at 641.6).
            const float footer = AZStd::max(664.0f - 2.0f * 18.7f, y + 6.0f * itemH + 5.0f * 2.0f);
            textIn(Face::Mono, "DEV BUILD \xc2\xb7 Hearth Engine", L(72.0f), footer, 11.0f, 1.7f, Hex(0x968c6c));
            textIn(Face::Mono, "Based on the SCP Foundation, scp-wiki.wikidot.com \xc2\xb7 CC BY-SA 3.0", L(72.0f), footer + 18.7f, 11.0f, 1.7f,
                Hex(0x968c6c));

            // Squad panel, top right: 300 wide inside 16 / 18 px padding, 1 px border.
            {
                const float x0 = R(1280.0f - 56.0f - 338.0f);
                const float panelH = 1.0f + 16.0f + 11.0f * 1.3f + 12.0f + 4.0f * 13.0f * 1.3f + 3.0f * 8.0f + 16.0f + 1.0f;
                lrect(x0, Y(64.0f), 338.0f * s, panelH * s, AZ::Color(10 / 255.0f, 9 / 255.0f, 6 / 255.0f, 0.78f));
                loutline(x0, Y(64.0f), 338.0f * s, panelH * s, AZStd::max(1.0f, s), AZ::Color(216 / 255.0f, 195 / 255.0f, 90 / 255.0f, 0.35f));
                const float cx = x0 + 19.0f * s;
                const float right = x0 + (338.0f - 19.0f) * s;
                textIn(Face::Mono, "SQUAD", cx, 81.0f, 11.0f, 1.3f, dim, 0, 2.0f);
                textIn(Face::Mono, "1 / 4", right, 81.0f, 11.0f, 1.3f, dim, 2, 2.0f);
                const AZStd::string name = RegString("/NoclipProtocol/Player/Name", "Dragongamer9");
                float rowY = 81.0f + 11.0f * 1.3f + 12.0f;
                for (int r = 0; r < 4; ++r)
                {
                    const float mid = rowY + 13.0f * 1.3f * 0.5f;
                    if (r == 0)
                    {
                        lrect(cx, Y(mid - 4.0f), 8.0f * s, 8.0f * s, menuGold);
                        textIn(Face::Mono, name, cx + 18.0f * s, rowY, 13.0f, 1.3f, menuText);
                        textIn(Face::Mono, "LEAD", right, rowY + 1.0f, 11.0f, 1.3f, dim, 2);
                    }
                    else
                    {
                        // 8 px box plus its 1 px border: 10 x 10, the name 10 px after it.
                        loutline(cx, Y(mid - 5.0f), 10.0f * s, 10.0f * s, AZStd::max(1.0f, s), menuMuted);
                        textIn(Face::Mono, "open slot", cx + 20.0f * s, rowY, 13.0f, 1.3f, menuMuted);
                    }
                    rowY += 13.0f * 1.3f + 8.0f;
                }
            }

            // Bottom right: red dot, "SIGNAL LOST · LEVEL 0".
            {
                const AZStd::string signal = "SIGNAL LOST \xc2\xb7 LEVEL 0";
                const float right = R(1280.0f - 56.0f - 2.0f);
                const float w = measure(Face::Mono, signal, 12.0f, 2.0f);
                textIn(Face::Mono, signal, right, 664.0f - 15.6f, 12.0f, 1.3f, menuGold, 2, 2.0f);
                // An 8 px circle, 10 px before the text.
                const float dcx = right - w - 10.0f * s - 4.0f * s;
                const float dcy = Y(664.0f - 7.8f);
                const float rr = 4.0f * s;
                for (int k = 0; k < 16; ++k)
                {
                    const float a0 = 6.2831853f * k / 16.0f;
                    const float a1 = 6.2831853f * (k + 1) / 16.0f;
                    const AZ::Color red = Hex(0xc4412f);
                    lv.push_back(P(dcx, dcy)); lc.push_back(red);
                    lv.push_back(P(dcx + std::cos(a0) * rr, dcy + std::sin(a0) * rr)); lc.push_back(red);
                    lv.push_back(P(dcx + std::cos(a1) * rr, dcy + std::sin(a1) * rr)); lc.push_back(red);
                }
            }
            lflush(4);
            m_click = false;
        }
        // ---- pause menu and settings ----
        else if (m_page != Page::None)
        {
            lrect(0.0f, 0.0f, W, H, AZ::Color(8 / 255.0f, 7 / 255.0f, 4 / 255.0f, 0.78f));
            m_buttons.clear();
            const AZ::Vector2 cursor(m_cursor.GetX() * W, m_cursor.GetY() * H);
            auto button = [&](const AZStd::string& label, float y, int action, bool primary = false)
            {
                const float x = C(490);
                const float w = 300.0f * s;
                const float h = 44.0f * s;
                const bool hover = cursor.GetX() >= x && cursor.GetX() <= x + w && cursor.GetY() >= Y(y) && cursor.GetY() <= Y(y) + h;
                lrect(x, Y(y), w, h, hover ? AZ::Color(Gold.GetR(), Gold.GetG(), Gold.GetB(), 0.25f) : Panel);
                lrect(x, Y(y), 3.0f * s, h, primary || hover ? Gold : Line);
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
                const float bx0 = C(830) - bw * 2.0f - 90.0f * s;
                for (int k = 0; k < (up ? 2 : 1); ++k)
                {
                    const float bx = k == 0 ? (up ? bx0 : C(830) - 90.0f * s) : C(830) - bw;
                    const bool hover = cursor.GetX() >= bx && cursor.GetX() <= bx + bw && cursor.GetY() >= Y(y) && cursor.GetY() <= Y(y) + 24.0f * s;
                    loutline(bx, Y(y), up ? bw : 90.0f * s, 24.0f * s, AZStd::max(1.0f, 1.5f * s), hover ? Gold : Line);
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
            lflush(3);
            m_click = false;
        }
    }
} // namespace NoclipProtocol
