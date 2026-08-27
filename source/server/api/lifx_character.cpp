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

#include "lifx_character.h"
#include "lifx_debug.h"
#include "server/cm_server.h"
#include "server/hooks/furnace/engine_internals.h"
#include "server/hooks/character/hook_vital_process_tick.h"
#include "server/hooks/character/hook_wounds_deal_damage.h"
#include "server/hooks/character/hook_apply_damage.h"
#include "server/hooks/character/hook_onepunchman.h"

#include <cstdio>
#include <cstdlib>

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------
namespace
{
	int* HardHpField(void* charInfo)
	{
		return reinterpret_cast<int*>(static_cast<char*>(charInfo)
		                              + ::Engine::Off::CharInfo_HardHP);
	}
	int* SoftHpField(void* charInfo)
	{
		return reinterpret_cast<int*>(static_cast<char*>(charInfo)
		                              + ::Engine::Off::CharInfo_SoftHP);
	}

	// Parse a uint32 charID from a TorqueScript argv slot. Returns 0 if the
	// arg is missing or unparseable — callers should treat 0 as "invalid".
	uint32_t ParseCharId(int argc, const char* argv[], int index)
	{
		if (index >= argc || argv[index] == nullptr) return 0;
		return static_cast<uint32_t>(std::strtoul(argv[index], nullptr, 10));
	}

	int ParseInt(int argc, const char* argv[], int index)
	{
		if (index >= argc || argv[index] == nullptr) return 0;
		return std::atoi(argv[index]);
	}
}

// ----------------------------------------------------------------------------
// Command bodies. Each matches the relevant Con::*Callback signature from
// source/server/cm_constants.h.
//
//   IntCallback     S32 (*)(LPVOID obj, S32 argc, const char* argv[])
//   StringCallback  const char* (*)(LPVOID obj, S32 argc, const char* argv[])
//   VoidCallback    void (*)(LPVOID obj, S32 argc, const char* argv[])
//
// argv[0] is always the command name; real args start at argv[1].
// ----------------------------------------------------------------------------
namespace
{
	// HP is stored internally as `display_value * 1,000,000` (see
	// engine_internals.h::kHpScale). Our get/set callbacks talk in display
	// units so TorqueScript sees natural numbers like 50 or 100.
	S32 GetPlayerHp(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID = ParseCharId(argc, argv, 1);
		void* ci = ::Engine::Character_GetByID(charID);
		if (!ci) {
			Con::Warning("Lifx::getPlayerHp: charID %u not found", charID);
			return 0;
		}
		return static_cast<S32>(*HardHpField(ci) / ::Engine::kHpScale);
	}

	S32 GetPlayerSoftHp(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID = ParseCharId(argc, argv, 1);
		void* ci = ::Engine::Character_GetByID(charID);
		if (!ci) {
			Con::Warning("Lifx::getPlayerSoftHp: charID %u not found", charID);
			return 0;
		}
		return static_cast<S32>(*SoftHpField(ci) / ::Engine::kHpScale);
	}

	// Memory-only write. No persist, no broadcast. Used to isolate whether
	// the write is sticking at all.
	void SetPlayerHpMemoryOnly(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID = ParseCharId(argc, argv, 1);
		const auto newHp  = ParseInt(argc, argv, 2);
		void* ci = ::Engine::Character_GetByID(charID);
		if (!ci) {
			Con::Warning("Lifx::setPlayerHpMemoryOnly: charID %u not found", charID);
			return;
		}
		const int rawBefore = *HardHpField(ci);
		*HardHpField(ci) = newHp * ::Engine::kHpScale;
		const int rawAfter = *HardHpField(ci);
		Con::Echo("Lifx::setPlayerHpMemoryOnly: charID=%u  before=%d  wrote=%d  after=%d",
		          charID, rawBefore, newHp * ::Engine::kHpScale, rawAfter);
	}

	// Write + persist only (no broadcast). Tests whether persist is what
	// resets the in-memory value.
	void SetPlayerHpPersistOnly(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID = ParseCharId(argc, argv, 1);
		const auto newHp  = ParseInt(argc, argv, 2);
		void* ci = ::Engine::Character_GetByID(charID);
		if (!ci) {
			Con::Warning("Lifx::setPlayerHpPersistOnly: charID %u not found", charID);
			return;
		}
		*HardHpField(ci) = newHp * ::Engine::kHpScale;
		::Engine::Character_PersistHp(ci);
		const int rawAfter = *HardHpField(ci);
		Con::Echo("Lifx::setPlayerHpPersistOnly: charID=%u  wrote=%d  after persist=%d",
		          charID, newHp * ::Engine::kHpScale, rawAfter);
	}

	// Write + broadcast only (no DB persist).
	//
	// We can't use Character_PersistHp because it's a save-AND-reload — it
	// reloads from the DB after writing, clobbering our in-memory value with
	// the stale row. (Confirmed empirically via setPlayerHpPersistOnly: raw
	// value goes from 50,000,000 -> 105,000,000 across the call.)
	//
	// Memory write + immediate broadcast is what gameplay HP changes use
	// internally; this matches that contract. The change is in-memory only,
	// so it will be lost when the natural save cycle later persists the live
	// state (which by then equals whatever the engine recomputed). For
	// session-level overrides — admin "set HP to X" commands — that's
	// usually what you want anyway, because saving direct overrides into the
	// DB risks racing with the engine's own save-from-derived path.
	void SetPlayerHp(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID = ParseCharId(argc, argv, 1);
		const auto newHp  = ParseInt(argc, argv, 2);
		void* ci = ::Engine::Character_GetByID(charID);
		if (!ci) {
			Con::Warning("Lifx::setPlayerHp: charID %u not found", charID);
			return;
		}
		*HardHpField(ci) = newHp * ::Engine::kHpScale;
		::Engine::Character_SendChanges(ci, 0xFFFFFFFFu, /*sendNow*/ 1);
		const int rawAfter = *HardHpField(ci);
		Con::Echo("Lifx::setPlayerHp: charID=%u  wrote=%d  after broadcast=%d",
		          charID, newHp * ::Engine::kHpScale, rawAfter);
	}

	void SetPlayerSoftHp(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID = ParseCharId(argc, argv, 1);
		const auto newSoftHp = ParseInt(argc, argv, 2);
		void* ci = ::Engine::Character_GetByID(charID);
		if (!ci) {
			Con::Warning("Lifx::setPlayerSoftHp: charID %u not found", charID);
			return;
		}
		*SoftHpField(ci) = newSoftHp * ::Engine::kHpScale;
		::Engine::Character_SendChanges(ci, 0xFFFFFFFFu, /*sendNow*/ 1);
		Con::Echo("Lifx::setPlayerSoftHp: charID=%u SoftHP=%d", charID, newSoftHp);
	}

	// ============================================================================
	// LIVE HP path — via TorqueScript ShapeBase::applyDamage
	// ============================================================================
	//
	// The engine's actual HP-for-gameplay lives on the Player ShapeBase (not on
	// CmCharacterInfo). The standard primitive for changing it is
	//     ShapeBase::applyDamage(F32 amount, SimObjectId hitAuthor)
	// which is already exposed to TorqueScript via `fnShapeBase_applyDamage`.
	// applyDamage:
	//   - positive amount: damages the player (HP -= amount)
	//   - negative amount: heals the player (HP += -amount), clamped at full
	//   - broadcasts to the owning client automatically
	//
	// We orchestrate the player lookup + applyDamage call from script because
	// TorqueScript already has the right primitives (ClientGroup iteration,
	// getControlObject), and using them avoids inventing yet another C++ helper.
	//
	// The script template below: iterate ClientGroup, find the connection whose
	// charID matches, get its control object (the Player ShapeBase), and call
	// applyDamage. Logs "[LiFx] applyDamage: no player for charID N" if no
	// connected player has that charID.

	// TS template for "find connected player by charID and applyDamage".
	//
	// Method name on GameConnection is getCharacterId() (verified against the
	// engine's fnGameConnection_getCharacterId binding at 0x23960). My earlier
	// reading of `%c.charID` was wrong — that's a non-existent field and
	// silently returns empty, which never matched.
	//
	// LiF's Player::applyDamage takes (amount, hitAuthor). We pass the player
	// themselves as hitAuthor so the engine's killcam/attribution doesn't get
	// a null reference. Adjust if you want a specific source (e.g., a GM
	// admin's player object).
	// Compact TS template that calls a single named primitive (applyDamage or
	// setDamageLevel) on the player matched by charID. No nested ifs, no
	// inline comments — previous version with both calls + // comments
	// tripped the TS parser. The shape is:
	//
	//   for c in ClientGroup:
	//     if c.getCharacterId() == charID:
	//       p = c.getControlObject()
	//       if isObject(p):
	//         before = p.getDamageLevel()
	//         <PRIMITIVE_CALL>
	//         after  = p.getDamageLevel()
	//         echo before/after
	//
	// The PRIMITIVE_CALL placeholder (%s) gets one of:
	//   "%p.applyDamage(<amount>, %p);"
	//   "%p.setDamageLevel(<newLevel>);"
	const char* kPlayerOpScriptFmt = R"TS(
		%%charID = %u;
		%%done = false;
		if (isObject(ClientGroup)) {
			for (%%i = 0; %%i < ClientGroup.getCount(); %%i++) {
				%%c = ClientGroup.getObject(%%i);
				if (%%c.getCharacterId() == %%charID) {
					%%p = %%c.getControlObject();
					if (isObject(%%p)) {
						%%before = %%p.getDamageLevel();
						%s
						%%after = %%p.getDamageLevel();
						echo("[LiFx] %s: player=" @ %%p @ " before=" @ %%before @ " after=" @ %%after);
						%%done = true;
					}
					break;
				}
			}
		}
		if (!%%done) echo("[LiFx] %s: no player for charID " @ %%charID);
	)TS";

	void ApplyDamageCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID = ParseCharId(argc, argv, 1);
		const auto amount = ParseInt(argc, argv, 2);
		char primitive[128];
		std::snprintf(primitive, sizeof(primitive), "%%p.applyDamage(%d, %%p);", amount);
		char script[2048];
		std::snprintf(script, sizeof(script), kPlayerOpScriptFmt,
		              charID, primitive, "applyDamage", "applyDamage");
		Con::Evaluate(script, /*echo*/ false, /*fileName*/ "<Lifx::applyDamage>");
		Con::Echo("Lifx::applyDamage: charID=%u amount=%d", charID, amount);
	}

	// Convenience: overheal via large negative damage. Engine clamps at 0.
	void HealToFullCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID = ParseCharId(argc, argv, 1);
		const char* primitive = "%p.applyDamage(-999999, %p);";
		char script[2048];
		std::snprintf(script, sizeof(script), kPlayerOpScriptFmt,
		              charID, primitive, "healToFull", "healToFull");
		Con::Evaluate(script, false, "<Lifx::healToFull>");
		Con::Echo("Lifx::healToFull: charID=%u", charID);
	}

	// Sets the player's live HP by computing currentHp - targetHp and
	// applying that as damage. Uses CmCharacterInfo's HardHP as the current
	// reference (the persisted shadow approximates displayed HP).
	void SetPlayerLiveHpCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID  = ParseCharId(argc, argv, 1);
		const auto targetHp = ParseInt(argc, argv, 2);
		void* ci = ::Engine::Character_GetByID(charID);
		if (!ci) {
			Con::Warning("Lifx::setPlayerLiveHp: charID %u not found", charID);
			return;
		}
		const int currentHp = *HardHpField(ci) / ::Engine::kHpScale;
		const int delta = currentHp - targetHp;
		char primitive[128];
		std::snprintf(primitive, sizeof(primitive), "%%p.applyDamage(%d, %%p);", delta);
		char script[2048];
		std::snprintf(script, sizeof(script), kPlayerOpScriptFmt,
		              charID, primitive, "setPlayerLiveHp", "setPlayerLiveHp");
		Con::Evaluate(script, false, "<Lifx::setPlayerLiveHp>");
		Con::Echo("Lifx::setPlayerLiveHp: charID=%u from %d to %d (delta=%d)",
		          charID, currentHp, targetHp, delta);
	}

	// Debug: dump the suspected HP fields on the most-recently-seen
	// CharacterVitalParameters object (captured by hook_vital_process_tick).
	void VitalDumpCb(LPVOID /*obj*/, S32 /*argc*/, const char** /*argv*/)
	{
		const auto count = Hooks::VitalParams::g_callCount.load();
		Con::Echo("Lifx::vitalDump: Process_tick hook has fired %llu times so far",
		          (unsigned long long)count);
		void* vp = Hooks::VitalParams::g_lastSeen;
		if (!vp) {
			if (count == 0) {
				Con::Echo("Lifx::vitalDump: no Process_tick calls have happened — hook may not be attached, or the function isn't being invoked yet. Check that you've restarted the server after deploying the LiFx DLL.");
			} else {
				Con::Echo("Lifx::vitalDump: hook fired but no VitalParameters pointer captured (all calls had self=null)");
			}
			return;
		}
		auto base = static_cast<char*>(vp);
		long long a = *reinterpret_cast<long long*>(base + 0x2D8);
		long long b = *reinterpret_cast<long long*>(base + 0x2E0);
		long long c = *reinterpret_cast<long long*>(base + 0x2F8);
		Con::Echo("Lifx::vitalDump: obj=%p  +2D8=%lld  +2E0=%lld  +2F8=%lld  effective=%.3f",
		          vp, a, b, c, double(c - a + b) / 1000000.0);
	}

	// Debug: write a single int64 field on the last-seen VitalParameters.
	// Use after vitalDump confirms which field moves on damage:
	//   Lifx::vitalPoke(0x2D8, 55000000);   // set damage-taken to 55M (50 HP if maxHp=105)
	void VitalPokeCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		if (argc < 3) { Con::Warning("Lifx::vitalPoke usage: <hexOffset> <int64Value>"); return; }
		void* vp = Hooks::VitalParams::g_lastSeen;
		if (!vp) { Con::Echo("Lifx::vitalPoke: no VitalParameters captured yet"); return; }
		// Always parse the offset as hex. The help text says "hex offset" and
		// users will type things like "188" expecting +0x188; auto-base parsing
		// would interpret a bare "188" as decimal (= 0xBC) and silently write
		// to the wrong place. "0x"-prefixed values still parse correctly.
		const char* offStr = argv[1];
		if (offStr[0] == '0' && (offStr[1] == 'x' || offStr[1] == 'X')) offStr += 2;
		const auto offset = static_cast<unsigned>(std::strtoul(offStr, nullptr, 16));
		const auto value  = static_cast<long long>(std::strtoll(argv[2], nullptr, 0));
		auto base = static_cast<char*>(vp);
		const long long before = *reinterpret_cast<long long*>(base + offset);
		*reinterpret_cast<long long*>(base + offset) = value;
		const long long after = *reinterpret_cast<long long*>(base + offset);
		Con::Echo("Lifx::vitalPoke: obj=%p offset=0x%X before=%lld wrote=%lld after=%lld",
		          vp, offset, before, value, after);
	}

	// Direct setDamageLevel — bypasses Player::applyDamage gating. Use this
	// if applyDamage shows up as a no-op in the diagnostic echoes.
	void SetDamageLevelCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const auto charID = ParseCharId(argc, argv, 1);
		const auto level  = ParseInt(argc, argv, 2);
		char primitive[64];
		std::snprintf(primitive, sizeof(primitive), "%%p.setDamageLevel(%d);", level);
		char script[2048];
		std::snprintf(script, sizeof(script), kPlayerOpScriptFmt,
		              charID, primitive, "setDamageLevel", "setDamageLevel");
		Con::Evaluate(script, false, "<Lifx::setDamageLevel>");
		Con::Echo("Lifx::setDamageLevel: charID=%u level=%d", charID, level);
	}
}

// ----------------------------------------------------------------------------
// CmCharacterWounds::dealDamage — real damage entry point.
// ----------------------------------------------------------------------------
//
// The dealDamage hook (hook_wounds_deal_damage.cpp) captures the last
// `CmCharacterWounds*` the engine processed. We re-invoke dealDamage on that
// pointer to fire a genuine in-engine damage event for the most recently hit
// player. To populate the captured pointer for a given player, that player
// needs to receive at least one real damage tick first (e.g. take 1 fall hit,
// or get punched once).
namespace
{
	void WoundDumpCb(LPVOID /*obj*/, S32 /*argc*/, const char* /*argv*/[])
	{
		void* self = Hooks::WoundsDealDamage::g_lastSelf;
		const auto count = Hooks::WoundsDealDamage::g_callCount.load();
		Con::Echo("[lifx-wound] callCount=%llu  lastSelf=%p  lastBodyPart=%d",
		          (unsigned long long)count, self,
		          Hooks::WoundsDealDamage::g_lastBodyPart);
		if (!self) {
			Con::Echo("  (no wounds pointer captured yet — take 1 hit on a player first)");
		}
	}

	void DealDamageCb(LPVOID /*obj*/, S32 argc, const char* argv[])
	{
		const int bodyPart = ParseInt(argc, argv, 1);
		if (bodyPart < 0 || bodyPart > 5) {
			Con::Warning("Lifx::dealDamage: bodyPart must be 0..5 (head/chest/L-arm/R-arm/L-leg/R-leg)");
			return;
		}
		void* self = Hooks::WoundsDealDamage::g_lastSelf;
		if (!self) {
			Con::Warning("Lifx::dealDamage: no wounds pointer captured yet — take 1 hit first");
			return;
		}
		Con::Echo("[lifx-wound] invoking dealDamage(%p, %d)", self, bodyPart);
		::Engine::Wounds_DealDamage(self, bodyPart);
	}
}

// ----------------------------------------------------------------------------
// Registration. Called by Hooks::Engine::ConsoleInit once Torque's console is
// ready (see source/server/hooks_engine.cpp).
// ----------------------------------------------------------------------------
std::atomic<bool> Lifx::Debug::g_enabled{false};

void Lifx::Api::Character::Register()
{
	Con::AddCommand("Lifx", "setDebug",
	                [](LPVOID, S32 argc, const char* argv[]) {
		if (argc < 2) {
			Con::Echo("Lifx::debug is currently %s", Lifx::Debug::Enabled() ? "ON" : "OFF");
			return;
		}
		const bool on = std::strtol(argv[1], nullptr, 0) != 0;
		Lifx::Debug::g_enabled.store(on, std::memory_order_relaxed);
		Con::Echo("Lifx::debug -> %s", on ? "ON" : "OFF");
	},
	                "(0|1) - toggle verbose per-tick/per-event LiFx debug echoes (default OFF). No arg = report state.",
	                1, 2);

	Con::AddCommand("Lifx", "getPlayerHp",     &GetPlayerHp,
	                "(int charID) - returns the player's current (Hard) HP in display units, or 0 if not found",
	                2, 2);
	Con::AddCommand("Lifx", "getPlayerSoftHp", &GetPlayerSoftHp,
	                "(int charID) - returns the player's current Soft HP cap in display units, or 0 if not found",
	                2, 2);
	Con::AddCommand("Lifx", "setPlayerHpMemoryOnly", &SetPlayerHpMemoryOnly,
	                "(int charID, int hp) - DEBUG: write HP to memory only (no persist, no broadcast). Reports before/after raw values.",
	                3, 3);
	Con::AddCommand("Lifx", "setPlayerHpPersistOnly", &SetPlayerHpPersistOnly,
	                "(int charID, int hp) - DEBUG: write HP and persist to DB (no broadcast). Reports raw value after persist.",
	                3, 3);
	Con::AddCommand("Lifx", "setPlayerHp",     &SetPlayerHp,
	                "(int charID, int hp) - sets the player's current (Hard) HP and persists it",
	                3, 3);
	Con::AddCommand("Lifx", "setPlayerSoftHp", &SetPlayerSoftHp,
	                "(int charID, int softHp) - sets the player's Soft HP cap and persists it",
	                3, 3);

	// Live HP path (via ShapeBase::applyDamage in TorqueScript)
	Con::AddCommand("Lifx", "applyDamage",     &ApplyDamageCb,
	                "(int charID, int amount) - apply damage (negative = heal) to a connected player's live HP",
	                3, 3);
	Con::AddCommand("Lifx", "healToFull",      &HealToFullCb,
	                "(int charID) - heal a connected player to full HP via the engine's natural healing path",
	                2, 2);
	Con::AddCommand("Lifx", "setPlayerLiveHp", &SetPlayerLiveHpCb,
	                "(int charID, int targetHp) - set a connected player's live HP by computing and applying delta damage",
	                3, 3);
	Con::AddCommand("Lifx", "setDamageLevel", &SetDamageLevelCb,
	                "(int charID, int level) - directly set the ShapeBase damage level (bypasses Player::applyDamage gating)",
	                3, 3);

	// Investigative commands — wired alongside the Process_tick telemetry hook.
	Con::AddCommand("Lifx", "vitalDump", &VitalDumpCb,
	                "() - dump suspected HP fields on the last-seen CharacterVitalParameters object",
	                1, 1);
	Con::AddCommand("Lifx", "vitalPoke", &VitalPokeCb,
	                "(hex offset, int64 value) - write a single int64 to the last-seen VitalParameters at the given hex offset",
	                3, 3);

	// CmCharacterWounds::dealDamage — fires the engine's real damage path
	// against the last player that took a hit. See the "CmCharacterWounds"
	// section above.
	Con::AddCommand("Lifx", "woundDump", &WoundDumpCb,
	                "() - show the captured CmCharacterWounds pointer + dealDamage call count",
	                1, 1);
	Con::AddCommand("Lifx", "dealDamage", &DealDamageCb,
	                "(int bodyPart 0..5) - re-invoke CmCharacterWounds::dealDamage on the last-hit player",
	                2, 2);

	// Re-invoke the suspected HP-apply function on the last-captured
	// character-stats pointer with a synthesized damage packet. If this
	// function IS the HP-broadcast step, this will move the HUD.
	Con::AddCommand("Lifx", "applyHpDamage",
	                [](LPVOID, S32 argc, const char* argv[]) {
		void* cs = Hooks::HitApplyDamage::g_lastCharStats;
		if (!cs) { Con::Warning("Lifx::applyHpDamage: no charStats captured yet — take 1 hit first"); return; }
		if (argc < 3) { Con::Warning("usage: Lifx::applyHpDamage(<hardHpDmg>, <softHpDmg>) — values are display HP (e.g. 5 = 5 HP)"); return; }
		const long long hardHp = std::strtoll(argv[1], nullptr, 0) * ::Engine::kHpScale;
		const long long softHp = std::strtoll(argv[2], nullptr, 0) * ::Engine::kHpScale;
		// Build a 48-byte stack packet mirroring the engine's local_568
		// layout: hardHp at +0x00, softHp at +0x08, the rest zeroed.
		long long pkt[6] = { hardHp, softHp, 0, 0, 0, 0 };
		LIFX_DBG("[lifx-apply] manual invoke: charStats=%p  hardHp=%lld softHp=%lld",
		          cs, hardHp, softHp);
		::Engine::AtRva<void(__fastcall*)(void*, void*)>(CmOffset::HIT_APPLY_DAMAGE)(cs, pkt);
	},
	                "(int hardHp, int softHp) - apply damage directly via FUN_140090F60. Values in display HP units (e.g. 5 = 5 HP damage).",
	                3, 3);

	// Stage an override of the next real combat hit. The next time the
	// engine's Player::_applyHit reaches FUN_140090F60, our hook will
	// overwrite the damage packet with these values before the engine
	// processes it. Useful for verifying our values reach the right place.
	// Set hard HP to an exact target value. Uses the Process_tick-populated
	// charID→charStats registry so the target doesn't have to have taken a
	// hit; just be connected. Reads the LIVE HP field at charStats+0x118
	// (which stores -HP * 1e6) and computes the delta to apply.
	Con::AddCommand("Lifx", "setHardHp",
	                [](LPVOID, S32 argc, const char* argv[]) {
		if (argc < 3) { Con::Warning("usage: Lifx::setHardHp(charID, hp)"); return; }
		const auto charID = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 0));
		const auto target = std::strtoll(argv[2], nullptr, 0);
		void* cs = Hooks::VitalParams::LookupCharStats(charID);
		if (!cs) {
			Con::Warning("Lifx::setHardHp: charID %u not in registry — is the player connected?",
			             charID);
			return;
		}
		// Empirical formula: HUD hard HP = floor(-field/1e6) + 1. So to
		// land HUD = target, write field = -(target - 1) × 1e6.
		const long long currentRaw     = ::Engine::HardHpRaw(cs);
		const long long targetInternal = target - 1;          // 0-based internal
		const long long targetRaw      = -targetInternal * ::Engine::kHpScale;
		const long long deltaRaw       = targetRaw - currentRaw;
		long long pkt[6] = { deltaRaw, 0, 0, 0, 0, 0 };
		LIFX_DBG("[lifx-apply] setHardHp: charID=%u  charStats=%p  curRaw=%lld  tgtRaw=%lld (internal %lld)  dmgRaw=%lld",
		         charID, cs, currentRaw, targetRaw, targetInternal, deltaRaw);
		::Engine::AtRva<void(__fastcall*)(void*, void*)>(CmOffset::HIT_APPLY_DAMAGE)(cs, pkt);
	},
	                "(charID, hp) - set hard HP to a target value (display units). Player must be connected.",
	                3, 3);

	// Set soft HP to an exact target. HUD soft HP = (+0x218 - +0x1F8 + +0x200)
	// from process_tick decomp. So to land effective = N × 1e6:
	//   +0x1F8 = -N × 1e6   (damage term)
	//   +0x200 = 0          (bonus term)
	//   +0x218 = 0          (effective-max term)
	// Then ping apply_damage with a zero packet to trigger the HUD broadcast.
	// The previous drift came from apply_damage's heal path shifting +0x200
	// independently of our intent.
	Con::AddCommand("Lifx", "setSoftHp",
	                [](LPVOID, S32 argc, const char* argv[]) {
		if (argc < 3) { Con::Warning("usage: Lifx::setSoftHp(charID, hp)"); return; }
		const auto charID = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 0));
		const auto target = std::strtoll(argv[2], nullptr, 0);
		void* cs = Hooks::VitalParams::LookupCharStats(charID);
		if (!cs) { Con::Warning("Lifx::setSoftHp: charID %u not in registry", charID); return; }
		auto cb = static_cast<char*>(cs);
		// 1) tiny apply_damage ping to force the send_changes broadcast path.
		//    1 raw unit = 1e-6 HP, well below display granularity.
		long long pkt[6] = { 0, 1, 0, 0, 0, 0 };
		::Engine::AtRva<void(__fastcall*)(void*, void*)>(CmOffset::HIT_APPLY_DAMAGE)(cs, pkt);
		// 2) overwrite the triplet to the precise target. The broadcast above
		//    arms send_changes for this tick; the engine resends on the next
		//    process_tick (~immediate at the HUD's refresh rate) with these
		//    fresh values.
		// HUD is 1-based ("alive ≥ 1"): target N → effective (N-1)×1e6.
		*reinterpret_cast<long long*>(cb + ::Engine::kSoftHpDamageOff) = -(target - 1) * ::Engine::kHpScale;
		*reinterpret_cast<long long*>(cb + ::Engine::kSoftHpBonusOff)  = 0;
		*reinterpret_cast<long long*>(cb + ::Engine::kSoftHpEffMaxOff) = 0;
		LIFX_DBG("[lifx-apply] setSoftHp: charID=%u  charStats=%p  target=%lld  effective=%.3f",
		         charID, cs, target, ::Engine::SoftHpEffective(cs) / double(::Engine::kHpScale));
	},
	                "(charID, hp) - set soft HP to a target value (display units). Player must be connected.",
	                3, 3);

	// Dump the registry — useful for debugging which charIDs are seen.
	Con::AddCommand("Lifx", "dumpCharStats",
	                [](LPVOID, S32, const char*[]) { Hooks::VitalParams::DumpRegistry(); },
	                "() - dump the charID→charStats registry with live HP per entry",
	                1, 1);

	// Dump the hard + soft HP triplet fields the engine uses for the
	// effective-HP / death-check formula. The HUD effectively reads the
	// SUM (effMax - damage + bonus), so seeing all three lets us correlate
	// the in-game HUD reading with what's actually in memory.
	Con::AddCommand("Lifx", "dumpHpTriplets",
	                [](LPVOID, S32 argc, const char* argv[]) {
		if (argc < 2) { Con::Warning("usage: Lifx::dumpHpTriplets(charID)"); return; }
		const auto charID = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 0));
		void* cs = Hooks::VitalParams::LookupCharStats(charID);
		if (!cs) { Con::Warning("charID %u not in registry", charID); return; }
		const long long h_em  = ::Engine::ReadI64At(cs, ::Engine::kHardHpEffMaxOff);
		const long long h_dmg = ::Engine::ReadI64At(cs, ::Engine::kHardHpDamageOff);
		const long long h_bn  = ::Engine::ReadI64At(cs, ::Engine::kHardHpBonusOff);
		const long long s_em  = ::Engine::ReadI64At(cs, ::Engine::kSoftHpEffMaxOff);
		const long long s_dmg = ::Engine::ReadI64At(cs, ::Engine::kSoftHpDamageOff);
		const long long s_bn  = ::Engine::ReadI64At(cs, ::Engine::kSoftHpBonusOff);
		const double scale = double(::Engine::kHpScale);
		Con::Echo("[lifx-hp] charID=%u  HARD:  effMax(+0x138)=%.3f  dmg(+0x118)=%.3f  bonus(+0x120)=%.3f  -> effective=%.3f",
		          charID, h_em / scale, h_dmg / scale, h_bn / scale,
		          ::Engine::HardHpEffective(cs) / scale);
		Con::Echo("[lifx-hp] charID=%u  SOFT:  effMax(+0x218)=%.3f  dmg(+0x1F8)=%.3f  bonus(+0x200)=%.3f  -> effective=%.3f",
		          charID, s_em / scale, s_dmg / scale, s_bn / scale,
		          ::Engine::SoftHpEffective(cs) / scale);
	},
	                "(charID) - dump hard+soft HP triplet (effMax / damage / bonus) and computed effective.",
	                2, 2);

	// Lethal: drop HP to 0 AND invoke the engine's "you are dead/unconscious"
	// virtual on charStats (vtable slot 0x130/8 = slot 38). From the
	// Player::_applyHit decompile, this is the lethal-branch call the engine
	// makes when accumulated damage exceeds max. Calling it directly fires
	// the unconsciousness/ragdoll path without needing a real weapon descriptor.
	// Knockout: drop SOFT HP to 0 + invoke the engine's "unconscious" virtual
	// on charStats (vtable byte offset 0x140 = slot 40). From the
	// Player::_applyHit decompile this is the lethal-branch B call when
	// soft HP runs out — produces the same in-game knockout state as a real
	// combat hit that drops soft HP below 1, without going through
	// Player::_applyHit's orchestration.
	Con::AddCommand("Lifx", "knockout",
	                [](LPVOID, S32 argc, const char* argv[]) {
		if (argc < 2) { Con::Warning("usage: Lifx::knockout(charID)"); return; }
		const auto charID = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 0));
		void* cs = Hooks::VitalParams::LookupCharStats(charID);
		if (!cs) { Con::Warning("Lifx::knockout: charID %u not in registry", charID); return; }
		// 1) drop soft HP to 0 via apply_damage so the field is consistent.
		long long pkt[6] = { 0, 1000LL * ::Engine::kHpScale, 0, 0, 0, 0 };
		::Engine::AtRva<void(__fastcall*)(void*, void*)>(CmOffset::HIT_APPLY_DAMAGE)(cs, pkt);
		// 2) invoke the knockout vtable slot.
		void** vt = *reinterpret_cast<void***>(cs);
		auto koFn = reinterpret_cast<void(__fastcall*)(void*, void*)>(vt[0x140 / 8]);
		Con::Echo("[lifx-knockout] charID=%u  charStats=%p  vtable=%p  koFn=%p",
		          charID, cs, vt, (void*)koFn);
		koFn(cs, nullptr);
	},
	                "(charID) - knock out: drop soft HP to 0 + invoke charStats vtable slot 40 (unconscious virtual)",
	                2, 2);

	Con::AddCommand("Lifx", "kill",
	                [](LPVOID, S32 argc, const char* argv[]) {
		if (argc < 2) { Con::Warning("usage: Lifx::kill(charID)"); return; }
		const auto charID = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 0));
		void* cs = Hooks::VitalParams::LookupCharStats(charID);
		if (!cs) { Con::Warning("Lifx::kill: charID %u not in registry", charID); return; }
		// 1) overkill via apply_damage so HP visibly drops to 0.
		long long pkt[6] = { 1000LL * ::Engine::kHpScale, 1000LL * ::Engine::kHpScale, 0, 0, 0, 0 };
		::Engine::AtRva<void(__fastcall*)(void*, void*)>(CmOffset::HIT_APPLY_DAMAGE)(cs, pkt);
		// 2) invoke the lethal vtable slot. 0x130 is the byte offset within
		// the vtable; divide by 8 to get the slot index for the function ptr.
		void** vt = *reinterpret_cast<void***>(cs);
		auto deathFn = reinterpret_cast<void(__fastcall*)(void*, void*)>(vt[0x130 / 8]);
		Con::Echo("[lifx-kill] charID=%u  charStats=%p  vtable=%p  deathFn=%p",
		          charID, cs, vt, (void*)deathFn);
		deathFn(cs, nullptr);
	},
	                "(charID) - kill: drop HP to 0 + invoke the engine's lethal-state virtual on charStats",
	                2, 2);

	// Diagnostic: dump the Player accumulated-damage fields the death-checks
	// in Player::_applyHit consult (Player + 0xBC0/0xBC8/0xBE0 +
	// 0xCC0/0xCA0/0xCA8). Helps identify which one is "current damage" vs
	// "max" so a future setHardHp can also update them and the natural
	// death checks will fire on the next combat tick.
	Con::AddCommand("Lifx", "dumpPlayerDeathFields",
	                [](LPVOID, S32 argc, const char* argv[]) {
		if (argc < 2) { Con::Warning("usage: Lifx::dumpPlayerDeathFields(charID)"); return; }
		const auto charID = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 0));
		void* cs = Hooks::VitalParams::LookupCharStats(charID);
		if (!cs) { Con::Warning("not in registry"); return; }
		// Player = charStats - 0xAA8.
		auto player = static_cast<char*>(cs) - ::Engine::kCharStatsToPlayerDelta;
		auto rd = [&](unsigned off) {
			return *reinterpret_cast<long long*>(player + off);
		};
		Con::Echo("[lifx-death] charID=%u player=%p  death-check field dump:", charID, player);
		Con::Echo("  param_1[0x178] = Player+0xBC0 = %lld", rd(0xBC0));
		Con::Echo("  param_1[0x179] = Player+0xBC8 = %lld", rd(0xBC8));
		Con::Echo("  param_1[0x17c] = Player+0xBE0 = %lld", rd(0xBE0));
		Con::Echo("  param_1[0x194] = Player+0xCA0 = %lld", rd(0xCA0));
		Con::Echo("  param_1[0x195] = Player+0xCA8 = %lld", rd(0xCA8));
		Con::Echo("  param_1[0x198] = Player+0xCC0 = %lld", rd(0xCC0));
		Con::Echo("  death cond A: (0x17C - 0x178) + 0x179 = %lld  (engine triggers death if < 1)",
		          (rd(0xBE0) - rd(0xBC0)) + rd(0xBC8));
		Con::Echo("  death cond B: (0x198 - 0x194) + 0x195 = %lld  (engine triggers death if < 1)",
		          (rd(0xCC0) - rd(0xCA0)) + rd(0xCA8));
	},
	                "(charID) - dump the Player accumulated-damage fields the death-checks read",
	                2, 2);

	// Direct write to a Player+offset field. Use after dumpPlayerDeathFields
	// to drive death-check conditions below 1 and verify natural death fires
	// on the next tick.
	Con::AddCommand("Lifx", "pokePlayer",
	                [](LPVOID, S32 argc, const char* argv[]) {
		if (argc < 4) { Con::Warning("usage: Lifx::pokePlayer(charID, hexOffset, int64Value)"); return; }
		const auto charID = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 0));
		void* cs = Hooks::VitalParams::LookupCharStats(charID);
		if (!cs) { Con::Warning("not in registry"); return; }
		const char* offStr = argv[2];
		if (offStr[0] == '0' && (offStr[1] == 'x' || offStr[1] == 'X')) offStr += 2;
		const auto offset = static_cast<unsigned>(std::strtoul(offStr, nullptr, 16));
		const long long value = std::strtoll(argv[3], nullptr, 0);
		auto player = static_cast<char*>(cs) - ::Engine::kCharStatsToPlayerDelta;
		const long long before = *reinterpret_cast<long long*>(player + offset);
		*reinterpret_cast<long long*>(player + offset) = value;
		Con::Echo("[lifx-poke] %lld -> %lld", before, value);
	},
	                "(charID, hexOffset, int64Value) - direct write to Player+offset",
	                4, 4);

	Con::AddCommand("Lifx", "stageHitOverride",
	                [](LPVOID, S32 argc, const char* argv[]) {
		if (argc < 3) { Con::Warning("usage: Lifx::stageHitOverride(<hardHp>, <softHp>)"); return; }
		Hooks::HitApplyDamage::g_overrideHardHp = std::strtoll(argv[1], nullptr, 0) * ::Engine::kHpScale;
		Hooks::HitApplyDamage::g_overrideSoftHp = std::strtoll(argv[2], nullptr, 0) * ::Engine::kHpScale;
		Hooks::HitApplyDamage::g_pendingOverride = true;
		Con::Echo("[lifx-apply] staged override: hardHp=%lld softHp=%lld — takes effect on next real hit",
		          Hooks::HitApplyDamage::g_overrideHardHp,
		          Hooks::HitApplyDamage::g_overrideSoftHp);
	},
	                "(int hardHp, int softHp) - next combat hit will have its damage values replaced with these (display HP).",
	                3, 3);

	// Manual baseline tools for the Process_tick wide-scan window.
	Con::AddCommand("Lifx", "vitalMark",
	                [](LPVOID, S32, const char*[]) { Hooks::VitalParams::Mark(); },
	                "() - snapshot the +0x100..+0x400 window of the last-seen VitalParameters object",
	                1, 1);
	Con::AddCommand("Lifx", "vitalDiff",
	                [](LPVOID, S32, const char*[]) { Hooks::VitalParams::Diff(); },
	                "() - print every int64 field that changed since vitalMark",
	                1, 1);

	// ---- Per-player PvP pacifist toggle -----------------------------------
	// Flag a charID so every weapon they wield (melee path) deals zero damage
	// to OTHER PLAYERS — the generalised believer-weapon behaviour, per-player
	// and for any weapon. PvE (animals/NPCs) and world damage are unaffected.
	// Enforced in the ONEPUNCHMAN hook (hook_onepunchman.cpp).
	Con::AddCommand("Lifx", "setPacifist",
	                [](LPVOID, S32 argc, const char* argv[]) {
		if (argc < 3) { Con::Warning("usage: Lifx::setPacifist(charID, 0|1)"); return; }
		const auto charID = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 0));
		const bool on = std::strtol(argv[2], nullptr, 0) != 0;
		if (charID == 0) { Con::Warning("Lifx::setPacifist: invalid charID"); return; }
		Hooks::OnePunchMan::SetPacifist(charID, on);
		Con::Echo("[lifx-pacifist] charID=%u pacifist=%s", charID, on ? "ON (PvP damage off)" : "OFF");
	},
	                "(charID, 0|1) - toggle whether this player's weapons can hurt other players",
	                3, 3);

	Con::AddCommand("Lifx", "isPacifist",
	                [](LPVOID, S32 argc, const char* argv[]) -> const char* {
		if (argc < 2) { Con::Warning("usage: Lifx::isPacifist(charID)"); return "0"; }
		const auto charID = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 0));
		return Hooks::OnePunchMan::IsPacifist(charID) ? "1" : "0";
	},
	                "(charID) - returns 1 if the player is flagged pacifist, else 0",
	                2, 2);

	Con::AddCommand("Lifx", "dumpPacifist",
	                [](LPVOID, S32, const char*[]) { Hooks::OnePunchMan::DumpPacifist(); },
	                "() - list every charID currently flagged pacifist",
	                1, 1);
}
