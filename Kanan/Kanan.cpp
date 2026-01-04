#pragma comment(lib, "urlmon")

#include <imgui.h>
#include <imgui_freetype.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

#include <Scan.hpp>
#include <Config.hpp>
#include <filesystem>
#include <iostream>
#include <String.hpp>
#include <Utility.hpp>

#include "FontData.hpp"
#include "Log.hpp"
#include "Kanan.hpp"
#include "MabiMessageHook.hpp"
#include "../Kanan/metrics_gui/metrics_gui.h"
#include "Hotkey.hpp"
#include "Version.h"

using namespace std;

IMGUI_IMPL_API LRESULT  ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace kanan {
    unique_ptr<Kanan> g_kanan{ nullptr };

    //metrics
    MetricsGuiMetric frameTimeMetric("Frame time", "s", MetricsGuiMetric::USE_SI_UNIT_PREFIX);

    MetricsGuiPlot frameTimePlot;

	Hotkey  m_key;
	Hotkey  m_housingKey;
	Hotkey  m_astralKey;

    Kanan::Kanan(string path)
        : m_path{ move(path) },
        m_uiConfigPath{ m_path + "/ui.ini" },
        m_modConfigPath{ m_path + "/config.txt" },
        m_batchPath{ m_path + "/ExtractKanan.bat" },
        m_updatePath{ m_path + "/KananUpdate.zip" },
        m_astralPath{ m_path + "/astral.ini" },
        m_d3d9Hook{ nullptr },
        m_dinputHook{ nullptr },
        m_mesHook{ nullptr },
        m_wmHook{ nullptr },
        m_game{ nullptr },
        m_mods{ m_path },
        m_isUpdate{ false },
        m_isNotifyUpdate{ true },
        m_isMp3Fixed{ false },
        m_defaultMods{ true },
        m_interactiveWindows{ false },
        m_isUIOpen{ true },
        m_isLogOpen{ false },
        m_isAboutOpen{ false },
        m_isUpdateOpen{ false },
        m_isInitialized{ false },
        m_areModsReady{ false },
        m_areModsLoaded{ false },
        m_wnd{ nullptr },
        m_modWindowEnabled{ false },
        m_isUIOpenByDefault{ true }
    {
        log("Entering Kanan constructor.");

        //
        // Hook D3D9 and set the callbacks.
        //
        log("Hooking D3D9...");

        m_d3d9Hook = make_unique<D3D9Hook>();

        m_d3d9Hook->onPresent = [this](auto&) { onFrame(); };
        m_d3d9Hook->onPreReset = [](auto&) { ImGui_ImplDX9_InvalidateDeviceObjects(); };
        m_d3d9Hook->onPostReset = [](auto&) { ImGui_ImplDX9_CreateDeviceObjects(); };

        if (!m_d3d9Hook->isValid()) {
            error("Failed to hook D3D9.");
        }

        //
        // We initialize mods now because this constructor is still being executed
        // from the startup thread so we can take as long as necessary to do so here.
        //
        initializeMods();


        //metrics settings

        frameTimeMetric.mSelected = true;

        frameTimePlot.mBarRounding = 0.f;    // amount of rounding on bars
        frameTimePlot.mRangeDampening = 0.95f;  // weight of historic range on axis range [0,1]
        frameTimePlot.mInlinePlotRowCount = 2;      // height of DrawList() inline plots, in text rows
        frameTimePlot.mPlotRowCount = 5;      // height of DrawHistory() plots, in text rows
        frameTimePlot.mVBarMinWidth = 6;      // min width of bar graph bar in pixels
        frameTimePlot.mVBarGapWidth = 1;      // width of bar graph inter-bar gap in pixels
        frameTimePlot.mShowAverage = true;  // draw horizontal line at series average
        frameTimePlot.mShowInlineGraphs = false;  // show history plot in DrawList()
        frameTimePlot.mShowOnlyIfSelected = false;  // draw show selected metrics
        frameTimePlot.mShowLegendDesc = true;   // show series description in legend
        frameTimePlot.mShowLegendColor = true;   // use series color in legend
        frameTimePlot.mShowLegendUnits = true;   // show units in legend values
        frameTimePlot.mShowLegendAverage = false;  // show series average in legend
        frameTimePlot.mShowLegendMin = true;   // show plot y-axis minimum in legend
        frameTimePlot.mShowLegendMax = true;   // show plot y-axis maximum in legend
        frameTimePlot.mBarGraph = true;  // use bars to draw history
        frameTimePlot.mStacked = true;  // stack series when drawing history
        frameTimePlot.mSharedAxis = false;  // use first series' axis range
        frameTimePlot.mFilterHistory = true;   // allow single plot point to represent more than on history value

        frameTimePlot.AddMetric(&frameTimeMetric);

        log("Leaving Kanan constructor.");
    }

    void Kanan::initializeMods() {
        if (m_areModsReady) {
            return;
        }

        //
        // Initialize the Game object.
        //
        log("Creating the Game object...");

        m_game = make_unique<Game>();

        //
        // Initialize all the mods.
        //
        log("Loading mods...");

        m_mods.loadMods();

        log("Done initializing.");

        m_areModsReady = true;
    }

    void Kanan::onInitialize() {
        if (m_isInitialized) {
            return;
        }

        log("Beginning intialization... ");

        // Grab the HWND from the device's creation parameters.
        log("Getting window from D3D9 device...");

        auto device = m_d3d9Hook->getDevice();
        D3DDEVICE_CREATION_PARAMETERS creationParams{};

        device->GetCreationParameters(&creationParams);

        m_wnd = creationParams.hFocusWindow;

        //
        // ImGui.
        //
        log("Initializing ImGui...");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();

        auto& io = ImGui::GetIO();

        io.IniFilename = m_uiConfigPath.c_str();
        ImFontConfig config;
        config.MergeMode = true;
        io.Fonts->AddFontFromMemoryCompressedTTF(g_font_compressed_data, g_font_compressed_size, 16.0f);
        io.Fonts->AddFontFromMemoryCompressedTTF(g_font_compressed_data, g_font_compressed_size, 16.0f, &config, io.Fonts->GetGlyphRangesJapanese());
        io.Fonts->AddFontFromMemoryCompressedTTF(g_font_compressed_data, g_font_compressed_size, 16.0f, &config, io.Fonts->GetGlyphRangesKorean());
        ImGuiFreeType::BuildFontAtlas(io.Fonts, 0);

        if (!ImGui_ImplWin32_Init(m_wnd)) {
            error("Failed to initialize ImGui.");
        }

        if (!ImGui_ImplDX9_Init(device)) {
            error("Failed to initialize ImGui.");
        }

        ImGui::StyleColorsDark();

        //
        // DInputHook.
        //
        log("Hooking DInput...");

        m_dinputHook = make_unique<DInputHook>(m_wnd);

        m_dinputHook->onKeyDown = [this](DInputHook& dinput, DWORD key) {
            for (auto&& mod : m_mods.getMods()) {
                mod->onKeyDown(key);
            }
        };
        m_dinputHook->onKeyUp = [this](DInputHook& dinput, DWORD key) {
            for (auto&& mod : m_mods.getMods()) {
                mod->onKeyUp(key);
            }
        };

        if (!m_dinputHook->isValid()) {
            error("Failed to hook DInput.");
        }

        //
        // WindowsMessageHook.
        //
        log("Hooking the windows message procedure...");

        m_wmHook = make_unique<WindowsMessageHook>(m_wnd);

        m_wmHook->onMessage = [this](auto wnd, auto msg, auto wParam, auto lParam) {
            return onMessage(wnd, msg, wParam, lParam);
        };

        if (!m_wmHook->isValid()) {
            error("Failed to hook windows message procedure.");
        }

        //
        // Time critical mods.
        //
        log("Loading time critical mods...");

        m_mods.loadTimeCriticalMods();

        m_mesHook = make_unique<MabiMessageHook>(&(m_mods.m_messageMods));

        m_isInitialized = true;
    }

    void Kanan::onFrame() {
        if (!m_isInitialized) {
            onInitialize();
        }

        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        auto& io = ImGui::GetIO();

        //update our metrics with framerate data
        frameTimeMetric.AddNewValue(1.f / io.Framerate);
        frameTimePlot.UpdateAxes();

        if (m_areModsReady) {
            // Make sure the config for all the mods gets loaded.
            if (!m_areModsLoaded) {
                loadConfig();
            }

            if (!m_isMp3Fixed) {
                fixMabiProMp3();
            }

            for (const auto& mod : m_mods.getMods()) {
                mod->onFrame();
            }

            if (wasKeyPressed(m_key.hotkey)) {
                m_isUIOpen = !m_isUIOpen;
				
                // Save the config whenever the menu closes.
                if (!m_isUIOpen) {
                    saveConfig();
                }
            }

			if (wasKeyPressed(m_housingKey.hotkey)) {
				housingBoard();
			}

			if (wasKeyPressed(m_astralKey.hotkey)) {
				viewAstralWorld();
			}

            if (m_isUIOpen || (m_interactiveWindows && m_modWindowEnabled)) {
                // Block input if the user is interacting with the UI.

                if (io.WantCaptureMouse || io.WantCaptureKeyboard || io.WantTextInput) {
                    m_dinputHook->ignoreInput();
                }
                else {
                    m_dinputHook->acknowledgeInput();
                }
            }
            else {
                m_dinputHook->acknowledgeInput();
            }

            if (m_isUIOpen)
            {
                drawUI();

                if (m_isLogOpen) {
                    drawLog(&m_isLogOpen);
                }

                if (m_isAboutOpen) {
                    drawAbout();
                }

                if (m_isUpdate) {
                    drawUpdateMessage();
                }

                if (m_defaultMods) {
                    drawDefaultMods();
                }
            }

            //draw metric window
            if (m_ismetricsopen) {
                Drawmetrics();
            }

            m_modWindowEnabled = false;

            for (const auto& mod : m_mods.m_messageMods) {
                m_modWindowEnabled = mod->onWindow() || m_modWindowEnabled;
            }

            for (const auto& mod : m_mods.getMods()) {
                m_modWindowEnabled = mod->onWindow() || m_modWindowEnabled;
            }

        }
        else {
            ImGui::OpenPopup("Loading...");
            ImGui::SetNextWindowPosCenter(ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2{ 450.0f, 200.0f });

            if (ImGui::BeginPopupModal("Loading...", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
                ImGui::TextWrapped("Kanan is currently setting things up. Please wait a moment...");
                ImGui::EndPopup();
            }

            auto& io = ImGui::GetIO();

            if (io.WantCaptureMouse || io.WantCaptureKeyboard || io.WantTextInput) {
                m_dinputHook->ignoreInput();
            }
            else {
                m_dinputHook->acknowledgeInput();
            }
        }

        ImGui::EndFrame();






        // This fixes mabi's Film Style Post Shader making ImGui render as a black box.
        auto device = m_d3d9Hook->getDevice();

        device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    }

    bool Kanan::onMessage(HWND wnd, UINT message, WPARAM wParam, LPARAM lParam) {

        if (m_isUIOpen || (m_interactiveWindows && m_modWindowEnabled)) {
            if (ImGui_ImplWin32_WndProcHandler(wnd, message, wParam, lParam) != 0) {
                // If the user is interacting with the UI we block the message from going to the game.
                auto& io = ImGui::GetIO();

                if (io.WantCaptureMouse || io.WantCaptureKeyboard || io.WantTextInput) {
                    return false;
                }
            }
        }

        for (auto& mod : m_mods.getMods()) {
            if (!mod->onMessage(wnd, message, wParam, lParam)) {
                return false;
            }
        }

        return true;
    }

    bool Kanan::checkVersion()
    {
        IStream* lpSrc;
        const ULONG size = 70;
        char szBuffer[size];

        memset(szBuffer, 0, size);

        // Delete previously created batch file
        if (std::filesystem::exists(m_batchPath)) std::filesystem::remove(m_batchPath);

        if (URLOpenBlockingStream(NULL, L"https://raw.githubusercontent.com/ryuugana/kanan-mabipro/master/Kanan/Version.h", &lpSrc, 0, NULL) != S_OK)
        {
            return false;
        }
        else
        {
            lpSrc->Read(szBuffer, size - 1, NULL);
        }

        string remoteVersion = "";

        for each (char var in szBuffer)
        {
            if (var >= '0' && var <= '9')
            {
                remoteVersion.push_back(var);
            }
        }

        log("Local version is %d and remote version is %s", version, remoteVersion.c_str());

        return version < atoi(remoteVersion.c_str());
    }

    void Kanan::updateKanan()
    {
        URLDownloadToFileA(NULL, "https://github.com/ryuugana/kanan-mabipro/releases/latest/download/KananMabiPro.zip", m_updatePath.c_str(), 0, NULL);

        std::ofstream batch_file(m_batchPath);
        batch_file <<
            "echo \"Extracting Kanan Update\"\n"
            "timeout /t 5 /nobreak\n"
            "tar -xf KananUpdate.zip\n"
            "del KananUpdate.zip\n"
            "MabiProLauncher22.exe\n";
        batch_file.close();

        PROCESS_INFORMATION processInformation = { 0 };
        STARTUPINFOA startupInfo = { 0 };
        BOOL result = CreateProcessA(NULL,
            const_cast<char*>(m_batchPath.c_str()),
            NULL,
            NULL,
            FALSE,
            CREATE_NO_WINDOW,
            NULL,
            NULL,
            &startupInfo,
            &processInformation);

        if(result) exit(0);
        log("Failed to update Kanan.");
    }

    void Kanan::applyDefaultMods(bool astralWorld)
    {
        if (astralWorld)
        {
            std::ofstream astralConfig(m_astralPath);
            astralConfig <<
                ";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n"
                ";\n"
                ";                          MABINOGI FANTASIA PATCH\n"
                ";                            - created by spr33 -\n"
                ";\n"
                ";	Copyright (C) Annyeong 2019, spr33 2009, chris & syoka 2008\n"
                ";	Special thanks to Blade3575 for creating Astral, for many of his additional\n"
                ";	  patches to the base patcher were disassembled from his work.\n"
                ";	Thanks to chris & syoka for starting memory patchers for Mabinogi,\n"
                ";	  and Sokcuri for the alarm patch and mss32 hook.\n"
                ";\n"
                ";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n"
                "\n"
                "[PATCH]\n"
                "; Sleep time for menu hook (milliseconds)\n"
                "; 20000 = 20 seconds\n"
                "WaitMenuHook=20000\n"
                "\n"
                "; Sets Thread Priority\n"
                ";  15 =  Realtime\n"
                ";   2 =  High\n"
                ";   1 =  Above Normal\n"
                ";   0 =  Normal\n"
                ";  -1 =  Below Normal\n"
                ";  -2 =  Low\n"
                "; -15 =  Idle\n"
                "SetThreadPriority=2\n"
                "\n"
                "; Disable Data Folder usage\n"
                "; By default MabiPro already has it turned on\n"
                "DisableDataFolder=0\n"
                "\n"
                "; Reduce the level of CPU usage, optionally only while minimized (1~100).\n"
                "CPUReduction=90\n"
                "CPUReduction_OnlyMinimized=1\n"
                "\n"
                "; Block the popup ads on game exit.\n"
                "BlockEndingAds=0\n"
                "\n"
                "; Clear fog of war on dungeon minimaps.\n"
                "ClearDungeonFog=1\n"
                "\n"
                "; Sets the time of the sky\n"
                "; 0 = Sets the time to normal\n"
                "; 1 = Disable night time\n"
                "; 2 = Sets the time to always be night\n"
                "SetSkyTime=0\n"
                "\n"
                "; Enable coloring of ALT names based on character type.\n"
                "EnableNameColoring=1\n"
                "\n"
                "; Enable opening the right-click menu on your own character.\n"
                "EnableSelfRightClick=1\n"
                "\n"
                "; Enable opening of player shops from a distance.\n"
                "EnterRemoteShop=0\n"
                "\n"
                "; Increase the size of the in-game clock text.  Can cause cut-off text.\n"
                "LargeClockText=0\n"
                "\n"
                "; Modify the maximum zoom distance (1~50000).\n"
                "ModifyZoomLimit=15000\n"
                "\n"
                "; Allow moving while talking to NPCs.\n"
                "MoveWhileTalking=1\n"
                "\n"
                "; Remove the 30-second login delay after disconnecting from the server.\n"
                "RemoveLoginDelay=1\n"
                "\n"
                "; Modify the quality of screenshots.\n"
                "ScreenshotQuality=90\n"
                "\n"
                "; Show combat power numerically.\n"
                "ShowCombatPower=1\n"
                "ShowMaxHP=1\n"
                "\n"
                "; Shows the percentage towards your next exploration level in the character window.\n"
                "ShowExplorationPercent=1\n"
                "\n"
                "; Show the shop purchase and selling price in item descriptions.\n"
                "ShowItemPrice=1\n"
                "\n"
                "; Show item durability with 1000x precision.\n"
                "; The formatting string works by replacing values as follows: \n"
                "; {0} => Current dura        i.e. 14\n"
                "; {1} => Current dura x1000       13560 \n"
                "; {2} => Maximum dura             15\n"
                "; {3} => Maximum dura x1000       15000\n"
                "; For example: \"{1}/{3} ({0}/{2})\" => \"13560/15000 (14/15)\"\n"
                "ShowTrueDurability=1\n"
                "ShowTrueDurability_str=\"Durability {1}/{3} ({0}/{2})\"\n"
                "\n"
                "; Show item color codes.\n"
                "; Must have ShowTrueDurability enabled to work.\n"
                "ShowItemColor=1\n"
                "\n"
                "; Show food quality numerically.\n"
                "ShowTrueFoodQuality=0\n"
                "\n"
                "; Allow conversation with unequipped spirit weapons.\n"
                "TalkToUnequippedEgo=1\n"
                "\n"
                "; Enable CTRL-targeting props while in combat mode.\n"
                "TargetProps=0\n"
                "\n"
                "; Use bitmap fonts instead of vector fonts to prevent window lag.\n"
                "UseBitmapFonts=0\n"
                "\n"
                "; Enable Elf Lag Fix\n"
                "ElfLagFix=0\n"
                "\n"
                "; Show Negative HP\n"
                "ShowNegativeHP=1\n"
                "\n"
                "; Show Negative Stats\n"
                "ShowNegativeStats=1\n"
                "\n"
                "; Show Clock Minutes\n"
                "ShowClockMinutes=0\n"
                "\n"
                "; No Mount Timeout\n"
                "NoMountTimeout=1\n"
                "\n"
                "; No Channel Penalty Msg\n"
                "; Disables that annoying msg box when you change channels during/after combat\n"
                "NoChannelPenaltyMsg=1\n"
                "\n"
                "; No Channel Move Denial\n"
                "; (Allows you to move in the middle of talking to NPC, etc.)\n"
                "NoChannelMoveDenial=1\n"
                "\n"
                "; Enable Cutscene Skip\n"
                "EnableCutsceneSkip=1\n"
                "\n"
                "; Target Resting Enemies\n"
                "TargetRestingEnemies=1\n"
                "\n"
                "; Display Names From Far away\n"
                "DisplayNamesFar=1\n"
                "\n"
                "; Disable Sunlight Glare\n"
                "DisableSunlightGlare=0\n"
                "\n"
                "; Disable Gray Fog\n"
                "DisableGrayFog=0\n"
                "\n"
                "; Party Board To Housing\n"
                "PartyBoardToHousing=0\n"
                "\n"
                "; Show Detailed FPS\n"
                "ShowDetailedFPS=0\n"
                "\n"
                "; Show Simple FPS\n"
                "ShowSimpleFPS=1\n"
                "\n"
                "; Set Item Split Quantity\n"
                "ItemSplitQuantity=1\n"
                "\n"
                "; Default Ranged Swap\n"
                "; 0 = Ranged Attack\n"
                "; 1 = Magnum Shot\n"
                "; 2 = Mari's Arrow Revolver\n"
                "; 3 = Arrow Revolver\n"
                "; 4 = Support Shot\n"
                "; 5 = Mirage Missile\n"
                "; 6 = Crash Shot\n"
                "DefaultRangedSwap=0\n"
                "\n"
                "; Uncap Auto-Production\n"
                "UncapAutoProduction=1\n"
                "\n"
                "; Modify Render Distance (5000~100000)\n"
                "; Set to 0 to keep render distance as default\n"
                "ModifyRenderDistance=20000\n"
                "\n"
                "; Stay as Alchemy Golem\n"
                "; Prevents Character Snapback when out of range\n"
                "StayAsAlchemyGolem=1\n"
                "\n"
                "; Disable Screen Shake\n"
                "DisableScreenShake=1\n"
                "\n"
                "; Enable Naked Mode\n"
                "; Headless, cloth-less mode\n"
                "EnableNakedMode=0\n"
                "\n"
                "; Disable Cloud Render\n"
                "DisableCloudRender=0\n"
                "\n"
                "; Show Poison Durability\n"
                "ShowPoisonDurability=0\n"
                "\n"
                "; Show True HP\n"
                "ShowTrueHP=1\n"
                "\n"
                "; Show Item ID\n"
                "ShowItemID=1\n"
                "\n"
                "; Disable Flashy Dyes\n"
                "DisableFlashyDyes=0\n"
                "\n"
                "; Enable NPC Equip View\n"
                "EnableNPCEquipView=1\n"
                "\n"
                "; Enable Minimap Zoom\n"
                "; Credits to Rydian\n"
                "EnableMinimapZoom=1\n"
                "\n"
                "; Combat Mastery Swap\n"
                "; Provide the Skill ID in the field\n"
                "; 0 to turn off\n"
                "CombatMasterySwap=0\n"
                "\n"
                "; Enable User Commands\n"
                "UserCommands=0\n"
                "\n"
                "; Enable TrueType Font\n"
                "; Incompatible with bitmap\n"
                "EnableTTF=0\n"
                "\n"
                "; Modify Font Size\n"
                "; Enter a number from 1 to 30 to change font size to that number\n"
                "; Default font size in-game is 11\n"
                "ModifyFontSize=0\n"
                "\n"
                "; Faster Interface Windows\n"
                "; Disables animations when opening and closing windows such as the character or skill window\n"
                "FasterInterfaceWindows=1\n"
                "\n"
                "; Show Objects in Hide\n"
                "; Reveals things that go in hide such as elves\n"
                "ShowObjectsInHide=0\n"
                "\n"
                "; Show Unknown Quest Objectives\n"
                "; Shows quest objectives not yet revealed\n"
                "ShowUnknownQuestObjectives=1\n"
                "\n"
                "; Show Unknown Skill Requirements\n"
                "; Reveals the hidden skill requirements to train the skill\n"
                "ShowUnknownSkillRequirements=1\n"
                "\n"
                "; Show Unknown Upgrades\n"
                "; Reveals skill requirements that have yet to be unlocked\n"
                "ShowUnknownUpgrades=1\n"
                "\n"
                "; Show Unknown Titles\n"
                "; Reveals all the titles in the title list\n"
                "ShowUnknownTitles=1\n"
                "\n"
                "; Enable Instant Conversation\n"
                "; Immediately brings up the text conversation instead of trying to render it letter-by-letter\n"
                "InstantConversation=1\n"
                "\n"
                "; No Window Close On Talk\n"
                "; Prevents open windows from closing when talking to an NPC\n"
                "NoWindowCloseOnTalk=1\n"
                "\n"
                "; Warn Drop On All Items\n"
                "; Warns drop on all items as opposed to expensive items over 10,000 if enabled in game options\n"
                "WarnDropOnAllItems=0\n"
                "\n"
                "; Remove Blacklist Button\n"
                "; Removes the blacklist button when you right click someone\n"
                "RemoveBlacklistButton=0\n"
                "\n"
                "; ━─ Noginogi Alarm; imported from Noginogi-Party─ ━\n"
                "; * At the specified time, it informs you by the sentiment wave according to the specified message.\n"
                "; * If you modified the INI during game execution, please read the setting again for the application.\n"
                "; * The alarm function may be delayed from 1 minute to 2 ~ 3 minutes in game time.\n"
                "; * The function should be activated by setting \"0\" to \"1\".\n"
                "\n"
                "; - Whether or not Noginogi alarm function is used; if on, you'll be unable to turn off\n"
                "TimeAlarm =1\n"
                "\n"
                "; ──────────────────────────────────────────────────────────────────────────────────────────────────────\n"
                "; ── First alarm ─\n"
                "- Whether alarm is enabled (whether it is used individually or not).\n"
                "Alarm1_Using=1\n"
                "\n"
                "; - Exit message ( \"\n\" will be a line break)\n"
                "Alarm1_Text = \"TRANSFORMATION TIME!\"\n"
                "\n"
                "; - Set time (AlarmHour is set to 24, which tells you every hour)\n"
                "Alarm1_Hour=5\n"
                "\n"
                "; - Minutes to set\n"
                "Alarm1_Min = 50\n"
                "\n"
                " - Message type\n"
                "... 1: Flowing message (white)\n"
                "... 2: Flowing message (red)\n"
                "... 3: Central (Central subtitles)\n"
                "... 4: Center bottom (Sub subtitle)\n"
                "... 5: left center (weapon swap)\n"
                "... 6: Flowing message (green)\n"
                "... 7: Central subtitle + SYSTEM message\n"
                "... 8: Flowing message (green)\n"
                "... 9: Center subtitle blinks x 5\n"
                "Alarm1_Code=9\n"
                "\n"
                "── Second alarm\n"
                "; - Whether alarm is enabled (whether it is used individually or not).\n"
                "Alarm2_Using=0\n"
                "\n"
                "; - Exit message ( \"\n\" will be a line break)\n"
                "Alarm2_Text = \"<bold>I'm configured to alert every in-game hour!</bold>\"\n"
                "\n"
                "; - Set time (AlarmHour is set to 24, which tells you every hour)\n"
                "Alarm2_Hour=24\n"
                "\n"
                "; - Minutes to set\n"
                "Alarm2_Min = 0\n"
                "\n"
                " - Message type\n"
                "... 1: Flowing message (white)\n"
                "... 2: Flowing message (red)\n"
                "... 3: Central (Central subtitles)\n"
                "... 4: Center bottom (Sub subtitle)\n"
                "... 5: left center (weapon swap)\n"
                "... 6: Flowing message (green)\n"
                "... 7: Central subtitle + SYSTEM message\n"
                "... 8: Flowing message (green)\n"
                "... 9: Center subtitle blinks x 5\n"
                "Alarm2_Code=7\n"
                "\n"
                "──A third alarm ──\n"
                "; - Whether alarm is enabled (whether it is used individually or not).\n"
                "Alarm3_Using=0\n"
                "\n"
                "; - Exit message ( \"\n\" will be a line break)\n"
                "Alarm3_Text = \"This is the third alarm. \"\n"
                "\n"
                "; - Set time (AlarmHour is set to 24, which tells you every hour)\n"
                "Alarm3_Hour=24\n"
                "\n"
                "; - Minutes to set\n"
                "Alarm3_Min = 0\n"
                "\n"
                " - Message type\n"
                "... 1: Flowing message (white)\n"
                "... 2: Flowing message (red)\n"
                "... 3: Central (Central subtitles)\n"
                "... 4: Center bottom (Sub subtitle)\n"
                "... 5: left center (weapon swap)\n"
                "... 6: Flowing message (green)\n"
                "... 7: Central subtitle + SYSTEM message\n"
                "... 8: Flowing message (green)\n"
                "... 9: Center subtitle blinks x 5\n"
                "Alarm3_Code=2\n"
                "\n"
                "── Fourth alarm ─\n"
                "; - Whether alarm is enabled (whether it is used individually or not).\n"
                "Alarm4_Using=0\n"
                "\n"
                "; - Exit message ( \"\n\" will be a line break)\n"
                "Alarm4_Text = \" Fourth alarm. \"\n"
                "\n"
                "; - Set time (AlarmHour is set to 24, which tells you every hour)\n"
                "Alarm4_Hour=24\n"
                "\n"
                "; - Minutes to set\n"
                "Alarm4_Min = 0\n"
                "\n"
                " - Message type\n"
                "... 1: Flowing message (white)\n"
                "... 2: Flowing message (red)\n"
                "... 3: Central (Central subtitles)\n"
                "... 4: Center bottom (Sub subtitle)\n"
                "... 5: left center (weapon swap)\n"
                "... 6: Flowing message (green)\n"
                "... 7: Central subtitle + SYSTEM message\n"
                "... 8: Flowing message (green)\n"
                "... 9: Center subtitle blinks x 5\n"
                "Alarm4_Code=7\n"
                "\n"
                "──Fifth alarm ─\n"
                "; - Whether alarm is enabled (whether it is used individually or not).\n"
                "Alarm5_Using=0\n"
                "\n"
                "; - Exit message ( \"\n\" will be a line break)\n"
                "Alarm5_Text = \" Fifth alarm. \"\n"
                "\n"
                "; - Set time (AlarmHour is set to 24, which tells you every hour)\n"
                "Alarm5_Hour=24\n"
                "\n"
                "; - Minutes to set\n"
                "Alarm5_Min = 0\n"
                "\n"
                " - Message type\n"
                "... 1: Flowing message (white)\n"
                "... 2: Flowing message (red)\n"
                "... 3: Central (Central subtitles)\n"
                "... 4: Center bottom (Sub subtitle)\n"
                "... 5: left center (weapon swap)\n"
                "... 6: Flowing message (green)\n"
                "... 7: Central subtitle + SYSTEM message\n"
                "... 8: Flowing message (green)\n"
                "... 9: Center subtitle blinks x 5\n"
                "Alarm5_Code=7\n"
                "\n"
                "── Sixth alarm ──\n"
                "; - Whether alarm is enabled (whether it is used individually or not).\n"
                "Alarm6_Using=0\n"
                "\n"
                "; - Exit message ( \"\n\" will be a line break)\n"
                "Alarm6_Text = \"This is the sixth alarm. \"\n"
                "\n"
                "; - Set time (AlarmHour is set to 24, which tells you every hour)\n"
                "Alarm6_Hour=24\n"
                "\n"
                "; - Minutes to set\n"
                "Alarm6_Min = 0\n"
                "\n"
                " - Message type\n"
                "... 1: Flowing message (white)\n"
                "... 2: Flowing message (red)\n"
                "... 3: Central (Central subtitles)\n"
                "... 4: Center bottom (Sub subtitle)\n"
                "... 5: left center (weapon swap)\n"
                "... 6: Flowing message (green)\n"
                "... 7: Central subtitle + SYSTEM message\n"
                "... 8: Flowing message (green)\n"
                "... 9: Center subtitle blinks x 5\n"
                "Alarm6_Code=7\n"
                "\n"
                "The seventh alarm\n"
                "; - Whether alarm is enabled (whether it is used individually or not).\n"
                "Alarm7_Using=0\n"
                "\n"
                "; - Exit message ( \"\n\" will be a line break)\n"
                "Alarm7_Text = \"This is the seventh alarm. \"\n"
                "\n"
                "; - Set time (AlarmHour is set to 24, which tells you every hour)\n"
                "Alarm7_Hour=24\n"
                "\n"
                "; - Minutes to set\n"
                "Alarm7_Min = 0\n"
                "\n"
                " - Message type\n"
                "... 1: Flowing message (white)\n"
                "... 2: Flowing message (red)\n"
                "... 3: Central (Central subtitles)\n"
                "... 4: Center bottom (Sub subtitle)\n"
                    "... 5: left center (weapon swap)\n"
                    "... 6: Flowing message (green)\n"
                    "... 7: Central subtitle + SYSTEM message\n"
                    "... 8: Flowing message (green)\n"
                    "... 9: Center subtitle blinks x 5\n"
                    "Alarm7_Code=7\n"
                    "\n"
                    "──Eighth Alarm ──\n"
                    "; - Whether alarm is enabled (whether it is used individually or not).\n"
                    "Alarm8_Using=0\n"
                    "\n"
                    "; - Exit message ( \"\n\" will be a line break)\n"
                    "Alarm8_Text = \"This is the eighth alarm. \"\n"
                    "\n"
                    "; - Set time (AlarmHour is set to 24, which tells you every hour)\n"
                    "Alarm8_Hour=24\n"
                    "\n"
                    "; - Minutes to set\n"
                    "Alarm8_Min = 0\n"
                    "\n"
                    " - Message type\n"
                    "... 1: Flowing message (white)\n"
                    "... 2: Flowing message (red)\n"
                    "... 3: Central (Central subtitles)\n"
                    "... 4: Center bottom (Sub subtitle)\n"
                    "... 5: left center (weapon swap)\n"
                    "... 6: Flowing message (green)\n"
                    "... 7: Central subtitle + SYSTEM message\n"
                    "... 8: Flowing message (green)\n"
                    "... 9: Center subtitle blinks x 5\n"
                    "Alarm8_Code=7\n"
                    "\n"
                    "── Ninth alarm ─\n"
                    "; - Whether alarm is enabled (whether it is used individually or not).\n"
                    "Alarm9_Using=0\n"
                    "\n"
                    "; - Exit message ( \"\n\" will be a line break)\n"
                    "Alarm9_Text = \"The ninth alarm. \"\n"
                    "\n"
                    "; - Set time (AlarmHour is set to 24, which tells you every hour)\n"
                    "Alarm9_Hour=24\n"
                    "\n"
                    "; - Minutes to set\n"
                    "Alarm9_Min = 0\n"
                    "\n"
                    " - Message type\n"
                    "... 1: Flowing message (white)\n"
                    "... 2: Flowing message (red)\n"
                    "... 3: Central (Central subtitles)\n"
                    "... 4: Center bottom (Sub subtitle)\n"
                    "... 5: left center (weapon swap)\n"
                    "... 6: Flowing message (green)\n"
                    "... 7: Central subtitle + SYSTEM message\n"
                    "... 8: Flowing message (green)\n"
                    "... 9: Center subtitle blinks x 5\n"
                    "Alarm9_Code=7\n"
                    "\n"
                    "Tenth Alarm\n"
                    "; - Whether alarm is enabled (whether it is used individually or not).\n"
                    "Alarm10_Using=0\n"
                    "\n"
                    "; - Exit message ( \"\n\" will be a line break)\n"
                    "Alarm10_Text = \" Tenth alarm \"\n"
                    "\n"
                    "; - Set time (AlarmHour is set to 24, which tells you every hour)\n"
                    "Alarm10_Hour=24\n"
                    "\n"
                    "; - Minutes to set\n"
                    "Alarm10_Min = 0\n"
                    "\n"
                    " - Message type\n"
                    "... 1: Flowing message (white)\n"
                    "... 2: Flowing message (red)\n"
                    "... 3: Central (Central subtitles)\n"
                    "... 4: Center bottom (Sub subtitle)\n"
                    "... 5: left center (weapon swap)\n"
                    "... 6: Flowing message (green)\n"
                    "... 7: Central subtitle + SYSTEM message\n"
                    "... 8: Flowing message (green)\n"
                    "... 9: Center subtitle blinks x 5\n"
                    "Alarm10_Code=7\n"
                    "\n"
                    ";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n"
                    ";\n"
                    "; DEBUGGING OPTIONS\n"
                    "; Or, \"if everything works, DO NOT TOUCH\"\n"
                    ";\n"
                    ";;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;\n"
                    "\n"
                    "; Enable additional debugging information in the log file.\n"
                    "; Set this to 1 before posting any logs in a bug report!\n"
                    "Debug=0\n"
                    "\n"
                    "; Disable the CRT patch.\n"
                    "; Setting this to 1 will make it EXTREMELY likely for HackShield to detect you!\n"
                    "DisableCRTPatch = 1\n"
                    "\n"
                    "; Disable the menu modification and CPU limiting threads.\n"
                    "DisableExtraThreads = 0\n"
                    "UseDataFolder=0\n";
            astralConfig.close();
        }

        Config cfg{ m_modConfigPath };
        cfg.set<bool>("AssistantCharacterLocation.Enabled", true);
        cfg.set<bool>("AuctionMessageToChat.Enabled", true);
        cfg.set<bool>("AutoMute.Enabled", true);
        cfg.set<bool>("BlockSpam.Enabled", true);
        cfg.set<bool>("BlockPetPickupMessages.Enabled", true);
        cfg.set<bool>("BlockPetStatusMessages.Enabled", true);
        cfg.set<bool>("DelagSkill.Enabled", true);
        cfg.set<bool>("DisableSkillLocks.Enabled", true);
        cfg.set<bool>("DisableSkillRankUpMessage.Enabled", true);
        cfg.set<bool>("EnableMoneyLetters.Enabled", true);
        cfg.set<bool>("FastFlight.Enabled", true);
        cfg.set<bool>("FastNao.Enabled", true);
        cfg.set<bool>("FieldBossMessageToChat.Enabled", true);
        cfg.set<bool>("FieldBossNotify.Enabled", true);
        cfg.set<bool>("FixAstralWorldFlashy.Enabled", true);
        cfg.set<bool>("FixGiantCamera.Enabled", true);
        cfg.set<bool>("KeepPetWindowOpen.Enabled", true);
        cfg.set<bool>("NoPetIdle.Enabled", true);
        cfg.set<bool>("NoSMClear/FailMessage.Enabled", true);
        cfg.set<bool>("RemoveChatRestrictions.Enabled", true);
		cfg.set<bool>("UncapAlchemyAutoProduction.Enabled", true);


        if (!cfg.save(m_modConfigPath)) {
            log("Failed to save the config %s", m_modConfigPath.c_str());
        }

        loadConfig();
    }

    void Kanan::loadConfig() {
        log("Loading config %s", m_modConfigPath.c_str());

        struct stat buf;
        m_defaultMods = stat(m_modConfigPath.c_str(), &buf) != 0;

        Config cfg{ m_modConfigPath };
        m_isUIOpenByDefault = cfg.get<bool>("UI.OpenByDefault").value_or(true);
		m_key.hotkey = cfg.get<int>("UI.Keybind").value_or(VK_INSERT);
		m_housingKey.hotkey = cfg.get<int>("UI.HousingKey").value_or(0);
		m_astralKey.hotkey = cfg.get<int>("UI.AstralKey").value_or(0);
        m_isNotifyUpdate = cfg.get<bool>("UI.NotifyUpdate").value_or(true);
        m_isUpdate = checkVersion() && m_isNotifyUpdate;
        m_isMp3Fixed = cfg.get<bool>("UI.Mp3Fixed").value_or(false);
        m_interactiveWindows = cfg.get<bool>("UI.InteractiveWindows").value_or(true);
        m_isUIOpen = m_isUIOpenByDefault || m_isUpdate;

		if (m_key.hotkey == 0)
			m_isUIOpen = true;

        for (auto& mod : m_mods.getMods()) {
            mod->onConfigLoad(cfg);
        }

        for (auto& mod : m_mods.m_messageMods) {
            mod->onConfigLoad(cfg);
        }

        // Patch mods.
        for (auto& mods : m_mods.getPatchMods()) {
            for (auto& mod : mods.second) {
                mod->onConfigLoad(cfg);
            }
        }

        log("Config loading done.");

        m_areModsLoaded = true;
    }

    void Kanan::saveConfig() {
        log("Saving config %s", m_modConfigPath.c_str());

        Config cfg{};

        cfg.set<bool>("UI.OpenByDefault", m_isUIOpenByDefault);
        cfg.set<bool>("UI.NotifyUpdate", m_isNotifyUpdate);
        cfg.set<bool>("UI.Mp3Fixed", m_isMp3Fixed);
        cfg.set<bool>("UI.InteractiveWindows", m_interactiveWindows);
		cfg.set<int>("UI.Keybind", m_key.hotkey);
		cfg.set<int>("UI.HousingKey", m_housingKey.hotkey);
		cfg.set<int>("UI.AstralKey", m_astralKey.hotkey);

        for (auto& mod : m_mods.getMods()) {
            mod->onConfigSave(cfg);
        }

        for (auto& mod : m_mods.m_messageMods) {
            mod->onConfigSave(cfg);
        }

        // Patch mods.
        for (auto& mods : m_mods.getPatchMods()) {
            for (auto& mod : mods.second) {
                mod->onConfigSave(cfg);
            }
        }

        if (!cfg.save(m_modConfigPath)) {
            log("Failed to save the config %s", m_modConfigPath.c_str());
        }

        log("Config saving done.");
    }

    bool Kanan::isAmbientMp3(string path) {
        const string ambientMp3s[3] = { "battle_field_01.mp3", "camp.mp3", "Silence.mp3" };
        bool knownAmbientMp3 = false;

        for each (string mp3 in ambientMp3s) {
            knownAmbientMp3 |= path.compare(mp3) == 0;
        }

        knownAmbientMp3 |= path.find("ambient") != std::string::npos;

        return knownAmbientMp3;
    }

    void Kanan::fixMabiProMp3() {
        log("Attempting to fix MabiPro mp3 files.");

        mp3_status_fix status = no_mp3_found;

        for (const auto& entry : std::filesystem::directory_iterator(m_path + "/mp3/ambient")) {
            if (!isAmbientMp3(entry.path().filename().generic_string())) {
                status = mp3_move_success;
                string newMp3Path = m_path + "/mp3/" + entry.path().filename().string();
                try {
                    if (!std::filesystem::exists(newMp3Path)) {
                        std::filesystem::rename(entry.path(), newMp3Path);
                    }
                    else {
                        log("Existing mp3 at %s, skipping to next file.", newMp3Path);
                    }
                }
                catch (std::filesystem::filesystem_error& e) {
                    status = mp3_move_failure;
                    break;
                }
            }
        }

        switch (status) {
        case no_mp3_found:
            log("Did not find out of place mp3 files in ambient.");
            m_isMp3Fixed = true;
            saveConfig();
            break;
        case mp3_move_success:
            log("Successfully moved mp3 files out of ambient.");
            m_isMp3Fixed = true;
            saveConfig();
            break;
        case mp3_move_failure:
            log("Failed to move mp3 files out of ambient.");
            break;
        }
    }

    //6A 08 B8 D9 34 D4 63 E8 67 89 3F 00 8B 0D 38 DF
    void Kanan::housingBoard() {

        static auto housing = (char(__thiscall*)())scan("Pleione.dll", "6A 08 B8 D9 34 D4 63 E8 67 89 3F 00 8B 0D 38 DF").value_or(0);
        housing();
    }

    void Kanan::viewAstralWorld() {
        HMENU systemHMenu = GetSystemMenu(m_wnd, FALSE);
        //HMENU astralSubHMenu = GetSubMenu(systemHMenu, 8);
        LPARAM cmd = TrackPopupMenuEx(systemHMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_LEFTBUTTON,
            0, 0, m_wnd, NULL);
        if (cmd) 
            SendMessage(m_wnd, WM_SYSCOMMAND, cmd, 0);
    }

    void Kanan::drawUI() {
        ImGui::SetNextWindowSize(ImVec2{ 500, 700.0f }, ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Kanan for MabiPro", &m_isUIOpen, ImGuiWindowFlags_MenuBar)) {
            ImGui::End();
            return;
        }

        //
        // Menu bar
        //
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Save Config")) {
                    saveConfig();
                }
                if (ImGui::MenuItem("Force close Game")) {
                    ExitProcess(0);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Show Log", nullptr, &m_isLogOpen);
                ImGui::MenuItem("Metrics", nullptr, &m_ismetricsopen);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Settings")) {
                ImGui::MenuItem("UI Open By Default", nullptr, &m_isUIOpenByDefault);
                ImGui::MenuItem("Notify Updates", nullptr, &m_isNotifyUpdate);
                ImGui::MenuItem("Apply Recommended Settings", nullptr, &m_defaultMods);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Only enables recommended settings, this does not disable existing settings.");
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Help")) {
                ImGui::MenuItem("About Kanan", nullptr, &m_isAboutOpen);
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        // 
        // Rest of the UI
        //
        ImGui::TextWrapped(
            "Input to the game is blocked while interacting with this UI. \n\n"
            "Press the %s key to toggle this UI. \n"
            "Configuration is saved every time the %s key is used to close the UI. \n"
            "You can also save the configuration by using File->Save Config. \n\n"
            "You can stop Kanan from opening on start by using Settings->UI Open By Default. ", KeyNames[m_key.hotkey], KeyNames[m_key.hotkey]
        );
        ImGui::Spacing();
		ImGui::Separator();
		ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
        if (ImGui::Button("Housing Board", ImVec2(ImGui::GetContentRegionAvailWidth() * 0.50f, 50))) {
            housingBoard();
        }
        ImGui::SameLine();
        if (ImGui::Button("AstralWorld", ImVec2(ImGui::GetContentRegionAvailWidth(), 50))) {
            viewAstralWorld();
        }
        ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

		if (ImGui::CollapsingHeader("Kanan Hotkeys")) {
			ImGui::TextWrapped("Press Esc Key to delete a hotkey\n\nHotkey combos such as Ctrl+G are not supported");
			ImGui::Spacing();
			ImGui::Separator();
			m_key.Display("Open/Close/Save Kanan", ImVec2(ImGui::GetContentRegionAvailWidth(), 25));
			m_housingKey.Display("Open Housing Board", ImVec2(ImGui::GetContentRegionAvailWidth(), 25));
			m_astralKey.Display("Open AstralWorld", ImVec2(ImGui::GetContentRegionAvailWidth(), 25));
		}
        if (ImGui::CollapsingHeader("Interactive Mod Windows")) {
            ImGui::Checkbox("Enable interactive windows", &m_interactiveWindows);
            ImGui::TextWrapped("Allows Kanan mod windows to be moved without opening the main Kanan window.");
            ImGui::TextWrapped("If this is disabled you will need to open the main Kanan UI to move mod windows such as ChatLog or TickTimer.");
            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
            ImGui::TextWrapped("WARNING: Although not common, enabling this can result in performance decrease depending on hardware.");
            ImGui::TextWrapped("To test performance decrease look at your FPS and move your mouse around.");
            ImGui::TextWrapped("The faster the mouse is moved the lower the FPS should get.");
            ImGui::TextWrapped("There should be a noticeable difference in FPS if this is an issue.");
            ImGui::TextWrapped("If this is an issue it will always occur when the main Kanan UI is open, regardless of this setting.");
            ImGui::TextWrapped("To fix the issue make sure this setting is disabled and close the main Kanan UI by pressing %s.", KeyNames[m_key.hotkey]);
        }
        if (ImGui::CollapsingHeader("Patches")) {
            for (auto& mod : m_mods.getMods()) {
                mod->onPatchUI();
            }

            // Patch mods.
            for (auto& mods : m_mods.getPatchMods()) {
                auto& category = mods.first;

                if (!category.empty() && !ImGui::TreeNode(category.c_str())) {
                    continue;
                }

                for (auto& mod : mods.second) {
                    mod->onPatchUI();
                }

                if (!category.empty()) {
                    ImGui::TreePop();
                }
            }
        }

        for (const auto& mod : m_mods.m_messageMods) {
            mod->onUI();
        }

        for (const auto& mod : m_mods.getMods()) {
            mod->onUI();
        }

        ImGui::End();
    }

    void Kanan::drawAbout() {
        ImGui::SetNextWindowSize(ImVec2{ 475.0f, 275.0f }, ImGuiCond_Appearing);

        if (!ImGui::Begin("About", &m_isAboutOpen)) {
            ImGui::End();
            return;
        }

        ImGui::Text("Kanan's New Mabinogi Mod");
        ImGui::Text("https://github.com/cursey/kanan-new");
		ImGui::Text("Modified for MabiPro by Acros");
        ImGui::Text("https://github.com/ryuugana/kanan-mabipro");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Please come by the repository and let us know if there are "
            "any problems or mods you would like to see added. Contributors "
            "are always welcome!"
        );
        ImGui::Spacing();
        ImGui::Text("Kanan uses the following third-party libraries");
        ImGui::Text("    Dear ImGui (https://github.com/ocornut/imgui)");
        ImGui::Text("    FreeType (https://www.freetype.org/)");
        ImGui::Text("    JSON for Modern C++ (https://github.com/nlohmann/json)");
        ImGui::Text("    MinHook (https://github.com/TsudaKageyu/minhook)");
        ImGui::Text("    Roboto Font (https://fonts.google.com/specimen/Roboto)");
        ImGui::Text("    Metrics GUI (https://github.com/GameTechDev/MetricsGui)");

        ImGui::End();
    }

    void Kanan::drawUpdateMessage()
    {
        ImGui::SetNextWindowSize(ImVec2{ 475.0f, 275.0f }, ImGuiCond_Appearing);
        ImGui::SetNextWindowPosCenter();

        if (!ImGui::Begin("Kanan is not up to date", &m_isUpdateOpen)) {
            ImGui::End();
            return;
        }

        ImGui::Text("Would you like to close MabiPro and auto-update now?");
        ImGui::Dummy(ImVec2{ 30.0f, 30.0f });

        if (ImGui::Button("Yes", ImVec2(ImGui::GetContentRegionAvailWidth(), 50))) {
            updateKanan();
        }

        ImGui::Dummy(ImVec2{ 5.0f, 5.0f });

        if (ImGui::Button("Update Later", ImVec2(ImGui::GetContentRegionAvailWidth(), 50))) {
            m_isUpdate = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::Dummy(ImVec2{ 5.0f, 5.0f });

        if (ImGui::Button("Disable Updates", ImVec2(ImGui::GetContentRegionAvailWidth(), 50))) {
            m_isUpdate = false;
            m_isNotifyUpdate = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::End();
    }

    void Kanan::drawDefaultMods()
    {
        ImGui::SetNextWindowSize(ImVec2{ 475.0f, 275.0f }, ImGuiCond_Appearing);
        ImGui::SetNextWindowPosCenter();

        if (!ImGui::Begin("Default Mods", &m_defaultMods)) {
            ImGui::End();
            return;
        }

        ImGui::Text("Would you like to apply recommended default mods?");
        ImGui::Dummy(ImVec2{ 30.0f, 30.0f });

        if (ImGui::Button("Kanan", ImVec2(ImGui::GetContentRegionAvailWidth(), 50))) {
            applyDefaultMods(false);
            m_defaultMods = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::Dummy(ImVec2{ 5.0f, 5.0f });

        if (ImGui::Button("Kanan and AstralWorld", ImVec2(ImGui::GetContentRegionAvailWidth(), 50))) {
            applyDefaultMods(true);
            m_defaultMods = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::Dummy(ImVec2{ 5.0f, 5.0f });

        if (ImGui::Button("No", ImVec2(ImGui::GetContentRegionAvailWidth(), 50))) {
            m_defaultMods = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::End();
    }

    void Kanan::Drawmetrics() {

        //different style for if kanan is open or if kanan is closed. if metric is showing is true it will always render on screen.
        ImGui::SetNextWindowSize(ImVec2{ 619.0f, 186.0f }, ImGuiCond_FirstUseEver);
        if (m_isUIOpen) {
            if (!ImGui::Begin("Metrics display", &m_ismetricsopen)) {
                ImGui::End();
                return;
            }
        }
        else {
            if (!ImGui::Begin("Metrics display", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar)) {
                ImGui::End();
                return;
            }
        }

        frameTimePlot.DrawList();
        frameTimePlot.DrawHistory();



        ImGui::End();
    }

}
