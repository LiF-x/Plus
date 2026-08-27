/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx (client variant).
*  =================================================================================== */

#include "client_runtime.h"
#include "client_offsets.h"
#include "client_aux.h"
#include "hook_console.h"
#include "hook_console_init.h"
#include "hook_filestream.h"
#include "hook_naked_render.h"
#include "hook_equip_unpack.h"

__LIFX_INSTANTIATE(_Engine_Con_Evaluate);

void LifxClient::AttachHooks()
{
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

	__LIFX_ATTACH_HOOK(ClientOffset::CLIENT_CON_INTERNAL_PRINTF,
	                   _Engine_Con_InternalConsolePrintf,
	                   LifxClient::HookConsole::OnInternalPrintf);

	__LIFX_ATTACH_HOOK(ClientOffset::CLIENT_CON_INIT,
	                   _Engine_Con_Init,
	                   LifxClient::HookConsoleInit::OnConsoleInit);

	// Universal LFXE decrypt at the FileStream layer (issue #116) -- replaces
	// the per-format DTS + openFileStream-factory hooks; covers every loose
	// asset (shapes, *.cs.dso, DDS, PNG, ...).
	__LIFX_ATTACH_HOOK(ClientOffset::CLIENT_FILESTREAM_OPEN,
	                   _Engine_FS_Open, LifxClient::HookFileStream::OnOpen);
	__LIFX_ATTACH_HOOK(ClientOffset::CLIENT_FILESTREAM_READ,
	                   _Engine_FS_Read, LifxClient::HookFileStream::OnRead);
	__LIFX_ATTACH_HOOK(ClientOffset::CLIENT_FILESTREAM_GETSIZE,
	                   _Engine_FS_GetStreamSize, LifxClient::HookFileStream::OnGetStreamSize);
	__LIFX_ATTACH_HOOK(ClientOffset::CLIENT_FILESTREAM_GETPOS,
	                   _Engine_FS_GetPosition, LifxClient::HookFileStream::OnGetPosition);
	__LIFX_ATTACH_HOOK(ClientOffset::CLIENT_FILESTREAM_SETPOS,
	                   _Engine_FS_SetPosition, LifxClient::HookFileStream::OnSetPosition);
	__LIFX_ATTACH_HOOK(ClientOffset::CLIENT_FILESTREAM_DTOR,
	                   _Engine_FS_Dtor, LifxClient::HookFileStream::OnDtor);

	// Con::evaluate isn't hooked, just resolved. Called from OnConsoleInit
	// to inject TS glue at runtime (clientCmdGoToServer).
	_Engine_Con_Evaluate = reinterpret_cast<_Engine_Con_Evaluate_Fn>(
		reinterpret_cast<char*>(GetModuleHandle(NULL))
		+ static_cast<unsigned>(ClientOffset::CLIENT_CON_EVALUATE));

	// A2a (#125) naked-body cull: resolve the two engine fns the detour calls,
	// then detour tmpHideAllNakedMans to hide the armor mesh set instead.
	_Engine_QueryRenderObjects = reinterpret_cast<_Engine_QueryRenderObjects_Fn>(
		reinterpret_cast<char*>(GetModuleHandle(NULL)) + static_cast<unsigned>(ClientOffset::CLIENT_QUERY_OBJECTS));
	_Engine_SetMeshHidden = reinterpret_cast<_Engine_SetMeshHidden_Fn>(
		reinterpret_cast<char*>(GetModuleHandle(NULL)) + static_cast<unsigned>(ClientOffset::CLIENT_SET_MESH_HIDDEN));
	__LIFX_ATTACH_HOOK(ClientOffset::CLIENT_TMP_HIDE_NAKED,
	                   _Engine_TmpHideAllNaked, LifxClient::HookNakedRender::OnHideAllNaked);

	DetourTransactionCommit();

	// NOTE: the A2a (#125) NPCDecorative vtbl[115] patch is installed later, in
	// HookConsoleInit::OnConsoleInit — NOT here. DllMain attach runs under the
	// Wine loader lock before the engine console exists, so its diag() calls
	// would abort the attach (client fails to launch).
}

void LifxClient::DetachHooks()
{
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());

	LifxClient::HookEquipUnpack::RemoveVtablePatch();
	__LIFX_DETACH_HOOK(_Engine_TmpHideAllNaked,             LifxClient::HookNakedRender::OnHideAllNaked);
	__LIFX_DETACH_HOOK(_Engine_FS_Dtor,                     LifxClient::HookFileStream::OnDtor);
	__LIFX_DETACH_HOOK(_Engine_FS_SetPosition,              LifxClient::HookFileStream::OnSetPosition);
	__LIFX_DETACH_HOOK(_Engine_FS_GetPosition,              LifxClient::HookFileStream::OnGetPosition);
	__LIFX_DETACH_HOOK(_Engine_FS_GetStreamSize,            LifxClient::HookFileStream::OnGetStreamSize);
	__LIFX_DETACH_HOOK(_Engine_FS_Read,                     LifxClient::HookFileStream::OnRead);
	__LIFX_DETACH_HOOK(_Engine_FS_Open,                     LifxClient::HookFileStream::OnOpen);
	__LIFX_DETACH_HOOK(_Engine_Con_Init,                    LifxClient::HookConsoleInit::OnConsoleInit);
	__LIFX_DETACH_HOOK(_Engine_Con_InternalConsolePrintf,   LifxClient::HookConsole::OnInternalPrintf);

	DetourTransactionCommit();
}
