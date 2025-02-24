#include <algorithm>

#include <imgui.h>

#include <Pattern.hpp>
#include <Scan.hpp>

#include "Log.hpp"
#include "PatchMod.hpp"

using namespace std;
using nlohmann::json;

namespace kanan {
    PatchMod::PatchMod(string patchName, string tooltip)
        : m_isEnabled{ false },
        m_patches{},
        m_hasFailingPatch{ false },
        m_patchName{ move(patchName) },
        m_tooltip{ move(tooltip) },
        m_configName{},
        m_category{}
    {
        buildConfigName();
    }

    bool PatchMod::addPatch(const string& location, const string& pattern, int offset, vector<int16_t> patchBytes, int n) {
        auto address = scan(location, pattern);
        
        if (!address) {
            log("[%s] Failed to find pattern %s", m_patchName.c_str(), pattern.c_str());

            m_hasFailingPatch = true;

            return false;
        }

        for (int i = 0; i < n; ++i) {
            address = scan(location, *address + 1, pattern);

            if (!address) {
                log("[%s] Failed to find pattern (I=%d, N=%d) %s", m_patchName.c_str(), i, n, pattern.c_str());

                m_hasFailingPatch = true;

                return false;
            }
        }

        log("[%s] Found address %p for pattern %s", m_patchName.c_str(), *address, pattern.c_str());

        Patch patch{};

        patch.address = *address + offset;
        patch.bytes = move(patchBytes);

        m_patches.emplace_back(patch);

        return true;
    }

    void PatchMod::buildConfigName() {
        // Generate the config name from the name of the patch.
        m_configName = m_patchName;

        // Remove spaces.
        m_configName.erase(remove_if(begin(m_configName), end(m_configName), isspace), end(m_configName));

        // Add .Enabled
        m_configName.append(".Enabled");
    }

    void PatchMod::onPatchUI() {
        if (m_patches.empty() || m_hasFailingPatch) {
            return;
        }

        if (ImGui::Checkbox(m_patchName.c_str(), &m_isEnabled)) {
            apply();
        }

        if (!m_tooltip.empty() && ImGui::IsItemHovered()) {
            ImGui::SetTooltip(m_tooltip.c_str());
        }
    }

    void PatchMod::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>(m_configName).value_or(false);

        if (m_isEnabled) {
            apply();
        }
    }

    void PatchMod::onConfigSave(Config& cfg) {
        cfg.set<bool>(m_configName, m_isEnabled);
    }

    void PatchMod::apply() {
        if (m_patches.empty() || m_hasFailingPatch) {
            return;
        }

        log("[%s] Toggling %s", m_patchName.c_str(), m_isEnabled ? "on" : "off");

        if (m_isEnabled) {
            for (auto& p : m_patches) {
                patch(p);
            }
        }
        else {
            for (auto& p : m_patches) {
                undoPatch(p);
            }
        }
    }

    bool Hookcall(void* toHook, void* ourFunct, int len)
    {
        if (len < 5)
        {
            return false;
        }
        DWORD curProtection;
        VirtualProtect(toHook, len, PAGE_EXECUTE_READWRITE, &curProtection);
        memset(toHook, 0x90, len);

        DWORD relativeAddress = ((DWORD)ourFunct - (DWORD)toHook) - 5;
        *(BYTE*)toHook = 0xE8;
        *(DWORD*)((DWORD)toHook + 1) = relativeAddress;

        DWORD temp;
        VirtualProtect(toHook, len, curProtection, &temp);

        return true;
    }

	bool Hookjmp(void* toHook, void* ourFunct, int len)
	{
		if (len < 5)
		{
			return false;
		}
		DWORD curProtection;
		VirtualProtect(toHook, len, PAGE_EXECUTE_READWRITE, &curProtection);
		memset(toHook, 0x90, len);

		DWORD relativeAddress = ((DWORD)ourFunct - (DWORD)toHook) - 5;
		*(BYTE*)toHook = 0xE9;
		*(DWORD*)((DWORD)toHook + 1) = relativeAddress;

		DWORD temp;
		VirtualProtect(toHook, len, curProtection, &temp);

		return true;
	}

	//the code i use for injecting stuff
	//patch bytes
	void Patchmem(BYTE* dst, BYTE* src, unsigned int size)
	{
		DWORD oldprotect;
		//set prems of area of memory to execute&read&write, copy the old perms into oldprotect
		VirtualProtect(dst, size, PAGE_EXECUTE_READWRITE, &oldprotect);
		//write our new bytes into the dst with the bytes in src
		memcpy(dst, src, size);
		//set the perms of the area of memory back to what ever it was before we were here
		VirtualProtect(dst, size, oldprotect, &oldprotect);

	}

    void from_json(const json& j, PatchMod& mod) {
        mod.m_patchName = j.at("name").get<string>();

        if (j.find("desc") != j.end()) {
            mod.m_tooltip = j.at("desc").get<string>();
        }

        if (j.find("category") != j.end()) {
            mod.m_category = j.at("category").get<string>();
        }

        for (const auto& patch : j.at("patches")) {
			auto location = patch.at("location").get<string>();
            auto pattern = patch.at("pattern").get<string>();
            auto patchPattern = patch.at("patch").get<string>();
            int offset{ 0 };

            if (patch.find("offset") != patch.end()) {
                offset = patch.at("offset").get<int>();
            }

            int n{ 0 };

            if (patch.find("n") != patch.end()) {
                n = patch.at("n").get<int>();
            }

            mod.addPatch(location, pattern, offset, buildPattern(patchPattern), n);
        }

        mod.buildConfigName();
    }
}
