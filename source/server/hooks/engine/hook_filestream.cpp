/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "hook_filestream.h"

#include "core/crypto/key_provider.h"
#include "core/crypto/lfxe_filestream.h"

__CM_INSTATNTIATE(_Engine_FS_Open);
__CM_INSTATNTIATE(_Engine_FS_Read);
__CM_INSTATNTIATE(_Engine_FS_SetPosition);
__CM_INSTATNTIATE(_Engine_FS_GetPosition);
__CM_INSTATNTIATE(_Engine_FS_GetStreamSize);
__CM_INSTATNTIATE(_Engine_FS_Dtor);

namespace
{
	lfxe::BakedKeyProvider gKeys; // keyId 0; v2 chains a ServerKeyProvider
	constexpr U32 kAccessRead = 0; // Torque FileStream Read mode

	void announceOnce()
	{
		static bool announced = false;
		if (announced || !lfxe::FsConsumeFirstDecryptFlag())
			return;
		announced = true;
		Con::Echo("[LiFx] LFXE decryption active (first encrypted asset served)");
	}
}

// ---------------------------------------------------------------------------- //
bool Hooks::Engine::FileStream_OnOpen(void* self, void* path, U32 accessMode)
{
	const bool ok = _Engine_FS_Open(self, path, accessMode);
	if (!ok || accessMode != kAccessRead)
	{
		lfxe::FsUnregister(self); // drop stale state on a reused FileStream object
		return ok;
	}
	lfxe::FsRegisterIfEncrypted(self,
	                            _Engine_FS_GetStreamSize,
	                            _Engine_FS_SetPosition,
	                            _Engine_FS_Read,
	                            gKeys);
	announceOnce();
	return ok;
}

// ---------------------------------------------------------------------------- //
bool Hooks::Engine::FileStream_OnRead(void* self, U64 size, void* dst)
{
	bool out;
	if (lfxe::FsTryRead(self, size, dst, &out))
		return out;
	return _Engine_FS_Read(self, size, dst);
}

// ---------------------------------------------------------------------------- //
bool Hooks::Engine::FileStream_OnSetPosition(void* self, U64 pos)
{
	bool out;
	if (lfxe::FsTrySetPosition(self, pos, &out))
		return out;
	return _Engine_FS_SetPosition(self, pos);
}

// ---------------------------------------------------------------------------- //
U64 Hooks::Engine::FileStream_OnGetPosition(void* self)
{
	U64 out;
	if (lfxe::FsTryGetPosition(self, &out))
		return out;
	return _Engine_FS_GetPosition(self);
}

// ---------------------------------------------------------------------------- //
U64 Hooks::Engine::FileStream_OnGetStreamSize(void* self)
{
	U64 out;
	if (lfxe::FsTryGetSize(self, &out))
		return out;
	return _Engine_FS_GetStreamSize(self);
}

// ---------------------------------------------------------------------------- //
void Hooks::Engine::FileStream_OnDtor(void* self, U32 flags)
{
	lfxe::FsUnregister(self);
	_Engine_FS_Dtor(self, flags);
}
