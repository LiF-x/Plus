#pragma once
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

namespace tinyxml2 { class XMLElement; }

namespace Hooks::Engine
{
    // Startup only. Missing/disabled section preserves the stock server.
    bool ConfigurePlayerCapacity(const tinyxml2::XMLElement* root);
    bool AttachPlayerCapacityHook();
    // There is intentionally no live detach: the relocated native buffer
    // belongs to the process, not the DLL, and survives until process exit.
}
