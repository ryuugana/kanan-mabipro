#include <algorithm>
#include <cstdint>

#include <Windows.h>

#include "Log.hpp"
#include "ScreenshotWriter.hpp"

using namespace std;

namespace kanan {
    // EXL's JPEG writer for D3D9 images; returns 1 when written.
    using WriteJpeg = int(__cdecl*)(const void* path, IDirect3DSurface9* surface, const RECT* rect, unsigned long quality);

    // esl::CStringT<wchar_t>: a pointer to a header holding the length (in characters) at +0x0C and
    // the text at +0x1A; null when empty.
    struct EslString {
        const uint8_t* data;
    };

    using StringConstruct = EslString*(__thiscall*)(EslString* string, const wchar_t* text);
    using StringDestruct = void(__thiscall*)(EslString* string);

    static StringConstruct g_stringConstruct{ nullptr };
    static StringDestruct g_stringDestruct{ nullptr };

    static wstring toString(const void* string) {
        auto data = ((const EslString*)string)->data;

        if (data == nullptr) {
            return {};
        }

        return { (const wchar_t*)(data + 0x1A), *(const uint32_t*)(data + 0x0C) };
    }

    ScreenshotWriter& ScreenshotWriter::get() {
        static ScreenshotWriter writer{};

        return writer;
    }

    ScreenshotWriter::ScreenshotWriter()
        : onWrite{},
        onWritten{},
        m_hook{}
    {
        if (auto esl = GetModuleHandleA("ESL.dll")) {
            g_stringConstruct = (StringConstruct)GetProcAddress(esl,
                "??0?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@PB_W@Z");
            g_stringDestruct = (StringDestruct)GetProcAddress(esl,
                "??1?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@XZ");
        }

        auto exl = GetModuleHandleA("EXL.dll");
        auto writeJpeg = exl ? GetProcAddress(exl,
            "?WriteJPEG@CIJL@esl@@SA?AW4EIJLError@esl_constant@2@ABV?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@2@PAUIDirect3DSurface9@@PAU?$_rect@J@2@K@Z")
            : nullptr;

        if (writeJpeg != nullptr && g_stringConstruct != nullptr && g_stringDestruct != nullptr) {
            m_hook = make_unique<FunctionHook>((uintptr_t)writeJpeg, (uintptr_t)&ScreenshotWriter::onWriteJpeg);
        }

        log("[ScreenshotWriter] %s the screenshot writer", isHooked() ? "Hooked" : "Failed to hook");
    }

    bool ScreenshotWriter::isScreenshot(const wstring& path) {
        if (path.size() < 7 || _wcsicmp(path.c_str() + path.size() - 4, L".jpg") != 0) {
            return false;
        }

        return all_of(path.end() - 7, path.end() - 4, [](wchar_t c) { return c >= L'0' && c <= L'9'; });
    }

    int __cdecl ScreenshotWriter::onWriteJpeg(const void* path, IDirect3DSurface9* surface, const RECT* rect,
        unsigned long quality)
    {
        auto& self = get();
        auto file = toString(path);

        if (self.onWrite && self.onWrite(file, surface, rect, quality)) {
            return 1;
        }

        auto result = ((WriteJpeg)self.m_hook->getOriginal())(path, surface, rect, quality);

        if (result == 1 && self.onWritten) {
            self.onWritten(file, surface, rect);
        }

        return result;
    }

    int ScreenshotWriter::write(const wstring& path, IDirect3DSurface9* surface, const RECT* rect, unsigned long quality) {
        if (!isHooked()) {
            return 0;
        }

        EslString eslPath{};

        g_stringConstruct(&eslPath, path.c_str());

        auto result = ((WriteJpeg)m_hook->getOriginal())(&eslPath, surface, rect, quality);

        g_stringDestruct(&eslPath);

        if (result == 1 && onWritten) {
            onWritten(path, surface, rect);
        }

        return result;
    }
}
