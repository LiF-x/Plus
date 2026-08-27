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

/* Implementation of Torque3D Console API behaviour */

namespace Con
{
	enum class ConsoleType
	{
		String = 1149, // it's char*
		TorqueString = 1151, // it's pointer to torque's String class
		Integer = 1154,
		Boolean = 1158,
	};

	/* log output */

	// Default Torque console output (light gray color)
	void Echo(const char* fmt, ...);

	// Print warning string to console (yellow color)
	void Warning(const char* fmt, ...);

	// Print error string to console (red color)
	void Error(const char* fmt, ...);

	// Print verbose string to console (info() or hack()) (green color)
	void Info(const char* fmt, ...);

	/* variable registration */

	void AddVariable(const char* name, ConsoleType type, void* data, const char* usage = NULL);
	void AddConstant(const char* name, ConsoleType type, const void* data, const char* usage = NULL);

	void AddCommand(const char* ns, const char* name, IntCallback cb, const char* usage, S32 minArgs, S32 maxArgs);
	void AddCommand(const char* ns, const char* name, FloatCallback cb, const char* usage, S32 minArgs, S32 maxArgs);
	void AddCommand(const char* ns, const char* name, StringCallback cb, const char* usage, S32 minArgs, S32 maxArgs);
	void AddCommand(const char* ns, const char* name, VoidCallback cb, const char* usage, S32 minArgs, S32 maxArgs);
	void AddCommand(const char* ns, const char* name, BoolCallback cb, const char* usage, S32 minArgs, S32 maxArgs);

	void SetVariable(const char* name, const char* value);
	const char* GetVariable(const char* name);

	/* scripting instructions */

	// todo: handle return pointer (it's return ConsoleValueRef pointer)
	const char* Evaluate(const char* string, bool echo = false, const char* fileName = NULL);
}
