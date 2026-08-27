
/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.

	LIFX IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
	EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
	MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
	IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
	DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
	ARISING FROM, OUT OF OR IN CONNECTION WITH LIFX OR THE USE OR OTHER
	DEALINGS IN LIFX.
*  =================================================================================== */

#include "server/cm_server.h"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved)
{
	if (DetourIsHelperProcess())
	{
		return TRUE;
	}

	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(15));

		// Init detours
		DetourRestoreAfterWith();
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());

		// Init LiFx and attach main hooks
		gServer.Init();

		// Commit detours
		DetourTransactionCommit();
	}
	else if (ul_reason_for_call == DLL_PROCESS_DETACH)
	{
		// Finish detours
		DetourTransactionBegin();
		DetourUpdateThread(GetCurrentThread());

		// Try to stop LiFx and detach all hooks
		gServer.Stop();

		// Commit detours
		DetourTransactionCommit();
	}

	return TRUE;
}

extern "C" void __declspec(dllexport) __cdecl cm_loader() {}
