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

/* ===================================================================================
    Per-tree movable-object drop override.

    Vanilla code at +0x3716D0 selects ObjectTypeID 626 or 653 and stores it in
    [rbp+0x110]. The tree species byte is still available at [rbp+0x120]. A small
    generated trampoline preserves the vanilla selection and applies only XML
    entries matching that species.
*  =================================================================================== */

#include "hook_tree_drop.h"

#include "core/tinyxml2.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

namespace Hooks
{
    namespace Engine
    {
        namespace
        {
            constexpr std::uintptr_t kTreeDropSelectorRva = 0x3716D0;
            constexpr std::uintptr_t kTreeDropSelectorReturnRva = 0x3716EA;

            // Complete instructions from +0x3716D0 through +0x3716E4.
            constexpr std::array<std::uint8_t, 26> kOriginalSelectorBytes = {
                0xB8, 0x8D, 0x02, 0x00, 0x00,             // mov eax,653
                0xB9, 0x72, 0x02, 0x00, 0x00,             // mov ecx,626
                0x80, 0xBD, 0x10, 0x01, 0x00, 0x00, 0x00, // cmp byte [rbp+110],0
                0x0F, 0x45, 0xC1,                         // cmovnz eax,ecx
                0x89, 0x85, 0x10, 0x01, 0x00, 0x00        // mov [rbp+110],eax
            };

            constexpr std::size_t kVanillaSelectionSize = 20;
            constexpr std::size_t kAbsoluteJumpSize = 14;

            // Current TreeType order from the server data. Oak=7 and Apple=0 were
            // also confirmed dynamically at the selector.
            constexpr std::array<std::string_view, 12> kTreeTypeNames = {
                "apple", "birch", "elm", "spruce", "pine", "maple",
                "mulberry", "oak", "aspen", "hazel", "juniper", "spinny"
            };

            struct TreeDrop
            {
                std::uint8_t treeType = 0;
                U32 itemId = 0;
            };

            struct TreeDropState
            {
                bool enabled = false;
                bool attached = false;
                std::vector<TreeDrop> drops;

                std::uintptr_t moduleBase = 0;
                std::uintptr_t selectorAddress = 0;
                void* trampoline = nullptr;
                std::size_t trampolineSize = 0;
            };

            TreeDropState gTreeDrops;

            // ----------------------------------------------------------------- //
            bool EqualsIgnoreCase(std::string_view left, std::string_view right)
            {
                if (left.size() != right.size())
                    return false;

                for (std::size_t index = 0; index < left.size(); ++index)
                {
                    char a = left[index];
                    char b = right[index];

                    if (a >= 'A' && a <= 'Z')
                        a = static_cast<char>(a - 'A' + 'a');
                    if (b >= 'A' && b <= 'Z')
                        b = static_cast<char>(b - 'A' + 'a');

                    if (a != b)
                        return false;
                }

                return true;
            }

            // ----------------------------------------------------------------- //
            std::optional<std::uint8_t> ParseTreeType(std::string_view value)
            {
                for (std::size_t index = 0; index < kTreeTypeNames.size(); ++index)
                {
                    if (EqualsIgnoreCase(value, kTreeTypeNames[index]))
                        return static_cast<std::uint8_t>(index);
                }

                unsigned numericValue = 0;
                const auto result = std::from_chars(
                    value.data(),
                    value.data() + value.size(),
                    numericValue);

                if (result.ec == std::errc{}
                    && result.ptr == value.data() + value.size()
                    && numericValue <= 0xFF)
                {
                    return static_cast<std::uint8_t>(numericValue);
                }

                return std::nullopt;
            }

            // ----------------------------------------------------------------- //
            void AppendU32(std::vector<std::uint8_t>& code, std::uint32_t value)
            {
                for (unsigned shift = 0; shift < 32; shift += 8)
                    code.push_back(static_cast<std::uint8_t>(value >> shift));
            }

            // ----------------------------------------------------------------- //
            void AppendU64(std::vector<std::uint8_t>& code, std::uint64_t value)
            {
                for (unsigned shift = 0; shift < 64; shift += 8)
                    code.push_back(static_cast<std::uint8_t>(value >> shift));
            }

            // ----------------------------------------------------------------- //
            void AppendAbsoluteJump(
                std::vector<std::uint8_t>& code,
                std::uintptr_t destination)
            {
                // jmp qword ptr [rip+0], followed by the absolute destination.
                static constexpr std::uint8_t instruction[] = {
                    0xFF, 0x25, 0x00, 0x00, 0x00, 0x00
                };

                code.insert(
                    code.end(),
                    std::begin(instruction),
                    std::end(instruction));
                AppendU64(code, static_cast<std::uint64_t>(destination));
            }

            // ----------------------------------------------------------------- //
            bool MatchesTreeDropSelector(std::uintptr_t address)
            {
                return std::memcmp(
                    reinterpret_cast<const void*>(address),
                    kOriginalSelectorBytes.data(),
                    kOriginalSelectorBytes.size()) == 0;
            }

            // ----------------------------------------------------------------- //
            bool WriteExecutableMemory(
                std::uintptr_t address,
                const void* bytes,
                std::size_t size)
            {
                DWORD oldProtection = 0;
                if (!VirtualProtect(
                        reinterpret_cast<void*>(address),
                        size,
                        PAGE_EXECUTE_READWRITE,
                        &oldProtection))
                {
                    return false;
                }

                std::memcpy(reinterpret_cast<void*>(address), bytes, size);
                FlushInstructionCache(
                    GetCurrentProcess(),
                    reinterpret_cast<const void*>(address),
                    size);

                DWORD ignoredProtection = 0;
                if (!VirtualProtect(
                        reinterpret_cast<void*>(address),
                        size,
                        oldProtection,
                        &ignoredProtection))
                {
                    Lifx::ShowErrorMessage(
                        "Tree drop hook was written, but its page protection could not be restored.");
                }

                return true;
            }

            // ----------------------------------------------------------------- //
            bool BuildTrampoline()
            {
                std::vector<std::uint8_t> code;
                code.reserve(
                    kVanillaSelectionSize
                    + gTreeDrops.drops.size() * 14
                    + 6
                    + kAbsoluteJumpSize);

                // Preserve the original hardwood/softwood choice first.
                code.insert(
                    code.end(),
                    kOriginalSelectorBytes.begin(),
                    kOriginalSelectorBytes.begin() + kVanillaSelectionSize);

                for (const TreeDrop& drop : gTreeDrops.drops)
                {
                    // cmp byte ptr [rbp+0x120], treeType
                    const std::uint8_t compare[] = {
                        0x80, 0xBD, 0x20, 0x01, 0x00, 0x00, drop.treeType
                    };
                    code.insert(code.end(), std::begin(compare), std::end(compare));

                    // jne +5; mov eax,itemId
                    code.push_back(0x75);
                    code.push_back(0x05);
                    code.push_back(0xB8);
                    AppendU32(code, drop.itemId);
                }

                // mov dword ptr [rbp+0x110],eax
                static constexpr std::uint8_t storeResult[] = {
                    0x89, 0x85, 0x10, 0x01, 0x00, 0x00
                };
                code.insert(
                    code.end(),
                    std::begin(storeResult),
                    std::end(storeResult));

                AppendAbsoluteJump(
                    code,
                    gTreeDrops.moduleBase + kTreeDropSelectorReturnRva);

                void* trampoline = VirtualAlloc(
                    nullptr,
                    code.size(),
                    MEM_COMMIT | MEM_RESERVE,
                    PAGE_READWRITE);
                if (trampoline == nullptr)
                    return false;

                std::memcpy(trampoline, code.data(), code.size());

                DWORD oldProtection = 0;
                if (!VirtualProtect(
                        trampoline,
                        code.size(),
                        PAGE_EXECUTE_READ,
                        &oldProtection))
                {
                    VirtualFree(trampoline, 0, MEM_RELEASE);
                    return false;
                }

                FlushInstructionCache(GetCurrentProcess(), trampoline, code.size());

                gTreeDrops.trampoline = trampoline;
                gTreeDrops.trampolineSize = code.size();
                return true;
            }

            // ----------------------------------------------------------------- //
            std::array<std::uint8_t, kOriginalSelectorBytes.size()> MakeSelectorPatch()
            {
                std::array<std::uint8_t, kOriginalSelectorBytes.size()> patch{};
                patch.fill(0x90);

                // jmp qword ptr [rip+0], followed by the trampoline address.
                patch[0] = 0xFF;
                patch[1] = 0x25;
                patch[2] = 0x00;
                patch[3] = 0x00;
                patch[4] = 0x00;
                patch[5] = 0x00;
                const std::uint64_t destination =
                    reinterpret_cast<std::uint64_t>(gTreeDrops.trampoline);
                std::memcpy(patch.data() + 6, &destination, sizeof(destination));

                return patch;
            }
        }

        // --------------------------------------------------------------------- //
        bool ConfigureTreeDrops(const tinyxml2::XMLElement* root)
        {
            if (gTreeDrops.attached)
            {
                Lifx::ShowErrorMessage(
                    "Can't configure tree drops while the hook is attached.");
                return false;
            }

            gTreeDrops = {};

            if (root == nullptr)
            {
                Lifx::ShowErrorMessage(
                    "Can't configure tree drops: XML root element is null.");
                return false;
            }

            const tinyxml2::XMLElement* section =
                root->FirstChildElement("treeDrops");

            // Missing or disabled section preserves the original game behaviour.
            if (section == nullptr)
                return true;

            gTreeDrops.enabled = section->BoolAttribute("enabled", false);
            if (!gTreeDrops.enabled)
                return true;

            for (const tinyxml2::XMLElement* drop = section->FirstChildElement("drop");
                 drop != nullptr;
                 drop = drop->NextSiblingElement("drop"))
            {
                const char* treeValue = drop->Attribute("tree");
                if (treeValue == nullptr || *treeValue == '\0')
                {
                    Lifx::ShowErrorMessage(
                        "Invalid <treeDrops><drop>: tree is required.");
                    gTreeDrops = {};
                    return false;
                }

                const std::optional<std::uint8_t> treeType =
                    ParseTreeType(treeValue);
                if (!treeType.has_value())
                {
                    Lifx::ShowErrorMessage(
                        "Invalid <treeDrops><drop> tree '%s'. Use a known tree name or TreeType 0..255.",
                        treeValue);
                    gTreeDrops = {};
                    return false;
                }

                unsigned itemId = 0;
                if (drop->QueryUnsignedAttribute("itemId", &itemId)
                        != tinyxml2::XML_SUCCESS
                    || itemId == 0)
                {
                    Lifx::ShowErrorMessage(
                        "Invalid <treeDrops><drop>: itemId must be a positive integer.");
                    gTreeDrops = {};
                    return false;
                }

                const bool duplicate = std::any_of(
                    gTreeDrops.drops.begin(),
                    gTreeDrops.drops.end(),
                    [treeType](const TreeDrop& existing)
                    {
                        return existing.treeType == *treeType;
                    });
                if (duplicate)
                {
                    Lifx::ShowErrorMessage(
                        "Duplicate <treeDrops><drop> entry for tree '%s'.",
                        treeValue);
                    gTreeDrops = {};
                    return false;
                }

                gTreeDrops.drops.push_back({
                    *treeType,
                    static_cast<U32>(itemId)
                });
            }

            if (gTreeDrops.drops.empty())
            {
                Lifx::ShowErrorMessage(
                    "treeDrops is enabled, but no <drop> entries were provided.");
                gTreeDrops = {};
                return false;
            }

            std::sort(
                gTreeDrops.drops.begin(),
                gTreeDrops.drops.end(),
                [](const TreeDrop& left, const TreeDrop& right)
                {
                    return left.treeType < right.treeType;
                });

            return true;
        }

        // --------------------------------------------------------------------- //
        void AttachTreeDropHook()
        {
            if (!gTreeDrops.enabled || gTreeDrops.attached)
                return;

            gTreeDrops.moduleBase =
                reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            if (gTreeDrops.moduleBase == 0)
            {
                Lifx::ShowErrorMessage(
                    "Can't attach tree drop hook: main module base is null.");
                return;
            }

            gTreeDrops.selectorAddress =
                gTreeDrops.moduleBase + kTreeDropSelectorRva;
            if (!MatchesTreeDropSelector(gTreeDrops.selectorAddress))
            {
                Lifx::ShowErrorMessage(
                    "Can't attach tree drop hook: expected code was not found at +0x3716D0.");
                return;
            }

            if (!BuildTrampoline())
            {
                Lifx::ShowErrorMessage(
                    "Can't attach tree drop hook: trampoline allocation failed.");
                return;
            }

            const auto patch = MakeSelectorPatch();
            if (!WriteExecutableMemory(
                    gTreeDrops.selectorAddress,
                    patch.data(),
                    patch.size()))
            {
                VirtualFree(gTreeDrops.trampoline, 0, MEM_RELEASE);
                gTreeDrops.trampoline = nullptr;
                gTreeDrops.trampolineSize = 0;
                Lifx::ShowErrorMessage(
                    "Can't attach tree drop hook: selector memory is not writable.");
                return;
            }

            gTreeDrops.attached = true;
        }

        // --------------------------------------------------------------------- //
        void DetachTreeDropHook()
        {
            if (!gTreeDrops.attached)
                return;

            if (!WriteExecutableMemory(
                    gTreeDrops.selectorAddress,
                    kOriginalSelectorBytes.data(),
                    kOriginalSelectorBytes.size()))
            {
                Lifx::ShowErrorMessage(
                    "Can't detach tree drop hook: original selector could not be restored.");
                return;
            }

            gTreeDrops.attached = false;

            if (gTreeDrops.trampoline != nullptr)
            {
                VirtualFree(gTreeDrops.trampoline, 0, MEM_RELEASE);
                gTreeDrops.trampoline = nullptr;
                gTreeDrops.trampolineSize = 0;
            }
        }
    }
}
