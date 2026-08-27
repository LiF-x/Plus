#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).

	Universal LFXE decrypt hook on the engine's FileStream class (issue #116).
	Detours the FileStream methods themselves -- open / _read / getStreamSize /
	getPosition / setPosition / ~FileStream -- so *every* asset (shapes,
	compiled scripts, DDS textures, UI PNGs, anything read from a loose file)
	is transparently decrypted, not just the files routed through the
	openFileStream factory. Subsumes hook_dts + hook_dso. The heavy lifting
	(decrypt-at-open + serve-from-memory registry) lives in
	core/crypto/lfxe_filestream; this file is just the per-binary trampolines
	and detour entry points. Offsets in client_offsets.h.
*  =================================================================================== */

#include "client/client_aux.h"
#include <cstdint>

// FileStream methods (Win64 __fastcall, this in rcx). RVAs in client_offsets.h.
__LIFX_DECL_EXTERNAL(bool, __fastcall, _Engine_FS_Open, void* self, void* path, std::uint32_t accessMode);
__LIFX_DECL_EXTERNAL(bool, __fastcall, _Engine_FS_Read, void* self, std::uint64_t size, void* dst);
__LIFX_DECL_EXTERNAL(bool, __fastcall, _Engine_FS_SetPosition, void* self, std::uint64_t pos);
__LIFX_DECL_EXTERNAL(std::uint64_t, __fastcall, _Engine_FS_GetPosition, void* self);
__LIFX_DECL_EXTERNAL(std::uint64_t, __fastcall, _Engine_FS_GetStreamSize, void* self);
__LIFX_DECL_EXTERNAL(void, __fastcall, _Engine_FS_Dtor, void* self, std::uint32_t flags);

namespace LifxClient
{
	namespace HookFileStream
	{
		bool          __fastcall OnOpen(void* self, void* path, std::uint32_t accessMode);
		bool          __fastcall OnRead(void* self, std::uint64_t size, void* dst);
		bool          __fastcall OnSetPosition(void* self, std::uint64_t pos);
		std::uint64_t __fastcall OnGetPosition(void* self);
		std::uint64_t __fastcall OnGetStreamSize(void* self);
		void          __fastcall OnDtor(void* self, std::uint32_t flags);
	}
}
