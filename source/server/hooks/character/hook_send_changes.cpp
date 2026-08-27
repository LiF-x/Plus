/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "hook_send_changes.h"
#include "server/api/lifx_debug.h"

#include <cstring>

__CM_INSTATNTIATE(_CharInfo_SendChanges);

std::atomic<unsigned long long> Hooks::CharInfoSendChanges::g_callCount{0};
void* Hooks::CharInfoSendChanges::g_lastSelf = nullptr;
unsigned Hooks::CharInfoSendChanges::g_lastMask = 0;
std::atomic<unsigned> Hooks::CharInfoSendChanges::g_observedMaskUnion{0};

namespace
{
	// Window we snapshot — covers HardHP (+0x194), SoftHP (+0x19C), and a
	// generous band on either side in case HP-shaped fields live nearby.
	constexpr unsigned kBegin = 0x180;
	constexpr unsigned kEnd   = 0x200;
	constexpr unsigned kCount = (kEnd - kBegin) / 4;   // int32 stride

	struct Snap {
		int fields[kCount];
		bool valid = false;
	};

	void Capture(void* self, Snap& s)
	{
		auto base = static_cast<char*>(self);
		for (unsigned i = 0; i < kCount; ++i) {
			s.fields[i] = *reinterpret_cast<int*>(base + kBegin + i * 4);
		}
		s.valid = true;
	}

	// Decode the mask as a bit list for human reading.
	void EchoMaskBits(unsigned mask)
	{
		if (!mask) { Con::Echo("  mask bits: (none — mask==0)"); return; }
		char buf[256];
		buf[0] = 0;
		for (int b = 0; b < 32; ++b) {
			if (mask & (1u << b)) {
				char tmp[8];
				std::snprintf(tmp, sizeof(tmp), "%d ", b);
				std::strncat(buf, tmp, sizeof(buf) - std::strlen(buf) - 1);
			}
		}
		Con::Echo("  mask bits: %s", buf);
	}
}

void Hooks::CharInfoSendChanges::Call(void* self, unsigned mask, char sendNow)
{
	const auto n = ++g_callCount;
	g_lastSelf = self;
	g_lastMask = mask;
	g_observedMaskUnion.fetch_or(mask);

	if (!Lifx::Debug::Enabled()) {
		_CharInfo_SendChanges(self, mask, sendNow);
		return;
	}

	Snap before{}, after{};
	if (self) Capture(self, before);

	const auto unionBefore = g_observedMaskUnion.load();
	const bool logEntry = (n <= 20) || ((mask & ~unionBefore) != 0);

	if (logEntry) {
		Con::Echo("[lifx-send] #%llu  self=%p  mask=0x%08X  sendNow=%d",
		          (unsigned long long)n, self, mask, (int)sendNow);
		EchoMaskBits(mask);
	}

	_CharInfo_SendChanges(self, mask, sendNow);

	if (self) {
		Capture(self, after);
		// Diff: emit any int32 in the window that changed during the call.
		bool any = false;
		for (unsigned i = 0; i < kCount; ++i) {
			if (before.fields[i] == after.fields[i]) continue;
			if (!any) {
				Con::Echo("[lifx-send] #%llu  self+0x%X..+0x%X fields that changed during call:",
				          (unsigned long long)n, kBegin, kEnd);
				any = true;
			}
			const unsigned off = kBegin + i * 4;
			const int prev = before.fields[i];
			const int cur  = after.fields[i];
			Con::Echo("  +0x%03X (int32):  %d  ->  %d   (delta %+d, ~%+.3f at 1e6 scale)",
			          off, prev, cur, cur - prev, double(cur - prev) / 1e6);
		}
	}
}
