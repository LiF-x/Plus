/* ===================================================================================
    Copyright (c) 2026, LiFx Contributors. All Rights Reserved.
    Prepared with ChatGPT (Codex), in collaboration with Zbigi.

    This file is a part of LiFx.

    LIFX IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
    DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
    ARISING FROM, OUT OF OR IN CONNECTION WITH LIFX OR THE USE OR OTHER
    DEALINGS IN LIFX.
*  =================================================================================== */

#include "hook_player_capacity.h"
#include "server/cm_server.h"
#include "core/tinyxml2.h"
#include <array>
#include <charconv>
#include <cstring>
#include <limits>
#include <string_view>

namespace Hooks::Engine
{
    namespace
    {
        constexpr unsigned kStockSlots = 64, kMaxSlots = 128;
        constexpr DWORD kImageSize = 0xC8F000, kTimeStamp = 0x6465EF7D;
        constexpr std::array<unsigned char,14> kReset = {
            0x48,0x8D,0x05,0x4D,0xFA,0xA3,0x00,0x48,0x89,0x05,0x5E,0xF6,0xA3,0x00
        };
        constexpr std::array<unsigned char,65> kAppend = {
            0x48,0x8D,0x0D,0x1D,0xE8,0xA3,0x00,0x48,0x8B,0x05,0x2E,0xE2,0xA3,0x00,
            0x48,0x3B,0xC1,0x73,0x0D,0x48,0x89,0x28,0x48,0x83,0x05,0x1E,0xE2,0xA3,
            0x00,0x08,0xEB,0x21,0x48,0x8B,0x0D,0xFD,0xE5,0xA3,0x00,0x48,0x85,0xC9,
            0x74,0x0E,0x48,0x8B,0x01,0xBA,0x01,0x00,0x00,0x00,0xFF,0x90,0x30,0x01,
            0x00,0x00,0x48,0x89,0x2D,0xE3,0xE5,0xA3,0x00
        };
        constexpr std::array<unsigned char,30> kValidator = {
            0x8B,0x83,0x9C,0x00,0x00,0x00,0x39,0x05,0x2F,0x61,0x95,0x00,0x0F,0x8F,
            0x37,0x01,0x00,0x00,0x3B,0x05,0x27,0x61,0x95,0x00,0x0F,0x8F,0x2B,0x01,
            0x00,0x00
        };
        struct State
        {
            unsigned slots = 0;
            bool attached = false;
            std::uintptr_t base = 0;
            void* buffer = nullptr;
        } gCapacity;

        // Injection seams used by the regression harness for allocation,
        // page-protection and instruction-cache failure cases.
        struct MemoryApi
        {
            decltype(&VirtualAlloc) allocate = &VirtualAlloc;
            decltype(&VirtualProtect) protect = &VirtualProtect;
            decltype(&FlushInstructionCache) flush = &FlushInstructionCache;
        } gMemory;

        template<class T> T Read(std::uintptr_t address)
        {
            T value;
            std::memcpy(&value,reinterpret_cast<void*>(address),sizeof(value));
            return value;
        }
        template<class T> void Write(std::uintptr_t address, T value)
        {
            std::memcpy(reinterpret_cast<void*>(address),&value,sizeof(value));
        }

        bool Relative32(std::uintptr_t next, std::uintptr_t target, std::int32_t& value)
        {
            const auto distance = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(next);
            if (distance < (std::numeric_limits<std::int32_t>::min)()
                || distance > (std::numeric_limits<std::int32_t>::max)()) return false;
            value = static_cast<std::int32_t>(distance);
            return true;
        }

        bool ReferenceImage(std::uintptr_t base)
        {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0
                || dos->e_lfanew > 0x1000) return false;
            const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base+dos->e_lfanew);
            return pe->Signature == IMAGE_NT_SIGNATURE
                && pe->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64
                && pe->FileHeader.TimeDateStamp == kTimeStamp
                && pe->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC
                && pe->OptionalHeader.SizeOfImage == kImageSize;
        }

        void* AllocateNear(std::uintptr_t base, std::size_t bytes)
        {
            SYSTEM_INFO info{};
            GetSystemInfo(&info);
            const auto step = static_cast<std::uintptr_t>(info.dwAllocationGranularity);
            const auto center = (base + CmOffset::PLAYER_CAPACITY_TICK_RESET) & ~(step-1);
            const auto low = reinterpret_cast<std::uintptr_t>(info.lpMinimumApplicationAddress);
            const auto high = reinterpret_cast<std::uintptr_t>(info.lpMaximumApplicationAddress);
            // Leave ample room for all four instructions and the end pointer
            // inside the signed RIP-relative range, under Windows or Wine.
            for (std::uintptr_t delta = 0; delta < 0x70000000; delta += step)
            {
                const std::uintptr_t candidates[] = {center+delta, center >= delta ? center-delta : 0};
                for (const auto candidate : candidates)
                {
                    if (candidate < low || candidate > high-bytes) continue;
                    MEMORY_BASIC_INFORMATION region{};
                    if (!VirtualQuery(reinterpret_cast<void*>(candidate),&region,sizeof(region))
                        || region.State != MEM_FREE || region.RegionSize < bytes) continue;
                    if (void* allocation = gMemory.allocate(reinterpret_cast<void*>(candidate),bytes,
                                                           MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE))
                        return allocation;
                }
            }
            return nullptr;
        }

        struct Page { void* address; DWORD oldProtection = 0; bool open = false; };
        bool RestorePages(std::array<Page,4>& pages, std::size_t pageSize)
        {
            bool result = true;
            for (auto& page : pages)
            {
                DWORD ignored;
                if (page.open)
                {
                    if (!gMemory.protect(page.address,pageSize,page.oldProtection,&ignored)) result = false;
                    else page.open = false;
                }
            }
            return result;
        }

        bool InstallAt(std::uintptr_t base)
        {
            if (!ReferenceImage(base)
                || Read<unsigned>(base+CmOffset::PLAYER_CAPACITY_MIN) != 1
                || Read<unsigned>(base+CmOffset::PLAYER_CAPACITY_MAX) != kStockSlots
                || Read<std::uintptr_t>(base+CmOffset::PLAYER_CAPACITY_CURSOR) != 0
                || std::memcmp(reinterpret_cast<void*>(base+CmOffset::PLAYER_CAPACITY_VALIDATOR),kValidator.data(),kValidator.size())
                || std::memcmp(reinterpret_cast<void*>(base+CmOffset::PLAYER_CAPACITY_TICK_RESET),kReset.data(),kReset.size())
                || std::memcmp(reinterpret_cast<void*>(base+CmOffset::PLAYER_CAPACITY_APPEND),kAppend.data(),kAppend.size()))
            {
                Con::Error("[playerCapacity] unsupported/modified image or simulation already started; no patch installed");
                return false;
            }
            void* buffer = AllocateNear(base,gCapacity.slots*sizeof(void*));
            if (!buffer) { Con::Error("[playerCapacity] nearby movement buffer allocation failed"); return false; }
            const auto start = reinterpret_cast<std::uintptr_t>(buffer);
            auto reset = kReset;
            auto append = kAppend;
            const auto patch = [&](unsigned char* code, std::uintptr_t rva, std::uintptr_t target)
            {
                std::int32_t relative;
                if (!Relative32(base+rva+7,target,relative)) return false;
                std::memcpy(code+3,&relative,sizeof(relative));
                return true;
            };
            if (!patch(reset.data(),CmOffset::PLAYER_CAPACITY_TICK_RESET,start)
                || !patch(append.data(),CmOffset::PLAYER_CAPACITY_APPEND,start+gCapacity.slots*sizeof(void*))
                || !patch(append.data()+32,CmOffset::PLAYER_CAPACITY_OVERFLOW_READ,start)
                || !patch(append.data()+58,CmOffset::PLAYER_CAPACITY_OVERFLOW_WRITE,start))
            {
                VirtualFree(buffer,0,MEM_RELEASE);
                Con::Error("[playerCapacity] movement references exceed signed rel32 range");
                return false;
            }

            SYSTEM_INFO info{};
            GetSystemInfo(&info);
            const auto pageSize = static_cast<std::size_t>(info.dwPageSize);
            const auto pageAt = [&](std::uintptr_t rva) {
                return reinterpret_cast<void*>((base+rva) & ~(static_cast<std::uintptr_t>(pageSize)-1));
            };
            std::array<Page,4> pages = {{{pageAt(CmOffset::PLAYER_CAPACITY_TICK_RESET)},
                {pageAt(CmOffset::PLAYER_CAPACITY_APPEND)}, {pageAt(CmOffset::PLAYER_CAPACITY_MAX)},
                {pageAt(CmOffset::PLAYER_CAPACITY_CURSOR)}}};
            for (std::size_t i=0; i<pages.size(); ++i)
            {
                auto& page = pages[i];
                if (!gMemory.protect(page.address,pageSize,i<2 ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE,&page.oldProtection))
                {
                    const bool restored = RestorePages(pages,pageSize);
                    VirtualFree(buffer,0,MEM_RELEASE);
                    Con::Error("[playerCapacity] page preparation failed (protection restore=%d); no references changed",restored);
                    return false;
                }
                page.open = true;
            }

            // Console startup only: no world/tick threads may execute these
            // blocks yet. Acquire every writable page BEFORE the first write.
            std::memcpy(reinterpret_cast<void*>(base+CmOffset::PLAYER_CAPACITY_TICK_RESET),reset.data(),reset.size());
            std::memcpy(reinterpret_cast<void*>(base+CmOffset::PLAYER_CAPACITY_APPEND),append.data(),append.size());
            Write(base+CmOffset::PLAYER_CAPACITY_CURSOR,start);
            Write(base+CmOffset::PLAYER_CAPACITY_MAX,gCapacity.slots);
            const bool flushed = gMemory.flush(GetCurrentProcess(),reinterpret_cast<void*>(base+CmOffset::PLAYER_CAPACITY_TICK_RESET),reset.size())
                && gMemory.flush(GetCurrentProcess(),reinterpret_cast<void*>(base+CmOffset::PLAYER_CAPACITY_APPEND),append.size());
            if (!flushed)
            {
                std::memcpy(reinterpret_cast<void*>(base+CmOffset::PLAYER_CAPACITY_TICK_RESET),kReset.data(),kReset.size());
                std::memcpy(reinterpret_cast<void*>(base+CmOffset::PLAYER_CAPACITY_APPEND),kAppend.data(),kAppend.size());
                Write(base+CmOffset::PLAYER_CAPACITY_MAX,kStockSlots);
                Write(base+CmOffset::PLAYER_CAPACITY_CURSOR,std::uintptr_t{0});
                FlushInstructionCache(GetCurrentProcess(),nullptr,0);
                RestorePages(pages,pageSize);
                VirtualFree(buffer,0,MEM_RELEASE);
                Con::Error("[playerCapacity] instruction-cache flush failed; startup must stop");
                return false;
            }
            // Native code holds these addresses independently of the DLL.
            // Never release this allocation during DLL unload or live play.
            gCapacity.base = base;
            gCapacity.buffer = buffer;
            gCapacity.attached = true;
            if (!RestorePages(pages,pageSize))
            {
                Con::Error("[playerCapacity] page protection restore failed; startup must stop");
                return false;
            }
            Con::Echo("[playerCapacity] installed: validation ceiling=%u, movement buffer=%u; set world maxPlayers explicitly",
                      gCapacity.slots,gCapacity.slots);
            return true;
        }
    }

    bool ConfigurePlayerCapacity(const tinyxml2::XMLElement* root)
    {
        if (gCapacity.attached) return false;
        gCapacity.slots = 0;
        const auto* section = root ? root->FirstChildElement("playerCapacity") : nullptr;
        if (!section) return true;
        bool enabled = false;
        if (section->NextSiblingElement("playerCapacity")
            || section->QueryBoolAttribute("enabled",&enabled) != tinyxml2::XML_SUCCESS) return false;
        if (!enabled) return true;
        const char* raw = section->Attribute("slots");
        if (!raw) return false;
        const std::string_view text(raw);
        unsigned slots = 0;
        const auto parsed = std::from_chars(text.data(),text.data()+text.size(),slots);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data()+text.size()
            || slots <= kStockSlots || slots > kMaxSlots) return false;
        gCapacity.slots = slots;
        return true;
    }

    bool AttachPlayerCapacityHook()
    {
        if (!gCapacity.slots || gCapacity.attached) return true;
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        return base != 0 && InstallAt(base);
    }
}
