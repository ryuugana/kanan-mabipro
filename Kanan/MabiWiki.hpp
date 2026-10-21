#pragma once

#include <string>

#include "Mod.hpp"

namespace kanan {
    // Mabinogi Wiki: opens the wiki of the game's items, enchants, skills and more from the Kanan
    // menu or with .wiki in chat. The wiki is one page, MabiWiki_Standalone.html, shipped apart from
    // Kanan: it makes its data from the game's own files in the browser (the first time it asks for
    // the package folder) and keeps it next to itself. Kanan only opens it, from the game folder or
    // else from Kanan.asi's folder.
    class MabiWiki : public Mod {
    public:
        MabiWiki();
        ~MabiWiki();

        void onUI() override;

        // Opens the wiki in the browser. On failure, why (a chat message).
        bool open(std::wstring& error);

        // Where MabiWiki_Standalone.html is: the game folder's, else the one next to Kanan.asi; ""
        // when neither has it.
        static std::wstring pagePath();

        static MabiWiki* instance();

    private:
        std::string m_openStatus;  // the last Open click's outcome
    };
}
