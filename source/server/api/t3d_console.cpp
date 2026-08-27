
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

#include "server/cm_wrappers.h"

__CM_INSTATNTIATE(_Engine_Con_AddVariable);
__CM_INSTATNTIATE(_Engine_Con_AddConstant);
__CM_INSTATNTIATE(_Engine_AddCommand_Int);
__CM_INSTATNTIATE(_Engine_AddCommand_Float);
__CM_INSTATNTIATE(_Engine_AddCommand_String);
__CM_INSTATNTIATE(_Engine_AddCommand_Void);
__CM_INSTATNTIATE(_Engine_AddCommand_Bool);
__CM_INSTATNTIATE(_Engine_Con_Evaluate);
__CM_INSTATNTIATE(_Engine_Con_SetVariable);
__CM_INSTATNTIATE(_Engine_Con_GetVariable);

// ---------------------------------------------------------------------------- //
void Con::Echo(const char* fmt, ...)
{
	va_list arguments;
	va_start(arguments, fmt);
	char buffer[8192];
	vsnprintf(buffer, sizeof(buffer), fmt, arguments);
	_Engine_Con_InternalConsolePrintf(0, 0, buffer);
	va_end(arguments);
}

// ---------------------------------------------------------------------------- //
void Con::Warning(const char* fmt, ...)
{
	va_list arguments;
	va_start(arguments, fmt);
	char buffer[8192];
	vsnprintf(buffer, sizeof(buffer), fmt, arguments);
	_Engine_Con_InternalConsolePrintf(1, 0, buffer);
	va_end(arguments);
}

// ---------------------------------------------------------------------------- //
void Con::Error(const char* fmt, ...)
{
	va_list arguments;
	va_start(arguments, fmt);
	char buffer[8192];
	vsnprintf(buffer, sizeof(buffer), fmt, arguments);
	_Engine_Con_InternalConsolePrintf(2, 0, buffer);
	va_end(arguments);
}

// ---------------------------------------------------------------------------- //
void Con::Info(const char* fmt, ...)
{
	va_list arguments;
	va_start(arguments, fmt);
	char buffer[8192];
	vsnprintf(buffer, sizeof(buffer), fmt, arguments);
	_Engine_Con_InternalConsolePrintf(3, 0, buffer);
	va_end(arguments);
}

// ---------------------------------------------------------------------------- //
void Con::AddVariable(const char* name, ConsoleType type, void* data, const char* usage)
{
	if (gSpace.Pointers().Get("GlobalVars") == nullptr)
	{
		Error("Cant add variable [%s] because global dictionary pointer is null!", name);
		return;
	}

	_Engine_Con_AddVariable(gSpace.Pointers().Get("GlobalVars"), name, (S32)type, data, usage);
}

// ---------------------------------------------------------------------------- //
void Con::AddConstant(const char* name, ConsoleType type, const void* data, const char* usage)
{
	_Engine_Con_AddConstant(name, (S32)type, data, usage);
}

// ---------------------------------------------------------------------------- //
void Con::AddCommand(const char* ns, const char* name, IntCallback cb, const char* usage, S32 minArgs, S32 maxArgs)
{
	LPVOID namespace_ = _Engine_Con_LookupNamespace(ns);
	const char* string_table_ = _Engine_StringTableInsert(gSpace.Pointers().Get("StringTable"), name, false);

	_Engine_AddCommand_Int(namespace_, string_table_, cb, usage, minArgs, maxArgs, false, NULL);
}

// ---------------------------------------------------------------------------- //
void Con::AddCommand(const char* ns, const char* name, FloatCallback cb, const char* usage, S32 minArgs, S32 maxArgs)
{
	LPVOID namespace_ = _Engine_Con_LookupNamespace(ns);
	const char* string_table_ = _Engine_StringTableInsert(gSpace.Pointers().Get("StringTable"), name, false);

	_Engine_AddCommand_Float(namespace_, string_table_, cb, usage, minArgs, maxArgs, false, NULL);
}

// ---------------------------------------------------------------------------- //
void Con::AddCommand(const char* ns, const char* name, StringCallback cb, const char* usage, S32 minArgs, S32 maxArgs)
{
	LPVOID namespace_ = _Engine_Con_LookupNamespace(ns);
	const char* string_table_ = _Engine_StringTableInsert(gSpace.Pointers().Get("StringTable"), name, false);

	_Engine_AddCommand_String(namespace_, string_table_, cb, usage, minArgs, maxArgs, false, NULL);
}

// ---------------------------------------------------------------------------- //
void Con::AddCommand(const char* ns, const char* name, VoidCallback cb, const char* usage, S32 minArgs, S32 maxArgs)
{
	LPVOID namespace_ = _Engine_Con_LookupNamespace(ns);
	const char* string_table_ = _Engine_StringTableInsert(gSpace.Pointers().Get("StringTable"), name, false);

	_Engine_AddCommand_Void(namespace_, string_table_, cb, usage, minArgs, maxArgs, false, NULL);
}

// ---------------------------------------------------------------------------- //
void Con::AddCommand(const char* ns, const char* name, BoolCallback cb, const char* usage, S32 minArgs, S32 maxArgs)
{
	LPVOID namespace_ = _Engine_Con_LookupNamespace(ns);
	const char* string_table_ = _Engine_StringTableInsert(gSpace.Pointers().Get("StringTable"), name, false);

	_Engine_AddCommand_Bool(namespace_, string_table_, cb, usage, minArgs, maxArgs, false, NULL);
}

// ---------------------------------------------------------------------------- //
const char* Con::Evaluate(const char* string, bool echo, const char* fileName)
{
	return _Engine_Con_Evaluate(string, echo, fileName);
}

// ---------------------------------------------------------------------------- //
void Con::SetVariable(const char* name, const char* value)
{
	_Engine_Con_SetVariable(name, value);
}

// ---------------------------------------------------------------------------- //
const char* Con::GetVariable(const char* name)
{
	return _Engine_Con_GetVariable(name);
}
