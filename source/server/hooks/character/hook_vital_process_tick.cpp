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

#include "hook_vital_process_tick.h"
#include "server/api/lifx_debug.h"
#include "server/hooks/character/hook_setanimation.h"   // AnimRemap::OnHitTick (#154 contact-frame hit)
#include "server/hooks/furnace/engine_internals.h"
#include "server/hooks/sector/sector_edge_trigger.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <unordered_map>

__CM_INSTATNTIATE(_VitalParams_ProcessTick);

void* Hooks::VitalParams::g_lastSeen = nullptr;
std::atomic<unsigned long long> Hooks::VitalParams::g_callCount{0};

namespace {
	struct Snapshot {
		void* obj = nullptr;
		long long f_2D8 = 0;
		long long f_2E0 = 0;
		long long f_2F8 = 0;
		std::chrono::steady_clock::time_point lastLogged{};
	};
	thread_local Snapshot tlSnap;

	bool ShouldLog(Snapshot& snap, void* obj, long long a, long long b, long long c) {
		if (snap.obj != obj) return true;
		if (snap.f_2D8 != a || snap.f_2E0 != b || snap.f_2F8 != c) return true;
		auto now = std::chrono::steady_clock::now();
		if (now - snap.lastLogged > std::chrono::seconds(1)) return true;
		return false;
	}
}

void Hooks::VitalParams::ProcessTick(void* self)
{
	const auto count = ++g_callCount;

	if (count <= 3) {
		LIFX_DBG("[lifx-vp] hook fired #%llu  self=%p", (unsigned long long)count, self);
	}

	if (self) {
		Hooks::VitalParams::Internal_RegisterCharStats(self);

		auto base = static_cast<char*>(self);
		long long a = *reinterpret_cast<long long*>(base + 0x2D8);
		long long b = *reinterpret_cast<long long*>(base + 0x2E0);
		long long c = *reinterpret_cast<long long*>(base + 0x2F8);

		if (!g_lastSeen) g_lastSeen = self;

		if (Lifx::Debug::Enabled() && ShouldLog(tlSnap, self, a, b, c)) {
			Con::Echo("[lifx-vp] obj=%p  +2D8=%lld  +2E0=%lld  +2F8=%lld  (effective=%.3f)",
			          self, a, b, c, double(c - a + b) / 1000000.0);
			tlSnap.obj = self;
			tlSnap.f_2D8 = a;
			tlSnap.f_2E0 = b;
			tlSnap.f_2F8 = c;
			tlSnap.lastLogged = std::chrono::steady_clock::now();
		}

		// Wide scan: every ~1s, dump every non-zero int64 in +0x100..+0x400
		// for this object. Only for the FIRST object we ever saw, so multiple
		// players + NPCs don't drown the log. Also tracks deltas vs previous
		// dump so we can immediately spot which fields changed during the
		// window (e.g. a single hit).
		static std::atomic<long long> g_wideLastNs{0};
		static long long g_wideSnap[(0x400 - 0x100) / 8]{};
		static bool g_wideSnapValid = false;

		if (Lifx::Debug::Enabled() && self == g_lastSeen) {
			const auto now = std::chrono::steady_clock::now();
			const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
				now.time_since_epoch()).count();
			long long lastNs = g_wideLastNs.load();
			if (nowNs - lastNs > 1'000'000'000LL &&
			    g_wideLastNs.compare_exchange_strong(lastNs, nowNs))
			{
				Con::Echo("[lifx-vp-wide] obj=%p  scan +0x100..+0x400 (non-zero, ^ = changed):",
				          self);
				int idx = 0;
				for (unsigned off = 0x100; off < 0x400; off += 8, ++idx) {
					const auto v = *reinterpret_cast<long long*>(base + off);
					const auto prev = g_wideSnap[idx];
					const bool changed = g_wideSnapValid && prev != v;
					if (v != 0 || changed) {
						Con::Echo("  %c +0x%03X = %lld (%.3f)",
						          changed ? '^' : ' ',
						          off, v, double(v) / 1000000.0);
					}
					g_wideSnap[idx] = v;
				}
				g_wideSnapValid = true;
			}
		}
	}
	// Chunk 14 (#101): drive the world-coord edge detector off this tick.
	// Internally throttled to ~4 Hz so per-character firing-rate doesn't
	// matter. Reads pos from Hooks::SetControlObject::LastControlledPlayer.
	Hooks::SectorEdge::OnTick();

	// #154: fire any bandit swing whose contact frame has arrived (main-thread heartbeat).
	Hooks::AnimRemap::OnHitTick();

	_VitalParams_ProcessTick(self);
}

// Manual baseline (Lifx::vitalMark + Lifx::vitalDiff).
namespace {
	constexpr unsigned kScanBeg = 0x100;
	constexpr unsigned kScanEnd = 0x400;
	constexpr unsigned kScanCnt = (kScanEnd - kScanBeg) / 8;
	long long g_markSnap[kScanCnt]{};
	bool      g_markValid = false;
}

void Hooks::VitalParams::Mark()
{
	void* obj = g_lastSeen;
	if (!obj) {
		Con::Warning("Lifx::vitalMark: no VitalParameters object seen yet");
		return;
	}
	auto base = static_cast<char*>(obj);
	for (unsigned i = 0; i < kScanCnt; ++i) {
		g_markSnap[i] = *reinterpret_cast<long long*>(base + kScanBeg + i * 8);
	}
	g_markValid = true;
	Con::Echo("[lifx-vp] baseline marked on obj=%p  (%u int64 fields snapshotted)",
	          obj, kScanCnt);
}

// charID → charStats registry. Populated every Process_tick by reading the
// charID at +0x109C on the charStats sub-object (= Player + 0x1B44).
namespace {
	std::mutex g_regMutex;
	std::unordered_map<uint32_t, void*> g_registry;
}

namespace Hooks { namespace VitalParams {
	void Internal_RegisterCharStats(void* charStats) {
		const uint32_t cid = *reinterpret_cast<uint32_t*>(
			static_cast<char*>(charStats) + ::Engine::kCharIdOffOnCharStats);
		// Filter out garbage: 0 means uninitialized, very large means
		// we're reading some other field (e.g. just-allocated struct).
		if (cid == 0 || cid > 0x40000000u) return;
		std::lock_guard<std::mutex> lk(g_regMutex);
		g_registry[cid] = charStats;
	}
}}

void* Hooks::VitalParams::LookupCharStats(uint32_t charID) {
	std::lock_guard<std::mutex> lk(g_regMutex);
	auto it = g_registry.find(charID);
	return it == g_registry.end() ? nullptr : it->second;
}

void Hooks::VitalParams::DumpRegistry() {
	std::lock_guard<std::mutex> lk(g_regMutex);
	Con::Echo("[lifx-vp] charID→charStats registry (%zu entries):", g_registry.size());
	for (auto& [cid, cs] : g_registry) {
		const long long hardRaw = ::Engine::HardHpRaw(cs);
		const long long softRaw = ::Engine::SoftHpRaw(cs);
		const long long hardMax = ::Engine::HardHpMaxRaw(cs);
		const long long softMax = ::Engine::SoftHpMaxRaw(cs);
		Con::Echo("  charID=%u  charStats=%p  hardHp=%lld/%lld (raw %lld, max %lld)  softHp=%lld/%lld (raw %lld, max %lld)",
		          cid, cs,
		          ::Engine::HardHpDisplay(cs), hardMax / ::Engine::kHpScale, hardRaw, hardMax,
		          ::Engine::SoftHpDisplay(cs), softMax / ::Engine::kHpScale, softRaw, softMax);

	}
}

void Hooks::VitalParams::Diff()
{
	void* obj = g_lastSeen;
	if (!obj) {
		Con::Warning("Lifx::vitalDiff: no VitalParameters object seen yet");
		return;
	}
	if (!g_markValid) {
		Con::Warning("Lifx::vitalDiff: no baseline — call Lifx::vitalMark first");
		return;
	}
	auto base = static_cast<char*>(obj);
	Con::Echo("[lifx-vp-diff] obj=%p  fields that changed since vitalMark:", obj);
	int changed = 0;
	for (unsigned i = 0; i < kScanCnt; ++i) {
		const unsigned off = kScanBeg + i * 8;
		const long long cur = *reinterpret_cast<long long*>(base + off);
		const long long prev = g_markSnap[i];
		if (cur == prev) continue;
		const long long delta = cur - prev;
		Con::Echo("  +0x%03X:  %lld  ->  %lld   (delta %+lld, ~%+.3f at 1e6 scale)",
		          off, prev, cur, delta, double(delta) / 1000000.0);
		++changed;
	}
	if (!changed) Con::Echo("  (no changes)");
}
