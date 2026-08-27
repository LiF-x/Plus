/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	pdh.dll proxy / LiFx bootstrap loader.

	The LiF server (ddctd_cm_yo_server.exe) statically imports four symbols from
	pdh.dll. pdh.dll is not a Windows KnownDLL, so a local copy in the exe's
	directory is loaded by the OS in preference to the one in System32. By
	dropping this DLL next to the server exe we get a guaranteed early entry
	point: DllMain runs after the exe image is mapped but before its entry point
	executes -- ideal for installing the Detours-based LiFx hooks.

	The four imported symbols are forwarded to the real pdh.dll resolved from
	%WINDIR%\System32 at load time.
*  =================================================================================== */

#include <Windows.h>
#include <Pdh.h>

#pragma comment(linker, "/EXPORT:PdhOpenQueryW")
#pragma comment(linker, "/EXPORT:PdhCollectQueryData")
#pragma comment(linker, "/EXPORT:PdhAddCounterW")
#pragma comment(linker, "/EXPORT:PdhGetFormattedCounterValue")

typedef PDH_STATUS (WINAPI *PFN_PdhOpenQueryW)(LPCWSTR, DWORD_PTR, PDH_HQUERY*);
typedef PDH_STATUS (WINAPI *PFN_PdhCollectQueryData)(PDH_HQUERY);
typedef PDH_STATUS (WINAPI *PFN_PdhAddCounterW)(PDH_HQUERY, LPCWSTR, DWORD_PTR, PDH_HCOUNTER*);
typedef PDH_STATUS (WINAPI *PFN_PdhGetFormattedCounterValue)(PDH_HCOUNTER, DWORD, LPDWORD, PPDH_FMT_COUNTERVALUE);

static HMODULE                            g_real_pdh                        = nullptr;
static PFN_PdhOpenQueryW                  p_PdhOpenQueryW                   = nullptr;
static PFN_PdhCollectQueryData            p_PdhCollectQueryData             = nullptr;
static PFN_PdhAddCounterW                 p_PdhAddCounterW                  = nullptr;
static PFN_PdhGetFormattedCounterValue    p_PdhGetFormattedCounterValue     = nullptr;

static bool LoadRealPdh()
{
	wchar_t path[MAX_PATH];
	UINT n = GetSystemDirectoryW(path, MAX_PATH);
	if (n == 0 || n >= MAX_PATH - 10) return false;

	wcscat_s(path, MAX_PATH, L"\\pdh.dll");
	g_real_pdh = LoadLibraryW(path);
	if (!g_real_pdh) return false;

	p_PdhOpenQueryW               = (PFN_PdhOpenQueryW)              GetProcAddress(g_real_pdh, "PdhOpenQueryW");
	p_PdhCollectQueryData         = (PFN_PdhCollectQueryData)        GetProcAddress(g_real_pdh, "PdhCollectQueryData");
	p_PdhAddCounterW              = (PFN_PdhAddCounterW)             GetProcAddress(g_real_pdh, "PdhAddCounterW");
	p_PdhGetFormattedCounterValue = (PFN_PdhGetFormattedCounterValue)GetProcAddress(g_real_pdh, "PdhGetFormattedCounterValue");

	return p_PdhOpenQueryW
		&& p_PdhCollectQueryData
		&& p_PdhAddCounterW
		&& p_PdhGetFormattedCounterValue;
}

static void BootstrapLiFx(HMODULE hSelf)
{
	wchar_t path[MAX_PATH];
	DWORD n = GetModuleFileNameW(hSelf, path, MAX_PATH);
	if (n == 0 || n >= MAX_PATH) return;

	wchar_t* slash = wcsrchr(path, L'\\');
	if (!slash) return;

	size_t remaining = MAX_PATH - (size_t)(slash + 1 - path);
	// Opaque random filename — the proxy is the only thing that knows it,
	// so we can rotate it freely without rebuilding anything else. Anyone
	// scanning the server's directory for "the mod DLL" gets nothing
	// recognizable to match on. To change it: edit this literal, the `/Fe`
	// in build_linux.sh, and <TargetName> in win/LiFx.vcxproj — those three
	// must stay in sync.
	if (wcscpy_s(slash + 1, remaining, L"4ba5cb5e.dll") != 0) return;

	LoadLibraryW(path);
}

extern "C"
PDH_STATUS WINAPI PdhOpenQueryW(LPCWSTR szDataSource, DWORD_PTR dwUserData, PDH_HQUERY* phQuery)
{
	return p_PdhOpenQueryW(szDataSource, dwUserData, phQuery);
}

extern "C"
PDH_STATUS WINAPI PdhCollectQueryData(PDH_HQUERY hQuery)
{
	return p_PdhCollectQueryData(hQuery);
}

extern "C"
PDH_STATUS WINAPI PdhAddCounterW(PDH_HQUERY hQuery, LPCWSTR szFullCounterPath, DWORD_PTR dwUserData, PDH_HCOUNTER* phCounter)
{
	return p_PdhAddCounterW(hQuery, szFullCounterPath, dwUserData, phCounter);
}

extern "C"
PDH_STATUS WINAPI PdhGetFormattedCounterValue(PDH_HCOUNTER hCounter, DWORD dwFormat, LPDWORD lpdwType, PPDH_FMT_COUNTERVALUE pValue)
{
	return p_PdhGetFormattedCounterValue(hCounter, dwFormat, lpdwType, pValue);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID /*lpReserved*/)
{
	if (ul_reason_for_call == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hModule);
		if (!LoadRealPdh())
			return FALSE;

		BootstrapLiFx(hModule);
	}
	return TRUE;
}
