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

#include "hook_wounds_deal_damage.h"
#include "server/api/lifx_debug.h"

__CM_INSTATNTIATE(_Wounds_DealDamage);

std::atomic<unsigned long long> Hooks::WoundsDealDamage::g_callCount{0};
void* Hooks::WoundsDealDamage::g_lastSelf      = nullptr;
int   Hooks::WoundsDealDamage::g_lastBodyPart  = -1;

void Hooks::WoundsDealDamage::Call(void* self, int bodyPart)
{
	const auto n = ++g_callCount;
	g_lastSelf = self;
	g_lastBodyPart = bodyPart;

	if (Lifx::Debug::Enabled() && n <= 50) {
		Con::Echo("[lifx-wound] #%llu  bodyPart=%d",
		          (unsigned long long)n, bodyPart);
	}

	_Wounds_DealDamage(self, bodyPart);
}
