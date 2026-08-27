#pragma once

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

#include "server/cm_constants.h"

#include <detours/detours.h>

// ---------------------------------------------------------------------------- //
namespace Lifx
{
	extern void ShowInfoMessage(const char* fmt, ...);
	extern void ShowErrorMessage(const char* fmt, ...);
}

// ---------------------------------------------------------------------------- //

#define __CM_DECL_EXTERNAL(retType, fnConvention, fnName, ...)  \
	typedef retType (fnConvention*fnName##_Fn)(__VA_ARGS__); \
	extern fnName##_Fn fnName

#define __CM_DECL_INTERNAL(retType, fnConvertion, fnName, ...) \
	typedef retType (fnConvertion*fnName##_Fn)(__VA_ARGS__); \
	fnName##_Fn fnName

#define __CM_FIND(fnOffset, fnTarget) \
	void*fnTarget##Ptr = reinterpret_cast<LPVOID>((char*)GetModuleHandle(NULL) + fnOffset); \
	fnTarget = (fnTarget##_Fn)fnTarget##Ptr

#define __CM_INSTATNTIATE(fnName) fnName##_Fn fnName

// ---------------------------------------------------------------------------- //

// IMPORTANT: do NOT call __CM_ATTACH_HOOK twice for the same fnOffset within
// one DetourTransactionBegin/Commit block. Detours can't safely stack two
// attaches on the same target inside one transaction — the second trampoline
// ends up pointing at the first hook, infinite recursion on the next call,
// stack overflow inside the engine's event thread, and on Wine the loader
// section deadlocks behind it so the only escape is `kill -9 wineserver`.
// We hit this in issue #62 / PR #63: SECTORHANDOFF_AUTOPOST stacked on
// SERVERUUIDEVENT_SEND alongside the dumper. The fix is to install the hook
// ONCE and branch its body on whatever sub-flag should apply (see
// hook_netclassrep_dumper.cpp's ServerUUIDEventSend for the pattern).
#define __CM_ATTACH_HOOK(fnOffset, fnOriginal, fnHook) \
	void*fnOriginal##Ptr = reinterpret_cast<LPVOID>((char*)GetModuleHandle(NULL) + fnOffset); \
	fnOriginal = (fnOriginal##_Fn)fnOriginal##Ptr; \
	DetourAttach(&(PVOID&)fnOriginal, fnHook)

#define __CM_DETACH_HOOK(fnOriginal, fnHook) \
	DetourDetach(&(PVOID&)fnOriginal, fnHook)

