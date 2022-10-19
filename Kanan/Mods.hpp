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
        Mods(std::string filepath);

        void loadTimeCriticalMods();
        void loadMods();

        const auto& getMods() const {
            return m_mods;
        }

        const auto& getPatchMods() const {
            return m_patchMods;
        }

        std::vector<std::unique_ptr<MessageMod>> m_messageMods;

    private:
        std::string m_filepath;
        std::vector<std::unique_ptr<Mod>> m_mods;
        std::map<std::string, std::vector<std::unique_ptr<PatchMod>>> m_patchMods;
        std::mutex m_modsMutex;

        void addMod(std::unique_ptr<Mod>&& mod);
        void addPatchMod(const std::string& category, std::unique_ptr<PatchMod>&& mod);
        void addMessageMod(std::unique_ptr<MessageMod>&& mod);
    };
}
