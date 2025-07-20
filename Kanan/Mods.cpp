#include <fstream>
#include <filesystem>
#include <thread>

#include <json.hpp>
#include <String.hpp>
#include <Config.hpp>

#include "Mods.hpp"
#include "Log.hpp"

#include "PatchMod.hpp"

#include "AutoSetMTU.hpp"
#include "DisableNagle.hpp"
#include "BorderlessWindow.hpp"

#include "AutoMute.hpp"
#include "DisableFlashy.h"

#include "BlockSpam.hpp"
#include "MessageViewer.hpp"
#include "ScrollingMessageToChat.hpp"
#include "ChooseLoginNode.hpp"
#include "ChatLog.hpp"
#include "NaoCounter.hpp"
#include "TickTracker.hpp"


using namespace std;
using namespace std::filesystem;
using nlohmann::json;

namespace kanan {
    Mods::Mods(std::string filepath)
        : m_filepath{ move(filepath) },
        m_mods{},
        m_patchMods{},
        m_modsMutex{}
    {
        log("[Mods] Entering cosntructor.");
        log("[Mods] Leaving constructor.");
    }

    void Mods::loadTimeCriticalMods() {
        log("[Mods] Loading time critical mods...");

        //addMod(make_unique<UseDataFolder>());
        //addMod(make_unique<LoginScreen>());

        // Time critical mods need to have their settings loaded from the config
        // right away.
        Config cfg{ m_filepath + "/config.txt" };

        for (auto& mod : m_mods) {
            mod->onConfigLoad(cfg);
        }

        log("[Mods] Finished loading time critical mods.");
    }

    void Mods::loadMods() {
        log("[Mods] Loading mods...");

        // Load all .json files.
        for (const auto& p : directory_iterator(m_filepath)) {
            auto& path = p.path();

            if (path.extension() != ".json") {
                continue;
            }

            // Load patches from the patches json file.
            ifstream patchesFile{ path };

            if (!patchesFile) {
                log("Failed to load patches file: %s", path.c_str());
            }

            json patches{};

            patchesFile >> patches;

            for (auto& patch : patches) {
                // Create the new PatchMod
                auto patchMod = make_unique<PatchMod>();

                // Load it from json.
                *patchMod = patch;

                addPatchMod(patchMod->getCategory(), move(patchMod));
            }
        }

        addPatchMod("Quality of Life", make_unique<AutoMute>());
		addPatchMod("Graphics", make_unique<DisableFlashy>());

        for (auto& categories : m_patchMods) {
            auto& mods = categories.second;

            sort(mods.begin(), mods.end(), [](const auto& a, const auto& b) {
                return a->getName() < b->getName();
            });
        }

        addMessageMod(make_unique<BlockSpam>());
        addMessageMod(make_unique<ChooseLoginNode>());
        addMessageMod(make_unique<NaoCounter>());
        addMessageMod(make_unique<TickTimer>());
        addMessageMod(make_unique<ScrollingMessageToChat>());
        // Keep ChatLog below ScrollingMessageToChat to log the messages
        addMessageMod(make_unique<ChatLog>());

#ifdef TEST
        addMessageMod(make_unique<MessageViewer>());
#endif // TEST

        addMod(make_unique<AutoSetMTU>());
        addMod(make_unique<DisableNagle>());
        addMod(make_unique<BorderlessWindow>());
        
        log("[Mods] Finished loading mods.");
    }

    void Mods::addMod(std::unique_ptr<Mod>&& mod) {
        scoped_lock<mutex> _{ m_modsMutex };

        m_mods.emplace_back(move(mod));
    }

    void Mods::addPatchMod(const std::string& category, std::unique_ptr<PatchMod>&& mod) {
        scoped_lock<mutex> _{ m_modsMutex };

        m_patchMods[category].emplace_back(move(mod));
    }

    void Mods::addMessageMod(std::unique_ptr<MessageMod>&& mod) {
        scoped_lock<mutex> _{ m_modsMutex };

        m_messageMods.emplace_back(move(mod));

    }
}
