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

// Win64 executable-memory regression. Runs the actual native reset/append/
// overflow/validator bytes against fake connections, not a game process.
// Compile this TU and tinyxml2.cpp with the project's include directories.
#include "../source/server/hooks/engine/hook_player_capacity.cpp"
#include <cstdio>
#include <cstdlib>

namespace Con { void Error(const char*,...) {} void Echo(const char*,...) {} }
namespace
{
    unsigned checks = 0, deleted = 0, protectionCall = 0, failProtectionCall = 0;
    unsigned flushCall = 0, failFlushCall = 0;
    void* victim = nullptr;
    void Check(bool result, const char* label)
    {
        ++checks;
        if (!result) { std::fprintf(stderr,"FAIL: %s\n",label); std::exit(1); }
    }
    void __fastcall Delete(void* connection, unsigned delay)
    {
        Check(delay==1,"original safe-delete delay preserved");
        ++deleted; victim=connection;
    }
    BOOL WINAPI Protect(LPVOID address,SIZE_T bytes,DWORD mode,PDWORD old)
    {
        if (++protectionCall==failProtectionCall) return FALSE;
        return VirtualProtect(address,bytes,mode,old);
    }
    BOOL WINAPI Flush(HANDLE process,LPCVOID address,SIZE_T bytes)
    {
        if (++flushCall==failFlushCall) return FALSE;
        return FlushInstructionCache(process,address,bytes);
    }
    LPVOID WINAPI NoAllocation(LPVOID,SIZE_T,DWORD,DWORD) { return nullptr; }
}
using namespace Hooks::Engine;

static bool Configure(const char* xml)
{
    tinyxml2::XMLDocument doc;
    Check(doc.Parse(xml)==tinyxml2::XML_SUCCESS,"XML fixture parses");
    return ConfigurePlayerCapacity(doc.RootElement());
}

struct Fixture
{
    std::uintptr_t base = reinterpret_cast<std::uintptr_t>(VirtualAlloc(nullptr,kImageSize,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    Fixture()
    {
        Check(base!=0,"synthetic image allocation");
        auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
        dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x100;
        auto* pe=reinterpret_cast<IMAGE_NT_HEADERS64*>(base+0x100);
        pe->Signature=IMAGE_NT_SIGNATURE;pe->FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
        pe->FileHeader.TimeDateStamp=kTimeStamp;
        pe->OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;pe->OptionalHeader.SizeOfImage=kImageSize;
        Write(base+CmOffset::PLAYER_CAPACITY_MIN,1u);Write(base+CmOffset::PLAYER_CAPACITY_MAX,64u);
        std::memcpy(reinterpret_cast<void*>(base+CmOffset::PLAYER_CAPACITY_TICK_RESET),kReset.data(),kReset.size());
        std::memcpy(reinterpret_cast<void*>(base+CmOffset::PLAYER_CAPACITY_APPEND),kAppend.data(),kAppend.size());
        std::memcpy(reinterpret_cast<void*>(base+CmOffset::PLAYER_CAPACITY_VALIDATOR),kValidator.data(),kValidator.size());
        Write(base+CmOffset::PLAYER_CAPACITY_TICK_RESET+kReset.size(),static_cast<unsigned char>(0xC3));
        const unsigned char tail[]={0x48,0x83,0xC4,0x20,0x5D,0xC3};
        std::memcpy(reinterpret_cast<void*>(base+CmOffset::PLAYER_CAPACITY_APPEND+kAppend.size()),tail,sizeof(tail));
        // Save nonvolatile RBP and reserve the Win64 call shadow area.
        const unsigned char prologue[]={0x55,0x48,0x83,0xEC,0x20,0x48,0x89,0xCD,0xE9};
        std::memcpy(reinterpret_cast<void*>(base+0x2000),prologue,sizeof(prologue));
        Write(base+0x2009,static_cast<std::int32_t>(CmOffset::PLAYER_CAPACITY_APPEND-0x200D));
        const unsigned char validatorEntry[]={0x53,0x48,0x89,0xCB,0xE9};
        std::memcpy(reinterpret_cast<void*>(base+0x2100),validatorEntry,sizeof(validatorEntry));
        Write(base+0x2105,static_cast<std::int32_t>(CmOffset::PLAYER_CAPACITY_VALIDATOR-0x2109));
        const unsigned char accepted[]={0xB8,1,0,0,0,0x5B,0xC3}, rejected[]={0x31,0xC0,0x5B,0xC3};
        std::memcpy(reinterpret_cast<void*>(base+CmOffset::PLAYER_CAPACITY_VALIDATOR+kValidator.size()),accepted,sizeof(accepted));
        std::memcpy(reinterpret_cast<void*>(base+0x177FAE),rejected,sizeof(rejected));
        FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(base),kImageSize);
    }
    void Reset() { reinterpret_cast<void(*)()>(base+CmOffset::PLAYER_CAPACITY_TICK_RESET)(); }
    void Append(void* connection) { reinterpret_cast<void(*)(void*)>(base+0x2000)(connection); }
    bool Validate(int slots)
    {
        std::array<unsigned char,0xA0> settings{};
        std::memcpy(settings.data()+0x9C,&slots,4);
        return reinterpret_cast<int(*)(void*)>(base+0x2100)(settings.data())!=0;
    }
    void Stock()
    {
        Check(Read<unsigned>(base+CmOffset::PLAYER_CAPACITY_MAX)==64,"stock ceiling retained");
        Check(Read<std::uintptr_t>(base+CmOffset::PLAYER_CAPACITY_CURSOR)==0,"stock cursor retained");
        Check(!std::memcmp(reinterpret_cast<void*>(base+CmOffset::PLAYER_CAPACITY_TICK_RESET),kReset.data(),kReset.size()),"reset bytes unchanged");
        Check(!std::memcmp(reinterpret_cast<void*>(base+CmOffset::PLAYER_CAPACITY_APPEND),kAppend.data(),kAppend.size()),"append/overflow bytes unchanged");
    }
    ~Fixture()
    {
        // Test-only cleanup after all synchronous calls have returned.
        // Production keeps the allocation for the process lifetime.
        if (gCapacity.buffer) VirtualFree(gCapacity.buffer,0,MEM_RELEASE);
        gCapacity={};gMemory={};
        VirtualFree(reinterpret_cast<void*>(base),0,MEM_RELEASE);
    }
};

int main()
{
    Check(Configure("<cm_config/>"),"missing section");
    Check(AttachPlayerCapacityHook() && !gCapacity.attached,"missing section is inert");
    Check(Configure("<cm_config><playerCapacity enabled='0' slots='bad'/></cm_config>"),"disabled ignores slots");
    Check(AttachPlayerCapacityHook() && !gCapacity.attached,"disabled section is inert");
    const char* invalid[]={
        "<playerCapacity/>","<playerCapacity enabled='maybe' slots='128'/>",
        "<playerCapacity enabled='1'/>","<playerCapacity enabled='1' slots='0'/>",
        "<playerCapacity enabled='1' slots='64'/>","<playerCapacity enabled='1' slots='129'/>",
        "<playerCapacity enabled='1' slots='-1'/>","<playerCapacity enabled='1' slots='128x'/>",
        "<playerCapacity enabled='1' slots='4294967296'/>","<playerCapacity enabled='1' slots='0x80'/>",
        "<playerCapacity enabled='1' slots='128'/><playerCapacity enabled='0'/>"};
    for (const char* section:invalid)
    {
        const std::string xml=std::string("<cm_config>")+section+"</cm_config>";
        Check(!Configure(xml.c_str()) && gCapacity.slots==0,"malformed config rejected");
    }
    std::int32_t relative=0;
    Check(Relative32(0x100000000,0x80000000,relative) && relative==INT32_MIN,"negative rel32 limit");
    Check(Relative32(0x100000000,0x17FFFFFFF,relative) && relative==INT32_MAX,"positive rel32 limit");
    Check(!Relative32(0x100000000,0x7FFFFFFF,relative),"negative rel32 overflow");
    Check(!Relative32(0x100000000,0x180000000,relative),"positive rel32 overflow");

    std::array<void*,39> vtable{};vtable[0x130/8]=reinterpret_cast<void*>(&Delete);
    struct Connection { void** table; };
    std::array<Connection,129> connections{};
    for(auto& connection:connections) connection.table=vtable.data();
    {
        Fixture f;
        Check(f.Validate(64) && !f.Validate(65),"native 64-slot validator baseline");
        f.Reset();deleted=0;
        for(unsigned i=0;i<64;++i) f.Append(&connections[i]);
        Check(deleted==0,"stock 64 connections fit");
        f.Append(&connections[64]);
        Check(deleted==1 && victim==&connections[0],"stock 65th entry reproduces deletion of first processed connection");
    }
    {
        Fixture f;gCapacity.slots=128;
        const std::pair<unsigned,std::size_t> ranges[]={
            {CmOffset::PLAYER_CAPACITY_TICK_RESET,kReset.size()},
            {CmOffset::PLAYER_CAPACITY_APPEND,kAppend.size()},
            {CmOffset::PLAYER_CAPACITY_VALIDATOR,kValidator.size()}};
        for(const auto& range:ranges) for(std::size_t i=0;i<range.second;++i)
        {
            auto* byte=reinterpret_cast<unsigned char*>(f.base+range.first+i);
            *byte^=1;
            Check(!InstallAt(f.base) && !gCapacity.attached,"every signature byte checked");
            *byte^=1;f.Stock();
        }
        for(unsigned rva:std::array<unsigned,3>{CmOffset::PLAYER_CAPACITY_MIN,CmOffset::PLAYER_CAPACITY_MAX,0x108u})
        {
            const auto old=Read<unsigned>(f.base+rva);Write(f.base+rva,old+1);
            Check(!InstallAt(f.base),"modified bounds/PE rejected");Write(f.base+rva,old);f.Stock();
        }
        Write(f.base+CmOffset::PLAYER_CAPACITY_CURSOR,f.base+CmOffset::PLAYER_CAPACITY_STOCK_BUFFER);
        Check(!InstallAt(f.base),"late installation rejected");
        Write(f.base+CmOffset::PLAYER_CAPACITY_CURSOR,std::uintptr_t{0});f.Stock();
        gMemory.allocate=&NoAllocation;
        Check(!InstallAt(f.base),"allocation failure");f.Stock();gMemory={};
        for(unsigned failure=1;failure<=4;++failure)
        {
            protectionCall=0;failProtectionCall=failure;gMemory.protect=&Protect;
            Check(!InstallAt(f.base),"page preparation failure");f.Stock();gMemory={};
        }
        for(unsigned failure=1;failure<=2;++failure)
        {
            flushCall=0;failFlushCall=failure;gMemory.flush=&Flush;
            Check(!InstallAt(f.base),"cache failure rolls back all references");f.Stock();gMemory={};
        }
    }
    for(unsigned slots:{65u,96u,128u})
    {
        Fixture f;
        const auto xml=std::string("<cm_config><playerCapacity enabled='1' slots='")+std::to_string(slots)+"'/></cm_config>";
        Check(Configure(xml.c_str()) && gCapacity.slots==slots,"valid capacity configuration");
        Check(InstallAt(f.base),"complete capacity installation");
        Check(!Configure("<cm_config/>"),"live reconfiguration rejected");
        Check(AttachPlayerCapacityHook(),"repeat attach is inert");
        Check(!f.Validate(-1) && !f.Validate(0) && f.Validate(1) && f.Validate(64)
              && f.Validate(65) && f.Validate(static_cast<int>(slots)) && !f.Validate(static_cast<int>(slots+1)),"actual validator accepts only configured range");
        auto* buffer=static_cast<std::uintptr_t*>(gCapacity.buffer);
        buffer[slots]=0x1122334455667788ULL; // committed-page canary after logical end
        for(unsigned tick=0;tick<16;++tick)
        {
            f.Reset();deleted=0;victim=nullptr;
            Check(Read<std::uintptr_t>(f.base+CmOffset::PLAYER_CAPACITY_CURSOR)==reinterpret_cast<std::uintptr_t>(buffer),"tick resets to relocated storage");
            for(unsigned i=0;i<slots;++i)
            {
                f.Append(&connections[(i+tick)%129]);
                Check(deleted==0 && buffer[i]==reinterpret_cast<std::uintptr_t>(&connections[(i+tick)%129]),"all admitted pointers preserved without deletion");
            }
            Check(Read<std::uintptr_t>(f.base+CmOffset::PLAYER_CAPACITY_CURSOR)==reinterpret_cast<std::uintptr_t>(buffer+slots),"cursor reaches exclusive end");
            f.Append(&connections[(slots+tick)%129]);
            Check(deleted==1 && victim==&connections[tick%129],"native overflow preserved at the new ceiling");
            Check(buffer[0]==reinterpret_cast<std::uintptr_t>(&connections[(slots+tick)%129]),"overflow replacement uses relocated slot zero");
            Check(buffer[slots]==0x1122334455667788ULL,"no write beyond logical capacity");
            Check(Read<std::uintptr_t>(f.base+CmOffset::PLAYER_CAPACITY_STOCK_BUFFER)==0,"stock buffer remains untouched");
        }
    }
    // Protection restore failure is fatal to caller startup, but the complete
    // patch keeps its referenced memory alive rather than creating a UAF.
    { Fixture f;gCapacity.slots=128;protectionCall=0;failProtectionCall=5;gMemory.protect=&Protect;
      Check(!InstallAt(f.base) && gCapacity.attached && gCapacity.buffer,"post-write failure retains referenced storage"); }
    std::printf("PASS: %u checks; native 64/65 and 128 boundaries, all-pointer preservation, overflow, validation and failure paths\n",checks);
}
