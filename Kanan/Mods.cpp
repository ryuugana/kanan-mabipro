#include <algorithm>
#include <fstream>
#include <filesystem>
#include <thread>

#include <json.hpp>
#include <String.hpp>
#include <Config.hpp>

#include "Mods.hpp"
#include "Log.hpp"

#include "PatchMod.hpp"

// Misc Mods
#include "AutoSetMTU.hpp"
#include "CpuScheduling.hpp"
#include "ThreadPriority.hpp"
#include "OverlayDetection.hpp"
#include "DisplayScaling.hpp"
#include "GoldFormat.hpp"
#include "ScreenshotFix.hpp"
#include "LosslessScreenshots.hpp"
#include "DisableNagle.hpp"
#include "BorderlessWindow.hpp"
#include "MaxFrameRate.hpp"
#include "EntityHP.hpp"
#include "MeditationTint.hpp"
#include "UIScale.hpp"
#include "FieldOfView.hpp"
#include "StatusUI.hpp"

// Patch Mods
#include "CombatMasterySwap.hpp"
#include "DefaultRangedSwap.hpp"
#include "DisableFlashyDyes.hpp"
#include "DisableNighttime.hpp"
#include "DisplayNamesFar.hpp"
#include "FarDiceThrow.hpp"
#include "ItemSplitQuantity.hpp"
#include "LargeClockText.hpp"
#include "ModifyFontSize.hpp"
#include "ModifyRenderDistance.hpp"
#include "NameColoring.hpp"
#include "ModifyZoomLimit.hpp"
#include "ScreenshotQuality.hpp"
#include "ShowCombatPower.hpp"
#include "ShowExplorationPercent.hpp"
#include "ShowItemID.hpp"
#include "ShowPoisonDurability.hpp"
#include "ShowTrueDurability.hpp"
#include "ShowTrueFoodQuality.hpp"
#include "ShowTrueHP.hpp"
#include "TargetProps.hpp"
#include "TimeAlarm.hpp"
#include "UserCommands.hpp"
#include "MabiTrackers.hpp"
#include "AutoMute.hpp"
#include "DisableFlashy.h"
#include "FontStyle.hpp"

// Message Mods
#include "AutoLoginChannel.hpp"
#include "AutoMount.hpp"
#include "BlockSpam.hpp"
#include "DpsMeter.hpp"
#include "GetInfo.hpp"
#include "MessageViewer.hpp"
#include "ChooseLoginNode.hpp"
#include "ChatLog.hpp"
#include "EntityViewer.hpp"
#include "MaintLogin.hpp"
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

        // Patches.json's patches only change the game's code, and some change what it does while it's
        // still starting (like Derandomize Login Screen, which the login screen reads once when it's
        // built), so their settings are applied as soon as they're loaded rather than on Kanan's first
        // frame. The rest of the settings are loaded as usual later, which applies these again (the
        // original bytes are only saved the first time).
        {
            Config cfg{ m_filepath + "/config.txt" };

            for (auto& [category, mods] : m_patchMods) {
                for (auto& mod : mods) {
                    mod->onConfigLoad(cfg);
                }
            }
        }

        // Sections: what the mod is about, for players looking for it (Patches.json gives each
        // patch's section as its category).
        const string ui = "Interface";
        const string graphics = "Graphics & Camera";
        const string combat = "Combat & Skills";
        const string chat = "Chat & Messages";
        const string convenience = "Convenience";
        const string screenshots = "Screenshots";
        const string system = "Performance & System";
        const string fun = "Fun";
        const string debug = "Debug";

        m_sections = { ui, graphics, combat, chat, convenience, screenshots, system, fun, debug };

        addPatchMod(convenience, make_unique<AutoMute>(), "Auto Mute", "Mutes the game's sound while it's in the background.");
		addPatchMod(graphics, make_unique<DisableFlashy>(), "Disable Inventory Flashy",
            "Shows flashy items in your inventory and on the ground as their plain color.");
        addPatchMod(ui, make_unique<FontStyle>(), "", "Interface text with TrueType fonts or bitmap fonts.");
        addPatchMod(combat, make_unique<DefaultRangedSwap>(), "",
            "Uses another ranged skill in place of Ranged Attack: Magnum Shot, Arrow Revolver, Support Shot, "
            "Mirage Missile, Crash Shot.");
        addPatchMod(graphics, make_unique<DisableFlashyDyes>());
        addPatchMod(graphics, make_unique<DisableNighttime>());
        addPatchMod(ui, make_unique<DisplayNamesFar>());
        addPatchMod(ui, make_unique<NameColoring>(), "", "Colors character names by type (player, NPC, pet, enemy).");
        addPatchMod(ui, make_unique<LargeClockText>(), "", "Shows the in-game clock in large text.");
        addPatchMod(fun, make_unique<FarDiceThrow>(), "", "Throw dice much farther away.");
        addPatchMod(ui, make_unique<ShowCombatPower>(), "", "Shows the combat power and max HP numbers next to character names.");
        addPatchMod(ui, make_unique<ShowExplorationPercent>(), "",
            "Shows your exploration level and percent in the character window.");
        addPatchMod(ui, make_unique<ShowItemID>(), "", "Shows each item's ID in its description.");
        addPatchMod(ui, make_unique<ShowPoisonDurability>());
        addPatchMod(ui, make_unique<ShowTrueFoodQuality>());
        addPatchMod(ui, make_unique<ShowTrueHP>());
        addPatchMod(combat, make_unique<TargetProps>());
        addPatchMod(chat, make_unique<UserCommands>());

        for (auto& categories : m_patchMods) {
            auto& mods = categories.second;

            sort(mods.begin(), mods.end(), [](const auto& a, const auto& b) {
                return a->getName() < b->getName();
            });
        }

#ifdef TEST
        // There is only one node left (the slowest one)
        addMessageMod(make_unique<ChooseLoginNode>(), debug, "Choose Node", "Chooses the login server node.");
        addMessageMod(make_unique<MaintLogin>(), debug, "Maintenance Login", "Logs in during maintenance.");
        addMessageMod(make_unique<MessageViewer>(), debug, "Message Viewer", "Shows the network messages the game sends and receives.");
#endif

        addMessageMod(make_unique<AutoLoginChannel>(), convenience, "Auto Login Channel",
            "Logs in to the channel you choose automatically.");
        addMessageMod(make_unique<AutoMount>(), convenience, "Auto Mount",
            "Accepts mount requests automatically.");
        addMessageMod(make_unique<BlockSpam>(), chat, "Block Spam", "Hides spam messages from chat.");
        addMessageMod(make_unique<DpsMeter>(), combat, "DPS Meter", "Shows your damage per second.");
        addMessageMod(make_unique<GetInfo>(), "", "", "");
        addMessageMod(make_unique<NaoCounter>(), convenience, "Nao Counter", "Counts Nao Soul Stone revives.");
        addMessageMod(make_unique<TickTimer>(), combat, "Tick Timer", "Shows the timing of the game's regeneration ticks.");
        // Keep ChatLog below ScrollingMessageToChat to log the messages
        addMessageMod(make_unique<ChatLog>(), chat, "Chat Mods", "Includes various chat mods including a chat logger and adding time to chat.");
        addMod(make_unique<DisableNagle>(), system, "Disable Nagle", "Sends network messages right away, for less lag.");
        addMod(make_unique<BorderlessWindow>(), graphics, "Borderless Window", "Runs the game in a borderless window or fullscreen window.");
        addMod(make_unique<EntityViewer>(), ui, "Entity Viewer", "Provides a window to view clothing, conditions, and other information from entities.");
        addMod(make_unique<FieldOfView>(), graphics, "Field Of View", "Changes the camera's field of view.");
        addMod(make_unique<DisplayScaling>(), graphics, "Display Scaling",
            "Makes the game sharp instead of blurry on high resolution screens (1440p, 4K, laptops) with Windows "
            "display scaling (DPI) above 100%.");
        addMod(make_unique<MaxFrameRate>(), system, "Max Frame Rate", "Limits the frame rate (FPS cap), also in the background.");
        addMod(make_unique<EntityHP>(), ui, "Entity HP", "Shows HP numbers over monsters and characters.");
        addMod(make_unique<MeditationTint>(), ui, "Meditation Tint",
            "Tints characters that are meditating and shows a Meditation condition icon.");
        addMod(make_unique<UIScale>(), ui, "UI Scale", "Makes the game's interface bigger, with sharp or smooth text.");
        //addMod(make_unique<StatusUI>());
        addMod(make_unique<AutoSetMTU>(), system, "Auto Set MTU", "Sets your network MTU when you log in or change channels.");
        addMod(make_unique<CombatMasterySwap>(), combat, "Combat Mastery Swap",
            "Attacking with no skill loaded loads a skill of your choice instead, like Smash.");
        addMod(make_unique<ItemSplitQuantity>(), convenience, "Item Split Quantity",
            "The amount the item split window starts at.");
        addMod(make_unique<ModifyFontSize>(), ui, "Font Size", "Changes the size of TrueType interface text.");
        addMod(make_unique<ModifyRenderDistance>(), graphics, "Render Distance", "How far away the world is drawn.");
        addMod(make_unique<ModifyZoomLimit>(), graphics, "Zoom Limit", "Lets the camera zoom out farther.");
        addMod(make_unique<ScreenshotQuality>(), screenshots, "Screenshot Quality", "The JPEG quality screenshots are saved at.");
        addMod(make_unique<ShowTrueDurability>(), ui, "Show True Durability",
            "Shows exact item durability in descriptions, with item colors.");
        addMod(make_unique<TimeAlarm>(), convenience, "Time Alarm", "Alarms at in-game times, like transformation time.");
        addMod(make_unique<MabiTrackers>(), convenience, "Erinn Tracker",
            "The weather forecast for every region, where the moon gates lead, and the schedules of Price, Rua, Fleta and Tarlach, with countdowns, opened in your browser.");
        addMod(make_unique<CpuScheduling>(), system, "CPU Scheduling",
            "Fixes freezes on Intel CPUs with performance and efficiency cores; power throttling.");
        addMod(make_unique<ThreadPriority>(), system, "Thread Priority",
            "Raises the game's main thread priority so it runs first when other programs are busy.");
        addMod(make_unique<OverlayDetection>(), system, "Overlay Detection",
            "Finds overlays like MSI Afterburner/RTSS that can hide Kanan's menu.");
        addMod(make_unique<GoldFormat>(), ui, "Gold Format", "Shows gold with commas (46,500), periods (46.500), short (46.5k) or money letters, in thousands (k) and millions (m) (46k500).");
        addMod(make_unique<ScreenshotFix>(), screenshots, "Screenshot Fix",
            "Fixes screenshots not being saved with Windows display scaling (DPI).");
        addMod(make_unique<LosslessScreenshots>(), screenshots, "Lossless Screenshots", "Also saves each screenshot as a PNG.");

        sortMenu();

        log("[Mods] Finished loading mods.");
    }

    void Mods::addMod(std::unique_ptr<Mod>&& mod, const std::string& section, const std::string& name,
        const std::string& description)
    {
        scoped_lock<mutex> _{ m_modsMutex };

        m_menu.push_back({ mod.get(), section, name, description, false });
        m_mods.emplace_back(move(mod));
    }

    void Mods::addPatchMod(const std::string& section, std::unique_ptr<PatchMod>&& mod, const std::string& name,
        const std::string& description)
    {
        scoped_lock<mutex> _{ m_modsMutex };

        m_menu.push_back({ mod.get(), section, name.empty() ? mod->getName() : name,
            description.empty() ? mod->getTooltip() : description, true });
        m_patchMods[section].emplace_back(move(mod));
    }

    void Mods::addMessageMod(std::unique_ptr<MessageMod>&& mod, const std::string& section, const std::string& name,
        const std::string& description)
    {
        scoped_lock<mutex> _{ m_modsMutex };

        // Mods without a section have nothing to show in the menu.
        if (!section.empty()) {
            m_menu.push_back({ mod.get(), section, name, description, false });
        }

        m_messageMods.emplace_back(move(mod));
    }

    void Mods::sortMenu() {
        // Sections from other patch files go after the known ones.
        for (auto& entry : m_menu) {
            if (find(m_sections.begin(), m_sections.end(), entry.section) == m_sections.end()) {
                m_sections.push_back(entry.section);
            }
        }

        auto sectionIndex = [this](const string& section) {
            return find(m_sections.begin(), m_sections.end(), section) - m_sections.begin();
        };

        auto lowercase = [](string text) {
            transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return (char)tolower(c); });
            return text;
        };

        stable_sort(m_menu.begin(), m_menu.end(), [&](const MenuEntry& a, const MenuEntry& b) {
            if (a.section != b.section) {
                return sectionIndex(a.section) < sectionIndex(b.section);
            }

            if (a.isToggle != b.isToggle) {
                return a.isToggle;
            }

            return lowercase(a.name) < lowercase(b.name);
        });
    }
}
