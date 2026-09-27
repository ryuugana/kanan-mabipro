#pragma comment(lib, "urlmon")

#include <imgui.h>
#include <imgui_freetype.h>
#include <imgui_impl_dx9.h>
#include <imgui_impl_win32.h>

#include <Scan.hpp>
#include <Config.hpp>
#include <filesystem>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <Shlwapi.h>
#include <String.hpp>
#include <Utility.hpp>

#include "FontData.hpp"
#include "GoldFormat.hpp"
#include "Log.hpp"
#include "Kanan.hpp"
#include "MabiMessageHook.hpp"
#include "../Kanan/metrics_gui/metrics_gui.h"
#include "Hotkey.hpp"
#include "HttpCore.hpp"
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

    Kanan::Kanan(string path, HMODULE hmod) :
        characterId{ 0 },
        m_path{ move(path) },
        m_hmod{ hmod },
        m_uiConfigPath{ m_path + "/ui.ini" },
        m_modConfigPath{ m_path + "/config.txt" },
        m_updateExecPath{ m_path + "/Update.exe" },
        m_updateZipPath{ m_path + "/KananUpdater.zip" },
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
        m_fontSize { 16 },
        m_tmpFontSize { m_fontSize },
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
        config.MergeMode = false;
        io.FontAllowUserScaling = true;
        io.Fonts->AddFontFromMemoryCompressedTTF(g_font_compressed_data, g_font_compressed_size, m_fontSize);

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
        // Kanan loads before the game creates its window, so the game can show frames while Kanan is
        // still setting up its mods on the startup thread. Nothing runs until that's done and g_kanan
        // points to this Kanan (mods use it as soon as their settings are loaded).
        if (!m_areModsReady || g_kanan.get() != this) {
            return;
        }

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

            // Once a session: if the files can't be moved now, trying every frame won't help.
            if (!m_isMp3Fixed && !m_isMp3Tried) {
                m_isMp3Tried = true;
                fixMabiProMp3();
            }

            if (m_isNewVersion.exchange(false) && m_isNotifyUpdate) {
                m_isUpdate = true;
                m_isUIOpen = true;
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

            ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0], m_fontSize);
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

            ImGui::PopFont();
        }

        ImGui::EndFrame();






        // This fixes mabi's Film Style Post Shader making ImGui render as a black box.
        auto device = m_d3d9Hook->getDevice();

        device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);

        ImGui::Render();
        ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

        if (m_areModsReady) {
            for (const auto& mod : m_mods.getMods()) {
                mod->onFrameDrawn();
            }
        }
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

        if (URLOpenBlockingStream(NULL, L"https://raw.githubusercontent.com/ryuugana/kanan-mabipro/master/Kanan/Version.h", &lpSrc, 0, NULL) != S_OK)
        {
            return false;
        }
        else
        {
            lpSrc->Read(szBuffer, size - 1, NULL);
            lpSrc->Release();
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
        std::string fileHash = "";
        std::string fileName = PathFindFileNameA(m_updateZipPath.data());

        std::string fileURL = "https://github.com/ryuugana/kanan-mabipro/releases/latest/download/";
        fileURL.append(fileName);

        log("Grabbing Kanan updater from latest release: ");

        std::string updateHash = GetKananReleaseHash(fileName);

        while (updateHash.length() < 1)
        {
            log("Failed to obtain hash, retrying...");
            Sleep(1000);
            updateHash = GetKananReleaseHash(fileName);
        }

        log("Obtained hash: %s", updateHash.c_str());


        log("Downloading Kanan updater zip to %s", m_updateZipPath.c_str());

        log("Comparing downloaded file hash to actual file hash: ");

        while (updateHash != fileHash)
        {
            if (!fileHash.empty())
            {
                log("Failed with hash - %s", fileHash.c_str());
                log("Retrying download and verifying hash : ");
                Sleep(1000);
            }
            URLDownloadToFileA(NULL, fileURL.c_str(), m_updateZipPath.c_str(), 0, NULL);
            fileHash = sha256_file(m_updateZipPath);
        }

        log("Success");

        log("Extracting update: ");

        if (unzip_file(m_updateZipPath, m_path))
        {
            log("Success");
        }
        else
        {
            log("Failed");
        }

        log("Update complete - Relaunching client.exe!");

        if (start_application(m_updateExecPath))
        {
            if (GetLastError() != 0)
            {
                log("Failed to relaunch patcher. Error: %d", GetLastError());
            }
            else
            {
                exit(0);
            }
        }
        else
        {
            exit(0);
        }

        log("Failed to update Kanan.");
    }

    void Kanan::applyDefaultMods()
    {
        Config cfg{ m_modConfigPath };
        cfg.set<bool>("AssistantCharacterLocation.Enabled", true);
        cfg.set<bool>("AuctionMessageToChat.Enabled", true);
        cfg.set<bool>("AutoMute.Enabled", true);
        cfg.set<bool>("BlockSpam.Enabled", true);
        cfg.set<bool>("BlockPetPickupMessages.Enabled", true);
        cfg.set<bool>("BlockPetStatusMessages.Enabled", true);
        cfg.set<bool>("ChatTime.Enabled", true);
        cfg.set<bool>("DelagSkill.Enabled", true);
        cfg.set<bool>("DisableNagle.Enabled", true);
        cfg.set<bool>("DisableSkillLocks.Enabled", true);
        cfg.set<bool>("DisableSkillRankUpMessage.Enabled", true);
        cfg.set<bool>("FastFlight.Enabled", true);
        cfg.set<bool>("FastNao.Enabled", true);
        cfg.set<bool>("FieldBossMessageToChat.Enabled", true);
        cfg.set<bool>("FieldBossNotify.Enabled", true);
        cfg.set<bool>("FixGiantCamera.Enabled", true);
        cfg.set<int>("GoldFormat.Style", GoldFormat::LETTERS);
        cfg.set<bool>("KeepPetWindowOpen.Enabled", true);
        cfg.set<bool>("NoPetIdle.Enabled", true);
        cfg.set<bool>("NoSMClear/FailMessage.Enabled", true);
        cfg.set<bool>("RemoveChatRestrictions.Enabled", true);
		cfg.set<bool>("UncapAlchemyAutoProduction.Enabled", true);

        // AstralWorld's recommended options, now part of Kanan.
        cfg.set<bool>("NameColoring.Enabled", true);
        cfg.set<bool>("ModifyZoomLimit.Enabled", true);
        cfg.set<int>("ModifyZoomLimit.Limit", 15000);
        cfg.set<bool>("ModifyRenderDistance.Enabled", true);
        cfg.set<int>("ModifyRenderDistance.Distance", 20000);
        cfg.set<bool>("ShowCombatPower.CombatPower", true);
        cfg.set<bool>("ShowTrueDurability.Enabled", true);
        cfg.set<bool>("ShowTrueDurability.ShowItemColor", true);
        cfg.set<bool>("ShowTrueHP.Enabled", true);
        cfg.set<bool>("ShowItemID.Enabled", true);
        cfg.set<bool>("UncapAutoProduction.Enabled", true);
        cfg.set<bool>("TimeAlarm.Enabled", true);
        cfg.set<bool>("TimeAlarm.Alarm1.Enabled", true);
        cfg.set("TimeAlarm.Alarm1.Text", "TRANSFORMATION TIME!");
        cfg.set<int>("TimeAlarm.Alarm1.Hour", 5);
        cfg.set<int>("TimeAlarm.Alarm1.Minute", 50);
        cfg.set<int>("TimeAlarm.Alarm1.Style", 9);


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
        m_isNotifyUpdate = cfg.get<bool>("UI.NotifyUpdate").value_or(true);
        m_isMp3Fixed = cfg.get<bool>("UI.Mp3Fixed").value_or(false);
        m_interactiveWindows = cfg.get<bool>("UI.InteractiveWindows").value_or(true);
        m_fontSize = cfg.get<int>("UI.FontSize").value_or(16);
        m_tmpFontSize = m_fontSize;
        m_key.hotkey = cfg.get<int>("UI.Keybind").value_or(VK_INSERT);
        m_housingKey.hotkey = cfg.get<int>("UI.HousingKey").value_or(0);
        m_isUIOpen = m_isUIOpenByDefault || m_isUpdate;

		if (m_key.hotkey == 0)
			m_isUIOpen = true;

        if (!m_isVersionChecked) {
            m_isVersionChecked = true;

            // Delete the batch file a previous update created.
            std::error_code ec{};
            std::filesystem::remove(m_updateExecPath, ec);

            // Downloading the latest version number would hold up the game's frame, so it's done on
            // its own thread; the next frame after it finds a newer one opens the update message.
            if (m_isNotifyUpdate) {
                std::thread{ [this] {
                    auto com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

                    if (checkVersion()) {
                        m_isNewVersion = true;
                    }

                    if (SUCCEEDED(com)) {
                        CoUninitialize();
                    }
                } }.detach();
            }
        }

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
        cfg.set<int>("UI.FontSize", m_fontSize);
		cfg.set<int>("UI.Keybind", m_key.hotkey);
		cfg.set<int>("UI.HousingKey", m_housingKey.hotkey);

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

        // No ambient folder means no mp3 in the wrong place.
        std::error_code ec{};
        std::filesystem::directory_iterator ambient{ m_path + "/mp3/ambient", ec };

        for (const auto& entry : ec ? std::filesystem::directory_iterator{} : ambient) {
            if (!isAmbientMp3(entry.path().filename().generic_string())) {
                status = mp3_move_success;
                string newMp3Path = m_path + "/mp3/" + entry.path().filename().string();
                try {
                    if (!std::filesystem::exists(newMp3Path)) {
                        std::filesystem::rename(entry.path(), newMp3Path);
                    }
                    else {
                        log("Existing mp3 at %s, skipping to next file.", newMp3Path.c_str());
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
                if (ImGui::MenuItem("New Client")) {
                    launch_client(m_path);
                }
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
        if (ImGui::Button("Housing Board", ImVec2(ImGui::GetContentRegionAvail().x, 50))) {
            housingBoard();
        }
        ImGui::Dummy(ImVec2{ 10.0f, 10.0f });


        if (ImGui::CollapsingHeader("Kanan Settings")) {
            if (ImGui::TreeNode("Kanan Hotkeys")) {
                ImGui::TextWrapped("Press Esc Key to delete a hotkey\n\nHotkey combos such as Ctrl+G are not supported");
                ImGui::Spacing();
                ImGui::Separator();
                m_key.Display("Open/Close/Save Kanan", ImVec2(ImGui::GetContentRegionAvail().x - 25, 25));
                m_housingKey.Display("Open Housing Board", ImVec2(ImGui::GetContentRegionAvail().x - 25, 25));
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Kanan Font Size")) {
                ImGui::TextWrapped("Set the font size for all Kanan windows");
                ImGui::Spacing();
                ImGui::TextWrapped("Some mod windows may need to be toggled to adjust to the new font size.");
                ImGui::Spacing();

                ImGui::InputInt("Font Size", &m_tmpFontSize, 1, 10);
                // Set minimum font size to 12
                if (m_tmpFontSize < 12)
                {
                    m_fontSize = 12;
                }
                // Set maximum font size to 50
                else if (m_tmpFontSize > 50)
                {
                    m_tmpFontSize = 50;
                    m_fontSize = m_tmpFontSize;
                }
                else
                {
                    m_fontSize = m_tmpFontSize;
                }

                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Interactive Mod Windows")) {
                ImGui::TextWrapped("Allows Kanan mod windows to be moved without opening the main Kanan window.");
                ImGui::TextWrapped("If this is disabled you will need to open the main Kanan UI to move mod windows such as ChatLog or TickTimer.");
                ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
                ImGui::Checkbox("Enable interactive windows", &m_interactiveWindows);
                ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
                ImGui::TextWrapped("WARNING: Although not common, enabling this can result in performance decrease depending on hardware.");
                ImGui::TextWrapped("To test for performance decrease look at your FPS and move your mouse around.");
                ImGui::TextWrapped("If there is a performance issue your FPS should get lower the faster the mouse is moved and the difference in FPS should be noticeable.");
                ImGui::TextWrapped("If this is an issue with your hardware it will always occur when the main Kanan UI is open, regardless of this setting.");
                ImGui::TextWrapped("To fix the issue make sure this setting is disabled and close the main Kanan UI by pressing %s.", KeyNames[m_key.hotkey]);
                ImGui::TreePop();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Checkbox("UI Open By Default", &m_isUIOpenByDefault);
            ImGui::Checkbox("Notify Updates", &m_isNotifyUpdate);
            ImGui::Checkbox("Apply Recommended Settings", &m_defaultMods);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Only enables recommended settings, this does not disable existing settings.");
            }
        }
        drawMods();

        ImGui::End();
    }

    // Every mod, by section, with a search box that finds mods by their name, description and section.
    void Kanan::drawMods() {
        ImGui::Spacing();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##SearchMods", "Search mods (name or what they do)...", m_search, sizeof(m_search));

        auto lowercase = [](string text) {
            transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)tolower(c); });
            return text;
        };

        // Every word typed must appear somewhere in the mod's name, description or section.
        vector<string> words{};
        istringstream query{ lowercase(m_search) };

        for (string word{}; query >> word; ) {
            words.push_back(word);
        }

        auto isSearching = !words.empty();
        auto wasSearchCleared = m_wasSearching && !isSearching;

        m_wasSearching = isSearching;

        auto isMatch = [&](const Mods::MenuEntry& entry) {
            if (!isSearching) {
                return true;
            }

            auto text = lowercase(entry.name + " " + entry.description + " " + entry.section);

            return all_of(words.begin(), words.end(), [&](const string& word) { return text.find(word) != string::npos; });
        };

        auto& menu = m_mods.getMenu();
        auto isAnyMatch = false;

        for (auto& section : m_mods.getSections()) {
            vector<const Mods::MenuEntry*> entries{};

            for (auto& entry : menu) {
                if (entry.section == section && isMatch(entry)) {
                    entries.push_back(&entry);
                }
            }

            if (entries.empty()) {
                continue;
            }

            isAnyMatch = true;

            // Searching opens the sections and mods it finds; clearing the search closes them again.
            if (isSearching || wasSearchCleared) {
                ImGui::SetNextItemOpen(isSearching);
            }

            if (!ImGui::CollapsingHeader(section.c_str())) {
                continue;
            }

            ImGui::PushID(section.c_str());

            auto wasToggle = true;

            for (auto entry : entries) {
                // A little room between the section's on/off patches and its mods with settings.
                if (!entry->isToggle && wasToggle && entry != entries.front()) {
                    ImGui::Spacing();
                }

                wasToggle = entry->isToggle;

                if (entry->isToggle) {
                    entry->mod->onPatchUI();
                }
                else {
                    if (isSearching || wasSearchCleared) {
                        ImGui::SetNextItemOpen(isSearching);
                    }

                    entry->mod->onUI();
                }
            }

            ImGui::PopID();
        }

        if (isSearching && !isAnyMatch) {
            ImGui::TextDisabled("No mods match \"%s\".", m_search);
        }
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
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        if (!ImGui::Begin("Kanan is not up to date", &m_isUpdateOpen)) {
            ImGui::End();
            return;
        }

        ImGui::Text("Would you like to close MabiPro and auto-update now?");
        ImGui::Dummy(ImVec2{ 30.0f, 30.0f });

        if (ImGui::Button("Yes", ImVec2(ImGui::GetContentRegionAvail().x , 50))) {
            m_isLogOpen = true;
            m_isUpdate = false;
            m_thread = std::thread(&Kanan::updateKanan, this);
        }

        ImGui::Dummy(ImVec2{ 5.0f, 5.0f });

        if (ImGui::Button("Update Later", ImVec2(ImGui::GetContentRegionAvail().x , 50))) {
            m_isUpdate = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::Dummy(ImVec2{ 5.0f, 5.0f });

        if (ImGui::Button("Disable Updates", ImVec2(ImGui::GetContentRegionAvail().x , 50))) {
            m_isUpdate = false;
            m_isNotifyUpdate = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::End();
    }

    void Kanan::drawDefaultMods()
    {
        ImGui::SetNextWindowSize(ImVec2{ 475.0f, 275.0f }, ImGuiCond_Appearing);
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (!ImGui::Begin("Default Mods", &m_defaultMods)) {
            ImGui::End();
            return;
        }

        ImGui::Text("Would you like to apply recommended default mods?");
        ImGui::Dummy(ImVec2{ 30.0f, 30.0f });

        if (ImGui::Button("Yes", ImVec2(ImGui::GetContentRegionAvail().x , 50))) {
            applyDefaultMods();
            m_defaultMods = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::Dummy(ImVec2{ 5.0f, 5.0f });

        if (ImGui::Button("No", ImVec2(ImGui::GetContentRegionAvail().x , 50))) {
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
