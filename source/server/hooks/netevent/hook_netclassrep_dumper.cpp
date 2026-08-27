/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "hook_netclassrep_dumper.h"
#include "sector_handoff_event.h"
#include "server/api/t3d_console.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#include <psapi.h>
#pragma comment(lib, "psapi.lib")

__CM_INSTATNTIATE(_NetClassRep_Add);
__CM_INSTATNTIATE(_ServerUUIDEvent_Send);

namespace
{
	std::mutex                    g_logMu;
	std::ofstream                 g_log;
	std::atomic<unsigned>         g_repCount{0};
	std::atomic<unsigned>         g_liveRepCount{0};
	std::atomic<bool>             g_sinkLogged{false};

	std::uintptr_t                g_modBase = 0;
	std::size_t                   g_modSize = 0;

	void EnsureModuleBounds()
	{
		if (g_modBase != 0) return;
		HMODULE h = GetModuleHandleA(nullptr);
		MODULEINFO mi{};
		if (h && GetModuleInformation(GetCurrentProcess(), h, &mi, sizeof(mi)))
		{
			g_modBase = reinterpret_cast<std::uintptr_t>(mi.lpBaseOfDll);
			g_modSize = mi.SizeOfImage;
		}
	}

	// Returns the qword's RVA if it falls inside the loaded module's image,
	// else 0. The caller treats 0 as "not an internal pointer".
	std::uintptr_t RvaInsideModule(std::uintptr_t p)
	{
		EnsureModuleBounds();
		if (g_modBase == 0) return 0;
		if (p < g_modBase || p >= g_modBase + g_modSize) return 0;
		return p - g_modBase;
	}

	// Page-level executable test. Used to flag pointers that land in .text
	// so the reader can immediately spot pack/unpack/factory candidates.
	bool IsExecutable(std::uintptr_t p)
	{
		MEMORY_BASIC_INFORMATION mbi{};
		if (VirtualQuery(reinterpret_cast<LPCVOID>(p), &mbi, sizeof(mbi)) == 0) return false;
		DWORD prot = mbi.Protect & 0xFF;
		return prot == PAGE_EXECUTE
		    || prot == PAGE_EXECUTE_READ
		    || prot == PAGE_EXECUTE_READWRITE
		    || prot == PAGE_EXECUTE_WRITECOPY;
	}

	void OpenLogOnce()
	{
		std::lock_guard<std::mutex> lk(g_logMu);
		if (g_log.is_open()) return;
		std::error_code ec;
		std::filesystem::create_directories("logs", ec);
		g_log.open("logs/netclassrep_dump.log", std::ios::app);
		if (g_log.is_open())
		{
			g_log << "// === LiFx netclassrep dumper open ===\n";
			g_log.flush();
		}
	}

	// Dump the first `qwords` qwords starting at `p`, one per line:
	//   +0xNN: 0xHHHHHHHHHHHHHHHH  [RVA 0xXXXX, EXEC]
	// EXEC tag means the page is executable, i.e. the qword could plausibly
	// be a function pointer.
	void DumpRegion(const char* tag, void* p, std::size_t qwords)
	{
		auto* base = reinterpret_cast<std::uintptr_t*>(p);
		std::lock_guard<std::mutex> lk(g_logMu);
		if (!g_log.is_open()) return;
		g_log << tag << " @ 0x" << std::hex << reinterpret_cast<std::uintptr_t>(p) << std::dec << "\n";
		for (std::size_t i = 0; i < qwords; ++i)
		{
			std::uintptr_t v = base[i];
			std::uintptr_t rva = RvaInsideModule(v);
			char line[160];
			if (rva)
			{
				std::snprintf(line, sizeof(line),
					"  +0x%02zX: 0x%016llX  [RVA 0x%llX%s]\n",
					i * 8, (unsigned long long)v, (unsigned long long)rva,
					IsExecutable(v) ? ", EXEC" : "");
			}
			else
			{
				std::snprintf(line, sizeof(line),
					"  +0x%02zX: 0x%016llX\n",
					i * 8, (unsigned long long)v);
			}
			g_log << line;
		}
		g_log.flush();
	}
} // namespace

void Hooks::NetEvent::NetClassRepAdd(void* rep)
{
	// 1) Original call still has to run so the engine's classRep list stays
	//    intact. Defer it until after we've snapshotted so a later thread
	//    can't free `rep` before we read it.
	OpenLogOnce();

	unsigned live = g_liveRepCount.fetch_add(1, std::memory_order_relaxed) + 1;
	if (live <= 20) // we already snapshotted existing entries on attach
	{
		char tag[64];
		std::snprintf(tag, sizeof(tag), "[netclassrep] live #%u", live);
		DumpRegion(tag, rep, 0x10); // 0x10 qwords = 0x80 bytes
	}

	_NetClassRep_Add(rep);
}

void Hooks::NetEvent::DumpExistingList()
{
	OpenLogOnce();
	EnsureModuleBounds();
	if (g_modBase == 0) return;

	// List head was identified statically at module_base + 0xBC00B0.
	// next-ptr lives at offset +0x50 on each rep. A defensive walk cap
	// keeps a corrupted list from spinning forever.
	auto** headSlot = reinterpret_cast<void**>(g_modBase + 0xBC00B0);
	void* node = *headSlot;
	{
		std::lock_guard<std::mutex> lk(g_logMu);
		if (g_log.is_open())
		{
			char hdr[160];
			std::snprintf(hdr, sizeof(hdr),
				"[netclassrep] walking existing list, head=0x%llX -> first=0x%llX\n",
				(unsigned long long)reinterpret_cast<std::uintptr_t>(headSlot),
				(unsigned long long)reinterpret_cast<std::uintptr_t>(node));
			g_log << hdr;
			g_log.flush();
		}
	}

	unsigned n = 0;
	while (node && n < 256)
	{
		++n;
		char tag[64];
		std::snprintf(tag, sizeof(tag), "[netclassrep] existing #%u", n);
		DumpRegion(tag, node, 0x10);
		void* next = *reinterpret_cast<void**>(
			reinterpret_cast<std::uintptr_t>(node) + 0x50);
		if (next == node) break; // cycle defensive
		node = next;
	}

	std::lock_guard<std::mutex> lk(g_logMu);
	if (g_log.is_open())
	{
		char tail[80];
		std::snprintf(tail, sizeof(tail), "[netclassrep] existing list walk complete, %u entries\n", n);
		g_log << tail;
		g_log.flush();
	}
	g_repCount.store(n, std::memory_order_relaxed);
}

void Hooks::NetEvent::ServerUUIDEventSend(void* conn)
{
	// First call only — log the sink at conn+0x1F8 plus the first 8 qwords
	// of its vtable, then chain to the original.
	if (!g_sinkLogged.exchange(true, std::memory_order_acq_rel))
	{
		OpenLogOnce();
		void* sink = nullptr;
		if (conn)
		{
			sink = *reinterpret_cast<void**>(
				reinterpret_cast<std::uintptr_t>(conn) + 0x1F8);
		}
		// IMPORTANT: don't hold g_logMu across DumpRegion — DumpRegion
		// re-acquires it internally, and std::mutex is non-recursive. An
		// earlier version deadlocked the engine's event thread here.
		{
			std::lock_guard<std::mutex> lk(g_logMu);
			if (g_log.is_open())
			{
				char hdr[160];
				std::snprintf(hdr, sizeof(hdr),
					"[netevent-sink] conn=0x%llX  sink (conn+0x1F8)=0x%llX\n",
					(unsigned long long)reinterpret_cast<std::uintptr_t>(conn),
					(unsigned long long)reinterpret_cast<std::uintptr_t>(sink));
				g_log << hdr;
				g_log.flush();
			}
		}
		// `sink` is the value at conn+0x1F8. In this binary that value
		// is itself an .rdata vtable address (embedded secondary-base
		// subobject in MSVC multi-inheritance), so the slots we want to
		// dump live AT sink, not at *sink — read the slots directly.
		if (sink)
		{
			DumpRegion("[netevent-sink] sink-vtable", sink, 0x10);
		}
	}

	_ServerUUIDEvent_Send(conn);

	// SectorHandoff auto-post — runs AFTER the original. Gated on the
	// config flag at runtime so the dumper itself stays standalone.
	// Lives here (rather than in a second hook) because Detours can't
	// stack two attaches on the same fn within one transaction — doing
	// so corrupted the trampoline chain and stack-overflowed the engine
	// (see issue #62 follow-up).
	if (conn && gServer.GetConfig().SectorHandoffAutoPost)
	{
		// Dispatcher-only path. We deliberately do NOT call PostTo here
		// because postNetEvent routes our event through the engine's
		// send drainer, which calls our padded vt[10] stub for the
		// classId-write step. The stub returns 0, the wire gets a junk
		// classId, and any real client on this connection rejects the
		// packet with "Invalid packet. (bad event class id: 1)" — and
		// then the client gets kicked. RE'ing vt[10] is chunk 9+.
		Hooks::SectorHandoff::TriggerSampleForward();
	}
}
