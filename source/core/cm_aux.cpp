
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

#include "cm_aux.h"

// ---------------------------------------------------------------------------- //
void Lifx::ShowInfoMessage(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	char buffer[256];
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	MessageBoxA(0, buffer, "CM_Server", 0);

	va_end(args);
}

// ---------------------------------------------------------------------------- //
void Lifx::ShowErrorMessage(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	char buffer[256];
	vsnprintf(buffer, sizeof(buffer), fmt, args);
	MessageBoxA(0, buffer, "CM_Server", MB_OK | MB_ICONERROR);

	va_end(args);
}