/* ===================================================================================
	Copyright (c) 2026, LiFx Contributors. All Rights Reserved.

	This file is a part of LiFx.
*  =================================================================================== */

#include "client_redirect.h"

#include "server/cm_server.h"
#include "server/api/t3d_console.h"
#include "server/hooks/character/hook_set_control_object.h"

#include <cstdio>

bool Hooks::ClientRedirect::RedirectCapturedClient(std::uint32_t peerWorldId,
                                                   const std::string& host,
                                                   std::uint16_t port)
{
	if (host.empty() || port == 0) {
		Con::Warning("[lifx-redirect] no peerHost/peerPort configured for this neighbour — skipping client redirect");
		return false;
	}

	// Even though we don't address commandToClient by pointer, having
	// a captured conn is a reasonable liveness gate: if no controlled
	// connection exists, there's no client to redirect.
	if (!Hooks::SetControlObject::LastControlledConnection()) {
		Con::Warning("[lifx-redirect] no captured GameConnection — log a client in first");
		return false;
	}

	// peerWorldId must fit the client's [1..49] slot range (see
	// scripts/client/serverConnection.cs initiateNewConnection guard).
	if (peerWorldId == 0 || peerWorldId > 49) {
		Con::Warning("[lifx-redirect] peerWorldId=%u outside the client's accepted [1..49] range; clamping to 49",
		             peerWorldId);
		peerWorldId = (peerWorldId == 0) ? 1u : 49u;
	}

	// Single-player demo: send the RPC to the first client in ClientGroup.
	// A later chunk will replace ClientGroup.getObject(0) with a (charID ->
	// %client) lookup. The 'GoToServer' verb dispatches on the client to
	// clientCmdGoToServer, registered at runtime by the LiFx client DLL
	// from inside its Con::Init hook (see source/client/hook_console_init.cpp).
	// The full statement:
	//   commandToClient(ClientGroup.getObject(0), 'GoToServer', 1,
	//                   <peerWorldId>, "IP:<host>:<port>");
	char buf[512];
	std::snprintf(buf, sizeof(buf),
		"commandToClient(ClientGroup.getObject(0), 'GoToServer', 1, %u, \"IP:%s:%u\");",
		(unsigned)peerWorldId, host.c_str(), (unsigned)port);

	Con::Echo("[lifx-redirect] eval: %s", buf);
	Con::Evaluate(buf, /*echo=*/false);
	return true;
}
