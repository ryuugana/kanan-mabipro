#include "ChatLog.hpp"

#include <sstream>
#include <ctime>
#include <String.hpp>

#include <Scan.hpp>

#include "dirent.h"
#include "imgui.h"
#include "MabiPacket.h"
#include "Log.hpp"
#include "Kanan.hpp"

namespace kanan {
	// Add Time to Chat puts the time in front of each line as the game adds it to the chat window,
	// instead of changing chat packets, so the game's own handling of the messages (speech bubbles,
	// name colors) sees them unchanged. The function is pleione::CInterfaceMgr's "add a line to the
	// chat window" (the one UserCommands prints with); every kind of chat line goes through it.
	using ChatLineFn = void(__fastcall*)(void* interfaceMgr, void* edx, const void* name, const void* message,
		unsigned long unknown1, unsigned long type, const void* extra, unsigned long unknown2);
	using StringCtorFn = void*(__thiscall*)(void* str, const wchar_t* text);
	using StringDtorFn = void(__thiscall*)(void* str);
	using StringContentFn = const wchar_t*(__thiscall*)(const void* str);

	static ChatLog* g_chatLog{ nullptr };
	static ChatLineFn g_originalChatLine{ nullptr };
	static StringCtorFn g_stringCtor{ nullptr };
	static StringDtorFn g_stringDtor{ nullptr };
	static StringContentFn g_stringContent{ nullptr };

	// In front of the name: the chat window's add-line function (Pleione.dll) first builds the name
	// part of a line in a string at [ebp-10h] (the chat type's tag, if any, then the name), then
	// looks up the name's color from the name itself:
	//   lea ecx, [ebp-10h] / nop / call CStringT::operator= / cmp dword ptr [ebp+74h], 0 / jne ...
	// The cmp/jne is replaced with a jump here, which puts the time in front of that string, so it
	// comes before the name while the color lookup still sees the name as it is.
	using StringAssignFn = void*(__thiscall*)(void* str, const wchar_t* text);

	static StringAssignFn g_stringAssign{ nullptr };
	static uintptr_t g_lineContinue{ 0 };      // after the cmp/jne, when the caller gave no color
	static uintptr_t g_lineColorGiven{ 0 };    // the jne's target

	// The line being added: its name as it came (the add-line function's first argument, at
	// [ebp+14h] where the patch is) and its text, and that name with the time in front, for the chat
	// log window and the chat's history (see hookedLogLine).
	static const void* g_liveName{ nullptr };
	static std::wstring g_liveNameText{};
	static std::wstring g_liveTimedName{};

	static void __stdcall prefixLine(void* line, const void* name) {
		g_liveName = nullptr;

		auto prefix = g_chatLog != nullptr ? g_chatLog->timePrefix() : std::wstring{};

		if (prefix.empty()) {
			return;
		}

		auto text = g_stringContent(line);
		auto timed = prefix + (text != nullptr ? text : L"");

		g_stringAssign(line, timed.c_str());

		auto nameText = name != nullptr ? g_stringContent(name) : nullptr;

		if (nameText != nullptr) {
			g_liveName = name;
			g_liveNameText = nameText;
			g_liveTimedName = prefix + g_liveNameText;
		}
	}

	static __declspec(naked) void hookLineText() {
		__asm {
			pushad
			pushfd
			push    dword ptr [ebp + 14h]
			lea     eax, [ebp - 10h]
			push    eax
			call    prefixLine
			popfd
			popad
			cmp     dword ptr [ebp + 74h], 0
			jne     given
			jmp     g_lineContinue

		given:
			jmp     g_lineColorGiven
		}
	}

	// After it builds the line for the chat at the bottom of the screen, the add-line function gives
	// the name as it came to the chat log window (CChatLogView, the window that can be opened and
	// expanded: name, message, extra, color, tab, and a flag) and to the chat's history, which that
	// window is filled from when it's opened (name, message, extra, color, tab). The color is already
	// worked out, so these never look it up from the name. When it's the line being added, they get
	// the name with the time; lines filled in from the history already have it.
	using LogLineFn = void(__fastcall*)(void* view, void* edx, const void* name, const void* message, const void* extra,
		unsigned long color, unsigned long tab, unsigned long flag);
	using HistoryLineFn = void(__fastcall*)(void* view, void* edx, const void* name, const void* message, const void* extra,
		unsigned long color, unsigned long tab);

	static LogLineFn g_originalLogLine{ nullptr };
	static HistoryLineFn g_originalHistoryLine{ nullptr };

	// Makes str the line's name with the time when name is the name of the line being added.
	static bool makeTimedName(const void* name, uintptr_t* str) {
		if (name == nullptr || name != g_liveName) {
			return false;
		}

		auto text = g_stringContent(name);

		if (text == nullptr || g_liveNameText != text) {
			return false;
		}

		g_stringCtor(str, g_liveTimedName.c_str());
		return true;
	}

	static void __fastcall hookedLogLine(void* view, void* edx, const void* name, const void* message, const void* extra,
		unsigned long color, unsigned long tab, unsigned long flag) {
		uintptr_t str[4]{};     // an esl::CStringT is a single pointer

		if (!makeTimedName(name, str)) {
			g_originalLogLine(view, edx, name, message, extra, color, tab, flag);
			return;
		}

		g_originalLogLine(view, edx, str, message, extra, color, tab, flag);
		g_stringDtor(str);
	}

	static void __fastcall hookedHistoryLine(void* view, void* edx, const void* name, const void* message, const void* extra,
		unsigned long color, unsigned long tab) {
		uintptr_t str[4]{};

		if (!makeTimedName(name, str)) {
			g_originalHistoryLine(view, edx, name, message, extra, color, tab);
			return;
		}

		g_originalHistoryLine(view, edx, str, message, extra, color, tab);
		g_stringDtor(str);
	}

	static void __fastcall hookedChatLine(void* interfaceMgr, void* edx, const void* name, const void* message,
		unsigned long unknown1, unsigned long type, const void* extra, unsigned long unknown2) {
		auto prefix = g_chatLog != nullptr ? g_chatLog->timePrefix() : std::wstring{};
		auto text = prefix.empty() || message == nullptr ? nullptr : g_stringContent(message);

		if (text == nullptr || text[0] == L'\0') {
			g_originalChatLine(interfaceMgr, edx, name, message, unknown1, type, extra, unknown2);
			return;
		}

		auto timed = prefix + text;
		uintptr_t str[4]{};     // an esl::CStringT is a single pointer
		g_stringCtor(str, timed.c_str());
		g_originalChatLine(interfaceMgr, edx, name, str, unknown1, type, extra, unknown2);
		g_stringDtor(str);
	}

	ChatLog::ChatLog()
		: m_fileLogEnabled{ false },
		m_startedLogging{ false },
		m_logs{},
		m_filter{},
		m_scrollToBottom{ false },
		m_autoScroll{ true },
		m_isChatLog{ false },
		m_isOpen{ false },
		m_isTime{ false },
		m_is24hour {false},
		m_isAuctionEnabled {false},
	    m_isFieldBossEnabled{ false },
	    m_isFieldBNotifyEnabled{ false },
		m_file{},
		m_partyMembers{}
	{
		m_hasSend = false;
		m_hasRecv = true;
		m_op.push_back(21100);
		m_op.push_back(21101);
		m_op.push_back(21107);
		m_op.push_back(21109);
		m_op.push_back(36502);
		m_op.push_back(36504);
		m_op.push_back(36520);
		m_op.push_back(50031);

		g_chatLog = this;

		auto esl = GetModuleHandleA("ESL.dll");

		if (esl != nullptr) {
			g_stringCtor = (StringCtorFn)GetProcAddress(esl, "??0?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@PB_W@Z");
			g_stringDtor = (StringDtorFn)GetProcAddress(esl, "??1?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAE@XZ");
			g_stringContent = (StringContentFn)GetProcAddress(esl, "?GetSafeContent@?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QBEPB_WXZ");
			g_stringAssign = (StringAssignFn)GetProcAddress(esl, "??4?$CStringT@_WVunicode_string_trait@esl@@Vunicode_string_implement@2@@esl@@QAEAAV01@PB_W@Z");
		}

		auto lineText = scan("Pleione.dll", "8D 4D F0 90 E8 ? ? ? ? 83 7D 74 00 75 11 FF 75 14 8B 0D ? ? ? ? E8");

		if (lineText && g_stringContent != nullptr && g_stringAssign != nullptr) {
			auto at = *lineText + 9;    // the cmp dword ptr [ebp+74h], 0 (4 bytes) and jne (2 bytes)
			g_lineContinue = at + 6;
			g_lineColorGiven = at + 6 + 0x11;

			auto rel = (int32_t)((uintptr_t)&hookLineText - (at + 5));

			m_linePatch.address = at;
			m_linePatch.bytes = { 0xE9, (int16_t)(rel & 0xFF), (int16_t)((rel >> 8) & 0xFF), (int16_t)((rel >> 16) & 0xFF),
				(int16_t)((rel >> 24) & 0xFF), 0x90 };

			if (patch(m_linePatch)) {
				log("[ChatLog] Patched the chat window's line text at %p", (void*)at);
				hookLogWindow();
				return;
			}

			log("[ChatLog] Couldn't patch the chat window's line text; putting the time in front of messages instead");
		}

		auto chatLine = scan("Pleione.dll", "6A 0C B8 ? ? ? ? E8 ? ? ? ? 8B F9 33 DB 38 5F 48 0F 85 ? ? ? ? A1 ? ? ? ? 38 98 89 04 00 00");

		if (g_stringCtor == nullptr || g_stringDtor == nullptr || g_stringContent == nullptr || !chatLine) {
			log("[ChatLog] Failed to find the chat window function; Add Time to Chat won't work");
			return;
		}

		m_chatLineHook = std::make_unique<FunctionHook>(*chatLine, (uintptr_t)&hookedChatLine);

		if (m_chatLineHook->isValid()) {
			g_originalChatLine = (ChatLineFn)m_chatLineHook->getOriginal();
			log("[ChatLog] Hooked the chat window at %p", (void*)*chatLine);
		}
		else {
			log("[ChatLog] Failed to hook the chat window; Add Time to Chat won't work");
			m_chatLineHook.reset();
		}
	}

	ChatLog::~ChatLog() {
		undoPatch(m_linePatch);
		m_chatLineHook.reset();
		m_logLineHook.reset();
		m_historyLineHook.reset();
		g_chatLog = nullptr;
	}

	// The chat log window's add-line (CChatLogView) and the chat's history add-line (CMainChatView).
	void ChatLog::hookLogWindow() {
		auto logLine = scan("Pleione.dll", "55 8B EC 56 57 8B 7D 18 57 8B F1 E8 ? ? ? ? 59 84 C0 74 ? FF 75 1C 8B 8C BE 48 01 00 00");
		auto historyLine = scan("Pleione.dll", "6A 10 B8 ? ? ? ? E8 ? ? ? ? 8B F9 8D 4D E4 E8 ? ? ? ? FF 75 08 8B 35 ? ? ? ? "
			"83 65 FC 00 8D 4D E4 FF D6 FF 75 0C 8D 4D E8 FF D6 FF 75 10 8D 4D EC FF D6 8B 45 14 89 45 F0 8B 45 18 8D B4 87 98 01 00 00");

		if (!logLine || !historyLine || g_stringCtor == nullptr || g_stringDtor == nullptr) {
			log("[ChatLog] Couldn't find the chat log window's lines; it won't show the time");
			return;
		}

		m_logLineHook = std::make_unique<FunctionHook>(*logLine, (uintptr_t)&hookedLogLine);
		m_historyLineHook = std::make_unique<FunctionHook>(*historyLine, (uintptr_t)&hookedHistoryLine);

		if (!m_logLineHook->isValid() || !m_historyLineHook->isValid()) {
			log("[ChatLog] Couldn't hook the chat log window's lines; it won't show the time");
			m_logLineHook.reset();
			m_historyLineHook.reset();
			return;
		}

		g_originalLogLine = (LogLineFn)m_logLineHook->getOriginal();
		g_originalHistoryLine = (HistoryLineFn)m_historyLineHook->getOriginal();
		log("[ChatLog] Hooked the chat log window's lines at %p and %p", (void*)*logLine, (void*)*historyLine);
	}

	std::wstring ChatLog::timePrefix() {
		return m_isTime ? L"[" + widen(getTime()) + L"] " : std::wstring{};
	}

	void ChatLog::onUI() {
		if (ImGui::TreeNode(getName().c_str())) {
			ImGui::BeginDisabled(!m_isEnabled && !m_isTime);
			ImGui::TextWrapped("Uses 24-hour clock instead of 12-hour clock for all related chat mods below. \n");
			ImGui::Checkbox("Use 24 hour clock", &m_is24hour);
			ImGui::EndDisabled();

			ImGui::Dummy(ImVec2{ 5.0f, 5.0f });

			if (ImGui::TreeNode("Add Time to Chat"))
			{
				ImGui::TextWrapped("Puts the time in front of each line in Mabinogi's chat window. Only the chat window "
					"changes: names keep their colors, and speech bubbles show the message as it is.\n");
				ImGui::Checkbox("Add Time to Chat", &m_isTime);
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("External Chat Log"))
			{
				ImGui::TextWrapped("Logs most chat messages to a text file when enabled. \n\n"
					"Logged chat messages are sent to txt files in the \"Kanan Chat Log\" folder in your MabiPro folder. \n\n"
					"Logged chat messages can also be viewed using Show Chat Log, which can be used as an alternative chat window.");
				ImGui::Dummy(ImVec2{ 5.0f, 5.0f });
				if (ImGui::Checkbox("Enable Chat Log", &m_isChatLog))
					startLogging();

				ImGui::BeginDisabled(!m_isChatLog);
				ImGui::Checkbox("Show Chat Log", &m_isOpen);
				ImGui::EndDisabled();
				ImGui::TreePop();
			}

			if (ImGui::TreeNode("Scrolling Messages to Chat"))
			{
				ImGui::TextWrapped("This mod moves scrolling messages from the top of the screen to the middle of the screen and chat as <System> messages.");
				ImGui::Dummy(ImVec2{ 10.0f, 10.0f });
				ImGui::Checkbox("Auction Messages To Chat", &m_isAuctionEnabled);
				ImGui::Checkbox("Field Boss Messages To Chat", &m_isFieldBossEnabled);

				ImGui::BeginDisabled(!m_isFieldBossEnabled);
				ImGui::Checkbox("Field Boss Notification", &m_isFieldBNotifyEnabled);
				ImGui::EndDisabled();
				ImGui::TreePop();
			}
			ImGui::TreePop();

			// Add Time to Chat doesn't need the packets (see hookedChatLine).
			m_isEnabled = m_isChatLog || m_isAuctionEnabled || m_isFieldBossEnabled;
		}
	}

	bool ChatLog::onWindow() {
		if (m_isOpen && m_startedLogging && m_isChatLog) {
			drawChatLog();
		}

		return m_isOpen && m_startedLogging && m_isChatLog;
	}

	void ChatLog::onConfigLoad(const Config& cfg) {
		m_isChatLog = cfg.get<bool>("ModChatLog.Enabled").value_or(false);
		m_isOpen = cfg.get<bool>("ChatLog.OpenByDefault").value_or(false);
		m_isTime = cfg.get<bool>("ChatTime.Enabled").value_or(false);

		// Until it's set, the clock the PC uses (Windows' time format has "H" for a 24-hour clock).
		wchar_t format[80]{};
		auto pcUses24Hour = GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STIMEFORMAT, format, 80) > 0 &&
			wcschr(format, L'H') != nullptr;
		m_is24hour = cfg.get<bool>("ChatTime.24Hour").value_or(pcUses24Hour);
		m_isAuctionEnabled = cfg.get<bool>("AuctionMessageToChat.Enabled").value_or(false);
		m_isFieldBossEnabled = cfg.get<bool>("FieldBossMessageToChat.Enabled").value_or(false);
		m_isFieldBNotifyEnabled = cfg.get<bool>("FieldBossNotify.Enabled").value_or(false);
		
		m_isEnabled = m_isChatLog || m_isAuctionEnabled || m_isFieldBossEnabled;

		if (m_isChatLog)
			startLogging();
	}

	void ChatLog::onConfigSave(Config& cfg) {
		cfg.set<bool>("ModChatLog.Enabled", m_isChatLog);
		cfg.set<bool>("ChatLog.OpenByDefault", m_isOpen);
		cfg.set<bool>("ChatTime.Enabled", m_isTime);
		cfg.set<bool>("ChatTime.24Hour", m_is24hour);
		cfg.set<bool>("AuctionMessageToChat.Enabled", m_isAuctionEnabled);
		cfg.set<bool>("FieldBossMessageToChat.Enabled", m_isFieldBossEnabled);
		cfg.set<bool>("FieldBossNotify.Enabled", m_isFieldBNotifyEnabled);
	}

	std::string ChatLog::getTime() {
		std::ostringstream ss;
		time_t now = time(0);
		tm localTimeNow;
		localtime_s(&localTimeNow, &now);
		std::string hour;
		std::string ampm;

		if (m_is24hour)
		{
			hour = std::to_string(localTimeNow.tm_hour);
		}
		else
		{
			ampm = (localTimeNow.tm_hour >= 12) ? "PM" : "AM";
			int hour12 = localTimeNow.tm_hour % 12;
			if (hour12 == 0) hour12 = 12; // Convert 0 (midnight) or 12 (noon) to 12
			hour = std::to_string(hour12);
		}

		ss << hour << (localTimeNow.tm_min < 10 ? ":0" : ":") << localTimeNow.tm_min;

		if (!m_is24hour)
			ss << " " << ampm;

		return ss.str();
	}

	void notify() {
		if (!FlashWindowEx) {
			HINSTANCE hLib = GetModuleHandleA("user32");
			if (hLib != NULL)
				(DWORD&)FlashWindowEx = (DWORD)GetProcAddress(hLib, "FlashWindowEx");
		}
		if (FlashWindowEx) {
			FLASHWINFO fInfo;
			fInfo.cbSize = sizeof(fInfo);
			fInfo.dwFlags = FLASHW_TRAY | FLASHW_TIMERNOFG;
			fInfo.hwnd = g_kanan->getWindow();
			fInfo.uCount = 0;
			fInfo.dwTimeout = 2500;
			FlashWindowEx(&fInfo);
		}
	}

	void ChatLog::onRecv(MabiMessage mabiMessage) {
		std::string message = "";
		CMabiPacket recvPacket;
		recvPacket.SetSource(mabiMessage.buffer, mabiMessage.size);
		int op = recvPacket.GetOP();

		if (op == 21100)
		{
			message.append(recvPacket.GetElement(2)->str);
		}
		else if (op == 36502 || op == 36504)
		{
			message.append("party info");
		}
		else
		{
			message.append(recvPacket.GetElement(1)->str);
		}

		if (message.empty() || (message.length() == 1 && message.data()[0] == ' '))
		{
			return;
		}
		else if (op == 21101)
		{
			// Handle m_isAuctionEnabled, m_isFieldBossEnabled, and m_isFieldBNotifyEnabled
			if (recvPacket.GetElement(0)->byte8 == 1 || recvPacket.GetElement(0)->byte8 == 8)
			{
				if (m_isAuctionEnabled && message.find("Channel 1") != string::npos) {
					PacketData data;
					data.type = 1;
					data.byte8 = 7;
					recvPacket.SetElement(&data, 0);
					data.type = T_INT;
					data.int32 = 0;
					recvPacket.SetElement(&data, 2);

					BYTE* p;
					int tmpSizw = recvPacket.BuildPacket(&p);

					// Only into the game's buffer if it fits.
					if (tmpSizw <= mabiMessage.size) {
						memcpy(mabiMessage.buffer, p, tmpSizw);
					}

					delete[] p;
				}
				else if ((m_isFieldBossEnabled && message.find("has appeared") != string::npos) ||
					(m_isFieldBossEnabled && message.find("has defeated") != string::npos)) {
					PacketData data;
					data.type = T_BYTE;
					data.byte8 = 7;
					recvPacket.SetElement(&data, 0);

					BYTE* p;
					int tmpSizw = recvPacket.BuildPacket(&p);

					if (tmpSizw <= mabiMessage.size) {
						memcpy(mabiMessage.buffer, p, tmpSizw);
					}

					delete[] p;

					if (message.find("has appeared") != string::npos && m_isFieldBNotifyEnabled)
						notify();
				}
			}

			if (recvPacket.GetElement(0)->byte8 != 7)
			{
				return;
			}
		}

		if (m_isChatLog)
		{
			ostringstream ss{};
			switch (op)
			{
			case 21100: // All + Personal Shop
				if (!string(recvPacket.GetElement(1)->str).find("<COMBAT>"))
					break;
				if (recvPacket.GetReciverId() > 4700000000000000 || (recvPacket.GetReciverId() > 0x10010000000000 && recvPacket.GetReciverId() < 0x10020000000000))
					return;
				if (strcmp(recvPacket.GetElement(1)->str, "<PERSONALSHOP>") == 0) {
					ss << getTime() << " | <PERSONALSHOP> " << ": " << message;
				}
				else if (strcmp(recvPacket.GetElement(1)->str, "<PARTY>") == 0) {
					ss << getTime() << " | <PARTY> " << ": " << message;
				}
				else {
					bool isEmote = false;
					for each(auto emote in m_emotes) {
						if (message.find(emote) != string::npos)
						{
							isEmote = true;
							break;
						}
					}

					if (isEmote) break;

					ss << getTime() << " | " << recvPacket.GetElement(1)->str << ": " << message;
				}
				break;
			case 21101: // System
				if (!string(recvPacket.GetElement(1)->str).find("<COMBAT>"))
					break;
				if (strcmp(recvPacket.GetElement(1)->str, "Your skill latency reduction value has been detected to be too high. Please lower it..") == 0)
					return;
				ss << getTime() << " | <SYSTEM> " << ": " << recvPacket.GetElement(1)->str;
				break;
			case 21107: // Whisper
				ss << getTime() << " | <WHISPER> " << recvPacket.GetElement(0)->str << ": " << recvPacket.GetElement(1)->str;
				break;
			case 21109: // Beginner
				ss << getTime() << " | <GLOBAL> " << recvPacket.GetElement(0)->str << ": " << recvPacket.GetElement(1)->str;
				break;
			case 36502: // Party info
					m_partyMembers[recvPacket.GetElement(2)->ID] = recvPacket.GetElement(3)->str;
					break;
			case 36504: // Party info
				for (int i = 14; i < recvPacket.GetElementNum();) {
					if (recvPacket.GetElement(i)->type == T_LONG) {
						m_partyMembers[recvPacket.GetElement(i)->ID] = recvPacket.GetElement(i + 1)->str;
						i += 11;
					}
					else {
						i += 8;
					}
				}
				break;
			case 36520: // Party
				if (m_partyMembers.find(recvPacket.GetElement(0)->ID) == m_partyMembers.end()) {
					ss << getTime() << " | <PARTY> " << ": " << message;
				}
				else {
					ss << getTime() << " | <PARTY> " << m_partyMembers[recvPacket.GetElement(0)->ID] << ": " << message;
				}
				break;
			case 50031: // Guild
				ss << getTime() << " | <GUILD> " << recvPacket.GetElement(0)->str << ": " << message;
				break;
			default:
				break;
			}
			if (ss.str().size() > 0)
			{
				std::string log = ss.str();
				std::replace(log.begin(), log.end(), '%', 'p');
				addChatLog(log.c_str());
			}
		}
	}

void ChatLog::deleteOldLogs(string path, string fileName, tm tstruct) {
		int year = 0;
		int month = 0;
		int day = 0;
		int pos = 0;
		string str;
		path.append(fileName);

		// Get date from text file
		while (year == 0 || month == 0 || day == 0) {
			str = fileName.substr(pos, fileName.length() - 1);

			size_t i = 0;
			for (; i < str.length(); i++) {
				if (!isdigit(str[i]) && isdigit(str[i - 1]))
					break;
			}
			if (i >= str.length())
				return;

			pos += i + 1;
			str = str.substr(0, i);

			if (year == 0) {
				year = atoi(str.c_str());
			}
			else if (month == 0) {
				month = atoi(str.c_str());
			}
			else if (day == 0) {
				day = atoi(str.c_str());
			}
		}

		// Delete file if it's older than a week
		if (month != tstruct.tm_mon + 1) {
			switch (month) {
			case 1:
			case 3:
			case 5:
			case 7:
			case 8:
			case 10:
			case 12:
				if ((tstruct.tm_mday - 7) + 31 > day)
					std::remove(path.c_str());
				break;
			case 2:
				if ((tstruct.tm_mday - 7) + 28 > day)
					std::remove(path.c_str());
				break;
			case 4:
			case 6:
			case 9:
			case 11:
				if ((tstruct.tm_mday - 7) + 30 > day)
					std::remove(path.c_str());
				break;
			default:
				break;
			}
		}
		else if ((tstruct.tm_mday - 7) > day) {
			std::remove(path.c_str());
		}
	}

	void ChatLog::startLogging() {
		if (m_startedLogging)
			return;

		if (CreateDirectory(L"Kanan Chat Logs", NULL) ||
			ERROR_ALREADY_EXISTS == GetLastError()) {
			time_t     now = time(0);
			struct tm  tstruct;
			char       buf[80];
			localtime_s(&tstruct, &now);
			strftime(buf, sizeof(buf), "%Y-%m-%d", &tstruct);


			DIR *dir;
			struct dirent *ent;
			string chatLogFolder = g_kanan->getPath();
			chatLogFolder.append("\\Kanan Chat Logs\\");

			if ((dir = opendir(chatLogFolder.c_str())) != NULL) {
				while ((ent = readdir(dir)) != NULL) {
					string str(ent->d_name);
					deleteOldLogs(chatLogFolder, str, tstruct);
				}
				closedir(dir);
			}
			else {
				return;
			}

			string chatLogPath = chatLogFolder + buf + ".txt";

			m_file.open(chatLogPath, std::ios::out | std::ios::app);
			if (m_file.fail())
				return;
			m_file.exceptions(m_file.exceptions() | std::ios::failbit | std::ifstream::badbit);

			m_startedLogging = true;

			log("Started logging chat in %s", chatLogPath);
		}
	}

	void ChatLog::addChatLog(const string& msg) {
		m_logs.push_back(msg);

		m_scrollToBottom = true;

		m_file << msg << std::endl;
	}

	void ChatLog::drawChatLog() {
		ImGui::SetNextWindowSize(ImVec2{ 500.0f, 400.0f }, ImGuiCond_FirstUseEver);

		if (!ImGui::Begin("Chat Log", &m_isOpen, ImGuiWindowFlags_NoFocusOnAppearing)) {
			ImGui::End();
			return;
		}

		ImGui::Checkbox("Auto Scroll  ", &m_autoScroll);

		ImGui::SameLine();
		m_filter.Draw("Filter", -100.0f);
		ImGui::Separator();
		ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

		if (m_filter.IsActive()) {
			for (auto log : m_logs)
			{
				if (m_filter.PassFilter(log.c_str())) {
					ImGui::TextWrapped(log.c_str());
				}
			}
		}
		else {
			for (auto log : m_logs)
			{
				ImGui::TextWrapped(log.c_str());
			}
		}

		if (m_scrollToBottom && m_autoScroll) {
			ImGui::SetScrollHereY(1.0f);
		}

		m_scrollToBottom = false;

		ImGui::EndChild();
		ImGui::End();
	}
}