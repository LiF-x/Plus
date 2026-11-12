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

// Win64 regression: compile this TU plus tinyxml2.cpp; link detours.lib.
// Executes the actual generated gate and Detours attach/detach in synthetic
// executable memory. It never loads, starts or attaches to a game process.
#include "../source/server/hooks/engine/hook_cart_carriers.cpp"
#include <cstdio>
#include <cstdlib>

namespace { int checks = 0, diagnostics = 0; }
namespace Lifx { void ShowErrorMessage(const char*, ...) { ++diagnostics; } }
namespace Con {
void Error(const char*, ...) { ++diagnostics; }
void Echo(const char*, ...) {}
}

static void Check(bool value, const char* description)
{
    ++checks;
    if (!value) { std::fprintf(stderr,"FAIL: %s\n",description); std::exit(1); }
}

using namespace Hooks::Carrier;

static bool Config(const char* text)
{
    tinyxml2::XMLDocument document;
    Check(document.Parse(text) == tinyxml2::XML_SUCCESS,"fixture XML parses");
    return ConfigureCartCarriers(document.RootElement());
}

int main()
{
    Check(Config("<cm_config/>"),"missing section");
    AttachCartCarrierHooks();
    Check(!gCartCarriers.attached,"missing section is inert");
    Check(Config("<cm_config><cartCarriers enabled='0'><cart/></cartCarriers></cm_config>"),"disabled section");
    AttachCartCarrierHooks();
    Check(!gCartCarriers.attached,"disabled section is inert");
    const char* invalid[] = {
        "<cartCarriers enabled='1'/>",
        "<cartCarriers enabled='yes'/>",
        "<cartCarriers enabled='1'><cart passiveId='0' harnessedId='3018'/></cartCarriers>",
        "<cartCarriers enabled='1'><cart passiveId='-1' harnessedId='3018'/></cartCarriers>",
        "<cartCarriers enabled='1'><cart passiveId='4294967296' harnessedId='3018'/></cartCarriers>",
        "<cartCarriers enabled='1'><cart passiveId='3016junk' harnessedId='3018'/></cartCarriers>",
        "<cartCarriers enabled='1'><cart passiveId='3016' harnessedId='3016'/></cartCarriers>",
        "<cartCarriers enabled='1'><cart passiveId='1461' harnessedId='3018'/></cartCarriers>",
        "<cartCarriers enabled='1'><cart passiveId='3016' harnessedId='1437'/></cartCarriers>",
        "<cartCarriers enabled='1'><cart passiveId='3016' harnessedId='3018'/><cart passiveId='3016' harnessedId='3020'/></cartCarriers>",
        "<cartCarriers enabled='1'><cart passiveId='3016' harnessedId='3018'/><cart passiveId='3018' harnessedId='3020'/></cartCarriers>",
        "<cartCarriers enabled='1'><cart passiveId='3016' harnessedId='3018'/><cart passiveId='3020' harnessedId='3018'/></cartCarriers>",
        "<cartCarriers enabled='1'><typo passiveId='3016' harnessedId='3018'/></cartCarriers>"
    };
    for (const char* section : invalid)
    {
        const std::string text = std::string("<cm_config>")+section+"</cm_config>";
        Check(!Config(text.c_str()),"malformed configuration rejected");
        Check(gCartCarriers.pairs.empty(),"no partial config retained");
    }
    // 4020/4021 are synthetic test inputs, not proposed content allocations.
    Check(Config("<cm_config><cartCarriers enabled='1'><cart passiveId='3016' harnessedId='3018'/><cart passiveId='4020' harnessedId='4021'/></cartCarriers></cm_config>"),"two custom pairs");

    constexpr std::size_t imageSize = 0x250000;
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr,imageSize,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Check(memory != nullptr,"synthetic image allocated");
    const auto base = reinterpret_cast<std::uintptr_t>(memory);
    auto* conversion = memory + CmOffset::CARRIER_CONVERT_OBJECT_TYPE;
    auto* gate = memory + CmOffset::CARRIER_PASSIVE_GATE;
    std::memset(conversion,0x90,0x90);
    std::memcpy(conversion,kConversionPrefix.data(),kConversionPrefix.size());
    // Synthetic original uses the same prologue/branches and returns EDX
    // unchanged for every mode. This is a test stand-in, not engine behavior.
    const unsigned char originalReturn[] = {0x8B,0xC2,0x48,0x83,0xC4,0x38,0xC3};
    for (const auto offset : {13,0x3D,0x82}) std::memcpy(conversion+offset,originalReturn,sizeof(originalReturn));
    std::array<unsigned char,0x90> originalConversion;
    std::memcpy(originalConversion.data(),conversion,originalConversion.size());
    std::memcpy(gate,kPassiveGate.data(),kPassiveGate.size());
    // Continuations return (original arithmetic flags << 1) | accepted.
    const unsigned char accept[] = {0x9C,0x58,0x48,0xD1,0xE0,0x48,0x83,0xC8,0x01,0xC3};
    const unsigned char reject[] = {0x9C,0x58,0x48,0xD1,0xE0,0xC3};
    std::memcpy(memory+CmOffset::CARRIER_PASSIVE_ACCEPTED,accept,sizeof(accept));
    std::memcpy(memory+CmOffset::CARRIER_PASSIVE_REJECTED,reject,sizeof(reject));
    std::vector<unsigned char> entry{0x8B,0xC1}; // mov eax,ecx
    AbsoluteJump(entry,reinterpret_cast<std::uintptr_t>(gate));
    std::memcpy(memory,entry.data(),entry.size());
    Check(FlushInstructionCache(GetCurrentProcess(),memory,imageSize) != 0,"synthetic image flushed");
    const auto probe = reinterpret_cast<std::uint64_t(__fastcall*)(U32)>(memory);
    const auto convert = reinterpret_cast<U32(__fastcall*)(U32,U32)>(conversion);
    constexpr std::uint64_t flagMask = (0x8D5ULL << 1) | 1;
    std::vector<std::uint64_t> baseline;
    for (U32 id=0;id<65536;++id) baseline.push_back(probe(id)&flagMask);

    gate[0] ^= 1;
    Check(!InstallAt(base),"occupied gate rejects both hooks");
    Check(std::memcmp(conversion,originalConversion.data(),originalConversion.size())==0,"conversion remains untouched");
    gate[0] ^= 1;
    conversion[0] ^= 1;
    Check(!InstallAt(base),"wrong conversion signature rejects both hooks");
    Check(std::memcmp(gate,kPassiveGate.data(),kPassiveGate.size())==0,"gate remains untouched");
    conversion[0] ^= 1;
    Check(DetourTransactionBegin()==NO_ERROR,"outer transaction for rejection case");
    Check(!InstallAt(base),"transaction conflict leaves both native sites intact");
    Check(DetourTransactionAbort()==NO_ERROR,"outer transaction remains owned by test");
    Check(std::memcmp(gate,kPassiveGate.data(),kPassiveGate.size())==0,"failed attach restores gate");
    Check(!gCartCarriers.attached && gCartCarriers.stub==nullptr,"failed attach frees unused stub");

    Check(InstallAt(base),"actual Detours install on synthetic image");
    for (U32 id=0;id<65536;++id)
    {
        const auto expected = (id==3016 || id==4020) ? baseline[1461] : baseline[id];
        Check((probe(id)&flagMask)==expected,"gate result and arithmetic flags preserved");
    }
    for (U32 mode : {0U,1U,2U,0xFFFFFFFFU})
        for (U32 id : {0U,1461U,1437U,1497U,1496U,3016U,3018U,4020U,4021U,0x80000000U,0xFFFFFFFFU})
        {
            const U32 expected = mode==1 && id==3016 ? 3018 : mode==1 && id==4020 ? 4021 : id;
            Check(convert(mode,id)==expected,"conversion ABI and original fallback");
        }
    for (U32 id : {0x80000000U,0xFFFFFFFFU}) Check((probe(id)&1)==0,"high unsigned IDs rejected");
    Check(!Config("<cm_config/>"),"active reconfiguration rejected");
    Check(convert(1,3016)==3018,"active configuration remains intact");
    DetachCartCarrierHooks();
    Check(!gCartCarriers.attached,"detached state");
    Check(std::memcmp(gate,kPassiveGate.data(),kPassiveGate.size())==0,"original gate restored");
    Check(std::memcmp(conversion,originalConversion.data(),originalConversion.size())==0,"original function restored");
    Check(convert(1,3016)==3016,"detached conversion uses original");
    Check((probe(3016)&flagMask)==baseline[3016],"detached gate rejects custom cart");
    DetachCartCarrierHooks();
    Check(Config("<cm_config/>"),"configuration can clear after detach");
    VirtualFree(memory,0,MEM_RELEASE);
    std::printf("PASS: %d checks; config, 65536 gate IDs/flags, conversion ABI, preflight and attach/detach; %d expected diagnostics\n",checks,diagnostics);
}
