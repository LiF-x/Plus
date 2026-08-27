/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).
*  =================================================================================== */

#include "hook_filestream.h"
#include "hook_console.h" // _Engine_Con_InternalConsolePrintf (resolved at attach)

#include "core/crypto/key_provider.h"
#include "core/crypto/lfxe_filestream.h"

__LIFX_INSTANTIATE(_Engine_FS_Open);
__LIFX_INSTANTIATE(_Engine_FS_Read);
__LIFX_INSTANTIATE(_Engine_FS_SetPosition);
__LIFX_INSTANTIATE(_Engine_FS_GetPosition);
__LIFX_INSTANTIATE(_Engine_FS_GetStreamSize);
__LIFX_INSTANTIATE(_Engine_FS_Dtor);

namespace
{
	lfxe::BakedKeyProvider gKeys;   // keyId 0; v2 chains a ServerKeyProvider
	constexpr std::uint32_t kAccessRead = 0; // Torque FileStream Read mode

	void announceOnce()
	{
		static bool announced = false;
		if (announced || !lfxe::FsConsumeFirstDecryptFlag())
			return;
		announced = true;
		if (_Engine_Con_InternalConsolePrintf)
			_Engine_Con_InternalConsolePrintf(
				0, 0, const_cast<char*>(
					"[LiFx-client] LFXE decryption active (first encrypted asset served)\n"));
	}
}

namespace LifxClient
{
	bool __fastcall HookFileStream::OnOpen(void* self, void* path, std::uint32_t accessMode)
	{
		const bool ok = _Engine_FS_Open(self, path, accessMode);
		if (!ok || accessMode != kAccessRead)
		{
			// Failed/non-read (re)open of a possibly-reused object: drop stale state.
			lfxe::FsUnregister(self);
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

	bool __fastcall HookFileStream::OnRead(void* self, std::uint64_t size, void* dst)
	{
		bool out;
		if (lfxe::FsTryRead(self, size, dst, &out))
			return out;
		return _Engine_FS_Read(self, size, dst);
	}

	bool __fastcall HookFileStream::OnSetPosition(void* self, std::uint64_t pos)
	{
		bool out;
		if (lfxe::FsTrySetPosition(self, pos, &out))
			return out;
		return _Engine_FS_SetPosition(self, pos);
	}

	std::uint64_t __fastcall HookFileStream::OnGetPosition(void* self)
	{
		std::uint64_t out;
		if (lfxe::FsTryGetPosition(self, &out))
			return out;
		return _Engine_FS_GetPosition(self);
	}

	std::uint64_t __fastcall HookFileStream::OnGetStreamSize(void* self)
	{
		std::uint64_t out;
		if (lfxe::FsTryGetSize(self, &out))
			return out;
		return _Engine_FS_GetStreamSize(self);
	}

	void __fastcall HookFileStream::OnDtor(void* self, std::uint32_t flags)
	{
		lfxe::FsUnregister(self);
		_Engine_FS_Dtor(self, flags);
	}
}
