/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).

	Loaded by the same pdh.dll proxy used on the server (yo_cm_client.exe
	imports the same four PDH symbols). Boot-logs into <client>/logs/
	lifx_client.log, then installs the engine console detours so all
	further work runs through the hook seam — never DllMain.
*  =================================================================================== */

#include "client_runtime.h"

#include <Windows.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>

namespace
{
	void WriteBootLine()
	{
		// Resolve the directory containing this DLL (i.e. the client
		// install dir). Drop a log file there so the user knows where
		// to look without having to grep deep into wineprefix paths.
		HMODULE hSelf = nullptr;
		GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			(LPCWSTR)&WriteBootLine, &hSelf);

		wchar_t selfPath[MAX_PATH] = {0};
		DWORD n = GetModuleFileNameW(hSelf, selfPath, MAX_PATH);
		if (n == 0 || n >= MAX_PATH) return;

		// Trim filename off; replace with "logs/lifx_client.log".
		wchar_t* slash = wcsrchr(selfPath, L'\\');
		if (!slash) return;
		*slash = L'\0';

		try {
			std::filesystem::path base{selfPath};
			std::filesystem::path logDir = base / L"logs";
			std::error_code ec;
			std::filesystem::create_directories(logDir, ec);
			std::filesystem::path logFile = logDir / L"lifx_client.log";

			FILE* fp = nullptr;
			_wfopen_s(&fp, logFile.c_str(), L"a");
			if (!fp) return;

			std::time_t now = std::time(nullptr);
			std::tm tm{};
			localtime_s(&tm, &now);
			char ts[64];
			std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm);

			std::fprintf(fp,
				"[%s] [lifx-client] DLL_PROCESS_ATTACH; pid=%lu\n",
				ts, GetCurrentProcessId());
			std::fclose(fp);
		} catch (...) {
			// Filesystem error during boot logging is non-fatal.
		}
	}
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hModule);
		WriteBootLine();
		// Detours-only work from DllMain — no engine console calls, and no
		// file I/O, here. The engine console isn't init'd yet, and any file
		// I/O under the Wine loader lock can abort the attach (observed in
		// #116). Engine-touching work happens inside
		// HookConsoleInit::OnConsoleInit; hooks attach cleanly here.
		LifxClient::AttachHooks();
	}
	else if (ul_reason_for_call == DLL_PROCESS_DETACH)
	{
		LifxClient::DetachHooks();
	}
	return TRUE;
}
