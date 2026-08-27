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

#include "hook_calc_hit_damage.h"

#include <cstring>

__CM_INSTATNTIATE(_Char_CalcHitDamage);

std::atomic<unsigned long long> Hooks::CharCalcHitDamage::g_callCount{0};
void* Hooks::CharCalcHitDamage::g_lastSelf = nullptr;

namespace
{
	// Take a snapshot of the byte range we suspect holds HP state. We log
	// the int64 view (since RoundedFloat<1e6,...> is int64-backed) so any
	// HP-shaped changes are easy to spot.
	struct Snapshot
	{
		long long fields[20];   // covers +0x280..+0x320 in 8-byte strides
		bool valid = false;
	};

	static constexpr unsigned kBaseOffset = 0x280;
	static constexpr unsigned kStride     = 8;

	void Capture(void* self, Snapshot& snap)
	{
		auto base = static_cast<char*>(self);
		for (int i = 0; i < 20; ++i) {
			snap.fields[i] = *reinterpret_cast<long long*>(base + kBaseOffset + i * kStride);
		}
		snap.valid = true;
	}

	void LogDiff(void* self, const Snapshot& before, const Snapshot& after)
	{
		Con::Echo("[lifx-hit] self=%p — fields that CHANGED:", self);
		bool any = false;
		for (int i = 0; i < 20; ++i) {
			if (before.fields[i] == after.fields[i]) continue;
			const auto offset = kBaseOffset + i * kStride;
			const auto deltaScaled = double(after.fields[i] - before.fields[i]) / 1000000.0;
			Con::Echo("  +0x%X (%d):  %lld  ->  %lld   (delta %lld, ~%.3f at 1e6 scale)",
			          offset, offset,
			          before.fields[i], after.fields[i],
			          after.fields[i] - before.fields[i],
			          deltaScaled);
			any = true;
		}
		if (!any) Con::Echo("  (no int64 fields in +0x280..+0x320 changed)");
	}
}

void* Hooks::CharCalcHitDamage::Call(void* self, void* out, int* result, void* attacker, void* extra)
{
	const auto count = ++g_callCount;
	if (self) g_lastSelf = self;

	// Snapshot before the engine runs the damage calc.
	Snapshot before{}, after{};
	if (self) Capture(self, before);

	// Log entry args for the first few calls so we can see the engine's
	// natural call pattern even if no fields change in our snapshot window.
	if (count <= 10) {
		Con::Echo("[lifx-hit] #%llu  self=%p  out=%p  result=%p  attacker=%p  extra=%p",
		          (unsigned long long)count, self, out, (void*)result, attacker, extra);
	}

	void* ret = _Char_CalcHitDamage(self, out, result, attacker, extra);

	if (self) {
		Capture(self, after);
		// Did any of our watched fields change as a result of this call?
		bool anyChanged = false;
		for (int i = 0; i < 20; ++i) {
			if (before.fields[i] != after.fields[i]) { anyChanged = true; break; }
		}
		if (anyChanged) LogDiff(self, before, after);
	}
	return ret;
}
