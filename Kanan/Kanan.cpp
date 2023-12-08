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
#include "ChatLog.hpp"
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
        m_d3d9Hook{ nullptr },
        m_dinputHook{ nullptr },
        m_mesHook{ nullptr },
        m_wmHook{ nullptr },
        m_game{ nullptr },
        m_mods{ m_path },
        m_isUpdate{ false },
        m_isNotifyUpdate{ true },
        m_isUIOpen{ true },
        m_isLogOpen{ false },
        m_isAboutOpen{ false },
        m_isUpdateOpen{ false },
        m_isInitialized{ false },
        m_areModsReady{ false },
        m_areModsLoaded{ false },
        m_wnd{ nullptr },
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

        //m_game = make_unique<Game>();

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


        //update our metrics with framerate data
        frameTimeMetric.AddNewValue(1.f / ImGui::GetIO().Framerate);
        frameTimePlot.UpdateAxes();

		auto& io = ImGui::GetIO();

        if (m_areModsReady) {
            // Make sure the config for all the mods gets loaded.
            if (!m_areModsLoaded) {
                loadConfig();
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

            if (m_isUIOpen) {
                // Block input if the user is interacting with the UI.

                if (io.WantCaptureMouse || io.WantCaptureKeyboard || io.WantTextInput) {
                    m_dinputHook->ignoreInput();
                }
                else {
                    m_dinputHook->acknowledgeInput();
                }

                drawUI();

                if (m_isLogOpen) {
                    drawLog(&m_isLogOpen);
                }

                if (m_isChatLogOpen) {
                    drawChatLog(&m_isChatLogOpen);
                }

                if (m_isAboutOpen) {
                    drawAbout();
                }

                if (m_isUpdate) {
                    drawUpdateMessage();
                }
            }
            else {
                // UI is closed so always pass input to the game.
                m_dinputHook->acknowledgeInput();
            }

            //draw metric window
            if (m_ismetricsopen) {
                Drawmetrics();
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

        if (m_isUIOpen) {
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
        string downloadPath = std::filesystem::current_path().string();
        string KananPath = downloadPath + "\\KananUpdate.zip";
        string BatchPath = downloadPath + "\\ExtractKanan.bat";
        URLDownloadToFileA(NULL, "https://github.com/ryuugana/kanan-mabipro/releases/latest/download/KananMabiPro.zip", KananPath.c_str(), 0, NULL);

        std::ofstream batch_file(BatchPath);
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
            const_cast<char*>(BatchPath.c_str()),
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

    void Kanan::loadConfig() {
        log("Loading config %s/config.txt", m_path.c_str());

        Config cfg{ m_path + "/config.txt" };
        m_isUIOpenByDefault = cfg.get<bool>("UI.OpenByDefault").value_or(true);
		m_key.hotkey = cfg.get<int>("UI.Keybind").value_or(VK_INSERT);
		m_housingKey.hotkey = cfg.get<int>("UI.HousingKey").value_or(0);
		m_astralKey.hotkey = cfg.get<int>("UI.AstralKey").value_or(0);
        m_isChatLogOpen = cfg.get<bool>("ChatLog.OpenByDefault").value_or(false);
        m_isNotifyUpdate = cfg.get<bool>("UI.NotifyUpdate").value_or(true);
        m_isUpdate = checkVersion() && m_isNotifyUpdate;
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
        log("Saving config %s/config.txt", m_path.c_str());

        Config cfg{};

        cfg.set<bool>("UI.OpenByDefault", m_isUIOpenByDefault);
        cfg.set<bool>("UI.NotifyUpdate", m_isNotifyUpdate);
		cfg.set<int>("UI.Keybind", m_key.hotkey);
		cfg.set<int>("UI.HousingKey", m_housingKey.hotkey);
		cfg.set<int>("UI.AstralKey", m_astralKey.hotkey);
        cfg.set<bool>("ChatLog.OpenByDefault", m_isChatLogOpen);

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

        if (!cfg.save(m_path + "/config.txt")) {
            log("Failed to save the config %s/config.txt", m_path.c_str());
        }

        log("Config saving done.");
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
                ImGui::MenuItem("Show Chat Log", nullptr, &m_isChatLogOpen);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Settings")) {
                ImGui::MenuItem("UI Open By Default", nullptr, &m_isUIOpenByDefault);
                ImGui::MenuItem("Notity Updates", nullptr, &m_isNotifyUpdate);
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
