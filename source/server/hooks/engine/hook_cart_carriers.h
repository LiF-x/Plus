#pragma once

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

#include "server/cm_server.h"
namespace tinyxml2 { class XMLElement; }

// Win64: ECX = conversion mode, EDX = ObjectTypeID, EAX = converted ID.
__CM_DECL_EXTERNAL(U32, __fastcall, _Carrier_ConvertObjectType, U32 mode, U32 objectTypeId);

namespace Hooks::Carrier
{
    U32 __fastcall ConvertObjectType(U32 mode, U32 objectTypeId);
    bool ConfigureCartCarriers(const tinyxml2::XMLElement* root);
    // Startup/shutdown only; owns its Detours transaction. No live reload.
    void AttachCartCarrierHooks();
    void DetachCartCarrierHooks();
}
