#pragma once

/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.

	Universal LFXE decrypt hook on the engine's FileStream class (issue #116),
	server twin of source/client/hook_filestream.*. Detours the FileStream
	methods (open / _read / getStreamSize / getPosition / setPosition /
	~FileStream) so every loose asset the server reads -- notably compiled
	scripts (*.cs.dso) -- is transparently decrypted at the stream layer,
	subsuming the openFileStream-factory hook (hook_dso). Shared decrypt +
	serve-from-memory registry lives in core/crypto/lfxe_filestream. Offsets
	in cm_offsets.h.
*  =================================================================================== */

#include "server/cm_server.h"

// FileStream methods (Win64 __fastcall, this in rcx). RVAs in cm_offsets.h.
__CM_DECL_EXTERNAL(bool, __fastcall, _Engine_FS_Open, void* self, void* path, U32 accessMode);
__CM_DECL_EXTERNAL(bool, __fastcall, _Engine_FS_Read, void* self, U64 size, void* dst);
__CM_DECL_EXTERNAL(bool, __fastcall, _Engine_FS_SetPosition, void* self, U64 pos);
__CM_DECL_EXTERNAL(U64, __fastcall, _Engine_FS_GetPosition, void* self);
__CM_DECL_EXTERNAL(U64, __fastcall, _Engine_FS_GetStreamSize, void* self);
__CM_DECL_EXTERNAL(void, __fastcall, _Engine_FS_Dtor, void* self, U32 flags);

namespace Hooks
{
	namespace Engine
	{
		bool FileStream_OnOpen(void* self, void* path, U32 accessMode);
		bool FileStream_OnRead(void* self, U64 size, void* dst);
		bool FileStream_OnSetPosition(void* self, U64 pos);
		U64  FileStream_OnGetPosition(void* self);
		U64  FileStream_OnGetStreamSize(void* self);
		void FileStream_OnDtor(void* self, U32 flags);
	}
}
