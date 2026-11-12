/* ===================================================================================
    Copyright (c) 2026, LiFx Contributors. All Rights Reserved.
    Adapted from LiFXXL cart support with Zbigi and ChatGPT (Codex).

    This file is a part of LiFx.

    LIFX IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
    DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
    ARISING FROM, OUT OF OR IN CONNECTION WITH LIFX OR THE USE OR OTHER
    DEALINGS IN LIFX.
*  =================================================================================== */

#include "hook_cart_carriers.h"
#include "core/tinyxml2.h"
#include <detours/detours.h>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

__CM_INSTATNTIATE(_Carrier_ConvertObjectType);

namespace Hooks::Carrier
{
    namespace
    {
        struct CartPair { U32 passive, harnessed; };
        struct State
        {
            std::vector<CartPair> pairs;
            std::uintptr_t base = 0;
            void* stub = nullptr;
            bool attached = false;
        } gCartCarriers;

        constexpr std::array<unsigned char, 13> kConversionPrefix = {
            0x48,0x83,0xEC,0x38,0x85,0xC9,0x74,0x7A,0x83,0xF9,0x01,0x74,0x30
        };
        constexpr std::array<unsigned char, 18> kPassiveGate = {
            0x3D,0xB5,0x05,0x00,0x00,0x74,0x0B,
            0x3D,0xD9,0x05,0x00,0x00,0x0F,0x85,0x9E,0x00,0x00,0x00
        };

        template<class T> void Append(std::vector<unsigned char>& code, T value)
        {
            const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
            code.insert(code.end(), bytes, bytes + sizeof(value));
        }

        void AbsoluteJump(std::vector<unsigned char>& code, std::uintptr_t target)
        {
            // RIP-indirect jump: no register, stack or flags are clobbered.
            code.insert(code.end(), {0xFF,0x25,0,0,0,0});
            Append(code, target);
        }

        std::vector<unsigned char> PassiveStub(std::uintptr_t accepted, std::uintptr_t rejected)
        {
            std::vector<unsigned char> code;
            std::vector<std::size_t> branches;
            const auto check = [&](U32 id)
            {
                code.push_back(0x3D); // cmp eax,id
                Append(code, id);
                code.insert(code.end(), {0x0F,0x84}); // je accepted
                branches.push_back(code.size());
                Append(code, std::int32_t{0});
            };
            for (const auto& pair : gCartCarriers.pairs) check(pair.passive);
            // Retain the stock comparisons last: rejected objects also leave
            // exactly the original CMP(EAX,1497) flags at the continuation.
            check(1461);
            check(1497);
            AbsoluteJump(code, rejected);
            const auto destination = code.size();
            AbsoluteJump(code, accepted);
            for (const auto offset : branches)
            {
                const auto relative = static_cast<std::int32_t>(destination - (offset + 4));
                std::memcpy(code.data() + offset, &relative, 4);
            }
            return code;
        }

        std::array<unsigned char, 18> GatePatch()
        {
            std::array<unsigned char, 18> patch;
            patch.fill(0x90);
            const unsigned char jump[] = {0xFF,0x25,0,0,0,0};
            std::memcpy(patch.data(), jump, sizeof(jump));
            std::memcpy(patch.data() + 6, &gCartCarriers.stub, sizeof(gCartCarriers.stub));
            return patch;
        }

        bool ParseId(const char* text, U32& result)
        {
            if (!text) return false;
            const std::string_view value(text);
            const auto parsed = std::from_chars(value.data(), value.data()+value.size(), result);
            return parsed.ec == std::errc{} && parsed.ptr == value.data()+value.size() && result != 0;
        }

        bool StockCartId(U32 id)
        {
            return id == 1461 || id == 1437 || id == 1497 || id == 1496;
        }

        void RestoreProtection(void* gate, DWORD protection)
        {
            DWORD ignored;
            if (!VirtualProtect(gate, kPassiveGate.size(), protection, &ignored))
                Con::Error("[cartCarriers] could not restore code-page protection, error=%lu", GetLastError());
        }

        bool InstallAt(std::uintptr_t base)
        {
            auto* gate = reinterpret_cast<unsigned char*>(base + CmOffset::CARRIER_PASSIVE_GATE);
            auto* conversion = reinterpret_cast<unsigned char*>(base + CmOffset::CARRIER_CONVERT_OBJECT_TYPE);
            if (std::memcmp(gate,kPassiveGate.data(),kPassiveGate.size()) != 0
                || std::memcmp(conversion,kConversionPrefix.data(),kConversionPrefix.size()) != 0)
            {
                Con::Error("[cartCarriers] unsupported build or occupied hook site; neither hook installed");
                return false;
            }
            const auto code = PassiveStub(base + CmOffset::CARRIER_PASSIVE_ACCEPTED,
                                          base + CmOffset::CARRIER_PASSIVE_REJECTED);
            void* stub = VirtualAlloc(nullptr,code.size(),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
            if (!stub) { Con::Error("[cartCarriers] stub allocation failed"); return false; }
            std::memcpy(stub,code.data(),code.size());
            DWORD ignored, gateProtection;
            if (!VirtualProtect(stub,code.size(),PAGE_EXECUTE_READ,&ignored)
                || !FlushInstructionCache(GetCurrentProcess(),stub,code.size())
                || !VirtualProtect(gate,kPassiveGate.size(),PAGE_EXECUTE_READWRITE,&gateProtection))
            {
                Con::Error("[cartCarriers] executable memory preparation failed");
                VirtualFree(stub,0,MEM_RELEASE);
                return false;
            }
            gCartCarriers.base = base;
            gCartCarriers.stub = stub;
            _Carrier_ConvertObjectType = reinterpret_cast<_Carrier_ConvertObjectType_Fn>(conversion);
            LONG rc = DetourTransactionBegin();
            if (rc == NO_ERROR)
            {
                rc = DetourUpdateThread(GetCurrentThread());
                if (rc == NO_ERROR)
                    rc = DetourAttach(reinterpret_cast<PVOID*>(&_Carrier_ConvertObjectType), ConvertObjectType);
                if (rc == NO_ERROR)
                {
                    // Startup, before the world loads. Both sites are preflighted
                    // before either changes; undo this gate if Detours rejects.
                    const auto patch = GatePatch();
                    std::memcpy(gate,patch.data(),patch.size());
                    if (!FlushInstructionCache(GetCurrentProcess(),gate,patch.size()))
                    {
                        rc = static_cast<LONG>(GetLastError());
                        DetourTransactionAbort();
                    }
                    else rc = DetourTransactionCommit();
                }
                else DetourTransactionAbort();
            }
            if (rc != NO_ERROR)
            {
                std::memcpy(gate,kPassiveGate.data(),kPassiveGate.size());
                FlushInstructionCache(GetCurrentProcess(),gate,kPassiveGate.size());
                VirtualFree(stub,0,MEM_RELEASE);
                gCartCarriers.stub = nullptr;
                _Carrier_ConvertObjectType = nullptr;
                Con::Error("[cartCarriers] installation rolled back, error=%ld",rc);
            }
            RestoreProtection(gate,gateProtection);
            gCartCarriers.attached = rc == NO_ERROR;
            return gCartCarriers.attached;
        }
    }

    U32 __fastcall ConvertObjectType(U32 mode, U32 objectTypeId)
    {
        if (mode == 1)
            for (const auto& pair : gCartCarriers.pairs)
                if (objectTypeId == pair.passive) return pair.harnessed;
        return _Carrier_ConvertObjectType(mode,objectTypeId);
    }

    bool ConfigureCartCarriers(const tinyxml2::XMLElement* root)
    {
        if (gCartCarriers.attached)
        {
            Lifx::ShowErrorMessage("cartCarriers requires a restart; live reconfiguration is not supported.");
            return false;
        }
        gCartCarriers = {};
        const auto fail = [](const char* reason)
        {
            gCartCarriers = {};
            Lifx::ShowErrorMessage("Invalid cartCarriers: %s. Carrier hooks disabled.",reason);
            return false;
        };
        if (!root) return fail("missing XML root");
        const auto* section = root->FirstChildElement("cartCarriers");
        if (!section) return true;
        bool enabled = false;
        if (section->Attribute("enabled") && section->QueryBoolAttribute("enabled",&enabled) != tinyxml2::XML_SUCCESS)
            return fail("enabled must be a boolean");
        if (!enabled) return true;
        for (const auto* entry = section->FirstChildElement(); entry; entry = entry->NextSiblingElement())
        {
            CartPair pair{};
            if (std::strcmp(entry->Name(),"cart") != 0
                || !ParseId(entry->Attribute("passiveId"),pair.passive)
                || !ParseId(entry->Attribute("harnessedId"),pair.harnessed))
                return fail("each cart needs positive uint32 passiveId and harnessedId");
            if (pair.passive == pair.harnessed || StockCartId(pair.passive) || StockCartId(pair.harnessed))
                return fail("use two distinct custom IDs; vanilla cart IDs are reserved");
            for (const auto& previous : gCartCarriers.pairs)
                if (pair.passive == previous.passive || pair.passive == previous.harnessed
                    || pair.harnessed == previous.passive || pair.harnessed == previous.harnessed)
                    return fail("an ObjectTypeID is reused across cart pairs");
            gCartCarriers.pairs.push_back(pair);
        }
        if (gCartCarriers.pairs.empty()) return fail("enabled section contains no carts");
        return true;
    }

    void AttachCartCarrierHooks()
    {
        if (gCartCarriers.pairs.empty() || gCartCarriers.attached) return;
        if (InstallAt(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))))
            Con::Echo("[cartCarriers] conversion and cargo-load gate installed for %zu custom pairs",gCartCarriers.pairs.size());
    }

    void DetachCartCarrierHooks()
    {
        if (!gCartCarriers.attached) return;
        auto* gate = reinterpret_cast<void*>(gCartCarriers.base + CmOffset::CARRIER_PASSIVE_GATE);
        DWORD protection;
        if (!VirtualProtect(gate,kPassiveGate.size(),PAGE_EXECUTE_READWRITE,&protection))
        {
            Con::Error("[cartCarriers] detach could not make gate writable; hooks retained");
            return;
        }
        LONG rc = DetourTransactionBegin();
        if (rc == NO_ERROR)
        {
            rc = DetourUpdateThread(GetCurrentThread());
            if (rc == NO_ERROR)
                rc = DetourDetach(reinterpret_cast<PVOID*>(&_Carrier_ConvertObjectType),ConvertObjectType);
            if (rc == NO_ERROR) rc = DetourTransactionCommit();
            else DetourTransactionAbort();
        }
        if (rc == NO_ERROR)
        {
            std::memcpy(gate,kPassiveGate.data(),kPassiveGate.size());
            FlushInstructionCache(GetCurrentProcess(),gate,kPassiveGate.size());
            VirtualFree(gCartCarriers.stub,0,MEM_RELEASE);
            gCartCarriers.stub = nullptr;
            gCartCarriers.attached = false;
            _Carrier_ConvertObjectType = nullptr;
        }
        else Con::Error("[cartCarriers] detach failed, hooks retained, error=%ld",rc);
        RestoreProtection(gate,protection);
    }
}
