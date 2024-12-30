#pragma once

#include <atomic>
#include <memory>
#include <string>

#include <Windows.h>

#include "D3D9Hook.hpp"
#include "DInputHook.hpp"
#include "MabiMessageHook.hpp"
#include "WindowsMessageHook.hpp"
#include "Game.hpp"
#include "Mods.hpp"

namespace kanan {
    // This is the main object that sets up all the patches and stuff.
    class Kanan {
    public:
        Kanan(std::string path);

        auto getGame() const {
            return m_game.get();
        }

        auto& getMods() const {
            return m_mods;
        }

		auto& getPath() const {
			return m_path;
		}

        auto getWindow() const {
            return m_wnd;
        }

        auto isUIOpen() {
            return m_isUIOpen;
        }

    private:
        std::string m_path;
        std::string m_uiConfigPath;
        std::string m_modConfigPath;
        std::string m_batchPath;
        std::string m_updatePath;
        std::string m_astralPath;
        std::unique_ptr<D3D9Hook> m_d3d9Hook;
        std::unique_ptr<DInputHook> m_dinputHook;
        std::unique_ptr<WindowsMessageHook> m_wmHook;
        std::unique_ptr<MabiMessageHook> m_mesHook;
        std::unique_ptr<Game> m_game;
        Mods m_mods;

        bool m_isUIOpen;
        bool m_isLogOpen;
        bool m_isAboutOpen;
        bool m_ismetricsopen;
        bool m_isUpdateOpen;
        bool m_isUpdate;
        bool m_isNotifyUpdate;
        bool m_isMp3Fixed;
        bool m_defaultMods;

        bool m_isInitialized;
        std::atomic_bool m_areModsReady;
        bool m_areModsLoaded;
        HWND m_wnd;

        bool m_isUIOpenByDefault{};

        void initializeMods();

        // These are callbacks that get called from the hooks that get created.
        void onInitialize();
        void onFrame();
        bool onMessage(HWND wnd, UINT message, WPARAM wParam, LPARAM lParam);

        bool checkVersion();
        void updateKanan();

        void applyDefaultMods(bool astralWorld);

        void loadConfig();
        void saveConfig();

        void housingBoard();
        void viewAstralWorld();

        void drawUI();
        void drawAbout();
        void drawUpdateMessage();
        void drawDefaultMods();
        void Drawmetrics();

        enum mp3_status_fix {
            no_mp3_found,
            mp3_move_success,
            mp3_move_failure
        };
    };

    extern std::unique_ptr<Kanan> g_kanan;
}
