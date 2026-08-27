
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

#include "cm_memory_mgr.h"
#include <comdef.h>

using namespace Lifx;

// ---------------------------------------------------------------------------- //
bool MemoryManager::CheckSignature(U8* source, U8* pattern, const std::string& mask)
{
	for (int i = 0; i < mask.size(); i++)
	{
		if (mask.at(i) == L'?' || (*(source + i) == *(pattern + i)))
		{
			continue;
		}
		else
		{
			return false;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------- //
uintptr_t MemoryManager::GetModuleBaseAddress(const char* name)
{
	HANDLE module_ = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
	MODULEENTRY32 module_entry_;

	module_entry_.dwSize = sizeof(module_entry_);

	do
	{
		if (!strcmp(_bstr_t(module_entry_.szModule), name))
		{
			CloseHandle(module_);
			return (uint64_t)(module_entry_.hModule);
		}
	} while (Module32Next(module_, &module_entry_));

	return 0;
}

// ---------------------------------------------------------------------------- //
uintptr_t MemoryManager::ScanSignature(const std::string& sig, U32 offset, const std::string& mod)
{
	// 0. Here we got base info about process and core module
	HANDLE process_ = GetCurrentProcess();
	MODULEINFO info;

	if (mod.empty())
	{
		GetModuleInformation(process_, GetModuleHandle(NULL), &info, sizeof(MODULEINFO));
	}
	else
	{
		GetModuleInformation(process_, GetModuleHandle(mod.c_str()), &info, sizeof(MODULEINFO));
	}

	// 1. Start address for scan - base address of root process
	uintptr_t start = reinterpret_cast<uintptr_t>(info.lpBaseOfDll);

	// 2. Scan all (?)
	size_t end = info.SizeOfImage;

	// 3. Format byte pattern
	BytePattern p;
	std::stringstream ss(sig);
	std::vector<std::string> tokens{ std::istream_iterator<std::string, char>(ss), {} };
	for (std::vector<std::string>::iterator it = tokens.begin(); it != tokens.end(); ++it)
	{
		std::string temp = *it;
		if (temp.size() == 1 || temp._Equal("??") || temp._Equal("?"))
		{
			p.mask.append("?");
			p.bytes.push_back(0);
		}
		else
		{
			p.mask.append("x");
			BYTE singleByte = static_cast<BYTE>( strtoul(temp.c_str(), NULL, 16) ); // 
			p.bytes.push_back(singleByte);
		}
	}

	PBYTE pBytes = new BYTE[p.bytes.size()];
	memcpy_s(pBytes, p.bytes.size(), p.bytes.data(), p.bytes.size());

	// 5. Try to find address from signature
	uintptr_t result = 0;
	MEMORY_BASIC_INFORMATION mbi = { 0 };
	size_t offset_ = 0;
	size_t bytesRead = 0;

	while (offset_ < (end - p.mask.size()) && result == 0)
	{
		size_t count = VirtualQueryEx(process_, reinterpret_cast<LPVOID>(start + offset_), &mbi, sizeof(mbi));
		if (!count) break;
		if (mbi.State != MEM_FREE)
		{
			U8* buffer = new U8[mbi.RegionSize];
			void* baseAddress = mbi.BaseAddress;
			ReadProcessMemory(process_, baseAddress, buffer, mbi.RegionSize, &bytesRead);
			if (bytesRead == 0) break;
			for (int i = 0; i < (mbi.RegionSize - p.mask.size()); i++)
			{
				if (CheckSignature(buffer + i, pBytes, p.mask))
				{
					result = start + offset_ + i;
					break;
				}
			}
			delete[] buffer;
		}
		offset_ += mbi.RegionSize;
	}

	return (result + offset);
}
