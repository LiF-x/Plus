#pragma once

#include <atomic>

// Global toggle for verbose per-tick / per-event LiFx echoes.
// Default OFF; flip via Lifx::setDebug(1) at runtime. Init banners,
// warnings, and explicit dump commands print unconditionally.
namespace Lifx { namespace Debug {
	extern std::atomic<bool> g_enabled;
	inline bool Enabled() { return g_enabled.load(std::memory_order_relaxed); }
}}

#define LIFX_DBG(...) do { if (::Lifx::Debug::Enabled()) Con::Echo(__VA_ARGS__); } while(0)
