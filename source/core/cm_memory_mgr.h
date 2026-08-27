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

#include "cm_platform.h"

namespace Lifx
{
	class MemoryManager
	{
	private:

		struct BytePattern
		{
			std::vector<U8> bytes;
			std::string mask;

			size_t Size()
			{
				return bytes.size() == mask.size() ? bytes.size() : -1;
			}
		};

		static bool CheckSignature(U8* source, U8* pattern, const std::string& mask);

	public:

		static uintptr_t GetModuleBaseAddress(const char* name);

		// ---------------------------------------------------------------------------- //
		// generic memory read
		template <typename T>
		static size_t Read(uintptr_t src, T& dst, size_t size = sizeof(T))
		{
			HANDLE process_ = GetCurrentProcess();
			size_t bytes_read_ = 0;

			if (!ReadProcessMemory(process_, reinterpret_cast<LPCVOID>(src), &dst, size, &bytes_read_))
			{
				//gLog.ShowErrorMessage("MemoryRead error: %d", GetLastError());
				return 0;
			}

			return bytes_read_;
		}

		// ---------------------------------------------------------------------------- //
		// generic memory write
		template <typename T>
		static size_t Write(uintptr_t addr, T& dst, size_t size = sizeof(T))
		{
			HANDLE process_ = GetCurrentProcess();
			size_t bytes_writed_ = 0;
			DWORD protect_ = 0;

			VirtualProtectEx(process_, reinterpret_cast<LPVOID>(addr), size, PAGE_READWRITE, &protect_);
			if (!WriteProcessMemory(process_, reinterpret_cast<LPVOID>(addr), &dst, size, &bytes_writed_))
			{
				//gLog.ShowErrorMessage("MemoryWrite error: %d", GetLastError());
				return 0;
			}
			VirtualProtectEx(process_, reinterpret_cast<LPVOID>(addr), size, protect_, &protect_);

			return bytes_writed_;
		}

		static uintptr_t ScanSignature(const std::string& sig, U32 offset = 0, const std::string& mod = "");
	};
}
