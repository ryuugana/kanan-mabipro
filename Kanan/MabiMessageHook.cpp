
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
	typedef DWORD(__thiscall* MabiRecvListenerSignature)(MabiMessage mabiMessage);
	const int PACKET_BUFFER_SIZE = 64 * 1024;
	BYTE packetBuffer[PACKET_BUFFER_SIZE];
	bool g_mabiMessageHook = false;
	std::vector<std::unique_ptr<MessageMod>>* mabiRecvListeners = nullptr;

	VOID ListenDownstream(LPVOID Buffer, LONG Size);



	MabiMessageHook::MabiMessageHook(std::vector<std::unique_ptr<MessageMod>>* mabiRecvMods)
	{
		if (g_mabiMessageHook == false) {
			if (DoInjection()) {
				g_mabiMessageHook = true;
				mabiRecvListeners = mabiRecvMods;
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
		log(result ? "...success" : "...failed");

		return result;
	}

	//
	// Hook Types & Vars
	//

	typedef DWORD(__thiscall* ReadFromNetworkBufferSignature)(LPVOID Buffer, LONG Size);
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
			SavedRecvPointer = MsgPointer;
			ListenDownstream(Buffer, Size);
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
			mov     eax, [ebp + 32 + 8]
			push    eax
			mov     eax, [ebp + 32 + 4]
			push    eax
			push	ecx
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
		MabiMessage mabiMessage;
		mabiMessage.buffer = (unsigned char*)Buffer;
		mabiMessage.size = Size;


		unsigned long op = GetOP(mabiMessage.buffer);
		for (uint32_t i = 0; i < mabiRecvListeners->size(); i++) {
			if ((*mabiRecvListeners)[i]->m_isEnabled) {
				if (op == (*mabiRecvListeners)[i]->getOp() || -1 == (*mabiRecvListeners)[i]->getOp()) {
					logNoNewLine("");
					op = (*mabiRecvListeners)[i]->onRecv(mabiMessage);
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
		ReadFromNetworkBufferHookContinueAddress = ReadFromNetworkBufferFunctionAddressLong + 9;

		Hookcall((void*)(ReadFromNetworkBufferFunctionAddressLong), ReadFromNetworkBufferHookTrap, 7);

		Patch m_patch;
		m_patch.address = ReadFromNetworkBufferFunctionAddress;
		m_patch.bytes = { 0xE9 };
		patch(m_patch);
		return TRUE;
	}

}