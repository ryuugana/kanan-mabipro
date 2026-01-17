#include <Scan.hpp>
#include <String.hpp>

#include "Log.hpp"
#include "Game.hpp"
#include <Module.cpp>

using namespace std;

namespace kanan {
    Game::Game()
        : m_rendererPtr{ nullptr },
        m_entityListPtr{ nullptr }, 
        m_worldPtr{nullptr}, 
        m_accountPtr{nullptr}
    {
        log("Entering Game constructor.");

        // Find the games global renderer pointer.
        auto rendererAddress = getModuleBase("Renderer2.dll");

        if (rendererAddress) {
            // Always at the same static memory address relative to Renderer2.dll
            m_rendererPtr = (CRenderer*)(*rendererAddress + 0x30E9D4);

            log("Got CRendererPtr %p", m_rendererPtr);
        }
        else {
            error("Failed to find address of CRendererPtr.");
        }
        
        // Find the games global entity list pointer.
        auto entityListAddress = scan("Pleione.dll", "? ? 03 01 00 00 00 00 00 00 00 00 00 00 00 D8");

        if (entityListAddress) {
            m_entityListPtr = *(CEntityListPtr**)(*entityListAddress);

            log("Got CEntityListPtr %p", m_entityListPtr);
        }
        else {
            error("Failed to find CEntityListPtr.");
        }

        // Find the games global world pointer.
        auto worldAddress = scan("Pleione.dll", "A1 60 DF FB 63 C3");

        if (worldAddress) {
            m_worldPtr = *(CWorldPtr**)(*worldAddress + 1);

            log("Got CWorldPtr %p", m_worldPtr);
        }
        else {
            error("Failed to find CWorldPtr.");
        }

         // Find the games global account pointer.
        auto accountAddress = scan("Pleione.dll", "8B 0D ? ? ? ? 56 FF 75 D4 8D 45 EC 50 E8");

        if (accountAddress) {
            m_accountPtr = *(CAccountPtr**)(*accountAddress + 2);

            log("Got CAccountPtr %p", m_accountPtr);
        }
        else {
            error("Failed to find CAccountPtr.");
        }

        log("Leaving Game constructor.");
    }

    KCharacter* Game::getCharacterByID(uint64_t id) {
        auto entityList = getEntityList();

        if (entityList == nullptr) {
            return nullptr;
        }

        // Go through the entity list looking for a character with the matching
        // ID.
        auto& characters = entityList->characters;
        auto highestIndex = characters.count;
        auto node = characters.root;

        for (uint32_t i = 0; i < highestIndex && node != nullptr; ++i, node = node->next) {
            auto character = (KCharacter*)node->character;

            if (character == nullptr) {
                continue;
            }

            auto characterID = character->getID();

            if (!characterID || *characterID != id) {
                continue;
            }

            return character;
        }

        // If we get here then no character with the specified ID is in the list.
        return nullptr;
    }

    KCharacter* Game::getLocalCharacter() {
        auto world = getWorld();
        log("World: %d", world);
        if (world == nullptr) {
            return nullptr;
        }

        return getCharacterByID(world->localPlayerID);
    }

    void Game::changeChannel(int channel) {
        static auto cc = (char(__thiscall*)(void*, CString**, char, char))scan("client.exe", "55 8B EC 6A ? 68 ? ? ? ? 64 A1 ? ? ? ? 50 83 EC ? 53 56 57 A1 ? ? ? ? 33 C5 50 8D 45 F4 64 A3 ? ? ? ? 8B D9 A1 ? ? ? ? 83 78 20 ?").value_or(0);
        static auto logged = false;

        if (!logged) {
            log("Change channel function = %p", cc);
            logged = true;
        }
        //patching to allow to work on nao
        if (channel < 1 || channel > 10) {
            return;
        }

        auto account = getAccount();

        if (!account) {
            return;
        }

        log("Account = %p", account);

        string str{ "Ch" };

        str += to_string(channel);

        log("Channel = %s", str.c_str());

        CString val{};

        // CMessage::WriteString only needs these members.
        val.capacity = sizeof(val.buffer) / 2;
        val.length = str.length();
        val.referenceCount = 1;

        wcscpy_s((wchar_t*)val.buffer, val.capacity, widen(str).c_str());

        auto val2 = &val;

        cc(account, &val2, 0, 0);
    }

    CRenderer* Game::getRenderer() const {
        if (m_rendererPtr == nullptr || m_rendererPtr->camera == nullptr) {
            return nullptr;
        }

        return m_rendererPtr;
    }

    CEntityList* Game::getEntityList() const {
        if (m_entityListPtr == nullptr || m_entityListPtr->entityList == nullptr) {
            return nullptr;
        }

        return m_entityListPtr->entityList + 0x28; // subtract offset to pointer
    }

    CWorld* Game::getWorld() const {
        if (m_worldPtr == nullptr || m_worldPtr->world == nullptr) {
            return nullptr;
        }

        return m_worldPtr->world;
    }

    CAccount* Game::getAccount() const {
		if (m_accountPtr == nullptr || m_accountPtr->account == nullptr) {
			return nullptr;
		}

		return m_accountPtr->account;
    }
    }
