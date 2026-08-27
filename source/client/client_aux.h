#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).

	Minimal client-side detour glue. Mirrors the macros in
	source/core/cm_aux.h but doesn't pull in any server-side headers
	(cm_constants.h, gServer, etc.), so the client DLL stays small.
*  =================================================================================== */

#include <Windows.h>
#include <detours/detours.h>

#define __LIFX_DECL_EXTERNAL(retType, fnConvention, fnName, ...)  \
	typedef retType (fnConvention*fnName##_Fn)(__VA_ARGS__);      \
	extern fnName##_Fn fnName

#define __LIFX_INSTANTIATE(fnName) fnName##_Fn fnName

#define __LIFX_ATTACH_HOOK(fnOffset, fnOriginal, fnHook)                                             \
	void* fnOriginal##Ptr = reinterpret_cast<LPVOID>((char*)GetModuleHandle(NULL) + (fnOffset));     \
	fnOriginal = (fnOriginal##_Fn)fnOriginal##Ptr;                                                   \
	DetourAttach(&(PVOID&)fnOriginal, fnHook)

#define __LIFX_DETACH_HOOK(fnOriginal, fnHook) \
	DetourDetach(&(PVOID&)fnOriginal, fnHook)
