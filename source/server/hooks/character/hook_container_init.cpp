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

#include "hook_container_init.h"
#include "server/cm_offsets.h"
#include "server/api/lifx_hostile.h"   // OnGraveContainerCaptured (#145 event-driven fill)

#include <mutex>
#include <cstdint>

__CM_INSTATNTIATE(_Container_TryInit);

namespace
{
	// CmServerInventoryContainer layout (verified in the tryInit decomp):
	constexpr unsigned kInitGuardOff = 0x14;   // u8: 0 => tryInit loads; nonzero => no-op (cached)
	constexpr unsigned kMidOff       = 0x18;   // u32: container's own ID (mID)
	constexpr unsigned kTypeOff      = 0x1c;   // u32: ObjectTypeID (loaded by tryInit)
	constexpr uint32_t kGraveType    = 1070;   // lootstone/grave container ObjectTypeID

	std::mutex g_mtx;
	void*      g_freshestGrave    = nullptr;   // last grave container that tryInit'd
	uint32_t   g_freshestGraveMid = 0;
	uint32_t   g_captureGen       = 0;         // bumped on every grave capture
}

unsigned long long __fastcall Hooks::ContainerInit::OnTryInit(void* container)
{
	// Run the real load first so the container's ObjectTypeID (+0x1c) / mID (+0x18)
	// are populated before we inspect them.
	const unsigned long long r = _Container_TryInit(container);
	if (container) {
		const uint32_t type = *reinterpret_cast<uint32_t*>(static_cast<char*>(container) + kTypeOff);
		if (type == kGraveType) {
			const uint32_t mid = *reinterpret_cast<uint32_t*>(static_cast<char*>(container) + kMidOff);
			bool isNew;
			{
				std::lock_guard<std::mutex> lk(g_mtx);
				isNew = (mid != g_freshestGraveMid);   // the engine re-tryInits a grave on every
				g_freshestGrave    = container;        // scope/access — only the first sighting is news
				g_freshestGraveMid = mid;
				++g_captureGen;
			}
			// Log only on first sighting of this grave (was ~120x/grave of spam otherwise).
			if (isNew)
				Con::Echo("[lifx-loot] tryInit captured GRAVE container mID=%u type=%u ptr=%p. #145",
				          mid, type, container);
			// Drive the armed worn-loot fill NOW (outside our lock; it re-enters this
			// module via ReloadFreshestGraveContainer). No-op unless a fill is armed,
			// and self-clearing so the grave's later re-inits don't double-fill.
			Lifx::Api::Hostile::OnGraveContainerCaptured(mid);
		}
	}
	return r;
}

uint32_t Hooks::ContainerInit::GraveCaptureGen()
{
	std::lock_guard<std::mutex> lk(g_mtx);
	return g_captureGen;
}

uint32_t Hooks::ContainerInit::FreshestGraveMid()
{
	std::lock_guard<std::mutex> lk(g_mtx);
	return g_freshestGraveMid;
}

bool Hooks::ContainerInit::ReloadFreshestGraveContainer(uint32_t expectMid)
{
	void*    c;
	uint32_t mid;
	{
		std::lock_guard<std::mutex> lk(g_mtx);
		c   = g_freshestGrave;
		mid = g_freshestGraveMid;
	}
	if (!c) {
		Con::Warning("[lifx-loot] reload: no grave container captured yet (the grave's container "
		             "may not tryInit until a restart — see whether a 'captured GRAVE container' line "
		             "appeared this session). #145");
		return false;
	}
	if (expectMid && mid != expectMid)
		Con::Warning("[lifx-loot] reload: freshest captured grave mID=%u != expected %u — reloading freshest.",
		             mid, expectMid);

	// Clear the "already-initialized" guard so the original tryInit re-runs its
	// DB load, then call the ORIGINAL (trampoline) directly — not the hook — so we
	// don't re-capture/recurse. The grave container is empty in-memory, so the
	// reload's item-adds can't duplicate.
	*(static_cast<uint8_t*>(c) + kInitGuardOff) = 0;
	const unsigned long long rr = _Container_TryInit(c);
	Con::Echo("[lifx-loot] reloaded grave container mID=%u ptr=%p -> tryInit=%llu. Loot should now be live. #145",
	          mid, c, (unsigned long long)rr);
	return true;
}
