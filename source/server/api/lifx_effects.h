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
	LiFx effects/status inspection commands exposed to TorqueScript.

	Currently exposed (all under the `Lifx::` namespace):

	    Lifx::dumpEffects(charID)  -> void  dump the raw effect-list bytes for
	                                        a character. Used to identify the
	                                        in-memory layout of effect entries
	                                        (effect ID, duration, magnitude, …)
	                                        before we can hook the apply path.

	This is investigation tooling for issue #30. See
	docs/effects_and_abilities.md §"Resurrection sickness" for the runtime-
	instrumentation plan this command is step 2 of.
*/

namespace Lifx
{
	namespace Api
	{
		namespace Effects
		{
			void Register();
		}
	}
}
