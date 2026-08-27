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

#include "core/cm_memory_mgr.h"

// Torque3D console returns
typedef const char* (*StringCallback)(LPVOID obj, S32 argc, const char* argv[]);
typedef S32(*IntCallback)(LPVOID obj, S32 argc, const char* argv[]);
typedef F32(*FloatCallback)(LPVOID obj, S32 argc, const char* argv[]);
typedef void(*VoidCallback)(LPVOID obj, S32 argc, const char* argv[]);
typedef bool(*BoolCallback)(LPVOID obj, S32 argc, const char* argv[]);