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

#include <cstdint>
#include <string>
#include <memory>
#include <unordered_map>
#include <sstream>
#include <mutex>
#include <fstream>
#include <format>
#include <filesystem>

#include <Windows.h>
#include <TlHelp32.h>
#include <Psapi.h>

/* Torque type definitions and other common stuff */

typedef int8_t S8;
typedef int_fast8_t FS8;
typedef int16_t S16;
typedef int_fast16_t FS16;
typedef int32_t S32;
typedef int_fast32_t FS32;
typedef uint8_t U8;
typedef uint_fast8_t FU8;
typedef uint16_t U16;
typedef uint_fast16_t FU16;
typedef uint32_t U32;
typedef uint_fast32_t FU32;
typedef float F32;
typedef double F64;

typedef char Utf8;
typedef unsigned short Utf16;
typedef unsigned int Utf32;
typedef const char* StringTableEntry;

extern const F32 kFloatInf;
static const F32 kFloatOne = F32(1.0);                                 ///< Constant float 1.0
static const F32 kFloatHalf = F32(0.5);                                ///< Constant float 0.5
static const F32 kFloatZero = F32(0.0);                                ///< Constant float 0.0
static const F32 kFloatPi = F32(3.14159265358979323846);               ///< Constant float PI
static const F32 kFloat2Pi = F32(2.0 * 3.14159265358979323846);        ///< Constant float 2*PI
static const F32 kFloatInversePi = F32(1.0 / 3.14159265358979323846);  ///< Constant float 1 / PI
static const F32 kFloatHalfPi = F32(0.5 * 3.14159265358979323846);     ///< Constant float 1/2 * PI
static const F32 kFloat2InversePi = F32(2.0 / 3.14159265358979323846); ///< Constant float 2 / PI
static const F32 kFloatInverse2Pi = F32(0.5 / 3.14159265358979323846); ///< Constant float 0.5 / PI

static const F32 kFloatSqrt2 = F32(1.41421356237309504880f);           ///< Constant float sqrt(2)
static const F32 kFloatSqrtHalf = F32(0.7071067811865475244008443f);   ///< Constant float sqrt(0.5)

static const S8  kS8Min = S8(-128);                                    ///< Constant Min Limit S8
static const S8  kS8Max = S8(127);                                     ///< Constant Max Limit S8
static const U8  kU8Max = U8(255);                                     ///< Constant Max Limit U8

static const S16 kS16Min = S16(-32768);                                ///< Constant Min Limit S16
static const S16 kS16Max = S16(32767);                                 ///< Constant Max Limit S16
static const U16 kU16Max = U16(65535);                                 ///< Constant Max Limit U16

static const S32 kS32Min = S32(-2147483647 - 1);                       ///< Constant Min Limit S32
static const S32 kS32Max = S32(2147483647);                            ///< Constant Max Limit S32
static const U32 kU32Max = U32(0xffffffff);                            ///< Constant Max Limit U32

static const F32 kF32Min = F32(1.175494351e-38F);                      ///< Constant Min Limit F32
static const F32 kF32Max = F32(3.402823466e+38F);                      ///< Constant Max Limit F32

typedef signed _int64		S64;
typedef std::int_fast64_t	FS64;
typedef unsigned _int64		U64;
typedef std::uint_fast64_t	FU64;

#undef LIFX_UNUSED
#ifdef _DEBUG
#define LIFX_UNUSED(var) ((0,0) ? (void)(var) : (void)0)
#else
#pragma warning(disable: 4189) // local variable is initialized but not referenced
#define LIFX_UNUSED(var) ((void)0)
#endif

#ifndef FN_CDECL
#define FN_CDECL __cdecl            ///< Calling convention
#endif

#define __LIFX_EXPORT_API __declspec(dllexport)
#define __LIFX_IMPORT_API __declspec(dllimport)

#define BIT(x) (1 << (x))                       ///< Returns value with bit x set (2^x)
typedef unsigned long long  dsize_t;

#define LIFX_ASSERT(st) assert(st)
#define LIFX_ASSERT_MSG(st,msg) assert(st && msg)

#include "cm_config.h"
