/* ===================================================================================
    Copyright (c) 2026, LiFx Contributors. All Rights Reserved.
    Contributed by Zbigi.

    This file is a part of LiFx.

    LIFX IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
    DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
    ARISING FROM, OUT OF OR IN CONNECTION WITH LIFX OR THE USE OR OTHER
    DEALINGS IN LIFX.
*  =================================================================================== */

// Regression check for the createAnimal x64 quality argument.
// Link with source/server/hooks/character/hook_animal_create.cpp.
// This calls a local substitute for the native factory, not a game process.
#include "server/hooks/character/hook_animal_create.h"
#include <type_traits>
#include <initializer_list>
#include <cstdio>

using ExpectedFactory = void* (__fastcall*)(void*, uint32_t, float, int, unsigned char);
static_assert(std::is_same_v<_Animal_CreateAnimal_Fn, ExpectedFactory>);
static_assert(std::is_same_v<decltype(&Hooks::AnimalCreate::OnCreateAnimal), ExpectedFactory>);

namespace {
int calls, tags, binds, errors;
int managerToken, animalToken;
uint32_t expectedType;
float expectedQuality;
int expectedId;
unsigned char expectedFlag;
void* result;

void* __fastcall NativeFactory(void* manager, uint32_t type, float quality,
                              int id, unsigned char flag)
{
    ++calls;
    if (manager != &managerToken || type != expectedType || quality != expectedQuality
        || id != expectedId || flag != expectedFlag || tags || binds) ++errors;
    return result;
}
}

namespace Hooks::AnimRemap {
void Register(void* animal)
{
    if (calls != 1 || animal != &animalToken || tags || binds) ++errors;
    ++tags;
}
}
namespace Lifx::Api::Hostile {
bool BindHostile(void* animal)
{
    if (calls != 1 || animal != &animalToken || tags != 1 || binds) ++errors;
    ++binds;
    return true;
}
}

int main()
{
    _Animal_CreateAnimal = &NativeFactory;
    for (float quality : {12.5f, 50.0f, 99.75f}) {
        for (uint32_t type : {116u, 755u}) {
            for (bool success : {false, true}) {
                calls = tags = binds = 0;
                expectedType = type; expectedQuality = quality;
                expectedId = 73; expectedFlag = 1;
                result = success ? &animalToken : nullptr;
                void* actual = Hooks::AnimalCreate::OnCreateAnimal(
                    &managerToken, type, quality, expectedId, expectedFlag);
                const int expectedSideEffects = success && type == 755;
                if (actual != result || calls != 1 || tags != expectedSideEffects
                    || binds != expectedSideEffects) ++errors;
                if (expectedSideEffects && Hooks::AnimalCreate::LastBandit() != result) ++errors;
            }
        }
    }
    std::printf("createAnimal ABI: 12 cases, %d errors\n", errors);
    return errors ? 1 : 0;
}
