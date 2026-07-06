
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
#include "MabiPacket.h"
#include <thread>


namespace kanan {
	const int PACKET_BUFFER_SIZE = 64 * 1024;
	BYTE packetBuffer[PACKET_BUFFER_SIZE];
	bool g_mabiMessageHook = false;
	bool g_firstLogin = true;
	std::vector<std::unique_ptr<MessageMod>>* mabiListeners = nullptr;

	VOID ListenDownstream(LPVOID Buffer, LONG Size);
	VOID ListenUpstream(LPVOID Buffer, LONG Size);



	MabiMessageHook::MabiMessageHook(std::vector<std::unique_ptr<MessageMod>>* mabiMods)
	{
		if (g_mabiMessageHook == false) {
			if (DoInjection()) {
				g_mabiMessageHook = true;
				mabiListeners = mabiMods;
				log("MabiMessage hooked successfully.");
			}
			else {
				g_mabiMessageHook = false;
				log("MabiMessage failed to hook.");
			}
		}
	}

	BOOL MabiMessageHook::DoInjection() {
		BOOL result = true;

		log("Initializing MabiMessageHook...");

		log("Patching ReadFromNetworkBuffer...");
		result &= PatchReadFromNetworkBuffer();
		log("Patching WriteToNetworkBuffer...");
		result &= PatchWriteToNetworkBuffer();
		log(result ? "...success" : "...failed");

		return result;
	}

	//
	// Hook Types & Vars
	//

	typedef DWORD(__thiscall* ReadFromNetworkBufferSignature)(LPVOID Buffer, LONG Size, LONG size);
	typedef DWORD(__thiscall* WriteToNetworkBufferSignature)(LPVOID MsgPointer, LPVOID Buffer, LONG size);

	ReadFromNetworkBufferSignature ReadFromNetworkBuffer = NULL;
	WriteToNetworkBufferSignature WriteToNetworkBuffer = NULL;

	LPVOID SavedRecvPointer = NULL;
	LPVOID SavedSendPointer = NULL;
	LONG ReadFromNetworkBufferHookContinueAddress;
	LONG WriteToNetworkBufferHookContinueAddress;
	BOOL InsideHookHandler = FALSE;

	//
	// Hook Handlers
	//

	void __stdcall ReadFromNetworkBufferHookHandler(LPVOID MsgPointer, LPVOID Buffer, LONG Size) {
		if (!InsideHookHandler) {
			InsideHookHandler = TRUE;
			SavedRecvPointer = MsgPointer;
			ListenDownstream(Buffer, Size);
			InsideHookHandler = FALSE;
		}
	}

	void __stdcall WriteToNetworkBufferHookHandler(LPVOID Buffer, LONG Size) {
		if (!InsideHookHandler) {
			InsideHookHandler = TRUE;
			ListenUpstream(Buffer, Size);
			InsideHookHandler = FALSE;
		}
	}

	//
	// Hook Traps
	//

	__declspec(naked) void ReadFromNetworkBufferHookTrap() {
		__asm {
			PUSHAD
			mov     ebp, esp

			// Size
			mov     eax, [ebp + 32 + 8]
			push    eax

			// Buffer
			mov     eax, [ebp + 32 + 4]
			push    eax

			// MsgPointer
			push	ecx
			call    ReadFromNetworkBufferHookHandler
			POPAD

			push    ebp
			mov     ebp, esp
			sub     esp, 28
			jmp     ReadFromNetworkBufferHookContinueAddress
		}
	}

	__declspec(naked) void WriteToNetworkBufferHookTrap() {
		__asm {
			PUSHAD
			// Size
			mov     eax, [ebp + 0xC]
			push    eax

			// Buffer
			mov     eax, [ebp + 8]
			push    eax

			call    WriteToNetworkBufferHookHandler
			POPAD

			pop     edi
			pop     esi
			pop     ebx
			leave
			ret     0x8
		}
	}

	VOID ListenDownstream(LPVOID Buffer, LONG Size) {
		MabiMessage mabiMessage;
		mabiMessage.buffer = (unsigned char*)Buffer;
		mabiMessage.size = Size;


		unsigned long op = GetOP(mabiMessage.buffer);
		for (uint32_t i = 0; i < mabiListeners->size(); i++) {
			if ((*mabiListeners)[i]->m_isEnabled && (*mabiListeners)[i]->getHasRecv()) {
				for each(int listenOp in(*mabiListeners)[i]->getOp())
					if (op == listenOp || -1 == listenOp) {
						(*mabiListeners)[i]->onRecv(mabiMessage);
						break;
					}
			}
		}
	}

	VOID ListenUpstream(LPVOID Buffer, LONG Size) {
		MabiMessage mabiMessage;
		mabiMessage.buffer = (unsigned char*)Buffer;
		mabiMessage.size = Size;

		unsigned long op = GetOP(mabiMessage.buffer);
		for (uint32_t i = 0; i < mabiListeners->size(); i++) {
			if ((*mabiListeners)[i]->m_isEnabled && (*mabiListeners)[i]->getHasSend()) {
				for each(int listenOp in(*mabiListeners)[i]->getOp())
					if (op == listenOp || -1 == listenOp) {
						(*mabiListeners)[i]->onSend(mabiMessage);
						break;
					}
			}
		}
	}

	//
	// Patching
	//

	BOOL MabiMessageHook::PatchReadFromNetworkBuffer() {
		std::optional<uintptr_t> ReadFromNetworkBufferFunctionAddressPtr = kanan::scan("Mint.dll", "55 8B EC 83 EC 1C 53 8B D9");
		DWORD ReadFromNetworkBufferFunctionAddress = *ReadFromNetworkBufferFunctionAddressPtr;
		LONG ReadFromNetworkBufferFunctionAddressLong = *(LONG*)(void*)(&ReadFromNetworkBufferFunctionAddress);

		ReadFromNetworkBuffer = (ReadFromNetworkBufferSignature)ReadFromNetworkBufferFunctionAddressLong;
		ReadFromNetworkBufferHookContinueAddress = ReadFromNetworkBufferFunctionAddressLong + 6;

		return Hookjmp((void*)(ReadFromNetworkBufferFunctionAddressLong), ReadFromNetworkBufferHookTrap, 6);
	}

	BOOL MabiMessageHook::PatchWriteToNetworkBuffer() {

		DWORD WriteToNetworkBufferFunctionAddress = (reinterpret_cast<uintptr_t>(GetModuleHandleA("Mint.dll")) + 0x60f46);
		LONG WriteToNetworkBufferFunctionAddressLong = *(LONG*)(void*)(&WriteToNetworkBufferFunctionAddress);

		WriteToNetworkBuffer = (WriteToNetworkBufferSignature)WriteToNetworkBufferFunctionAddressLong;

		// Returning at end of function
		// No need for a continue address

		return Hookjmp((void*)(WriteToNetworkBufferFunctionAddressLong), WriteToNetworkBufferHookTrap,7);
	}

}