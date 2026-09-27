#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "Mod.hpp"
#include "MessageMod.hpp"
#include "PatchMod.hpp"

namespace kanan {
    class Mods {
    public:
        // Where a mod is shown in Kanan's window, and what it can be searched by.
        struct MenuEntry {
            Mod* mod;
            std::string section;
            std::string name;
            std::string description;
            // On/off patches (drawn by onPatchUI) come before mods with settings (drawn by onUI).
            bool isToggle;
        };

        Mods(std::string filepath);

        void loadTimeCriticalMods();
        void loadMods();

        const auto& getMods() const {
            return m_mods;
        }

        const auto& getPatchMods() const {
            return m_patchMods;
        }

        // Every mod shown in the menu, by section (in getSections' order), toggles first, then by name.
        const auto& getMenu() const {
            return m_menu;
        }

        // The menu's sections, in the order they're shown.
        const auto& getSections() const {
            return m_sections;
        }

        std::vector<std::unique_ptr<MessageMod>> m_messageMods;

    private:
        std::string m_filepath;
        std::vector<std::unique_ptr<Mod>> m_mods;
        std::map<std::string, std::vector<std::unique_ptr<PatchMod>>> m_patchMods;
        std::vector<MenuEntry> m_menu;
        std::vector<std::string> m_sections;
        std::mutex m_modsMutex;

        void addMod(std::unique_ptr<Mod>&& mod, const std::string& section, const std::string& name,
            const std::string& description);
        // Uses the patch's own name and tooltip unless NAME or DESCRIPTION are given.
        void addPatchMod(const std::string& section, std::unique_ptr<PatchMod>&& mod, const std::string& name = "",
            const std::string& description = "");
        void addMessageMod(std::unique_ptr<MessageMod>&& mod, const std::string& section, const std::string& name,
            const std::string& description);
        void sortMenu();
    };
}
