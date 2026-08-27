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

/*
	LiFx character / player utility commands exposed to TorqueScript.

	These are Con::AddCommand registrations, NOT Detours hooks. They're
	installed once during ConsoleInit (after Torque's Con::Init runs), and
	then any TorqueScript can call them — which transitively means
	slashcommands too, since slashcommands resolve to TorqueScript functions
	via the engine's chat parser.

	Currently exposed (all under the `Lifx::` namespace in TorqueScript):

	    Lifx::getPlayerHp(charID)             -> int   current HardHP
	    Lifx::setPlayerHp(charID, hp)         -> void  sets HardHP and persists
	    Lifx::setPlayerSoftHp(charID, hp)     -> void  sets SoftHP and persists
	    Lifx::getPlayerSoftHp(charID)         -> int   current SoftHP

	Slashcommand wiring (in your server's TorqueScript startup):

	    function serverCmdMyHpSet(%client, %charID, %hp) {
	        Lifx::setPlayerHp(%charID, %hp);
	    }

	Then players type `/myhpset 12345 50` and the chat parser dispatches.

	Implementation notes:
	  - charID is the persistent character ID (uint32 from the `character`
	    table). To get it from a logged-in player, use the engine's existing
	    Player::getCharId / GameConnection::getControlObject::getCharId
	    accessors (already exposed to TorqueScript by the engine).
	  - The HP value is clamped by the engine elsewhere; writes outside the
	    normal range may snap back on the client's next state sync.
	  - These commands write directly to CmCharacterInfo and persist via the
	    engine's own SQL helper. They do NOT broadcast a synthetic CharInfo-
	    Event, so the client may take one state-sync tick to see the change.
*/

namespace Lifx
{
	namespace Api
	{
		namespace Character
		{
			// Register all character/player commands with Torque's console.
			// Call once from Hooks::Engine::ConsoleInit after the engine's
			// Con::Init has run (the console must exist before AddCommand).
			void Register();
		}
	}
}
