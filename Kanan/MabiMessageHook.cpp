
#include "MabiMessageHook.hpp"
#include <sstream>
#include <ios>
#include <Strsafe.h>
#include <tchar.h>
#include <fcntl.h>
#include <io.h>
#include "Scan.hpp"
#include "Log.hpp"
#include <Patch.hpp>
#include "MessageView.hpp"


namespace kanan {
	typedef DWORD(__thiscall* MabiRecvListenerSignature)(LPVOID Buffer, LONG Size);
	struct MabiRecvListener {
		uint32_t					op;
		MabiRecvListenerSignature	start;
	};

	const int PACKET_BUFFER_SIZE = 64 * 1024;
	BYTE packetBuffer[PACKET_BUFFER_SIZE];
	bool g_mabiMessageHook = false;
	std::vector<MabiRecvListener> mabiRecvListeners;

	VOID ListenDownstream(LPVOID Buffer, LONG Size);



	MabiMessageHook::MabiMessageHook()
	{
		if (g_mabiMessageHook == false) {
			if (DoInjection()) {
				g_mabiMessageHook = true;
				log("MabiMessage hooked successfully.");
			}
			else {
				g_mabiMessageHook = false;
				log("MabiMessage failed to hook.");
			}
		}
	}

	BOOL MabiMessageHook::DoInjection() {
		bool result = true;

		log("Initializing MabiMessageHook...");

		log("Patching ReadFromNetworkBuffer...");
		result &= PatchReadFromNetworkBuffer();
		log(result ? "...success" : "...failed");

		log("Finding WriteToNetworkBuffer...");
		result &= FindWriteToNetworkBuffer();
		log(result ? "...success" : "...failed");

		return result;
	}

	BOOL MabiMessageHook::addRecvListener(void* funcPtr, uint32_t op) {
		if (funcPtr == nullptr || op < 1)
			return false;

		MabiRecvListener tmpListener;
		tmpListener.op = op;
		tmpListener.start = (MabiRecvListenerSignature)funcPtr;

		mabiRecvListeners.push_back(tmpListener);

		return true;
	}

	//
	// Hook Types & Vars
	//

	typedef DWORD(__thiscall* ReadFromNetworkBufferSignature)(LPVOID MsgPointer, LPVOID Buffer, LONG Size);
	typedef DWORD(__thiscall* WriteToNetworkBufferSignature)(LPVOID MsgPointer, LPVOID Buffer, LONG size);

	ReadFromNetworkBufferSignature ReadFromNetworkBuffer = NULL;
	WriteToNetworkBufferSignature WriteToNetworkBuffer = NULL;

	LPVOID SavedRecvPointer = NULL;
	LONG ReadFromNetworkBufferHookContinueAddress;
	BOOL InsideHookHandler = FALSE;

	//
	// Hook Handlers
	//

	void __stdcall ReadFromNetworkBufferHookHandler(LPVOID MsgPointer, LPVOID Buffer, LONG Size) {
		if (!InsideHookHandler) {
			InsideHookHandler = TRUE;
			ListenDownstream(Buffer, Size);
			InsideHookHandler = FALSE;
			SavedRecvPointer = MsgPointer;
		}
	}

	//
	// Hook Traps
	//

	__declspec(naked) void ReadFromNetworkBufferHookTrap() {
		__asm {
			PUSHAD
			mov     ebp, esp
			mov     eax, [ebp + 32 + 8]
			push    eax
			mov     eax, [ebp + 32 + 4]
			push    eax
			push    ecx
			call    ReadFromNetworkBufferHookHandler
			POPAD

			push    ebp
			mov     ebp, esp
			sub     esp, 28
			push	ebx
			mov		ebx, ecx
			jmp     ReadFromNetworkBufferHookContinueAddress
		}
	}

	VOID ListenDownstream(LPVOID Buffer, LONG Size) {
		std::vector<uint8_t> v((BYTE*)Buffer, (BYTE*)Buffer + Size);

		MessageView msg{ v };

		for (int i = 0; i < mabiRecvListeners.size(); i++) {
			if (msg.op() == mabiRecvListeners[i].op) {
				mabiRecvListeners[i].start(Buffer, Size);
			}
		}


		std::ostringstream ss{};


		// TODO: Turn this into a mod
		if (msg.op() == 21101) {
			while (msg.peek() != MessageElementType::NONE) {
				if (msg.peek() == MessageElementType::STRING) {
					if (strcmp((*msg.get<std::string>()).c_str(), "Your skill latency reduction value has been detected to be too high. Please lower it..") == 0) {
						// Remove >skill spam
						memset(Buffer, 0, Size);
						return;
					}
				}
				else {
					msg.skip();
				}
			}
		}
		else {
#ifdef DEBUG
			ss << "OP: " << msg.op() << "\n";
			ss << "ID: " << msg.id() << "\n";
			while (msg.peek() != MessageElementType::NONE) {
				switch (msg.peek()) {
				case MessageElementType::BYTE: ss << "BYTE: " << (int)*msg.get<uint8_t>() << "\n"; break;
				case MessageElementType::SHORT: ss << "SHORT: " << *msg.get<uint16_t>() << "\n"; break;
				case MessageElementType::INT: ss << "INT: " << *msg.get<int32_t>() << "\n"; break;
				case MessageElementType::LONG: ss << "LONG: " << *msg.get<uint64_t>() << "\n"; break;
				case MessageElementType::FLOAT: ss << "FLOAT: " << *msg.get<float>() << "\n"; break;
				case MessageElementType::STRING: ss << "STRING: " << *msg.get<std::string>() << "\n"; break;
				case MessageElementType::BIN: ss << "BINARY BLOB: ...\n"; msg.skip(); break;
				}
			}
			log("%s", ss.str().c_str());
#endif
		}


	}

	//
	// Patching
	//

	BOOL MabiMessageHook::PatchReadFromNetworkBuffer() {
		WCHAR Str[2048];

		std::optional<uintptr_t> ReadFromNetworkBufferFunctionAddressPtr = kanan::scan("Mint.dll", "55 8B EC 83 EC 1C 53 8B D9");
		DWORD ReadFromNetworkBufferFunctionAddress = *ReadFromNetworkBufferFunctionAddressPtr;
		LONG ReadFromNetworkBufferFunctionAddressLong = *(LONG*)(void*)(&ReadFromNetworkBufferFunctionAddress);

		ReadFromNetworkBuffer = (ReadFromNetworkBufferSignature)ReadFromNetworkBufferFunctionAddressLong;
		ReadFromNetworkBufferHookContinueAddress = ReadFromNetworkBufferFunctionAddressLong + 9;

		Hookcall((void*)(ReadFromNetworkBufferFunctionAddressLong), ReadFromNetworkBufferHookTrap, 7);

		Patch m_patch;
		m_patch.address = ReadFromNetworkBufferFunctionAddress;
		m_patch.bytes = { 0xE9 };
		patch(m_patch);
		return TRUE;
	}

	BOOL MabiMessageHook::FindWriteToNetworkBuffer() {
		char Str[2048];

		std::optional<uintptr_t> WriteToNetworkBufferFunctionAddressPtr = kanan::scan("Mint.dll", "55 8B EC 83 EC 18 53 56 8B F1 8B 46 08 57");
		DWORD WriteToNetworkBufferFunctionAddress = *WriteToNetworkBufferFunctionAddressPtr;
		LONG WriteToNetworkBufferFunctionAddressLong = *(LONG*)(void*)(&WriteToNetworkBufferFunctionAddress);
		if (!WriteToNetworkBufferFunctionAddressLong) return FALSE;

		log("WriteToNetworkBuffer prologue address 0x%08X\r", WriteToNetworkBufferFunctionAddress);

		WriteToNetworkBuffer = (WriteToNetworkBufferSignature)WriteToNetworkBufferFunctionAddress;
		return TRUE;
	}

}