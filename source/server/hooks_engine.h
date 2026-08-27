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

#include "hooks/engine/hook_console.h"
#include "hooks/engine/hook_filestream.h"

// ///////////////////////////////////////////////
// CONSOLE

__CM_DECL_EXTERNAL(LPVOID, __fastcall, _Engine_Con_LookupNamespace, const char* ns);
__CM_DECL_EXTERNAL(const char*, __fastcall, _Engine_StringTableInsert, LPVOID stringTablePtr, const char* val, const bool caseSens);
__CM_DECL_EXTERNAL(void, __fastcall, _Engine_Con_AddVariable, LPVOID dictionaryPtr, const char* name, int type, LPVOID data, const char* usage);
__CM_DECL_EXTERNAL(void, __fastcall, _Engine_Con_AddConstant, const char* name, int type, LPCVOID data, const char* usage);
__CM_DECL_EXTERNAL(void, __fastcall, _Engine_AddCommand_Int, LPVOID ns, const char* name, IntCallback cb, const char* usage, S32 minArgs, S32 maxArgs, bool, LPVOID header);
__CM_DECL_EXTERNAL(void, __fastcall, _Engine_AddCommand_Float, LPVOID ns, const char* name, FloatCallback cb, const char* usage, S32 minArgs, S32 maxArgs, bool, LPVOID header);
__CM_DECL_EXTERNAL(void, __fastcall, _Engine_AddCommand_String, LPVOID ns, const char* name, StringCallback cb, const char* usage, S32 minArgs, S32 maxArgs, bool, LPVOID header);
__CM_DECL_EXTERNAL(void, __fastcall, _Engine_AddCommand_Void, LPVOID ns, const char* name, VoidCallback cb, const char* usage, S32 minArgs, S32 maxArgs, bool, LPVOID header);
__CM_DECL_EXTERNAL(void, __fastcall, _Engine_AddCommand_Bool, LPVOID ns, const char* name, BoolCallback cb, const char* usage, S32 minArgs, S32 maxArgs, bool, LPVOID header);
__CM_DECL_EXTERNAL(const char*, __fastcall, _Engine_Con_Evaluate, const char* string, bool echo, const char* fileName);
__CM_DECL_EXTERNAL(void, __fastcall, _Engine_Con_SetVariable, const char* name, const char* value);
__CM_DECL_EXTERNAL(const char*, __fastcall, _Engine_Con_GetVariable, const char* name);

// ////////////////////////////////////////////////////////////////////////////////////////

namespace Hooks
{
	namespace Engine
	{
		// global

		const char* StringTableInsert(void* _this, const char* val, const bool caseSens);

		// console

		bool InitConsoleAddCommandHooks();
		void ConsoleInit();
	}

	void AttachHooks();
	void DetachHooks();
}
