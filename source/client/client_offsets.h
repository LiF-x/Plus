#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).

	Client-side engine RVAs for yo_cm_client.exe. RE'd by anchor-string
	xref scan against the .rdata strings "Con::InternalConsolePrintf",
	"Con::evaluate", "Con::init" (see scripts/client_re_console.py).
	Each anchor has exactly one xref in .text; the containing function
	(recovered via .pdata) is the C++ implementation we want to hook.
*  =================================================================================== */

#include <cstdint>

enum ClientOffset : std::uint32_t
{
	// Con::InternalConsolePrintf(U32 type, U32 unk1, char* buffer)
	// Container of the sole `lea r8, [Con::InternalConsolePrintf]` xref.
	// Prologue saves ecx → r15d (the type/level arg), allocates a ~4 KB
	// stack frame, then calls into a vsnprintf-style helper. Same shape
	// as the server's CON_INTERNAL_PRINTF at 0x405090.
	CLIENT_CON_INTERNAL_PRINTF = 0x58F820,

	// Con::evaluate(const char* str, bool echo, const char* fileName)
	// Prologue saves rcx → rsi (string), dl → ebx (echo), r8 → rdi
	// (fileName). Same shape as server's CON_EVALUATE at 0x406A50.
	CLIENT_CON_EVALUATE        = 0x5917E0,

	// Con::init()
	// Engine console init. Sets four boot flags via mov byte [rip+...],1
	// then calls a sub-init, then logs with "Con::init" as a category
	// tag. Same shape as server's CON_INIT at 0x407990.
	CLIENT_CON_INIT            = 0x592DD0,

	// bool TSShape::read(TSShape* this /*rcx*/, Stream* s /*rdx*/)
	// The DTS parser entry. On entry `s` is positioned at 0; the function
	// reads the version word, compares it against the max supported
	// version (smReadVersion) and, on overflow, branches to the sole xref
	// of "Error: attempt to load a version %i dts-shape, can currently
	// only load version %i and before." (anchor @ 0x1411b9650). Returns
	// `al` (false on failure). Recovered by anchor-xref scan: that string
	// has exactly one LEA xref, into this function. See
	// scripts/client_re_dts.py.
	//
	// Hook seam for transparent DTS decryption: at entry, peek `s`; if it
	// carries the LFXE magic, decrypt the payload and call the original
	// with a Stream over the plaintext bytes; otherwise pass through.
	CLIENT_TSSHAPE_READ        = 0xB572B0,

	// Stream* openFileStream(pathObj* /*rcx*/, U32 accessMode /*edx*/)
	// The engine's general "open file -> Stream" factory: allocates a
	// FileStream (0x2040 bytes, vtable RVA 0xFF9BA0), calls its open()
	// (vtable slot 12 / off 0x60) with the path, returns the opened
	// Stream* in rax (or null on failure). ~24 callers. The DSO loader
	// exec() (RVA 0x597A10) uses it then runs getStreamSize()>=0xC and a
	// version==0x2750 check ON THE RETURNED STREAM -- so substituting a
	// decrypted MemStream here makes both checks pass (same seam idea as
	// the DTS version gate). Recovered from server exec() structural twin
	// (see scripts notes / issue #114).
	//
	// Decrypt seam (read-mode opens only): call original; if the returned
	// stream's first 4 bytes are the LFXE magic, slurp + decrypt and
	// return a MemStream over the plaintext, destroying the FileStream;
	// otherwise rewind and return it unchanged. Magic-gating scopes this
	// to exactly the files we encrypt and composes with CLIENT_TSSHAPE_READ.
	//
	// SUPERSEDED by the FILESTREAM_* hooks below (issue #116): this factory
	// is only one of 5 FileStream constructors, so it missed the texture/PNG
	// loaders that call FileStream::open directly. Kept for reference.
	CLIENT_OPEN_FILE_STREAM    = 0x61E620,

	// --- FileStream class methods (issue #116) --------------------------------
	// The engine has exactly one FileStream class; every loose-file read
	// funnels through these. The universal LFXE hook (hook_filestream.cpp)
	// detours them so all assets decrypt at the stream layer, subsuming the
	// DTS/DSO/factory hooks. FileStream vtable RVA 0xFF9BA0; slots verified by
	// disassembly (see docs/lfxe_texture_re.md, scripts/client_re_texture.py).
	//
	//   bool  FileStream::open(this, const char* path, U32 accessMode)  [slot 12]
	//   bool  FileStream::_read(this, U64 size, void* dst)              [slot 2]
	//   U64   FileStream::getPosition(this)                             [slot 6]
	//   bool  FileStream::setPosition(this, U64 pos)                    [slot 7]
	//   U64   FileStream::getStreamSize(this)                           [slot 8]
	//   void  FileStream::~FileStream(this, U32 flags)                  [slot 0]
	CLIENT_FILESTREAM_OPEN      = 0x61E9C0,
	CLIENT_FILESTREAM_READ      = 0x61DE40,
	CLIENT_FILESTREAM_GETPOS    = 0x61E910,
	CLIENT_FILESTREAM_SETPOS    = 0x61EBC0,
	CLIENT_FILESTREAM_GETSIZE   = 0x61E930,
	CLIENT_FILESTREAM_DTOR      = 0x61DD50,

	// --- A2a equip-render: naked-body mesh cull (issue #125) ------------------
	// We detour the shipped debug command tmpHideAllNakedMans (a console cmd that
	// queries renderable objects and hides a fixed 8-mesh body set) and replace
	// its mesh set with the 361 baked armor/clothing meshes, so a player-model
	// NPCDecorative renders as a naked body instead of all-meshes-overlapping.
	// All verified by disasm of 0x140A96420 (see /tmp/client_disasm.txt).
	CLIENT_TMP_HIDE_NAKED   = 0xA96420,  // void __fastcall(obj,argc,argv) console cmd; query+iterate+setMeshHidden
	CLIENT_QUERY_OBJECTS    = 0x9D25F0,  // void __fastcall(container, U32 mask, Vec* out) — fills {begin,end,cap}
	CLIENT_SET_MESH_HIDDEN  = 0x23DCF0,  // void __fastcall(obj, const char* meshName, char hidden)
	CLIENT_RENDER_CONTAINER = 0x1733080, // static container global passed to query (mask 0x8000)

	// --- A2a equip-over-ghost bolt-on, receive side (issue #125) --------------
	// Shared Player/ShapeBase unpackUpdate (NPCDecorative thunks here too). Found
	// via the "Error in Player::unpackUpdate()..." string xref @0x140EE2360.
	// Signature: void __fastcall(this /*rcx*/, NetConnection* conn /*rdx*/,
	// BitStream* stream /*r8*/). We detour it, run the original, then GATE on
	// *(void**)this == CLIENT_NPCDEC_VTABLE and read exactly the bits the server
	// appended in NPCDecorative packUpdate. BitStream: +0x10 buf, +0x18 bitPos,
	// +0x20 capBytes, +0x28 overflow; bits LSB-first (readFlag inlined).
	CLIENT_SHAPEBASE_UNPACK = 0x20CA90,
	// NPCDecorative primary vtable (RVA) — the gate. Found via RTTI
	// ".?AVNPCDecorative@NPCS@@" -> COL -> vtable.
	CLIENT_NPCDEC_VTABLE    = 0xF7BC08,

	// #171 — receive side for the native Animal equip-over-ghost (bandit clothing).
	// Animals::Animal primary vtable (RVA), found via RTTI ".?AVAnimal@Animals@@"
	// -> COL (offset==0, primary base) -> vtable; slot 56 holds the Animal's OWN
	// unpackUpdate (0x321D70, a real fn that wraps the shared reader 0x2107A0).
	// Same slot index (56) as NPCDecorative — both are NetObject::unpackUpdate.
	// The fn RVA is an install-time sanity check (refuse to patch on mismatch).
	CLIENT_ANIMAL_VTABLE     = 0xF3FD18,
	CLIENT_ANIMAL_UNPACK_FN  = 0x321D70,
};
