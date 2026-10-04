#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <Windows.h>
#include <DbgHelp.h>

#include <imgui.h>
#include <Config.hpp>
#include <FunctionHook.hpp>
#include <String.hpp>

#include "Log.hpp"
#include "CrashDiagnostics.hpp"

using namespace std;

namespace kanan {
    using NtTerminateProcessFn = LONG(NTAPI*)(HANDLE process, LONG status);
    using NtQueryInformationProcessFn = LONG(NTAPI*)(HANDLE process, ULONG infoClass, PVOID info, ULONG size,
        PULONG returnedSize);
    using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE process, DWORD processId, HANDLE file, MINIDUMP_TYPE type,
        PMINIDUMP_EXCEPTION_INFORMATION exception, PMINIDUMP_USER_STREAM_INFORMATION userStream,
        PMINIDUMP_CALLBACK_INFORMATION callback);

    // NtQueryInformationProcess's ProcessExecuteFlags, and its flag for exception chain validation
    // (SEHOP) being off (MEM_EXECUTE_OPTION_DISABLE_EXCEPTION_CHAIN_VALIDATION).
    constexpr ULONG processExecuteFlags = 34;
    constexpr ULONG executeNoChainValidation = 0x40;

    constexpr DWORD cppExceptionCode = 0xE06D7363;

    // Minidumps kept in KananCrash\; older ones are deleted as Kanan starts.
    constexpr size_t dumpsKept = 5;

    // Small, but with every thread's stack, the memory they point at and the modules.
    constexpr auto dumpType = (MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory |
        MiniDumpWithUnloadedModules);

    // What was found at startup, for the menu.
    static bool g_isInstalled{ false };
    static bool g_isSehopKnown{ false };
    static bool g_isSehopOn{ false };

    static unique_ptr<FunctionHook> g_terminateHook{};
    static MiniDumpWriteDumpFn g_miniDumpWriteDump{ nullptr };
    static wstring g_dumpFolder{};
    static atomic<LONG> g_exitHandled{ 0 };
    static atomic<LONG> g_overflowHandled{ 0 };

    // ntdll's image, which holds every thread's final exception handler.
    static uintptr_t g_ntdllStart{ 0 };
    static uintptr_t g_ntdllEnd{ 0 };

    // A dump is written by a helper thread made at startup: the thread ending the game may hold the
    // loader lock, and one that overflowed its stack has almost no stack left to work with.
    struct DumpRequest {
        wchar_t path[MAX_PATH];
        EXCEPTION_POINTERS* exception;  // the overflow, or null
        DWORD thread;                   // the thread it happened on
        bool written;
    };

    static DumpRequest g_request{};
    static HANDLE g_requestEvent{ nullptr };
    static HANDLE g_doneEvent{ nullptr };

    // The last exceptions seen, for the report when the game ends abnormally. C++ exceptions are
    // routine: the game throws one each time it closes a dialog.
    struct RecentException {
        DWORD code;
        uintptr_t address;
        uintptr_t info0;
        uintptr_t info1;
        DWORD thread;
        DWORD tick;
        char type[64];  // a C++ exception's type
    };

    constexpr LONG recentSize = 16;
    static RecentException g_recent[recentSize]{};
    static atomic<LONG> g_recentCount{ 0 };

    // What an exit code means, for the codes that end a game.
    static const char* exitMeaning(DWORD code) {
        switch (code) {
        case 0: return "a normal exit";
        case 1: return "ended by another program (Task Manager's End task, a launcher, anti-cheat or security software use this)";
        case 0xC0000005: return "an access violation nothing handled";
        case 0xC00000FD: return "a stack overflow (runaway recursion)";
        case 0xC0000409: return "a fail-fast or security check failure (Windows ended it on the spot)";
        case 0xC0000374: return "heap corruption (Windows ended it on the spot)";
        case 0xC000041D: return "an exception in a window callback nothing handled";
        case cppExceptionCode: return "a C++ exception nothing handled";
        case 0x80000003: return "a breakpoint nothing handled";
        case 0xC0000420: return "an assertion failure";
        case 0x40010004: return "ended by a debugger";
        case 0xFFFFFFFF: return "ended by another program (PowerShell's Stop-Process and many tools use this)";
        default: return "unknown (if it's a small number, the program that ended the game chose it)";
        }
    }

    // "Module.dll+0x1234" for an address, or the bare address.
    static void describe(uintptr_t address, char* out, size_t size) {
        HMODULE module{ nullptr };
        wchar_t path[MAX_PATH]{};

        if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCWSTR)address, &module) && GetModuleFileNameW(module, path, MAX_PATH) != 0)
        {
            auto name = wcsrchr(path, L'\\');

            snprintf(out, size, "%S+0x%X", name != nullptr ? name + 1 : path, (unsigned)(address - (uintptr_t)module));
            return;
        }

        snprintf(out, size, "%08X", (unsigned)address);
    }

    // Whether ADDRESS comes right after a call instruction in executable memory: a likely return
    // address. Kept free of C++ objects so the SEH guard can protect the reads.
    static bool isReturnAddress(uintptr_t address) {
        MEMORY_BASIC_INFORMATION memory{};

        if (address < 0x10000 || VirtualQuery((void*)(address - 7), &memory, sizeof(memory)) == 0 ||
            memory.State != MEM_COMMIT || (uintptr_t)memory.BaseAddress + memory.RegionSize < address ||
            (memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)) == 0)
        {
            return false;
        }

        __try {
            auto code = (const uint8_t*)address;

            return code[-5] == 0xE8 ||                              // call rel32
                (code[-2] == 0xFF && (code[-1] & 0x38) == 0x10) ||  // call reg, call [reg]
                (code[-3] == 0xFF && (code[-2] & 0x38) == 0x10) ||  // call [reg+disp8]
                (code[-6] == 0xFF && (code[-5] & 0x38) == 0x10) ||  // call [disp32], call [reg+disp32]
                (code[-7] == 0xFF && (code[-6] & 0x38) == 0x10);    // call [reg+index+disp32]
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Reads one stack slot, or 0 if it can't be read. SEH-guarded.
    static uintptr_t readSlot(const uintptr_t* slot) {
        __try {
            return *slot;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }

    // Logs the return addresses on a stack from FROM up (up to 256 KB): the calls in progress,
    // including through code without frame pointers (some may be stale). A runaway recursion shows
    // as a repeating run.
    static void logReturnAddresses(const uintptr_t* from, int limit) {
        MEMORY_BASIC_INFORMATION memory{};
        auto end = from + 0x10000;

        if (VirtualQuery(from, &memory, sizeof(memory)) != 0) {
            auto regionEnd = (const uintptr_t*)((uintptr_t)memory.BaseAddress + memory.RegionSize);

            end = regionEnd < end ? regionEnd : end;
        }

        uintptr_t last{ 0 };
        int found{ 0 };
        char where[MAX_PATH + 32];

        for (auto p = from; p < end && found < limit; ++p) {
            auto value = readSlot(p);

            if (value != 0 && value != last && isReturnAddress(value)) {
                describe(value, where, sizeof(where));
                log("[CrashDiagnostics]   %s", where);
                last = value;
                ++found;
            }
        }
    }

    // The return addresses on a stack from FROM up, as one line ("A <- B <- C"), up to MAX of them.
    static void returnChain(const uintptr_t* from, int max, char* out, size_t size) {
        MEMORY_BASIC_INFORMATION memory{};
        auto end = from + 0x2000;

        if (VirtualQuery(from, &memory, sizeof(memory)) != 0) {
            auto regionEnd = (const uintptr_t*)((uintptr_t)memory.BaseAddress + memory.RegionSize);

            end = regionEnd < end ? regionEnd : end;
        }

        uintptr_t last{ 0 };
        int found{ 0 };
        size_t used{ 0 };
        char where[MAX_PATH + 32];

        out[0] = '\0';

        for (auto p = from; p < end && found < max && used + 8 < size; ++p) {
            auto value = readSlot(p);

            if (value != 0 && value != last && isReturnAddress(value)) {
                describe(value, where, sizeof(where));
                used += snprintf(out + used, size - used, "%s%s", found == 0 ? "" : " <- ", where);
                last = value;
                ++found;
            }
        }
    }

    // A C++ exception's type name (".?AVbad_alloc@std@@") from its ThrowInfo. SEH-guarded.
    static void cppType(const EXCEPTION_RECORD* record, char* name, size_t size) {
        name[0] = '\0';

        if (record->NumberParameters < 3) {
            return;
        }

        __try {
            auto throwInfo = (const uint8_t*)record->ExceptionInformation[2];
            auto catchables = *(const uint8_t* const*)(throwInfo + 12);     // ThrowInfo::pCatchableTypeArray
            auto first = *(const uint8_t* const*)(catchables + 4);          // its first CatchableType
            auto type = *(const uint8_t* const*)(first + 4);                // CatchableType::pType

            strncpy_s(name, size, (const char*)(type + 8), _TRUNCATE);      // TypeDescriptor::name
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            name[0] = '\0';
        }
    }

    // Whether Windows will accept this thread's exception handler chain (fs:[0]) when it validates
    // it (SEHOP), before it hands an exception to any handler: every record inside the thread's stack
    // and aligned, each above the one before, and the last one ntdll's final handler. When it won't,
    // no handler runs and the game is ended on the spot. SEH-guarded.
    static bool isChainValid() {
        auto tib = (const NT_TIB*)NtCurrentTeb();
        auto low = (uintptr_t)tib->StackLimit;
        auto high = (uintptr_t)tib->StackBase;
        auto record = (uintptr_t)tib->ExceptionList;
        uintptr_t handler{ 0 };

        __try {
            for (int i = 0; i < 1024 && record != 0xFFFFFFFF; ++i) {
                if (record < low || record + 8 > high || (record & 3) != 0) {
                    return false;
                }

                auto next = ((const uintptr_t*)record)[0];

                handler = ((const uintptr_t*)record)[1];

                if (next != 0xFFFFFFFF && next <= record) {
                    return false;
                }

                record = next;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }

        return record == 0xFFFFFFFF && handler >= g_ntdllStart && handler < g_ntdllEnd;
    }

    // The handler chain as one line: each record's address and handler, until the end or the first
    // record Windows would reject. SEH-guarded.
    static void handlerChain(char* out, size_t size) {
        auto tib = (const NT_TIB*)NtCurrentTeb();
        auto low = (uintptr_t)tib->StackLimit;
        auto high = (uintptr_t)tib->StackBase;
        auto record = (uintptr_t)tib->ExceptionList;
        size_t used = snprintf(out, size, "stack %08X-%08X:", (unsigned)low, (unsigned)high);
        char where[MAX_PATH + 32];

        for (int i = 0; i < 32 && used + MAX_PATH + 64 < size; ++i) {
            if (record == 0xFFFFFFFF) {
                snprintf(out + used, size - used, " end");
                return;
            }

            if (record < low || record + 8 > high || (record & 3) != 0) {
                snprintf(out + used, size - used, " %08X (outside the stack or misaligned)", (unsigned)record);
                return;
            }

            auto next = readSlot((const uintptr_t*)record);
            auto handler = readSlot((const uintptr_t*)record + 1);

            describe(handler, where, sizeof(where));
            used += snprintf(out + used, size - used, " %08X:%s", (unsigned)record, where);

            if (next != 0xFFFFFFFF && next <= record) {
                snprintf(out + used, size - used, " -> %08X (not above the one before)", (unsigned)next);
                return;
            }

            record = next;
        }

        snprintf(out + used, size - used, " ...");
    }

    // "C++ exception .?AU...", or "Exception 0xC0000005 at Module+0x123 (info)".
    static void describeException(const EXCEPTION_RECORD* record, char* out, size_t size) {
        if (record->ExceptionCode == cppExceptionCode) {
            char type[128];

            cppType(record, type, sizeof(type));
            snprintf(out, size, "C++ exception %s", type[0] != '\0' ? type : "(unknown type)");
            return;
        }

        char where[MAX_PATH + 32];

        describe((uintptr_t)record->ExceptionAddress, where, sizeof(where));
        snprintf(out, size, "Exception 0x%08X at %s (%08X %08X)", (unsigned)record->ExceptionCode, where,
            record->NumberParameters > 0 ? (unsigned)record->ExceptionInformation[0] : 0u,
            record->NumberParameters > 1 ? (unsigned)record->ExceptionInformation[1] : 0u);
    }

    // An exception Windows won't let anything handle (see isChainValid): it ends the game, with no
    // error. Logged at once, while the game is still there.
    static void warnUnhandleable(EXCEPTION_POINTERS* info) {
        static atomic<LONG> s_warnings{ 0 };

        if (s_warnings++ >= 3) {
            return;
        }

        char what[MAX_PATH + 96];
        char chain[1024];
        char handlers[4096];

        describeException(info->ExceptionRecord, what, sizeof(what));
        returnChain((const uintptr_t*)info->ContextRecord->Esp, 10, chain, sizeof(chain));
        handlerChain(handlers, sizeof(handlers));

        log("[CrashDiagnostics] %s on thread %lu will end the game: Windows' exception chain validation (SEHOP) "
            "rejects the game's handler chain here, so nothing can handle it. Closing the mailbox or a spirit weapon "
            "chat can do this. To fix it, turn SEHOP off for Client.exe: Windows Security > App & "
            "browser control > Exploit protection settings > Program settings > Add program to customize > Add by "
            "program name > Client.exe > Validate exception chains (SEHOP) > Override system settings, Off.",
            what, GetCurrentThreadId());
        log("[CrashDiagnostics]   thrown via: %s", chain);
        log("[CrashDiagnostics]   handler chain: %s", handlers);
    }

    // Faults the game survives (something caught them) are logged as they happen, at most 3 times
    // from the same place and 100 lines a session: they often explain a crash that comes later.
    static CRITICAL_SECTION g_liveLock{};
    static uint32_t g_liveKeys[256]{};
    static uint8_t g_liveCounts[256]{};
    static LONG g_liveLogged{ 0 };

    static bool shouldLogLive(uint32_t key) {
        auto result = false;

        EnterCriticalSection(&g_liveLock);

        if (g_liveLogged < 100) {
            auto slot = (key * 2654435761u) >> 24;

            for (int i = 0; i < 256; ++i, slot = (slot + 1) & 255) {
                if (g_liveKeys[slot] == key || g_liveKeys[slot] == 0) {
                    g_liveKeys[slot] = key;
                    result = ++g_liveCounts[slot] <= 3;
                    break;
                }
            }

            if (result) {
                ++g_liveLogged;
            }
        }

        LeaveCriticalSection(&g_liveLock);
        return result;
    }

    static void logLive(EXCEPTION_POINTERS* info) {
        // A flood of exceptions (thousands a second) mustn't slow the game: only the first 50 each
        // second are looked at.
        static DWORD s_second{ 0 };
        static LONG s_thisSecond{ 0 };
        auto second = GetTickCount() / 1000;

        if (second != s_second) {
            s_second = second;
            s_thisSecond = 0;
        }

        if (++s_thisSecond > 50) {
            return;
        }

        auto record = info->ExceptionRecord;
        char chain[1024];

        returnChain((const uintptr_t*)info->ContextRecord->Esp, 8, chain, sizeof(chain));

        // The same place: the code, the address and the first return addresses on the stack.
        uint32_t key = record->ExceptionCode ^ (uint32_t)(uintptr_t)record->ExceptionAddress;

        for (size_t i = 0; chain[i] != '\0' && i < 64; ++i) {
            key = key * 31 + (uint8_t)chain[i];
        }

        if (!shouldLogLive(key == 0 ? 1 : key)) {
            return;
        }

        char what[MAX_PATH + 96];

        describeException(record, what, sizeof(what));
        log("[CrashDiagnostics] %s on thread %lu via: %s", what, GetCurrentThreadId(), chain);
    }

    static void remember(const EXCEPTION_RECORD* record) {
        auto& recent = g_recent[g_recentCount++ % recentSize];

        recent.code = record->ExceptionCode;
        recent.address = (uintptr_t)record->ExceptionAddress;
        recent.info0 = record->NumberParameters > 0 ? record->ExceptionInformation[0] : 0;
        recent.info1 = record->NumberParameters > 1 ? record->ExceptionInformation[1] : 0;
        recent.thread = GetCurrentThreadId();
        recent.tick = GetTickCount();
        recent.type[0] = '\0';

        if (record->ExceptionCode == cppExceptionCode) {
            cppType(record, recent.type, sizeof(recent.type));
        }
    }

    static void logRecentExceptions() {
        auto count = g_recentCount.load();
        auto now = GetTickCount();
        char where[MAX_PATH + 32];

        log("[CrashDiagnostics] Exceptions this session: %ld; the last ones:", count);

        for (auto i = count > recentSize ? count - recentSize : 0; i < count; ++i) {
            auto& recent = g_recent[i % recentSize];

            if (recent.code == cppExceptionCode) {
                log("[CrashDiagnostics]   C++ exception %s on thread %lu, %lu ms before",
                    recent.type[0] != '\0' ? recent.type : "(unknown type)", recent.thread, now - recent.tick);
                continue;
            }

            describe(recent.address, where, sizeof(where));
            log("[CrashDiagnostics]   0x%08X at %s (%08X %08X) on thread %lu, %lu ms before", recent.code, where,
                (unsigned)recent.info0, (unsigned)recent.info1, recent.thread, now - recent.tick);
        }
    }

    static void dumpPath(wchar_t* out, const wchar_t* kind, DWORD code) {
        SYSTEMTIME time{};

        GetLocalTime(&time);
        swprintf_s(out, MAX_PATH, L"%s\\%s_%04u-%02u-%02u_%02u-%02u-%02u_%08X.dmp", g_dumpFolder.c_str(), kind,
            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, (unsigned)code);
    }

    // Deletes all but the newest minidumps.
    static void pruneDumps() {
        namespace fs = std::filesystem;

        error_code ec{};
        vector<fs::directory_entry> dumps{};

        for (auto& entry : fs::directory_iterator{ g_dumpFolder, ec }) {
            if (entry.is_regular_file(ec) && _wcsicmp(entry.path().extension().c_str(), L".dmp") == 0) {
                dumps.push_back(entry);
            }
        }

        if (dumps.size() <= dumpsKept) {
            return;
        }

        sort(dumps.begin(), dumps.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
            error_code ec{};

            return a.last_write_time(ec) > b.last_write_time(ec);
        });

        for (auto i = dumpsKept; i < dumps.size(); ++i) {
            fs::remove(dumps[i].path(), ec);
        }
    }

    // Writes the requested dumps; for a stack overflow, also logs it (the overflowed thread can't).
    static DWORD WINAPI dumpWorker(LPVOID) {
        // One dump first, thrown away, so DbgHelp has loaded everything it needs before a dump at the
        // game's end, which may come while the loader lock is held.
        auto warmup = g_dumpFolder + L"\\_warmup.dmp";
        auto file = CreateFileW(warmup.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (file != INVALID_HANDLE_VALUE) {
            g_miniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, MiniDumpNormal, nullptr, nullptr, nullptr);
            CloseHandle(file);
            DeleteFileW(warmup.c_str());
        }

        for (;;) {
            WaitForSingleObject(g_requestEvent, INFINITE);
            ResetEvent(g_requestEvent);

            auto& request = g_request;

            if (request.exception != nullptr) {
                auto record = request.exception->ExceptionRecord;
                char where[MAX_PATH + 32];

                describe((uintptr_t)record->ExceptionAddress, where, sizeof(where));
                log("[CrashDiagnostics] Stack overflow on thread %lu at %s - the calls in progress on that thread:",
                    request.thread, where);
                logReturnAddresses((const uintptr_t*)request.exception->ContextRecord->Esp, 80);
                logRecentExceptions();
            }

            file = CreateFileW(request.path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            request.written = false;

            if (file != INVALID_HANDLE_VALUE) {
                MINIDUMP_EXCEPTION_INFORMATION exception{ request.thread, request.exception, FALSE };

                request.written = g_miniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, dumpType,
                    request.exception != nullptr ? &exception : nullptr, nullptr, nullptr) != FALSE;
                CloseHandle(file);
            }

            if (request.exception != nullptr) {
                log("[CrashDiagnostics] Minidump %s: %s", request.written ? "written" : "not written",
                    narrow(request.path).c_str());
            }

            SetEvent(g_doneEvent);
        }
    }

    // Has the helper thread write a dump, and waits for it.
    static bool requestDump(const wchar_t* path, EXCEPTION_POINTERS* exception) {
        if (g_requestEvent == nullptr) {
            return false;
        }

        wcscpy_s(g_request.path, path);
        g_request.exception = exception;
        g_request.thread = GetCurrentThreadId();
        ResetEvent(g_doneEvent);
        SetEvent(g_requestEvent);

        return WaitForSingleObject(g_doneEvent, 20000) == WAIT_OBJECT_0 && g_request.written;
    }

    // Set while this thread is in onException: an exception in there (a guarded read) comes back
    // through it.
    static thread_local bool t_isHandling{ false };

    static LONG CALLBACK onException(EXCEPTION_POINTERS* info) {
        auto record = info->ExceptionRecord;
        auto code = record->ExceptionCode;

        // Debug output and thread naming are not errors.
        if (code == DBG_PRINTEXCEPTION_C || code == 0x4001000A || code == 0x406D1388 || t_isHandling) {
            return EXCEPTION_CONTINUE_SEARCH;
        }

        t_isHandling = true;
        remember(record);

        if (code == EXCEPTION_STACK_OVERFLOW) {
            // The thread has one page of stack left: if the overflow is swallowed and it overflows
            // again, Windows ends the game without a word. Hand everything to the helper thread now,
            // while this one can still wait for it.
            if (g_overflowHandled.exchange(1) == 0) {
                static wchar_t path[MAX_PATH];

                dumpPath(path, L"stackoverflow", code);
                requestDump(path, info);
            }
        }
        else if (g_isSehopOn && !isChainValid()) {
            warnUnhandleable(info);
        }
        else if (code != cppExceptionCode) {
            logLive(info);
        }

        t_isHandling = false;
        return EXCEPTION_CONTINUE_SEARCH;
    }

    // Runs once, on the thread ending the game, while the game is still all there.
    static void reportExit(HANDLE process, LONG status) {
        if (status == 0) {
            log("[CrashDiagnostics] The game is exiting normally");
            return;
        }

        log("[CrashDiagnostics] The game is ending: %s, status 0x%08X (%s), on thread %lu",
            process == nullptr ? "it is exiting (ExitProcess)" : "TerminateProcess", (unsigned)status,
            exitMeaning((DWORD)status), GetCurrentThreadId());

        logRecentExceptions();

        void* frames[48]{};
        auto count = RtlCaptureStackBackTrace(0, _countof(frames), frames, nullptr);
        char where[MAX_PATH + 32];

        log("[CrashDiagnostics] Call stack (frame chain, %u frames):", count);

        for (USHORT i = 0; i < count; ++i) {
            describe((uintptr_t)frames[i], where, sizeof(where));
            log("[CrashDiagnostics]   %2u %s", i, where);
        }

        log("[CrashDiagnostics] Return addresses on the stack:");
        logReturnAddresses((const uintptr_t*)&count, 60);

        wchar_t path[MAX_PATH];

        dumpPath(path, L"exit", (DWORD)status);

        auto written = requestDump(path, nullptr);

        log("[CrashDiagnostics] Minidump %s: %s", written ? "written" : "not written", narrow(path).c_str());
    }

    static LONG NTAPI onTerminate(HANDLE process, LONG status) {
        auto original = (NtTerminateProcessFn)g_terminateHook->getOriginal();
        auto isSelf = process == nullptr || process == GetCurrentProcess() || GetProcessId(process) == GetCurrentProcessId();

        // Only the first time: ExitProcess ends the other threads first (a null handle), then
        // unloads the DLLs and ends itself.
        if (isSelf && g_exitHandled.exchange(1) == 0) {
            reportExit(process, status);
        }

        return original(process, status);
    }

    // Appends a line to a text file (the watcher runs outside the game, after it's gone).
    static void appendLine(const wstring& file, const string& line) {
        auto handle = CreateFileW(file.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (handle == INVALID_HANDLE_VALUE) {
            return;
        }

        DWORD written{};

        WriteFile(handle, line.data(), (DWORD)line.size(), &written, nullptr);
        WriteFile(handle, "\r\n", 2, &written, nullptr);
        CloseHandle(handle);
    }

    static string timestamp() {
        SYSTEMTIME time{};
        char text[32];

        GetLocalTime(&time);
        snprintf(text, sizeof(text), "%04u-%02u-%02u %02u:%02u:%02u", time.wYear, time.wMonth, time.wDay, time.wHour,
            time.wMinute, time.wSecond);
        return text;
    }

    // Starts the watcher: Windows' 32-bit rundll32 loading this DLL in watch-only mode, which records
    // the game's exit code however it ends - even when nothing inside the game can.
    static void startWatcher(HMODULE self, const string& gameFolder) {
        wchar_t rundll[MAX_PATH]{};
        wchar_t dll[MAX_PATH]{};

        if (GetSystemWow64DirectoryW(rundll, MAX_PATH) == 0) {
            GetSystemDirectoryW(rundll, MAX_PATH);
        }

        wcscat_s(rundll, L"\\rundll32.exe");
        GetModuleFileNameW(self, dll, MAX_PATH);

        auto commandLine = L"rundll32.exe \"" + wstring{ dll } + L"\",WatchGame " + to_wstring(GetCurrentProcessId()) +
            L" " + widen(gameFolder);
        STARTUPINFOW startup{ sizeof(startup) };
        PROCESS_INFORMATION process{};

        if (CreateProcessW(rundll, commandLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS,
            nullptr, nullptr, &startup, &process))
        {
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
            log("[CrashDiagnostics] Watcher started (process %lu)", process.dwProcessId);
        }
        else {
            log("[CrashDiagnostics] Couldn't start the watcher (error %lu)", GetLastError());
        }
    }

    void CrashDiagnostics::installAtStartup(const string& gameFolder, HMODULE self) {
        Config cfg{ gameFolder + "/config.txt" };
        auto isEnabled = cfg.get<bool>("CrashDiagnostics.Enabled").value_or(true);
        auto isWatcherEnabled = cfg.get<bool>("CrashDiagnostics.Watcher").value_or(false);
        auto ntdll = GetModuleHandleW(L"ntdll.dll");

        if (auto query = (NtQueryInformationProcessFn)GetProcAddress(ntdll, "NtQueryInformationProcess")) {
            ULONG flags{ 0 };

            g_isSehopKnown = query(GetCurrentProcess(), processExecuteFlags, &flags, sizeof(flags), nullptr) >= 0;
            g_isSehopOn = g_isSehopKnown && (flags & executeNoChainValidation) == 0;
        }

        if (g_isSehopKnown) {
            log("[CrashDiagnostics] Windows' exception chain validation (SEHOP) is %s for the game",
                g_isSehopOn ? "on" : "off");
        }

        if (!isEnabled) {
            log("[CrashDiagnostics] Off");
            return;
        }

        auto dos = (const IMAGE_DOS_HEADER*)ntdll;
        auto nt = (const IMAGE_NT_HEADERS*)((uintptr_t)ntdll + dos->e_lfanew);

        g_ntdllStart = (uintptr_t)ntdll;
        g_ntdllEnd = g_ntdllStart + nt->OptionalHeader.SizeOfImage;

        g_dumpFolder = widen(gameFolder) + L"\\KananCrash";
        CreateDirectoryW(g_dumpFolder.c_str(), nullptr);
        pruneDumps();

        // Windows' own DbgHelp, not one a game folder may carry.
        auto dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);

        if (dbghelp != nullptr) {
            g_miniDumpWriteDump = (MiniDumpWriteDumpFn)GetProcAddress(dbghelp, "MiniDumpWriteDump");
        }

        if (g_miniDumpWriteDump != nullptr) {
            g_requestEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            g_doneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

            if (CreateThread(nullptr, 0, dumpWorker, nullptr, 0, nullptr) == nullptr) {
                g_requestEvent = nullptr;
            }
        }

        InitializeCriticalSection(&g_liveLock);
        AddVectoredExceptionHandler(1, onException);

        if (auto terminate = GetProcAddress(ntdll, "NtTerminateProcess")) {
            g_terminateHook = make_unique<FunctionHook>((uintptr_t)terminate, (uintptr_t)&onTerminate);
        }

        g_isInstalled = true;

        log("[CrashDiagnostics] On: crashes are logged here%s%s",
            g_requestEvent != nullptr ? ", with a minidump in KananCrash when the game ends abnormally" : "",
            g_terminateHook && g_terminateHook->isValid() ? "" : " (couldn't watch for the game ending)");

        if (isWatcherEnabled) {
            startWatcher(self, gameFolder);
        }
    }

    bool CrashDiagnostics::isWatcherProcess() {
        wchar_t path[MAX_PATH]{};

        GetModuleFileNameW(nullptr, path, MAX_PATH);

        auto name = wcsrchr(path, L'\\');

        return _wcsicmp(name != nullptr ? name + 1 : path, L"rundll32.exe") == 0;
    }

    CrashDiagnostics::CrashDiagnostics()
        : m_isEnabled{ true },
        m_isWatcherEnabled{ false }
    {
    }

    void CrashDiagnostics::onUI() {
        if (ImGui::TreeNode("Crash Diagnostics")) {
            ImGui::TextWrapped("When the game crashes or closes on its own, writes what happened to kananLog.txt and, "
                "if the game ended abnormally, saves a minidump (a small crash file) in the KananCrash folder in the "
                "game folder. Send both to whoever is helping you. The previous session's log is kept as "
                "kananLog.prev.txt. Nothing runs while the game runs normally.");
            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
            ImGui::Checkbox("Crash diagnostics", &m_isEnabled);
            ImGui::Checkbox("Record how the game ended, even when Windows ends it", &m_isWatcherEnabled);
            ImGui::TextWrapped("Runs a small helper next to the game (Windows' rundll32 with Kanan) that records the "
                "game's exit code. Only needed when someone helping you asks for it: some antivirus software is "
                "suspicious of it.");
            ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
            ImGui::TextWrapped("Changes take effect the next time you start the game.");

            if (g_isSehopKnown) {
                ImGui::Dummy(ImVec2{ 10.0f, 10.0f });

                if (g_isSehopOn) {
                    ImGui::TextWrapped("Windows' exception chain validation (SEHOP) is on for the game. That's fine for "
                        "most players. Optional: if the game closes, or freezes and then closes, when you close the "
                        "mailbox or a spirit weapon chat (with the X or [Close] button), turning it off for the game "
                        "fixes it:");
                    ImGui::BulletText("Windows Security > App & browser control > Exploit protection settings");
                    ImGui::BulletText("Program settings > Add program to customize > Add by program name: Client.exe");
                    ImGui::BulletText("Validate exception chains (SEHOP): tick Override system settings, turn it Off");
                    ImGui::BulletText("Apply, then restart the game");
                }
                else {
                    ImGui::TextWrapped("Windows' exception chain validation (SEHOP) is off for the game.");
                }
            }

            ImGui::TreePop();
        }
    }

    void CrashDiagnostics::onConfigLoad(const Config& cfg) {
        m_isEnabled = cfg.get<bool>("CrashDiagnostics.Enabled").value_or(true);
        m_isWatcherEnabled = cfg.get<bool>("CrashDiagnostics.Watcher").value_or(false);
    }

    void CrashDiagnostics::onConfigSave(Config& cfg) {
        cfg.set<bool>("CrashDiagnostics.Enabled", m_isEnabled);
        cfg.set<bool>("CrashDiagnostics.Watcher", m_isWatcherEnabled);
    }
}

// The watcher's entry, run by rundll32: "WatchGame <game process id> <game folder>". Waits for the game
// to end and, unless it ended normally, records how in the game's kananLog.txt. Exported undecorated,
// the name rundll32 looks up.
#pragma comment(linker, "/EXPORT:WatchGame=_WatchGame@16")

extern "C" void CALLBACK WatchGame(HWND, HINSTANCE, LPSTR commandLine, int) {
    using namespace kanan;

    char* rest{ nullptr };
    auto processId = strtoul(commandLine, &rest, 10);

    while (rest != nullptr && *rest == ' ') {
        ++rest;
    }

    auto kananLog = widen(rest != nullptr ? rest : "") + L"\\kananLog.txt";
    auto game = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);

    if (game == nullptr) {
        appendLine(kananLog, "[CrashDiagnostics] The watcher couldn't open the game (error " +
            to_string(GetLastError()) + ")");
        return;
    }

    WaitForSingleObject(game, INFINITE);

    DWORD code{ 0 };
    char text[512];

    GetExitCodeProcess(game, &code);
    CloseHandle(game);

    if (code == 0) {
        return;
    }

    snprintf(text, sizeof(text), "[CrashDiagnostics] %s watcher: the game ended with exit code 0x%08lX - %s",
        timestamp().c_str(), code, exitMeaning(code));
    appendLine(kananLog, text);
}
